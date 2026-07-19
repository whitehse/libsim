# libsim fuzz policy (P0.4)

## Goals

- Crash-free operation under adversarial byte streams for **clock**, **timer**,
  **net**, and (class A) **uring** sim.
- Shared helpers so edgehost / agent / mobile do not each invent opcode
  decoders (`sim_fuzz.h`).
- Keep class B/C builds free of uring when requested.

## Host classes

| Class | Consumers | Modules | Build flag |
|-------|-----------|---------|------------|
| **A** | edgehost `sim_main` | clock, timer, net, **uring** | default |
| **B/C** | CPE agent, mobile | clock, timer, net | `-DLIBSIM_NO_URING=ON` |

Class B/C **must not** require linking `sim_uring`. Define `LIBSIM_NO_URING` when
including `sim.h` if the umbrella header is used.

## Harnesses

| Target | Source | Drive API |
|--------|--------|-----------|
| `fuzz_sim_bc` | `fuzz/fuzz_sim_bc.c` | `sim_fuzz_drive_bc` |
| `fuzz_sim_a` | `fuzz/fuzz_sim_a.c` | `sim_fuzz_drive_a` |

Enable with:

```bash
cmake -B build-fuzz -S . -DBUILD_FUZZ=ON -DCMAKE_C_COMPILER=clang
cmake --build build-fuzz --target fuzz_sim_bc fuzz_sim_a
./build-fuzz/fuzz_sim_bc -max_total_time=60
./build-fuzz/fuzz_sim_a -max_total_time=60
```

Requires clang with libFuzzer (`-fsanitize=fuzzer`).

## Helper contract

- `sim_fuzz_cursor_*` — safe reads; never read past end.
- `sim_fuzz_drive_bc` / `sim_fuzz_drive_a` — create/destroy all sim objects
  per input; bound step counts; return 0 (libFuzzer uses crashes as signal).
- Hosts may call the same drivers from their own `LLVMFuzzerTestOneInput` or
  compose lower-level cursor helpers for custom opcodes.

## What we do not fuzz here

- Real liburing / kernel io_uring.
- Production TLS, DNS, or sibling protocol parsers (fuzz those in their repos
  with local stubs if needed — program design allows agent/mobile parser fuzz
  before full libsim integration).

## CI recommendation

- Always: `ctest` (includes `sim_fuzz` unit smoke without libFuzzer).
- Optional nightly: short `fuzz_sim_bc` / `fuzz_sim_a` runs with ASan.

## Related

- [ADR-001](../decisions/001-host-classes-and-uring.md)
- Program design Track 0 / Decision X3 (host classes)
