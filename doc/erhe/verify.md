# erhe_verify

Stability: stable

## Purpose
Provides two foundational assertion macros used throughout the entire erhe codebase: `ERHE_VERIFY(expression)` for runtime condition checks and `ERHE_FATAL(format, ...)` for unconditional abort with a formatted error message. These are always active (not disabled in release builds) and include file/line information.

## Key Types
No classes -- macros plus a few free functions.

## Public API
- `ERHE_VERIFY(expression)` -- Evaluates `expression`; if false, reports the expression, function name, file, and line, then aborts. On MSVC, triggers `DebugBreak()` first.
- `ERHE_FATAL(format, ...)` -- Reports a printf-style formatted message with file and line, then aborts. On MSVC, triggers `DebugBreak()` first. On other compilers, calls `__builtin_trap()`.
- `erhe_report_fatal(file, line, format, ...)` -- What both macros call before aborting: prints the `file:line message` line to stdout and the callstack (cpptrace) to stderr (both to logcat under tag `erhe` on Android), then passes message and callstack to the fatal handler.
- `erhe_set_fatal_handler(Erhe_fatal_handler)` -- Installs `void (*)(const char* message, const char* callstack)`. `erhe::log::initialize_log_sinks()` installs one that writes both to `logs/log.txt` and flushes, so a crash reason is in the log file; before logging is initialized only stdout/stderr carry it. A fatal failure inside the handler skips the handler.
- `erhe_dump_callstack()` / `erhe_get_callstack()` -- Print the current callstack to stderr / return it as a string.

## Dependencies
- cpptrace (callstacks).
- On MSVC/Windows: `<windows.h>` for `DebugBreak()`.
- On Android: liblog.

## Notes
- Unlike standard `assert()`, these macros are never compiled out -- they remain active in all build configurations.
- On MSVC, `std::source_location` is used for file/line info; on other compilers, `__FILE__` and `__LINE__` are used.
- The `ERHE_FATAL` macro is marked with `__builtin_unreachable()` (non-MSVC) so the compiler knows control never returns.
