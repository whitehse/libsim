# ARCHITECTURE.md — libsim

## Purpose

Provide a **shared, deterministic** stand-in for time (and later network /
io_uring) so edgehost, CPE agent, and mobile cores can be unit-tested and
fuzzed without kernel features.

## Modules

| Module | PR | Role |
|--------|-----|------|
| `sim_clock` | P0.1 | Monotonic virtual time (ns); host advances explicitly |
| `sim_timer` | P0.1 | One-shot / repeating deadlines; pull events when due |
| `sim_net` | P0.2 | In-memory stream pipes, listen/connect/accept, ring buffers |
| `sim_uring` | P0.3 | SQ/CQ over net (+ clock timeouts); no liburing |
| `sim_fuzz` | P0.4 | Cursor + `drive_bc` / `drive_a` for libFuzzer hosts |

## Invariants

1. **No syscalls** in library code.
2. **No wall clock** — `sim_clock_now_*` is pure state.
3. **Pull events** — no timer callbacks into the host.
4. **Clock not owned by timer** — host destroys clock after timer mgr.
5. **sim_uring does not own net/clock** — host wires them; call `sim_uring_progress` after net I/O or clock advances.

## Consumers

- `~/edgehost` (class A) — FindLibsim after pins updated
- `~/netforensics` agent `sim_main` (class B)
- mobile core tests (class C)
