/**
 * P0.1 smoke: virtual clock + timer wheel.
 */
#include "sim.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_clock_basic(void)
{
    sim_clock_t *clk = sim_clock_create();
    assert(clk);
    assert(sim_clock_now_ns(clk) == 0);
    sim_clock_advance_ns(clk, 1000);
    assert(sim_clock_now_ns(clk) == 1000);
    sim_clock_advance_ms(clk, 2);
    assert(sim_clock_now_ns(clk) == 1000 + 2 * 1000000ull);
    sim_clock_set_ns(clk, 42);
    assert(sim_clock_now_ns(clk) == 42);
    sim_clock_destroy(clk);
    printf("  PASS: clock basic\n");
}

static void test_timer_oneshot(void)
{
    sim_clock_t *clk = sim_clock_create();
    sim_timer_mgr_t *mgr = sim_timer_create(clk);
    sim_timer_event_t ev;
    uint64_t id;
    int n = 0;
    int marker = 7;

    assert(mgr);
    id = sim_timer_schedule_after(mgr, 5000, &marker);
    assert(id != 0);
    assert(sim_timer_count(mgr) == 1);

    assert(sim_timer_next_event(mgr, &ev) == 0);

    sim_clock_advance_ns(clk, 4999);
    assert(sim_timer_next_event(mgr, &ev) == 0);

    sim_clock_advance_ns(clk, 1);
    assert(sim_timer_next_event(mgr, &ev) == 1);
    assert(ev.type == SIM_TIMER_EVENT_FIRED);
    assert(ev.timer_id == id);
    assert(ev.user == &marker);
    assert(sim_timer_count(mgr) == 0);
    assert(sim_timer_next_event(mgr, &ev) == 0);

    (void)n;
    sim_timer_destroy(mgr);
    sim_clock_destroy(clk);
    printf("  PASS: timer oneshot\n");
}

static void test_timer_repeat_and_cancel(void)
{
    sim_clock_t *clk = sim_clock_create();
    sim_timer_mgr_t *mgr = sim_timer_create_with_config(
        clk, &(sim_timer_config_t){ .max_timers = 8 });
    sim_timer_event_t ev;
    uint64_t id;
    int fires = 0;

    id = sim_timer_schedule_every(mgr, 1000, NULL);
    assert(id != 0);

    sim_clock_advance_ns(clk, 1000);
    while (sim_timer_next_event(mgr, &ev)) {
        assert(ev.timer_id == id);
        fires++;
    }
    assert(fires == 1);

    sim_clock_advance_ns(clk, 3000);
    fires = 0;
    while (sim_timer_next_event(mgr, &ev)) {
        fires++;
    }
    assert(fires == 3); /* 2ms, 3ms, 4ms deadlines */

    assert(sim_timer_cancel(mgr, id) == 0);
    assert(sim_timer_count(mgr) == 0);
    sim_clock_advance_ns(clk, 10000);
    assert(sim_timer_next_event(mgr, &ev) == 0);

    sim_timer_destroy(mgr);
    sim_clock_destroy(clk);
    printf("  PASS: timer repeat + cancel\n");
}

static void test_timer_full(void)
{
    sim_clock_t *clk = sim_clock_create();
    sim_timer_config_t cfg = { .max_timers = 2 };
    sim_timer_mgr_t *mgr = sim_timer_create_with_config(clk, &cfg);
    assert(sim_timer_schedule_after(mgr, 1, NULL) != 0);
    assert(sim_timer_schedule_after(mgr, 2, NULL) != 0);
    assert(sim_timer_schedule_after(mgr, 3, NULL) == 0);
    sim_timer_destroy(mgr);
    sim_clock_destroy(clk);
    printf("  PASS: timer full\n");
}

int main(void)
{
    printf("libsim_clock_timer:\n");
    test_clock_basic();
    test_timer_oneshot();
    test_timer_repeat_and_cancel();
    test_timer_full();
    printf("ALL PASS\n");
    return 0;
}
