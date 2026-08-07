#include "mapper_runtime.h"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "mapper_logic.h"
#include "nvs.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

enum {
    MAPPER_CORE = 1,
    MAPPER_TASK_STACK = 2048,
    MAPPER_TRANSITION_TIMEOUT_US = 50000,
    MAPPER_STOP_CHECK_INTERVAL = 32,
    MAPPER_GPIO36_OUT1_BIT = 4,
    MAPPER_TRACE_FIRST_RECORDS = 5,
    MAPPER_TRACE_LAST_RECORDS = 5,
    MAPPER_TRACE_MAGIC = 0x4d335452,
    MAPPER_TRACE_VERSION = 1,
};

#define MAPPER_GPIO36_OUT1_MASK \
    (UINT32_C(1) << MAPPER_GPIO36_OUT1_BIT)

_Static_assert((int)PIN_MAPPER_CPU_D0 == (int)GPIO_NUM_42,
               "mapper_logic.h assumes CPU D0 on GPIO42");
_Static_assert((int)PIN_MAPPER_ROMSEL_INV == (int)GPIO_NUM_43,
               "mapper_logic.h assumes ROMSEL_inv on GPIO43");
_Static_assert((int)PIN_MAPPER_PRG_EN_N == (int)GPIO_NUM_44,
               "mapper_logic.h assumes PRG_EN_n on GPIO44");
_Static_assert((int)PIN_CHR_BANK_LOAD == (int)GPIO_NUM_36,
               "direct GPIO output assumes CHR A13 on GPIO36");

static const char *TAG = "mapper_runtime";
static TaskHandle_t s_mapper_task;
static DRAM_ATTR volatile bool s_requested;
static DRAM_ATTR volatile bool s_active;
static DRAM_ATTR volatile uint32_t s_bank0_writes;
static DRAM_ATTR volatile uint32_t s_bank1_writes;
static DRAM_ATTR volatile unsigned s_current_bank;
static DRAM_ATTR volatile mapper_trace_snapshot_t s_live_trace;
static mapper_trace_snapshot_t s_saved_trace;
static DRAM_ATTR volatile bool s_trace_frozen;
static volatile bool s_trace_saved;

typedef struct {
    uint32_t magic;
    uint32_t version;
    mapper_trace_snapshot_t trace;
} mapper_trace_blob_t;

static MAPPER_ALWAYS_INLINE uint8_t mapper_trace_state(uint32_t sample)
{
    return (uint8_t)(((sample & MAPPER_GPIO_IN1_D0_MASK) != 0u ? 1u : 0u) |
                     ((sample & MAPPER_GPIO_IN1_ROMSEL_INV_MASK) != 0u ? 2u : 0u) |
                     ((sample & MAPPER_GPIO_IN1_PRG_EN_N_MASK) != 0u ? 4u : 0u));
}

static MAPPER_ALWAYS_INLINE void mapper_trace_store(
    const mapper_trace_record_t *record)
{
    if (__atomic_load_n(&s_trace_frozen, __ATOMIC_RELAXED)) {
        return;
    }
    const uint32_t ordinal = s_live_trace.total_candidates++;
    uint32_t index;
    if (ordinal < MAPPER_TRACE_FIRST_RECORDS) {
        index = ordinal;
    } else {
        index = MAPPER_TRACE_FIRST_RECORDS +
                ((ordinal - MAPPER_TRACE_FIRST_RECORDS) %
                 MAPPER_TRACE_LAST_RECORDS);
    }
    volatile mapper_trace_record_t *destination =
        &s_live_trace.records[index];
    destination->ordinal = ordinal;
    destination->poll_count = record->poll_count;
    destination->transition_count = record->transition_count;
    destination->result = record->result;
    for (unsigned i = 0; i < MAPPER_TRACE_TRANSITIONS_PER_RECORD; ++i) {
        destination->transitions[i].delta_cycles =
            record->transitions[i].delta_cycles;
        destination->transitions[i].state = record->transitions[i].state;
        destination->transitions[i].reserved = 0u;
    }
    if (s_live_trace.stored_count < MAPPER_TRACE_RECORD_COUNT) {
        ++s_live_trace.stored_count;
    }
}

