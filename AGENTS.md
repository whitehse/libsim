# AGENTS.md — libsim

**Project identity**: Pure C **simulation primitives** for fuzzing and unit-host
tests of Edge Platform apps. **No syscalls, no threads, no wall clock.** Hosts
drive a virtual clock; timers and (later) net/uring fire only when that clock
advances.

**Program track**: Track 0 (`edge-platform-program-design.md`).  
**Current milestone**: **P0.1** — scaffold + `sim_clock` + `sim_timer`.

## Key commands

```bash
cmake -B build -S . && cmake --build build
ctest --test-dir build --output-on-failure
```

## Documentation map

| Doc | Role |
|-----|------|
| `AGENTS.md` | This file |
| `ARCHITECTURE.md` | Boundaries and host classes |
| `TODO.md` | P0.x checklist |
| `docs/DOMAIN.md` | Glossary |
| Program design | `~/edge-platform-program-design.md` |

## Operating rules

- Never call OS time, sockets, files, or threads inside libsim.
- Time only moves via `sim_clock_advance_*` / `sim_clock_set_*`.
- Events are **pull** (`sim_timer_next_event`), not callbacks.
- C11, CMake ≥ 3.20, `-Wall -Wextra -Wpedantic -Werror`.
- Opaque types; create-time allocation only for mgr/clock objects.

## Definition of done

- Builds clean under strict warnings.
- `ctest` passes.
- Docs match exported headers.

## Current status

**P0.1**: `sim_clock` + `sim_timer` + smoke tests.  
**Next**: **P0.2** `sim_net` (in-memory pipes for class B/C hosts).

## Host classes (preview)

| Class | Uses |
|-------|------|
| **A** (edgehost) | clock + timer + net + sim_uring (P0.3) |
| **B/C** (CPE agent, mobile) | clock + timer + net only |
