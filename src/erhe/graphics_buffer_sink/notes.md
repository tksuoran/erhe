# erhe_graphics_buffer_sink

## Purpose
Bridges `erhe::primitive`'s abstract `Buffer_sink` interface to actual GPU buffers
managed by `erhe::graphics`. It allocates space in GPU vertex/index buffers and
enqueues data transfers through the graphics `Buffer_transfer_queue`, allowing
primitive build pipelines to write mesh data directly into GPU memory.

## Key Types
- `Graphics_buffer_sink` -- concrete `Buffer_sink` that wraps GPU `Buffer` objects and a `Buffer_transfer_queue`

## Public API
- Construct with a `Buffer_transfer_queue`, a list of vertex `Buffer*`, and an index `Buffer*`.
- `allocate_vertex_buffer()` / `allocate_index_buffer()` -- reserve space, return `Buffer_range`.
- `enqueue_vertex_data()` / `enqueue_index_data()` -- submit data for GPU transfer.
- `buffer_ready()` -- signal that a writer has finished producing data.
- Query helpers: `get_used_*_byte_count()`, `get_available_*_byte_count()`.

## Dependencies
- `erhe::graphics` -- `Buffer`, `Buffer_allocator`, `Buffer_transfer_queue`
- `erhe::primitive` -- `Buffer_sink` (base class), `Buffer_range`, buffer writers
- `erhe::profile` -- profiling mutex macro

## Notes
Thread-safe: uses a mutex around allocations. Supports multiple vertex buffer
streams (for multi-stream vertex layouts). This is the standard sink used at
runtime; `Cpu_buffer_sink` in `erhe::primitive` is the CPU-only alternative
used for raytrace geometry.
