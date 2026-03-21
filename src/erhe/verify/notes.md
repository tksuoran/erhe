# erhe_verify

## Purpose
Provides two foundational assertion macros used throughout the entire erhe codebase: `ERHE_VERIFY(expression)` for runtime condition checks and `ERHE_FATAL(format, ...)` for unconditional abort with a formatted error message. These are always active (not disabled in release builds) and include file/line information.

## Key Types
No classes -- this is a header-only macro library.

## Public API
- `ERHE_VERIFY(expression)` -- Evaluates `expression`; if false, prints the expression, function name, file, and line, then aborts. On MSVC, triggers `DebugBreak()` first.
- `ERHE_FATAL(format, ...)` -- Prints a printf-style formatted message with file and line, then aborts. On MSVC, triggers `DebugBreak()` first. On other compilers, calls `__builtin_trap()`.

## Dependencies
- Standard library only (`<cstdio>`, `<cstdlib>`, `<source_location>` on MSVC).
- On MSVC/Windows: `<windows.h>` for `DebugBreak()`.

## Notes
- Unlike standard `assert()`, these macros are never compiled out -- they remain active in all build configurations.
- On MSVC, `std::source_location` is used for file/line info; on other compilers, `__FILE__` and `__LINE__` are used.
- The `ERHE_FATAL` macro is marked with `__builtin_unreachable()` (non-MSVC) so the compiler knows control never returns.
- This is a header-only library (single header `verify.hpp`).
