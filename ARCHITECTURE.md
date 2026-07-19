# ARCHITECTURE.md — libsim

## Purpose

Provide a **shared, deterministic** stand-in for time (and later network /
io_uring) so edgehost, CPE agent, and mobile cores can be unit-tested and
fuzzed without kernel features.

## P0.1 modules

| Module | Role |
|--------|------|
| `sim_clock` | Monotonic virtual time (ns); host advances explicitly |
| `sim_timer` | One-shot / repeating deadlines; pull events when due |

## Invariants

1. **No syscalls** in library code.
2. **No wall clock** — `sim_clock_now_*` is pure state.
3. **Pull events** — no timer callbacks into the host.
4. **Clock not owned by timer** — host destroys clock after timer mgr.

## Planned (not in P0.1)

| Module | PR |
|--------|-----|
| `sim_net` | P0.2 — in-memory bidirectional pipes / packet queues |
| `sim_uring` | P0.3 — class-A submission/completion sim |
| fuzz helpers | P0.4 |

## Consumers

- `~/edgehost` (class A) — FindLibsim after pins updated
- `~/netforensics` agent `sim_main` (class B)
- mobile core tests (class C)
