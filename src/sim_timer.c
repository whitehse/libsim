#include "sim_timer.h"

#include <stdlib.h>
#include <string.h>

#define SIM_TIMER_DEFAULT_MAX 64

typedef struct {
    int      used;
    int      repeating;
    uint64_t id;
    uint64_t deadline_ns;
    uint64_t interval_ns;
    void    *user;
} timer_slot_t;

struct sim_timer_mgr {
    sim_clock_t *clk; /* not owned */
    timer_slot_t *slots;
    size_t        max_timers;
    size_t        active;
    uint64_t      next_id;
};

sim_timer_config_t sim_timer_default_config(void)
{
    sim_timer_config_t c;
    memset(&c, 0, sizeof(c));
    c.max_timers = SIM_TIMER_DEFAULT_MAX;
    return c;
}

sim_timer_mgr_t *sim_timer_create(sim_clock_t *clk)
{
    sim_timer_config_t c = sim_timer_default_config();
    return sim_timer_create_with_config(clk, &c);
}

sim_timer_mgr_t *sim_timer_create_with_config(sim_clock_t *clk,
                                             const sim_timer_config_t *cfg)
{
    sim_timer_mgr_t *mgr;
    size_t max_t;

    if (!clk) {
        return NULL;
    }
    max_t = (cfg && cfg->max_timers) ? cfg->max_timers : SIM_TIMER_DEFAULT_MAX;
    if (max_t == 0) {
        max_t = SIM_TIMER_DEFAULT_MAX;
    }

    mgr = (sim_timer_mgr_t *)calloc(1, sizeof(*mgr));
    if (!mgr) {
        return NULL;
    }
    mgr->slots = (timer_slot_t *)calloc(max_t, sizeof(timer_slot_t));
    if (!mgr->slots) {
        free(mgr);
        return NULL;
    }
    mgr->clk = clk;
    mgr->max_timers = max_t;
    mgr->next_id = 1;
    return mgr;
}

void sim_timer_destroy(sim_timer_mgr_t *mgr)
{
    if (!mgr) {
        return;
    }
    free(mgr->slots);
    free(mgr);
}

static timer_slot_t *alloc_slot(sim_timer_mgr_t *mgr)
{
    size_t i;
    for (i = 0; i < mgr->max_timers; i++) {
        if (!mgr->slots[i].used) {
            return &mgr->slots[i];
        }
    }
    return NULL;
}

static uint64_t alloc_id(sim_timer_mgr_t *mgr)
{
    uint64_t id = mgr->next_id++;
    if (id == 0) {
        id = mgr->next_id++; /* skip 0 */
    }
    return id;
}

uint64_t sim_timer_schedule_at(sim_timer_mgr_t *mgr, uint64_t deadline_ns,
                               void *user)
{
    timer_slot_t *s;
    if (!mgr) {
        return 0;
    }
    s = alloc_slot(mgr);
    if (!s) {
        return 0;
    }
    memset(s, 0, sizeof(*s));
    s->used = 1;
    s->id = alloc_id(mgr);
    s->deadline_ns = deadline_ns;
    s->user = user;
    mgr->active++;
    return s->id;
}

uint64_t sim_timer_schedule_after(sim_timer_mgr_t *mgr, uint64_t delay_ns,
                                  void *user)
{
    uint64_t now;
    if (!mgr || !mgr->clk) {
        return 0;
    }
    now = sim_clock_now_ns(mgr->clk);
    if (delay_ns > UINT64_MAX - now) {
        return sim_timer_schedule_at(mgr, UINT64_MAX, user);
    }
    return sim_timer_schedule_at(mgr, now + delay_ns, user);
}

uint64_t sim_timer_schedule_every(sim_timer_mgr_t *mgr, uint64_t interval_ns,
                                  void *user)
{
    timer_slot_t *s;
    uint64_t now;
    if (!mgr || !mgr->clk || interval_ns == 0) {
        return 0;
    }
    s = alloc_slot(mgr);
    if (!s) {
        return 0;
    }
    now = sim_clock_now_ns(mgr->clk);
    memset(s, 0, sizeof(*s));
    s->used = 1;
    s->repeating = 1;
    s->id = alloc_id(mgr);
    s->interval_ns = interval_ns;
    if (interval_ns > UINT64_MAX - now) {
        s->deadline_ns = UINT64_MAX;
    } else {
        s->deadline_ns = now + interval_ns;
    }
    s->user = user;
    mgr->active++;
    return s->id;
}

int sim_timer_cancel(sim_timer_mgr_t *mgr, uint64_t timer_id)
{
    size_t i;
    if (!mgr || timer_id == 0) {
        return 1;
    }
    for (i = 0; i < mgr->max_timers; i++) {
        if (mgr->slots[i].used && mgr->slots[i].id == timer_id) {
            memset(&mgr->slots[i], 0, sizeof(mgr->slots[i]));
            if (mgr->active > 0) {
                mgr->active--;
            }
            return 0;
        }
    }
    return 1;
}

int sim_timer_next_event(sim_timer_mgr_t *mgr, sim_timer_event_t *out)
{
    size_t i;
    size_t best = (size_t)-1;
    uint64_t best_deadline = UINT64_MAX;
    uint64_t now;
    timer_slot_t *s;

    if (!mgr || !out || !mgr->clk) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    now = sim_clock_now_ns(mgr->clk);

    for (i = 0; i < mgr->max_timers; i++) {
        s = &mgr->slots[i];
        if (!s->used) {
            continue;
        }
        if (s->deadline_ns > now) {
            continue;
        }
        if (s->deadline_ns < best_deadline ||
            (s->deadline_ns == best_deadline && best != (size_t)-1 &&
             s->id < mgr->slots[best].id)) {
            best_deadline = s->deadline_ns;
            best = i;
        }
    }
    if (best == (size_t)-1) {
        return 0;
    }

    s = &mgr->slots[best];
    out->type = SIM_TIMER_EVENT_FIRED;
    out->timer_id = s->id;
    out->deadline_ns = s->deadline_ns;
    out->user = s->user;

    if (s->repeating && s->interval_ns > 0) {
        /* Advance one interval per event so catch-up is pull-driven. */
        if (s->interval_ns > UINT64_MAX - s->deadline_ns) {
            s->deadline_ns = UINT64_MAX;
        } else {
            s->deadline_ns += s->interval_ns;
        }
    } else {
        memset(s, 0, sizeof(*s));
        if (mgr->active > 0) {
            mgr->active--;
        }
    }
    return 1;
}

size_t sim_timer_count(const sim_timer_mgr_t *mgr)
{
    return mgr ? mgr->active : 0;
}
