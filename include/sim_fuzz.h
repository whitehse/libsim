/**
 * @file sim_fuzz.h
 * @brief Byte-stream helpers to drive libsim from libFuzzer inputs (P0.4).
 *
 * Hosts call these from LLVMFuzzerTestOneInput to exercise clock/timer/net
 * (and optionally uring) without inventing per-repo opcode decoders.
 * No syscalls.
 */
#ifndef SIM_FUZZ_H
#define SIM_FUZZ_H

#include "sim_clock.h"
#include "sim_net.h"
#include "sim_timer.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Cursor over a fuzz buffer. */
typedef struct {
    const uint8_t *data;
    size_t         size;
    size_t         pos;
} sim_fuzz_cursor_t;

void sim_fuzz_cursor_init(sim_fuzz_cursor_t *c, const uint8_t *data, size_t size);

/** Bytes remaining. */
size_t sim_fuzz_remaining(const sim_fuzz_cursor_t *c);

/** Read one byte; returns 0 and leaves *ok=0 if exhausted. */
uint8_t sim_fuzz_u8(sim_fuzz_cursor_t *c, int *ok);

/** Little-endian integers; *ok=0 if not enough bytes. */
uint16_t sim_fuzz_u16(sim_fuzz_cursor_t *c, int *ok);
uint32_t sim_fuzz_u32(sim_fuzz_cursor_t *c, int *ok);

/**
 * Copy up to max bytes into dst; actual length written to *out_len.
 * Returns 0 on success, -1 if cursor exhausted before any byte when max>0
 * and no length prefix was available — still safe if max==0.
 * Format: 1 byte length N, then min(N, max, remaining) payload bytes.
 */
int sim_fuzz_bytes(sim_fuzz_cursor_t *c, uint8_t *dst, size_t max, size_t *out_len);

/**
 * Drive clock + timer + net with remaining fuzz bytes as opcodes.
 * Creates temporary objects, runs bounded ops, destroys them.
 * Safe for empty input. Returns 0 always (crash = failure).
 *
 * Opcode map (low 4 bits of each op byte):
 *  0 advance_ns (u16)
 *  1 schedule_after (u16 delay)
 *  2 schedule_every (u16 interval) — cancel previous every if any
 *  3 cancel last timer
 *  4 drain timer events
 *  5 pipe (create if none)
 *  6 send (len-prefixed payload on end A)
 *  7 recv (up to 64 on end B)
 *  8 close A
 *  9 close B
 * 10 listen+socket+connect+accept once
 * 11 drain net events
 * 12..15 nop
 */
int sim_fuzz_drive_bc(const uint8_t *data, size_t size);

#if !defined(LIBSIM_NO_URING)
#include "sim_uring.h"

/**
 * Class-A drive: BC ops plus uring submit/progress over a pipe.
 * Extra opcodes in high nibble when low nibble is 12–15 reserved for uring:
 *  Actually: if (op & 0x80) uring path else bc path mixed in one stream.
 *
 * When (op & 0x80):
 *  0x80 nop sqe submit
 *  0x81 prep_recv + submit + progress
 *  0x82 prep_send payload + submit + progress
 *  0x83 prep_poll IN + progress
 *  0x84 prep_timeout u16 + progress after advance
 *  else treated as nop
 */
int sim_fuzz_drive_a(const uint8_t *data, size_t size);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SIM_FUZZ_H */
