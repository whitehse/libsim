# DOMAIN.md — libsim

| Term | Meaning |
|------|---------|
| **Virtual time** | Library-owned counter (ns); not wall clock |
| **Advance** | Host moves virtual time forward; may cause timers to become due |
| **Timer mgr** | Set of one-shot/repeating deadlines bound to one clock |
| **Pull event** | Host calls `sim_timer_next_event` after advances |
| **Class A host** | Needs full uring sim (edgehost) — P0.3 |
| **Class B/C host** | Needs clock/timer/net only |
| **sim_net pipe** | Two connected stream endpoints; send on A → recv on B |
| **EAGAIN** | Ring full (send) or empty (recv); non-blocking shape |
| **SQE / CQE** | Submission / completion queue entries (`sim_uring`) |
| **progress** | Retry deferred uring ops after net or clock changes |
