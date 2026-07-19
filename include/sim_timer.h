/**
 * @file sim_timer.h
 * @brief Virtual timer wheel driven by sim_clock (no syscalls, no threads).
 *
 * Schedule one-shot or repeating deadlines in virtual time. Call
 * sim_timer_poll after advancing the clock to drain due events.
 */
#ifndef SIM_TIMER_H
#define SIM_TIMER_H

#include "sim_clock.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sim_timer_mgr sim_timer_mgr_t;

typedef struct {
    /** Max concurrent timers (0 → default 64). */
    size_t max_timers;
} sim_timer_config_t;

typedef enum {
    SIM_TIMER_EVENT_NONE = 0,
    SIM_TIMER_EVENT_FIRED = 1
} sim_timer_event_type_t;

typedef struct {
    sim_timer_event_type_t type;
    uint64_t               timer_id;
    uint64_t               deadline_ns;
    void                  *user;
} sim_timer_event_t;

sim_timer_config_t sim_timer_default_config(void);

/**
 * Create a timer manager bound to an existing clock (not owned).
 * Returns NULL on OOM or bad args.
 */
sim_timer_mgr_t *sim_timer_create(sim_clock_t *clk);
sim_timer_mgr_t *sim_timer_create_with_config(sim_clock_t *clk,
                                             const sim_timer_config_t *cfg);
void sim_timer_destroy(sim_timer_mgr_t *mgr);

/**
 * Schedule a one-shot timer at absolute deadline_ns.
 * Returns timer id (>0) or 0 on failure (full / bad args).
 */
uint64_t sim_timer_schedule_at(sim_timer_mgr_t *mgr, uint64_t deadline_ns,
                               void *user);

/**
 * Schedule a one-shot timer delay_ns after now.
 */
uint64_t sim_timer_schedule_after(sim_timer_mgr_t *mgr, uint64_t delay_ns,
                                  void *user);

/**
 * Schedule a repeating timer: first fire after interval_ns, then every
 * interval_ns. Returns id or 0.
 */
uint64_t sim_timer_schedule_every(sim_timer_mgr_t *mgr, uint64_t interval_ns,
                                  void *user);

/** Cancel a timer. Returns 0 if cancelled, 1 if id unknown. */
int sim_timer_cancel(sim_timer_mgr_t *mgr, uint64_t timer_id);

/**
 * Pull next due event for current clock time. Returns 1 if an event was
 * written, 0 if none due.
 */
int sim_timer_next_event(sim_timer_mgr_t *mgr, sim_timer_event_t *out);

/** Number of active (not cancelled) timers. */
size_t sim_timer_count(const sim_timer_mgr_t *mgr);

#ifdef __cplusplus
}
#endif

#endif /* SIM_TIMER_H */
