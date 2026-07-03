# Mesh_memory

`erhe::scene_renderer::Mesh_memory` owns the GPU vertex and index storage that
backs every `erhe::primitive::Buffer_mesh` in the editor. Pools grow lazily as
allocations come in; nothing is reserved up front.

## Files

- `src/erhe/scene_renderer/erhe_scene_renderer/mesh_memory.{hpp,cpp}`
- `src/erhe/scene_renderer/erhe_scene_renderer/buffer_pool.{hpp,cpp}`
- `src/erhe/scene_renderer/definitions/mesh_memory_config.py` (config struct
  generated into `generated/mesh_memory_config.hpp`)
- `src/erhe/primitive/erhe_primitive/buffer_range.hpp`
- `src/erhe/primitive/erhe_primitive/buffer_sink.hpp`
- `src/erhe/primitive/erhe_primitive/buffer_info.hpp`

## Types

### Mesh_memory

`Mesh_memory` implements both `erhe::primitive::Vertex_buffer_sink` and
`erhe::primitive::Index_buffer_sink` directly. It holds:

- Public hardcoded vertex formats. The two primary ones are
  `vertex_format_skinned` and `vertex_format_not_skinned`; both use three
  streams (binding 0 = position family, binding 1 =
  normal/tangent/tex_coord/color, binding 2 = wireframe-bias smooth normal plus
  three custom attributes), and only stream 0 differs: the skinned form adds
  `joint_indices` and `joint_weights`. The solid-wireframe and edge-line work
  added `vertex_format_not_skinned_wireframe` / `vertex_format_skinned_wireframe`
  (expanded fill geometry) and `vertex_format_edge_line` /
  `vertex_format_edge_line_joints` (wide-line edges).
- `std::vector<Vertex_input_entry> m_vertex_input_entries` -- a cache of
  `(key, unique_ptr<Vertex_input_state>, Vertex_format)`. The constructor
  pre-warms it with one entry per built-in format; further entries are added
  on demand by `get_vertex_input_from_vertex_format`. The `key` field is the
  entry's index in the vector and is what `Buffer_mesh::vertex_input_key`
  stores.
- `std::vector<Buffer_pool> m_vertex_pools` -- one pool per
  `Vertex_stream` INSTANCE (by address), grown lazily by
  `allocate_vertex_buffer_range`. Note: two Vertex_streams with the same
  byte layout but belonging to different Vertex_formats do NOT share a
  pool; see "Lockstep invariant" below for why.
- `std::vector<Buffer_pool> m_index_pools` -- one pool per index `Format`.
  Grown lazily by `allocate_index_buffer_range`.
- `erhe::graphics::Buffer_transfer_queue m_buffer_transfer_queue` -- collects
  enqueued vertex/index uploads; `flush(command_buffer)` submits them.

The constructor allocates no GPU buffers. It populates the two vertex format
members and pre-warms one `Vertex_input_entry` per format; the pools start
empty.

`make_primitive_buffer_info()` returns a `Buffer_info` configured for the
non-skinned vertex format with 32-bit indices, with `*this` as both the
vertex and index buffer sink. Every editor call site that builds a primitive
without skinning data uses this helper.

### Buffer_pool

`Buffer_pool` owns the GPU buffers for one vertex stream layout OR one index
format -- the two roles are selected by which constructor is called. A pool
holds `std::vector<unique_ptr<Pool_block>>`, where each `Pool_block` is
`{ buffer_id, unique_ptr<erhe::graphics::Buffer>, erhe::buffer::Free_list_allocator }`.

`allocate(element_count)` walks existing blocks first-fit (two passes -- the
second pass runs after `create_new_block`). If no block has room, a new block
is created with capacity `max(block_size_bytes, requested_size)`, so a single
allocation larger than the default block size gets a buffer sized exactly to
fit. Hitting `max_blocks` aborts via `ERHE_FATAL`.

