# erhe_buffer

## Purpose

Provides buffer allocation primitives used by both GPU and CPU buffer systems. Contains the `Free_list_allocator` (reclaimable sub-allocator), `Buffer_allocation` (RAII allocation handle), `IBuffer` interface, and `Cpu_buffer` implementation.

## Key Types

- **`Free_list_allocator`** -- Sorted free list allocator with merge-on-free. Supports `allocate(byte_count, alignment)` and `free(byte_offset, byte_count)`. Thread-safe via mutex. Used by `Cpu_buffer`, `Graphics_buffer_sink`, and any system that needs reclaimable sub-allocation within a fixed-capacity buffer.

- **`Buffer_allocation`** -- Move-only RAII handle. Stores a pointer to a `Free_list_allocator` plus byte offset and count. Destructor calls `allocator->free()` to return the allocation. Moved-from state is safe (destructor is no-op).

- **`IBuffer`** -- Abstract interface for a byte buffer supporting capacity queries, allocation, deallocation, and span access.

- **`Cpu_buffer`** -- Concrete `IBuffer` backed by `std::vector<std::byte>`. Uses `Free_list_allocator` internally. Used for raytrace mesh data and staging buffers.

## Public API

- `Free_list_allocator::allocate(byte_count, alignment)` -- Returns byte offset or nullopt.
- `Free_list_allocator::free(byte_offset, byte_count)` -- Returns allocation to free list.
- `Free_list_allocator::get_capacity/get_used/get_free/get_allocation_count()` -- Query allocator state.
- `Buffer_allocation(allocator, byte_offset, byte_count)` -- RAII handle construction.
- `Cpu_buffer::allocate_bytes/free_bytes/get_span/get_allocator()` -- Buffer operations.

## Architecture

The allocation hierarchy:

```
Free_list_allocator (reusable, lives in erhe::buffer)
  ├── Used by Cpu_buffer (CPU-side, raytrace data)
  └── Used by Graphics_buffer_sink (GPU vertex/index buffers)

Buffer_allocation (RAII handle, references a Free_list_allocator)
  └── Held by Buffer_mesh (freed when mesh is destroyed)
```

## Important: Member Declaration Order

When a class holds both a `Buffer_allocation` and the buffer/allocator it was allocated from, the `Buffer_allocation` must be declared **after** the buffer so it is destroyed **first** (C++ destroys members in reverse declaration order). Incorrect order causes use-after-free.

## Dependencies

- **erhe libraries:** `erhe::utility` (public), `erhe::profile`, `erhe::verify` (private)
- **External:** Standard library only
