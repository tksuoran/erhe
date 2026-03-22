# erhe_profile -- Code Review

## Summary
A profiling abstraction layer that provides a unified macro API across Tracy, Superluminal, NVTX, and a no-op fallback. It also includes a custom allocator for Tracy memory tracking and an `erhe::vector` type alias. The macro design is clean and the multi-backend approach works well, though there are some minor issues.

## Strengths
- Clean abstraction: all profiling calls go through `ERHE_PROFILE_*` macros, making backend swaps seamless
- Handles the Tracy/OpenGL function name collision elegantly with `#define`/`#undef` around the Tracy include
- Custom `Profile_allocator` properly tracks allocations for Tracy
- No-op fallback uses `static_cast<void>(...)` to suppress unused variable warnings correctly
- The `erhe::vector` alias allows project-wide allocation tracking when enabled

## Issues
- **[moderate]** `Profile_allocator::deallocate` (profile.hpp:157-161) passes `const std::size_t` parameter but ignores it. The Tracy `ERHE_PROFILE_MEM_FREE_S` macro does not use size, but `std::free` does not need it either. This is correct behavior but the unused parameter should be marked `[[maybe_unused]]` or use `static_cast<void>` to suppress potential warnings.
- **[minor]** profile.cpp:5 has a stray backtick: `#include <mimalloc-new-delete.h>\`` -- this appears to be a typo that would cause a compilation error if `ERHE_USE_MIMALLOC` is defined.
- **[minor]** `ERHE_PROFILE_FUNCTION()` no-op (profile.hpp:106) has a trailing semicolon in the macro definition (`#define ERHE_PROFILE_FUNCTION();`). This means using `ERHE_PROFILE_FUNCTION()` at call sites produces a double semicolon. While harmless, it is inconsistent with the Tracy version.
- **[minor]** The `#if 1` guard around `Profile_allocator` (profile.hpp:128) is a development artifact. It should be a proper named preprocessor define or removed.
- **[minor]** Memory tracking macros (`ERHE_PROFILE_MEM_ALLOC`, etc.) are missing from the Superluminal and NVTX paths. They are only defined for Tracy and the no-op fallback.

## Suggestions
- Fix the stray backtick in `profile.cpp:5`.
- Remove the trailing semicolon from the no-op `ERHE_PROFILE_FUNCTION()` definition.
- Replace `#if 1` with a named define like `#if ERHE_USE_PROFILE_ALLOCATOR` (already partially present but the `#if 1` bypasses it).
- Add the memory tracking no-op macros to the Superluminal and NVTX paths for completeness.