The allocation returns a `Buffer_sink_allocation` containing:
- a `Buffer_range { count, element_size, byte_offset, pool_id, buffer_id }`,
- an `erhe::buffer::Buffer_allocation` RAII handle on the pool block's
  allocator that releases the bytes when the owning `Buffer_mesh` is
  destroyed.

`is_compatible(const Vertex_stream&)` checks pointer identity against the
Vertex_stream the pool was constructed from. It does NOT compare layout
fields. Two `Vertex_stream` instances that are byte-for-byte identical
but live in different `Vertex_format` objects therefore land in different
pools -- this is required for the lockstep invariant described below,
not an optimisation. The class-level comment in `buffer_pool.hpp` spells
out the failure mode if the check is loosened.

`is_compatible(Format)` matches the index format exactly.

`get_buffer(buffer_id)` returns the raw `erhe::graphics::Buffer*` for a block.
`get_vertex_stream()` / `get_index_format()` return the pool's keying
metadata for inspection.

### Buffer_range and Pool_buffer_identity

`erhe::primitive::Buffer_range` is the data side of an allocation:
`{ count, element_size, byte_offset, pool_id, buffer_id }`. It does NOT carry
an `erhe::graphics::Buffer*`; consumers resolve the buffer through
`Mesh_memory::get_vertex_buffer` / `get_index_buffer` (overloads accept either
a `Buffer_range` or a `Pool_buffer_identity`).

`Pool_buffer_identity { pool_id, buffer_id }` is the identity-only projection
of a range. It is what `Render_bucket` keys on so a bucket holds primitives
that all bind the same physical buffer set.

`Buffer_mesh` carries `std::vector<Buffer_range> vertex_buffer_ranges`, one
`Buffer_range index_buffer_range`, and a `size_t vertex_input_key` that
indexes into `Mesh_memory::m_vertex_input_entries`.

## Sink protocol

Geometry build code calls into the sinks via `Buffer_info`:

- `allocate_vertex_buffer_range(stream, vertex_count)` -- iterates
  `m_vertex_pools`, returning the first pool whose `is_compatible(stream)`
  returns true. If none match, a new vertex `Buffer_pool` is appended with
  `block_size_bytes = vertex_pool_block_size_mb * MB` and
  `max_blocks = max_buffers_per_pool`, then `allocate` is called on it.
- `allocate_index_buffer_range(index_format, index_count)` -- analogous, using
  `index_pool_block_size_mb`.
- `enqueue_vertex_data` / `enqueue_index_data` -- look up the physical buffer
  via `pool_id` and enqueue the upload through `m_buffer_transfer_queue`.
- `vertex_writer_ready` / `index_writer_ready` -- same path for the writer
  flow that drains a `Vertex_buffer_writer` / `Index_buffer_writer`.

`flush(command_buffer)` drains the transfer queue. Callers schedule this once
per frame around mesh uploads.

## Render bucketing

Multi-draw rendering requires identical vertex/index bindings across the
draw call, so primitives are grouped before draw submission.

`Buffer_set` is the bucket key:
- `vertex_input_key` (index into `m_vertex_input_entries`),
- `Pool_buffer_identity index_buffer`,
- `std::vector<Pool_buffer_identity> vertex_buffers` (one entry per stream).

`Render_bucket` aggregates a `Buffer_set`, a `Shader_key` plus its hash, and
the list of `Mesh_primitive_entry` (mesh + primitive index) that match.

`bucket_primitives(buckets, mesh_memory, environment_shader_key, meshes,
primitive_mode, variant_signature_policy)` walks the mesh span, derives a
per-primitive shader key (or reuses the environment key when the policy is
`accept_all_variant_signatures`), and either appends each primitive to an
existing bucket whose `accept()` returns true or starts a new bucket.

`Variant_signature_policy` has two values:
- `accept_all_variant_signatures` -- used by the shadow renderer; the
  environment shader key is reused as-is.
- `require_exact_variant_signature` -- used by the forward renderer; the
  per-primitive key is derived from material + vertex format + skinning state
  so primitives that need different shader variants end up in different
  buckets.

## Config

