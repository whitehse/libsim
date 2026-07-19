/**
 * @file sim_clock.h
 * @brief Deterministic virtual clock for fuzz and unit hosts (no syscalls).
 *
 * Hosts advance time explicitly with sim_clock_advance / sim_clock_set.
 * Wall-clock time is never read inside this library.
 */
#ifndef SIM_CLOCK_H
#define SIM_CLOCK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sim_clock sim_clock_t;

typedef struct {
    /** Initial virtual time in nanoseconds (default 0). */
    uint64_t start_ns;
} sim_clock_config_t;

/** Default config: start_ns = 0. */
sim_clock_config_t sim_clock_default_config(void);

sim_clock_t *sim_clock_create(void);
sim_clock_t *sim_clock_create_with_config(const sim_clock_config_t *cfg);
void sim_clock_destroy(sim_clock_t *clk);

/** Current virtual time (ns since epoch of this clock). */
uint64_t sim_clock_now_ns(const sim_clock_t *clk);

/** Set absolute virtual time (ns). */
void sim_clock_set_ns(sim_clock_t *clk, uint64_t ns);

/** Advance virtual time by delta_ns (saturates at UINT64_MAX). */
void sim_clock_advance_ns(sim_clock_t *clk, uint64_t delta_ns);

/** Convenience: milliseconds. */
uint64_t sim_clock_now_ms(const sim_clock_t *clk);
void sim_clock_advance_ms(sim_clock_t *clk, uint64_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif /* SIM_CLOCK_H */
