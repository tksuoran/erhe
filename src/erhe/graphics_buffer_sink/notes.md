# erhe_graphics_buffer_sink

## Purpose
Bridges `erhe::primitive`'s abstract `Buffer_sink` interface to actual GPU buffers
managed by `erhe::graphics`. Allocates space using `Free_list_allocator` from
`erhe::buffer`, enqueues data transfers through `Buffer_transfer_queue`, and returns
`Buffer_sink_allocation` (containing both `Buffer_range` and `Buffer_allocation` RAII
handle) so that GPU buffer space is automatically reclaimed when meshes are destroyed.

## Key Types
- `Vertex_buffer_entry` -- Pairs a GPU `Buffer*` with its `Free_list_allocator`.
- `Graphics_buffer_sink` -- Concrete `Buffer_sink` that manages vertex/index GPU buffer allocation and data transfer.

## Public API
- Construct with a `Buffer_transfer_queue`, a list of vertex `Buffer*`, and an index `Buffer*`.
- `allocate_vertex_buffer()` / `allocate_index_buffer()` -- Reserve space, return `Buffer_sink_allocation` (range + RAII handle).
- `enqueue_vertex_data()` / `enqueue_index_data()` -- Submit data for GPU transfer.
- `buffer_ready()` -- Signal that a writer has finished producing data.
- `get_vertex_allocator(stream)` / `get_index_allocator()` -- Access allocators directly.
- Query helpers: `get_used_*_byte_count()`, `get_available_*_byte_count()`.

## Dependencies
- `erhe::buffer` -- `Free_list_allocator`, `Buffer_allocation`
- `erhe::graphics` -- `Buffer`, `Buffer_transfer_queue`
- `erhe::primitive` -- `Buffer_sink` (base class), `Buffer_range`, `Buffer_sink_allocation`, buffer writers
- `erhe::profile` -- profiling mutex macro

## Notes
Thread-safe: uses a mutex around allocations. Supports multiple vertex buffer
streams (for multi-stream vertex layouts). This is the standard sink used at
runtime; `Cpu_buffer_sink` in `erhe::primitive` is the CPU-only alternative
used for raytrace geometry. Both sinks now support memory reclamation via
`Buffer_allocation` handles.
