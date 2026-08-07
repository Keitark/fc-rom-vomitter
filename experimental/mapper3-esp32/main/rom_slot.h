#pragma once

#include <stdbool.h>
#include <stdint.h>

bool rom_slot_sequence_newer(uint32_t candidate, uint32_t current);
int rom_slot_choose(bool a_valid, uint32_t a_sequence,
                    bool b_valid, uint32_t b_sequence);
