# ADR-001: Host classes and optional sim_uring

## Status

Accepted

## Date

2026-07-18

## Context

Edge Platform has three host classes (program design Decision X3):

- **A** — edgehost production uses Linux io_uring; fuzz needs `sim_uring`.
- **B/C** — CPE agent (libuv) and mobile cores must not pull uring APIs into
  device/phone toolchains.

P0.3 added `sim_uring`. P0.4 needs a policy so B/C consumers stay clean.

## Decision

1. **Default builds** include `sim_uring` (class A ready).
2. **`LIBSIM_NO_URING`** (CMake `LIBSIM_NO_URING=ON` or compile definition)
   omits `sim_uring.c` and excludes `sim_uring.h` from the umbrella `sim.h`.
3. **Fuzz helpers** live in `sim_fuzz.h`:
   - `sim_fuzz_drive_bc` — always available
   - `sim_fuzz_drive_a` — only when uring is compiled in
4. Document usage in [guides/fuzz-policy.md](../guides/fuzz-policy.md).

## Consequences

- edgehost links default libsim and uses uring sim in `sim_main` (P1.5).
- Agent/mobile may pin libsim with `-DLIBSIM_NO_URING=ON`.
- Single repo serves all classes; no fork of libsim.

## Alternatives considered

| Option | Why not |
|--------|---------|
| Separate libsim_uring package | Extra pin/repo churn for one module |
| Always compile uring stubs for B/C | Risk of accidental API use on devices |