`Mesh_memory_config` (generated from `definitions/mesh_memory_config.py`):

| Key | Default | Notes |
|-----|---------|-------|
| `vertex_pool_block_size_mb` | 32 | Default capacity of a freshly grown vertex pool block. |
| `index_pool_block_size_mb` | 16 | Default capacity of a freshly grown index pool block. |
| `edge_line_vertex_pool_block_size_mb` | 8 | Currently unused -- the edge-line vertex pool is not implemented; the config key is reserved. |
| `max_buffers_per_pool` | 64 | Hard cap. `Buffer_pool::create_new_block` aborts when exceeded. |

## Notes

- Thread safety: `Buffer_pool` does not take a mutex internally. Build code
  serializes its own allocation flow.
- The `format_pools.{hpp,cpp}` files exist on disk but are empty / fully
  commented out -- there is no `Format_pools` type in the current build.
- `Buffer_mesh::edge_line_vertex_buffer_range` is populated by
  `Build_context_root::allocate_edge_line_vertex_buffer()` whenever the
  primitive request includes edge lines and `Buffer_info::edge_line_vertex_stream`
  is set (which `Mesh_memory::make_primitive_buffer_info()` points at
  `vertex_format_edge_line.streams.front()`). Because pools key on
  `Vertex_stream` pointer identity, the edge-line stream (vec4 position +
  vec4 smooth normal) lives in its own dedicated pool, distinct from
  the main mesh vertex pools. `Content_wide_line_renderer::add_mesh`
  consumes the range via `Mesh_memory::get_vertex_buffer(range)` to
  resolve the underlying `Buffer*`.

## Lockstep invariant

The forward renderer issues `multi_draw_indexed_primitives_indirect`.
Each indirect draw command carries a single scalar `vertexOffset` (a.k.a.
`base_vertex`) that the GPU applies uniformly to every vertex binding:

    byte_read_K = (vertexOffset + N) * stride_K

`Buffer_mesh::base_vertex()` (see `src/erhe/primitive/erhe_primitive/buffer_mesh.cpp`)
computes that scalar from stream 0 only:
`vertex_buffer_ranges[0].byte_offset / element_size`. For the GPU's
reads on streams 1, 2, ... to land on the same mesh's bytes, the
quantity `byte_offset_K / stride_K` must be identical for every stream
K of the mesh. We call this the **lockstep invariant**.

Within a single Vertex_format, lockstep holds automatically: every mesh
visits every stream pool in sequence, so all that format's pools advance
by the same vertex count per mesh.

It breaks the moment two Vertex_formats share a pool for some streams
but not for others. Concretely: `vertex_format_skinned.streams[1]` and
`vertex_format_not_skinned.streams[1]` are byte-identical. If they
shared a pool, the shared pool would advance for both formats' meshes,
while each format's private stream-0 pool would advance only for its
own meshes. A skinned mesh allocated after a batch of non-skinned
cubes would end up with:

- stream 0 starting at the beginning of the skinned-stream-0 pool
  (logical vertex offset 0)
- stream 1 starting after all the cubes' stream-1 data in the shared
  pool (logical vertex offset = cumulative cube vertex count)

`vertexOffset` derived from stream 0 would then read stream 1 from the
cubes' bytes -- positions land correctly, normals / tangents /
tex_coords / colors come back as garbage from another mesh.

To make this class of bug impossible, `Buffer_pool::is_compatible`
keys on `Vertex_stream` pointer identity (not layout equality). The
cost is one buffer pool per (Vertex_format, stream-index) tuple
instead of one per unique layout. Memory waste is bounded and small
(a few MB across the editor's current formats); the invariant is
preserved by construction.

There are alternative ways to escape the invariant -- moving per-mesh
offsets into per-binding `set_vertex_buffer` offsets, or padding all
pools to advance globally -- but the first is incompatible with
multi-draw indirect and the second wastes more memory than the
current approach. See the long-form rationale in the `Buffer_pool`
class comment in `src/erhe/scene_renderer/erhe_scene_renderer/buffer_pool.hpp`.
