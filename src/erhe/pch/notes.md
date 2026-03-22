# erhe_pch

## Purpose
Precompiled header (PCH) target for the erhe project. Aggregates commonly used
standard library, glm, spdlog, and fmt headers into a single precompiled header
to speed up compilation across all erhe libraries.

## Key Types
- No types defined. This is a build-system-only library.

## Public API
- `erhe_windows.hpp` -- safe Windows.h include with `WIN32_LEAN_AND_MEAN`, `NOMINMAX`, `STRICT` guards.
- The PCH itself includes: glm (glm.hpp, packing, quaternion, type_ptr), fmt, spdlog, and ~20 standard headers (string, vector, memory, mutex, filesystem, optional, etc.).

## Dependencies
- External: glm (header-only), spdlog (header-only), fmt
- Threads (pthreads on Linux)

## Notes
- All erhe libraries that link `erhe_pch` get the precompiled header automatically via CMake's `target_precompile_headers`.
- The `erhe_windows.hpp` header is important on Windows to prevent common macro conflicts (`min`/`max`, etc.).
- Controlled by `ERHE_USE_PRECOMPILED_HEADERS` CMake option.
