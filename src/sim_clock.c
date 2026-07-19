#include "sim_clock.h"

#include <stdlib.h>
#include <string.h>

struct sim_clock {
    uint64_t now_ns;
};

sim_clock_config_t sim_clock_default_config(void)
{
    sim_clock_config_t c;
    memset(&c, 0, sizeof(c));
    c.start_ns = 0;
    return c;
}

sim_clock_t *sim_clock_create(void)
{
    sim_clock_config_t c = sim_clock_default_config();
    return sim_clock_create_with_config(&c);
}

sim_clock_t *sim_clock_create_with_config(const sim_clock_config_t *cfg)
{
    sim_clock_t *clk = (sim_clock_t *)calloc(1, sizeof(*clk));
    if (!clk) {
        return NULL;
    }
    if (cfg) {
        clk->now_ns = cfg->start_ns;
    }
    return clk;
}

void sim_clock_destroy(sim_clock_t *clk)
{
    free(clk);
}

uint64_t sim_clock_now_ns(const sim_clock_t *clk)
{
    return clk ? clk->now_ns : 0;
}

void sim_clock_set_ns(sim_clock_t *clk, uint64_t ns)
{
    if (!clk) {
        return;
    }
    clk->now_ns = ns;
}

void sim_clock_advance_ns(sim_clock_t *clk, uint64_t delta_ns)
{
    if (!clk) {
        return;
    }
    if (delta_ns > UINT64_MAX - clk->now_ns) {
        clk->now_ns = UINT64_MAX;
    } else {
        clk->now_ns += delta_ns;
    }
}

uint64_t sim_clock_now_ms(const sim_clock_t *clk)
{
    return sim_clock_now_ns(clk) / 1000000ull;
}

void sim_clock_advance_ms(sim_clock_t *clk, uint64_t delta_ms)
{
    if (delta_ms > UINT64_MAX / 1000000ull) {
        sim_clock_advance_ns(clk, UINT64_MAX);
    } else {
        sim_clock_advance_ns(clk, delta_ms * 1000000ull);
    }
}
