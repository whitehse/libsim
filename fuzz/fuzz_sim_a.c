/* libFuzzer harness — class A (includes sim_uring). */
#include "sim_fuzz.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
#if defined(LIBSIM_NO_URING)
    return sim_fuzz_drive_bc(data, size);
#else
    return sim_fuzz_drive_a(data, size);
#endif
}
