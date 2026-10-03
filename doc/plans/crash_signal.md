# Positive crash signal for harness-run apps

Status: in progress

This plan extends `doc/agents/debugging.md` ("Crash reporting": the editor's
crash handler, the minidump and the `ERHE_FATAL` report in `logs/log.txt`)
with a run artifact that says whether a smoke-tested run reached steady state
and ended cleanly.

A smoke-testing agent detects a crash from the exit code, the `[crash]` lines
on stderr, a `logs/editor_crash_*.dmp` on Windows and the
`Main loop: completed frame <n>` lines (n <= 12) in `logs/log.txt`. The frame
lines show that the editor reached the main loop; nothing states that a run
ended cleanly, other executables (`src/example`, `src/hello_swap`,
`src/hextiles`) emit no such line, and on macOS / Linux a fault leaves no
artifact beyond the exit status.

## Outstanding work

1. **Run marker.** Each executable writes `logs/<exe>_run.marker` lines
   `startup_complete` (the editor: at the frame where it logs
   `Main loop: completed frame 12`) and `shutdown_clean` (after an orderly
   shutdown returns from the main loop). A run whose marker lacks
   `shutdown_clean` and whose exit code is nonzero is a crash.
2. **Crash line in the marker.** The paths that already report a crash write
   `crashed at: <reason>` to the marker before terminating: the Windows
   `unhandled_exception_filter` and `abort_handler` in
   `src/editor/crash_handler.cpp`, and the `erhe_report_fatal()` handler that
   `erhe::log` installs (`src/erhe/log/erhe_log/log.cpp`).
3. **`std::terminate` handler.** `install_crash_handler()` installs a
   `std::set_terminate` handler on every platform that logs the active
   exception's `what()` and the callstack through erhe logging, writes the
   marker crash line, then aborts (reaching the Windows abort path, so the
   minidump is still written).
4. **macOS / Linux postmortem.** `install_crash_handler()` installs
   `SIGSEGV` / `SIGBUS` / `SIGILL` / `SIGFPE` / `SIGABRT` handlers that write
   the callstack (`erhe_dump_callstack()`) and the marker crash line, then
   re-raise with the default disposition so the exit status and any core dump
   stay intact. Under a debugger the handlers stay uninstalled, as on Windows.
5. **`scripts/smoke_test.py`.** One wrapper launches an executable with
   `ERHE_AI_DRIVER` set, waits on the marker (never on a timer), applies the
   per-run timeout, and prints a structured result: `ok`, `crashed`
   (marker reason, minidump path when present), `hung` (last marker line) or
   `no_marker`, with the exit code.
