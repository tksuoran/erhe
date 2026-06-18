#pragma once

namespace editor {

// Install process-wide crash / abort handling.
//
// On Windows this:
//   - suppresses the modal Windows Error Reporting popup and the debug CRT
//     "Abort / Retry / Ignore" dialog (routing assert / error reports to stderr),
//     so a fatal abort() does not hang headless / automated runs;
//   - intercepts abort() (e.g. OpenNL's nl_assert, which calls abort() directly
//     and offers no hook), unhandled SEH faults (access violations, ...) and
//     writes a minidump to logs/editor_crash_<pid>_<tick>.dmp before terminating
//     the process with a non-zero exit code.
//
// No-op on non-Windows platforms, where abort() already fails fast (no modal
// dialog) and a core dump / debugger provides the post-mortem.
//
// When a debugger is attached (e.g. launched with F5 from Visual Studio) this is
// a no-op: the default behaviour is left in place so asserts / abort() / faults
// break into the debugger for live inspection. The handler only takes over for
// unattended / headless runs.
//
// Call once, as early as possible in main(), before any subsystem starts.
void install_crash_handler();

// True when a debugger is currently attached (Windows: IsDebuggerPresent; always
// false on other platforms). Used to make Geogram asserts break into the debugger
// at the failure site (GEO::ASSERT_BREAKPOINT) instead of throwing - a throw from
// Geogram's own parallel worker threads (e.g. parallel_delaunay_3d) escapes every
// editor-side catch and just terminates the process.
[[nodiscard]] auto is_debugger_present() -> bool;

} // namespace editor
