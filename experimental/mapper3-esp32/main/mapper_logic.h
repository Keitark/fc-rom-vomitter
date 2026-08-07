#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(__GNUC__)
#define MAPPER_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define MAPPER_ALWAYS_INLINE __forceinline
#else
#define MAPPER_ALWAYS_INLINE inline
#endif

/*
 * GPIO42/43/44 occupy consecutive bits 10/11/12 in GPIO_IN1_REG.
 * Keeping these definitions independent of ESP-IDF lets the qualification
 * truth table run in the native host-test executable as well.
 */
enum {
    MAPPER_GPIO_OUT_LOAD_MODE_BIT = 21,
    MAPPER_GPIO_IN1_D0_BIT = 10,
    MAPPER_GPIO_IN1_ROMSEL_INV_BIT = 11,
    MAPPER_GPIO_IN1_PRG_EN_N_BIT = 12,
};

#define MAPPER_GPIO_OUT_LOAD_MODE_MASK \
    (UINT32_C(1) << MAPPER_GPIO_OUT_LOAD_MODE_BIT)
#define MAPPER_GPIO_IN1_D0_MASK \
    (UINT32_C(1) << MAPPER_GPIO_IN1_D0_BIT)
#define MAPPER_GPIO_IN1_ROMSEL_INV_MASK \
    (UINT32_C(1) << MAPPER_GPIO_IN1_ROMSEL_INV_BIT)
#define MAPPER_GPIO_IN1_PRG_EN_N_MASK \
    (UINT32_C(1) << MAPPER_GPIO_IN1_PRG_EN_N_BIT)
#define MAPPER_GPIO_IN1_WRITE_MASK \
    (MAPPER_GPIO_IN1_ROMSEL_INV_MASK | MAPPER_GPIO_IN1_PRG_EN_N_MASK)

typedef enum {
    MAPPER3_EVENT_NONE = 0,
    MAPPER3_EVENT_BANK0,
    MAPPER3_EVENT_BANK1,
} mapper3_event_t;

typedef struct {
    bool write_active;
    unsigned qualified_samples;
    unsigned candidate_bank;
} mapper3_tracker_t;

static inline bool mapper3_sample_is_write(uint32_t gpio_in1_sample)
{
    return (gpio_in1_sample & MAPPER_GPIO_IN1_WRITE_MASK) ==
           MAPPER_GPIO_IN1_WRITE_MASK;
}

static inline bool mapper3_capture_allowed(uint32_t gpio_out_sample,
                                           uint32_t gpio_in1_sample)
{
    return (gpio_out_sample & MAPPER_GPIO_OUT_LOAD_MODE_MASK) == 0u &&
           mapper3_sample_is_write(gpio_in1_sample);
}

static inline unsigned mapper3_sample_bank(uint32_t gpio_in1_sample)
{
    return (gpio_in1_sample & MAPPER_GPIO_IN1_D0_MASK) != 0u ? 1u : 0u;
}

static MAPPER_ALWAYS_INLINE bool mapper3_confirm_write(
    uint32_t first, uint32_t confirm, unsigned *bank)
{
    if (!mapper3_sample_is_write(first) ||
        !mapper3_sample_is_write(confirm)) {
        return false;
    }
    *bank = mapper3_sample_bank(confirm);
    return true;
}

static MAPPER_ALWAYS_INLINE void mapper3_tracker_reset(mapper3_tracker_t *tracker)
{
    tracker->write_active = false;
    tracker->qualified_samples = 0u;
    tracker->candidate_bank = 0u;
}

/*
 * Track the complete write-qualified interval, not the complete /ROMSEL
 * interval. 6502 code fetches and a following STA $8000+ can keep
 * ROMSEL_inv asserted continuously across several CPU cycles. Treating that
 * as one pulse merges the preceding PRG reads with the mapper write and then
 * rejects the write.
 *
 * A write-qualified interval is ROMSEL_inv=1 and PRG_EN_n=1. U21 can remain
 * high for a few nanoseconds at the beginning of an ordinary PRG read, so a
 * single high/high poll is not sufficient. A real CPU write lasts a complete
 * bus cycle and produces at least two consecutive qualified samples in the
 * fast loop. The final D0 observed in a confirmed interval is committed when
 * either qualifier falls.
 */
static MAPPER_ALWAYS_INLINE mapper3_event_t mapper3_tracker_step(
    mapper3_tracker_t *tracker, uint32_t gpio_out_sample,
    uint32_t gpio_in1_sample)
{
    if ((gpio_out_sample & MAPPER_GPIO_OUT_LOAD_MODE_MASK) != 0u) {
        mapper3_tracker_reset(tracker);
        return MAPPER3_EVENT_NONE;
    }

    const bool write_now = mapper3_sample_is_write(gpio_in1_sample);
    const unsigned bank = mapper3_sample_bank(gpio_in1_sample);

    if (!tracker->write_active) {
        if (!write_now) {
            return MAPPER3_EVENT_NONE;
        }
        tracker->write_active = true;
        tracker->qualified_samples = 1u;
        tracker->candidate_bank = bank;
        return MAPPER3_EVENT_NONE;
    }

    if (write_now) {
        if (tracker->qualified_samples != UINT32_MAX) {
            ++tracker->qualified_samples;
        }
        tracker->candidate_bank = bank;
        return MAPPER3_EVENT_NONE;
    }

    const mapper3_event_t event =
        tracker->qualified_samples >= 2u
            ? (tracker->candidate_bank != 0u ? MAPPER3_EVENT_BANK1
                                             : MAPPER3_EVENT_BANK0)
            : MAPPER3_EVENT_NONE;
    mapper3_tracker_reset(tracker);
    return event;
}
