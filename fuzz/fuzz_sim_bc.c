/* libFuzzer harness — class B/C (clock, timer, net). No uring. */
#include "sim_fuzz.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    return sim_fuzz_drive_bc(data, size);
}
