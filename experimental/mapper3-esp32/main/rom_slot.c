#include "rom_slot.h"

#include <stdint.h>

bool rom_slot_sequence_newer(uint32_t candidate, uint32_t current)
{
    return (int32_t)(candidate - current) > 0;
}

int rom_slot_choose(bool a_valid, uint32_t a_sequence,
                    bool b_valid, uint32_t b_sequence)
{
    if (!a_valid && !b_valid) {
        return -1;
    }
    if (a_valid && !b_valid) {
        return 0;
    }
    if (!a_valid && b_valid) {
        return 1;
    }
    return rom_slot_sequence_newer(b_sequence, a_sequence) ? 1 : 0;
}
