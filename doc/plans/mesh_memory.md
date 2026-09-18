# Mesh memory and primitive shapes: outstanding work

Status: proposed

Extends `doc/erhe/mesh_memory.md`, `doc/erhe/mesh_memory_deferred_free.md`,
`doc/erhe/primitive_shape_locking.md` and `doc/erhe/vertex_position_quantization.md`.

## Destroy empty pool blocks

`Buffer_pool` only appends blocks and never destroys one, so the process
footprint never drops below its high-water mark (see "Pool blocks are never
destroyed" in `doc/erhe/mesh_memory.md`).

Destroy a block whose allocator reports zero used bytes, at a safe point
(scene close, or the frame-completion gate the deferred frees already use).
`erhe::graphics::Buffer` destruction already defers `vmaDestroyBuffer` through
a completion handler, so the release path exists; what is missing is the "is
this block empty and safe to drop" bookkeeping and a trigger. Measure before
and after with the `get_memory_usage` MCP tool, which reports per-pool
`capacity_bytes` / `used_bytes` / `block_count`.

## Vulkan upload path: order in-place overwrites

`Vulkan_command_buffer::upload_to_buffer` records a post-copy barrier that
chains the transfer write to the buffer's consumer stages, but no pre-copy
barrier against earlier reads of the same range. An in-place overwrite of a
range that earlier commands read (paint tool, live vertex edits) is therefore
unordered.

- Staging path: add a `VkBufferMemoryBarrier2` before `vkCmdCopyBuffer` with
  src = the buffer's consumer stages / access from
  `buffer_usage_to_vk_stage_access(usage)` and dst = COPY / TRANSFER_WRITE,
  covering the same range.
- Host-visible direct memcpy path: decide between routing buffers with
  vertex / index / storage usage through the staging copy always, and keeping
  the memcpy with in-place overwrite documented as the caller's
  responsibility. Log which memory type the pools actually get before
  deciding.

Verify with the Vulkan synchronization validation layer during a scene load:
no buffer WAR / RAW reports on the mesh vertex and index pool buffers.

## Draw-list robustness against shared-primitive swaps

Every site that swaps a shared `Primitive`'s render shape in place has to
re-register every mesh sharing that primitive (see "Shared primitives and
draw-list records" in `doc/erhe/mesh_memory_deferred_free.md`). Make the draw list
robust by construction instead: a buffer-mesh generation on
`Primitive_render_shape` checked at `flush_pending`, or a shape-to-meshes
back-reference.

## Synchronize the reference-returning primitive shape accessors

`Primitive_shape::get_element_mappings()`, `get_raytrace()` and
`get_renderable_mesh()` hand out references to state that `commit_*` mutates,
so the state lock cannot protect their readers (see "Limits" in
`doc/erhe/primitive_shape_locking.md`). `get_renderable_mesh()`'s safety rests on
the contract that every `commit_*` call site holds `item_host_mutex`. Give the
three accessors signatures that can be made safe (returning by value, or a
lock-holding view), or state the contract where the compiler can check it.

`Brush::get_geometry()` is a second lazy builder, called from a per-frame ImGui
window, with the same shape of problem in a different subsystem.

## Vertex position quantization follow-ups

Quantization applies to the meshoptimizer optimized variant only; see
`doc/erhe/vertex_position_quantization.md` for the design it follows.

- **One definition of the affine.** The encode affine is written out in
  `primitive_builder.cpp`, `primitive.cpp`, `primitive_buffer.cpp` and
  `mesh_component_transform.cpp` with the same epsilon, because
  `erhe_primitive` cannot see `erhe_scene_renderer`. They agree by inspection
  only. Move `Position_quantization` / `get_position_quantization` down into
  `erhe_math` next to `Aabb`, the way `Vertex_position_encoding` sits in
  `erhe_dataformat`. Do this before anyone touches the encoding.
- **Metal attribute alignment.** `Device_info::min_vertex_attribute_alignment`
  is read in `Mesh_memory` and assigned by no backend, so it stays 1. With
  quantization on, the skinned stream 0 puts `joint_indices` at offset 6 and
  `joint_weights` at offset 10. GL and Vulkan core accept that; Metal and
  MoltenVK's portability subset may not, and no Metal run with quantization on
  has been made. Set `min_vertex_attribute_alignment = 4` on Metal, or reorder
  stream 0 so the two 1-byte attributes precede the position - which also
  needs the position attribute's offset added at both BLAS build sites, since
  both assume the position sits at offset 0 of stream 0.
- **Per-primitive encoding.** Quantize only above a vertex count, or only
  below an AABB diagonal. It needs a parallel format set and the extra pools
  that come with `Mesh_memory` keying each pool on a `Vertex_stream` instance.
  Solve one trap first: solid-wireframe draws derive their key from
  `buffer_mesh->vertex_input_key` but bind the expanded key, so under a
  per-primitive scheme the key could describe a different encoding than the
  bound format. Derive from the bound key first.
- **A padded `snorm16x4_aabb` storage format.** The enumerator is reserved for
  it. It is what a device supporting no 3-component 16-bit snorm vertex format
  would need, and it is in Vulkan's mandatory acceleration-structure format
  table.
- **A separate float3 position copy for BLAS input**, needed only on a device
  that supports neither `format_16_vec3_snorm` nor the in-place snorm16x4
  read. Not implemented. The shape it should take: an optional
  `Buffer_mesh::raytrace_position_buffer_range` like the existing optional
  `edge_line_joint_buffer_range`, in its own float3 stride-12 `Buffer_pool` so
  it stays out of the stream-0 lockstep group, written by the two encoders
  where the unencoded positions are already in hand. Watch the arithmetic: a
  mesh carrying both pays 8 + 12 bytes per vertex where float3 alone costs 12,
  so it is only a win when the copy is confined to meshes actually in the
  TLAS - allocate it lazily per BLAS cache entry, never always-on.
- **Metal ray tracing with quantization.** `choose_position_format` declines
  quantization when ray tracing is available and
  `Device_info::min_acceleration_structure_vertex_stride` (12 on Metal)
  exceeds the smallest quantized stream-0 stride (8). Lifting it means raising
  the stream-0 stride to 12 or 16 on Metal, which gives back part of the
  saving. Untested on hardware.
- **16-bit indices** would reintroduce the alignment problem the 4-byte stride
  rule solves: the instance records hand `index_address` to the same
  `buffer_reference_align = 4` buffer reference. A 16-bit index path needs the
  same stride rule stream 0 got.
- **An out-of-AABB vertex drag** is verified by reading the code only; it needs
  real mouse interaction, which the MCP path does not reach.
