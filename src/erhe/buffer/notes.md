# erhe_buffer

## Purpose
Provides a simple CPU-side buffer abstraction with an allocation interface. Used for
staging data on the CPU before uploading to GPU buffers, or for any scenario requiring
a contiguous byte buffer with bump-pointer allocation and thread-safe access.

## Key Types
- `IBuffer` -- Abstract interface for a byte buffer supporting capacity queries, aligned allocation, and span access.
- `Cpu_buffer` -- Concrete implementation backed by `std::vector<std::byte>` with mutex-protected bump allocation.

## Public API
- `allocate_bytes(byte_count, alignment)` -- Bump-allocate from the buffer, returns byte offset or nullopt.
- `get_span()` -- Returns a `std::span<std::byte>` to the entire backing memory.
- `get_capacity_byte_count()` / `get_used_byte_count()` / `get_available_byte_count(alignment)` -- Query buffer state.

## Dependencies
- **erhe libraries:** `erhe::utility` (public), `erhe::profile`, `erhe::verify` (private)
- **External:** Standard library only

## Notes
- Allocation is bump-only (no deallocation). The buffer cannot be compacted or freed partially.
- Thread safety is provided via a profiler-aware mutex wrapping `std::mutex`.
- Move-only; copy is deleted.