/*
 * This loop executes entirely from IRAM on core 1. Interrupts on that core are
 * disabled while RUN is active, so the loop cannot be preempted between the
 * qualified GPIO snapshot and the bank output write. Wi-Fi and HTTP continue
 * on core 0.
 *
 * The task is started while LOAD_MODE is still high. Reading the output
 * register here gates mapper writes until the hardware handoff actually
 * exposes the console bus, so the first console write cannot land in a
 * start-up gap.
 *
 * A first high/high qualifier can be U21's propagation state at the beginning
 * of an ordinary PRG read. One immediate second GPIO snapshot occurs after
 * the LVC gate's propagation interval and rejects that hazard. Requiring four
 * snapshots discarded most of ROM #48's real writes on the physical board.
 *
 * The trace keeps raw, uncompressed GPIO samples around every accepted bank
 * selection. GPIO36 is updated before the post-event samples are collected,
 * so diagnostics cannot delay the mapper action itself.
 */
static void IRAM_ATTR mapper_poll_loop(void)
{
    mapper_trace_record_t pulse_trace;
    uint32_t previous_sample = REG_READ(GPIO_IN1_REG);
    uint32_t previous_cycle = esp_cpu_get_cycle_count();
    unsigned stop_countdown = MAPPER_STOP_CHECK_INTERVAL;
    portDISABLE_INTERRUPTS();

    /*
     * The task becomes active while LOAD_MODE is still asserted. Wait for the
     * console handoff once; do not pay for a GPIO_OUT read on every hot-loop
     * poll. The stop path asserts LOAD_MODE before clearing s_requested, so
     * the console is already isolated during the bounded stop-check latency.
     */
    while ((REG_READ(GPIO_OUT_REG) & MAPPER_GPIO_OUT_LOAD_MODE_MASK) != 0u) {
        if (!__atomic_load_n(&s_requested, __ATOMIC_RELAXED)) {
            portENABLE_INTERRUPTS();
            return;
        }
    }

    while (true) {
        if (--stop_countdown == 0u) {
            stop_countdown = MAPPER_STOP_CHECK_INTERVAL;
            if (!__atomic_load_n(&s_requested, __ATOMIC_RELAXED)) {
                break;
            }
        }

        const uint32_t sample = REG_READ(GPIO_IN1_REG);
        const uint32_t sample_cycle = esp_cpu_get_cycle_count();
        if (!mapper3_sample_is_write(sample)) {
            previous_sample = sample;
            previous_cycle = sample_cycle;
            continue;
        }

        const uint32_t confirm = REG_READ(GPIO_IN1_REG);
        const uint32_t confirm_cycle = esp_cpu_get_cycle_count();
        unsigned bank;
        if (!mapper3_confirm_write(sample, confirm, &bank)) {
            previous_sample = confirm;
            previous_cycle = confirm_cycle;
            continue;
        }

        const mapper3_event_t event =
            bank != 0u ? MAPPER3_EVENT_BANK1 : MAPPER3_EVENT_BANK0;
        if (bank != 0u) {
            REG_WRITE(GPIO_OUT1_W1TS_REG, MAPPER_GPIO36_OUT1_MASK);
            s_current_bank = 1u;
            ++s_bank1_writes;
        } else {
            REG_WRITE(GPIO_OUT1_W1TC_REG, MAPPER_GPIO36_OUT1_MASK);
            s_current_bank = 0u;
            ++s_bank0_writes;
        }

        /* Record raw samples only after the real-time GPIO update. */
        pulse_trace.poll_count = MAPPER_TRACE_TRANSITIONS_PER_RECORD;
        pulse_trace.transition_count = MAPPER_TRACE_TRANSITIONS_PER_RECORD;
        pulse_trace.result = (uint8_t)event;
        pulse_trace.transitions[0].delta_cycles = 0u;
        pulse_trace.transitions[0].state = mapper_trace_state(previous_sample);
        pulse_trace.transitions[0].reserved = 0u;
        pulse_trace.transitions[1].delta_cycles =
            (uint16_t)(sample_cycle - previous_cycle);
        pulse_trace.transitions[1].state = mapper_trace_state(sample);
        pulse_trace.transitions[1].reserved = 0u;
        pulse_trace.transitions[2].delta_cycles =
            (uint16_t)(confirm_cycle - previous_cycle);
        pulse_trace.transitions[2].state = mapper_trace_state(confirm);
        pulse_trace.transitions[2].reserved = 0u;
        uint32_t last_sample = confirm;
        uint32_t last_cycle = confirm_cycle;
        for (unsigned i = 3u; i < MAPPER_TRACE_TRANSITIONS_PER_RECORD; ++i) {
            last_sample = REG_READ(GPIO_IN1_REG);
            last_cycle = esp_cpu_get_cycle_count();
            pulse_trace.transitions[i].delta_cycles =
                (uint16_t)(last_cycle - previous_cycle);
            pulse_trace.transitions[i].state = mapper_trace_state(last_sample);
            pulse_trace.transitions[i].reserved = 0u;
        }
        mapper_trace_store(&pulse_trace);
        previous_sample = last_sample;
        previous_cycle = last_cycle;

        /* One write-qualified interval produces exactly one bank event. */
        while (mapper3_sample_is_write(previous_sample)) {
            previous_sample = REG_READ(GPIO_IN1_REG);
            previous_cycle = esp_cpu_get_cycle_count();
        }
    }
    portENABLE_INTERRUPTS();
}

