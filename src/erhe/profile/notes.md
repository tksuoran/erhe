# erhe_profile

## Purpose
Profiling abstraction layer that provides a unified macro API for instrumenting
code with scoped profiling zones, GPU profiling, memory tracking, and mutex
annotation. Dispatches to Tracy, Superluminal, NVTX, or compiles to no-ops
depending on the selected backend.

## Key Types
- `Profile_allocator<T>` -- STL allocator that reports allocations/frees to the profiler
- `erhe::vector<T>` -- alias for `std::vector<T, Profile_allocator<T>>` when profiling is enabled

## Public API (Macros)
- `ERHE_PROFILE_FUNCTION()` -- instrument current function scope
- `ERHE_PROFILE_SCOPE(name)` -- named profiling scope
- `ERHE_PROFILE_COLOR(name, color)` -- colored profiling scope
- `ERHE_PROFILE_GPU_SCOPE(name)` -- GPU profiling zone
- `ERHE_PROFILE_GPU_CONTEXT` / `ERHE_PROFILE_FRAME_END` -- GPU context and frame markers
- `ERHE_PROFILE_MUTEX(Type, var)` -- declare a mutex visible to the profiler
- `ERHE_PROFILE_LOCKABLE_BASE(Type)` -- base type for lockable (Tracy `LockableBase` or plain type)
- `ERHE_PROFILE_MESSAGE(msg, len)` / `ERHE_PROFILE_MESSAGE_LITERAL(msg)` -- log messages to profiler
- `ERHE_PROFILE_MEM_ALLOC` / `ERHE_PROFILE_MEM_FREE` variants -- memory tracking

## Dependencies
- Optional externals (compile-time selected): Tracy, Superluminal PerformanceAPI, NVTX3
- `erhe::gl` -- for Tracy OpenGL integration (when `ERHE_TRACY_GL` is defined)

## Notes
- Backend is selected via `ERHE_PROFILE_LIBRARY` CMake option: `tracy`, `superluminal`, `nvtx`, or `none`.
- When no profiler is selected, all macros expand to no-ops (zero overhead).
- The `Profile_allocator` is always active (controlled by `ERHE_USE_PROFILE_ALLOCATOR`).
- Tracy integration redefines GL query functions to use erhe's dynamic loader, then undefines them after include.
- `ERHE_PROFILE_MUTEX` is used extensively throughout erhe to make mutex contention visible in Tracy.
