# Positive crash signal for harness-run apps

Status: proposed

This plan extends `doc/building.md` (how the executables are built and run)
with a run artifact that says whether a smoke-tested run reached steady state.

When an agent runs an erhe executable (editor, example) for smoke testing, the
only crash signal today is "exit code != 0 plus maybe empty output". That
misses a crash whose exit looks like a clean timeout, and it reports a false
positive when a user quit returns a nonzero code. A run that produced no output
inside the timeout window reads as "fine" even when the process crashed.

## Wanted

A per-process artifact that states "I reached steady state without crashing" or
"I crashed, here is where".

- The app writes a `logs/<exe>_run.marker` line (`startup_complete`,
  `shutdown_clean`) at well-defined points. The smoke-test wrapper greps for
  the expected marker; its absence plus a nonzero exit code is a crash.
- A postmortem hook pairs with it: a top-level `std::terminate_handler` and
  `SetUnhandledExceptionFilter` that flushes spdlog, writes a `crashed at: ...`
  marker, and on Windows writes a minidump next to the marker. The wrapper
  reads the marker for the failure mode and the dump for the postmortem.
- One canonical wrapper (`scripts/smoke_test.py`) returns a structured result,
  so every session has the same invocation.