static void mapper_task(void *argument)
{
    (void)argument;
    while (true) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!__atomic_load_n(&s_requested, __ATOMIC_ACQUIRE)) {
            continue;
        }
        __atomic_store_n(&s_active, true, __ATOMIC_RELEASE);
        mapper_poll_loop();
        __atomic_store_n(&s_active, false, __ATOMIC_RELEASE);
    }
}

static esp_err_t wait_for_active_state(bool expected)
{
    for (unsigned elapsed = 0; elapsed < MAPPER_TRANSITION_TIMEOUT_US;
         ++elapsed) {
        if (__atomic_load_n(&s_active, __ATOMIC_ACQUIRE) == expected) {
            return ESP_OK;
        }
        esp_rom_delay_us(1);
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t mapper_trace_load_saved(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("mapperdiag", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    mapper_trace_blob_t blob;
    size_t size = sizeof(blob);
    err = nvs_get_blob(handle, "trace", &blob, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK || size != sizeof(blob) ||
        blob.magic != MAPPER_TRACE_MAGIC ||
        blob.version != MAPPER_TRACE_VERSION) {
        return err == ESP_OK ? ESP_ERR_INVALID_VERSION : err;
    }
    s_saved_trace = blob.trace;
    s_trace_saved = true;
    return ESP_OK;
}

static esp_err_t mapper_trace_save(void)
{
    __atomic_store_n(&s_trace_frozen, true, __ATOMIC_RELEASE);
    __atomic_store_n(&s_requested, false, __ATOMIC_RELEASE);
    ESP_RETURN_ON_ERROR(wait_for_active_state(false), TAG,
                        "mapper trace could not stop polling");

    mapper_trace_blob_t blob = {
        .magic = MAPPER_TRACE_MAGIC,
        .version = MAPPER_TRACE_VERSION,
    };
    memcpy(&blob.trace, (const void *)&s_live_trace, sizeof(blob.trace));
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open("mapperdiag", NVS_READWRITE, &handle), TAG,
                        "mapper trace NVS open failed");
    esp_err_t err = nvs_set_blob(handle, "trace", &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        s_saved_trace = blob.trace;
        __atomic_store_n(&s_trace_saved, true, __ATOMIC_RELEASE);
        ESP_LOGI(TAG, "saved mapper trace: %" PRIu32 " candidates, %" PRIu32
                      " stored records",
                 blob.trace.total_candidates, blob.trace.stored_count);
    }
    return err;
}

static void mapper_trace_button_task(void *argument)
{
    (void)argument;
    bool released = gpio_get_level(PIN_BOOT_BUTTON) != 0;
    while (true) {
        const bool pressed = gpio_get_level(PIN_BOOT_BUTTON) == 0;
        if (released && pressed) {
            esp_err_t err = mapper_trace_save();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "mapper trace save failed: %s",
                         esp_err_to_name(err));
            }
            released = false;
        } else if (!pressed) {
            released = true;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t mapper_runtime_init(void)
{
    __atomic_store_n(&s_requested, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_active, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_trace_frozen, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_trace_saved, false, __ATOMIC_RELEASE);
    ESP_RETURN_ON_ERROR(mapper_trace_load_saved(), TAG,
                        "saved mapper trace load failed");
    const gpio_config_t boot_config = {
        .pin_bit_mask = UINT64_C(1) << PIN_BOOT_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&boot_config), TAG,
                        "BOOT trace button configuration failed");
    if (xTaskCreatePinnedToCore(
            mapper_task, "mapper_runtime", MAPPER_TASK_STACK, NULL,
            configMAX_PRIORITIES - 1, &s_mapper_task, MAPPER_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(mapper_trace_button_task, "mapper_trace_save",
                                4096, NULL, 4, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t mapper_runtime_start(void)
{
    if (s_mapper_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (__atomic_load_n(&s_active, __ATOMIC_ACQUIRE)) {
        return ESP_OK;
    }

    const gpio_config_t input_config = {
        .pin_bit_mask = (UINT64_C(1) << PIN_MAPPER_CPU_D0) |
                        (UINT64_C(1) << PIN_MAPPER_ROMSEL_INV) |
                        (UINT64_C(1) << PIN_MAPPER_PRG_EN_N),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&input_config), TAG,
                        "mapper GPIO input configuration failed");

    /*
     * U2 disables its spare B8 output while LOAD_MODE is high. Keep GPIO42
     * at a defined zero during that interval without requiring an additional
     * bodge resistor. The weak pull-down is negligible once U2 drives B8 in
     * RUN mode.
     */
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(PIN_MAPPER_CPU_D0,
                                           GPIO_PULLDOWN_ONLY), TAG,
                        "mapper D0 pull-down configuration failed");

    /* Every LOAD->RUN handoff starts in CHR bank 0 with fresh diagnostics. */
    s_bank0_writes = 0u;
    s_bank1_writes = 0u;
    s_current_bank = 0u;
    s_live_trace.total_candidates = 0u;
    s_live_trace.stored_count = 0u;
    __atomic_store_n(&s_trace_frozen, false, __ATOMIC_RELEASE);
    gpio_set_level(PIN_CHR_BANK_LOAD, 0);
    ESP_RETURN_ON_ERROR(
        gpio_set_direction(PIN_CHR_BANK_LOAD, GPIO_MODE_OUTPUT), TAG,
        "CHR bank output configuration failed");

    __atomic_store_n(&s_requested, true, __ATOMIC_RELEASE);
    xTaskNotifyGive(s_mapper_task);
    ESP_RETURN_ON_ERROR(wait_for_active_state(true), TAG,
                        "mapper core did not enter RUN polling");
    return ESP_OK;
}

esp_err_t mapper_runtime_stop(void)
{
    if (s_mapper_task == NULL) {
        return ESP_OK;
    }
    __atomic_store_n(&s_requested, false, __ATOMIC_RELEASE);
    ESP_RETURN_ON_ERROR(wait_for_active_state(false), TAG,
                        "mapper core did not leave RUN polling");
    return ESP_OK;
}

bool mapper_runtime_active(void)
{
    return __atomic_load_n(&s_active, __ATOMIC_ACQUIRE);
}

void mapper_runtime_get_stats(mapper_runtime_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    stats->bank0_writes = __atomic_load_n(&s_bank0_writes, __ATOMIC_RELAXED);
    stats->bank1_writes = __atomic_load_n(&s_bank1_writes, __ATOMIC_RELAXED);
    stats->current_bank = __atomic_load_n(&s_current_bank, __ATOMIC_RELAXED);
    stats->trace_candidates = s_live_trace.total_candidates;
    stats->trace_stored = s_live_trace.stored_count;
    stats->trace_saved = __atomic_load_n(&s_trace_saved, __ATOMIC_ACQUIRE);
    stats->saved_trace_candidates =
        stats->trace_saved ? s_saved_trace.total_candidates : 0u;
    stats->saved_trace_records =
        stats->trace_saved ? s_saved_trace.stored_count : 0u;
}

bool mapper_runtime_get_saved_trace(mapper_trace_snapshot_t *trace)
{
    if (trace == NULL ||
        !__atomic_load_n(&s_trace_saved, __ATOMIC_ACQUIRE)) {
        return false;
    }
    *trace = s_saved_trace;
    return true;
}
