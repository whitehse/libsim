/**
 * P0.4: unit smoke for fuzz cursor + drive helpers (no libFuzzer required).
 */
#include "sim_fuzz.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_cursor(void)
{
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    sim_fuzz_cursor_t c;
    int ok = 1;
    uint8_t buf[8];
    size_t len = 0;

    sim_fuzz_cursor_init(&c, data, sizeof(data));
    assert(sim_fuzz_remaining(&c) == 5);
    assert(sim_fuzz_u8(&c, &ok) == 0x01 && ok);
    assert(sim_fuzz_u16(&c, &ok) == 0x0302 && ok);

    /* length-prefixed: len=2, bytes 04 05 */
    sim_fuzz_cursor_init(&c, data + 2, 3); /* 03 04 05 — use 02 04 05 style */
    {
        const uint8_t p[] = {2, 0xAA, 0xBB, 0xCC};
        sim_fuzz_cursor_init(&c, p, sizeof(p));
        assert(sim_fuzz_bytes(&c, buf, sizeof(buf), &len) == 0);
        assert(len == 2 && buf[0] == 0xAA && buf[1] == 0xBB);
    }
    printf("  PASS: cursor\n");
}

static void test_drive_bc_empty(void)
{
    assert(sim_fuzz_drive_bc(NULL, 0) == 0);
    assert(sim_fuzz_drive_bc((const uint8_t *)"", 0) == 0);
    printf("  PASS: drive_bc empty\n");
}

static void test_drive_bc_script(void)
{
    /* op5 pipe, op0 advance, op1 schedule, op4 drain, op6 send hello, op7 recv */
    const uint8_t script[] = {
        0x05,                   /* pipe */
        0x00, 0x10, 0x00,       /* advance 16 ns */
        0x01, 0x05, 0x00,       /* schedule after 5 */
        0x00, 0x05, 0x00,       /* advance 5 */
        0x04,                   /* drain timers */
        0x06, 5, 'h', 'e', 'l', 'l', 'o', /* send */
        0x07,                   /* recv */
        0x0b,                   /* drain net events */
    };
    assert(sim_fuzz_drive_bc(script, sizeof(script)) == 0);
    printf("  PASS: drive_bc script\n");
}

#if !defined(LIBSIM_NO_URING)
static void test_drive_a(void)
{
    const uint8_t script[] = {
        0x80,                         /* uring nop */
        0x82, 2, 'x', 'y',            /* uring send */
        0x81,                         /* uring recv */
        0x00, 0x01, 0x00,             /* bc advance */
        0x84, 0x0a, 0x00,             /* timeout 10 + advance inside driver */
    };
    assert(sim_fuzz_drive_a(script, sizeof(script)) == 0);
    printf("  PASS: drive_a script\n");
}
#endif

int main(void)
{
    printf("libsim_fuzz:\n");
    test_cursor();
    test_drive_bc_empty();
    test_drive_bc_script();
#if !defined(LIBSIM_NO_URING)
    test_drive_a();
#endif
    printf("ALL PASS\n");
    return 0;
}
