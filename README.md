# libsim

Pure C **simulation** library for Edge Platform hosts: virtual clock, timers,
and in-memory stream net (pipes / listen-accept). io_uring sim next (P0.3).
No syscalls.

## Build

```bash
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

## Quick use

```c
#include "sim.h"

sim_clock_t *clk = sim_clock_create();
sim_timer_mgr_t *tm = sim_timer_create(clk);
uint64_t id = sim_timer_schedule_after(tm, 1e6 /* 1ms */, NULL);
sim_clock_advance_ms(clk, 1);
sim_timer_event_t ev;
while (sim_timer_next_event(tm, &ev)) {
    /* handle SIM_TIMER_EVENT_FIRED */
}
sim_timer_destroy(tm);
sim_clock_destroy(clk);
```

## Docs

- [AGENTS.md](AGENTS.md)
- [ARCHITECTURE.md](ARCHITECTURE.md)
- Program design: `~/edge-platform-program-design.md` (Track 0)

## License

MIT
