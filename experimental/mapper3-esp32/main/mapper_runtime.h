#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

enum {
    MAPPER_TRACE_RECORD_COUNT = 10,
    MAPPER_TRACE_TRANSITIONS_PER_RECORD = 8,
    MAPPER_TRACE_RESULT_REJECTED = 0,
    MAPPER_TRACE_RESULT_BANK0 = 1,
    MAPPER_TRACE_RESULT_BANK1 = 2,
};

typedef struct {
    uint16_t delta_cycles;
    uint8_t state;
    uint8_t reserved;
} mapper_trace_transition_t;

typedef struct {
    uint32_t ordinal;
    uint16_t poll_count;
    uint8_t transition_count;
    uint8_t result;
    mapper_trace_transition_t transitions[MAPPER_TRACE_TRANSITIONS_PER_RECORD];
} mapper_trace_record_t;

typedef struct {
    uint32_t total_candidates;
    uint32_t stored_count;
    mapper_trace_record_t records[MAPPER_TRACE_RECORD_COUNT];
} mapper_trace_snapshot_t;

/*
 * The runtime engine owns GPIO36 only while active. LOAD-mode SRAM code must
 * stop it before changing GPIO36 itself.
 */
esp_err_t mapper_runtime_init(void);
esp_err_t mapper_runtime_start(void);
esp_err_t mapper_runtime_stop(void);
bool mapper_runtime_active(void);

typedef struct {
    uint32_t bank0_writes;
    uint32_t bank1_writes;
    unsigned current_bank;
    uint32_t trace_candidates;
    uint32_t trace_stored;
    bool trace_saved;
    uint32_t saved_trace_candidates;
    uint32_t saved_trace_records;
} mapper_runtime_stats_t;

void mapper_runtime_get_stats(mapper_runtime_stats_t *stats);
bool mapper_runtime_get_saved_trace(mapper_trace_snapshot_t *trace);
