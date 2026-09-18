# Mesh memory: frame-safe frees and shared-primitive swaps

Stability: stable

Two rules that keep a mesh-memory range from being read while it is being
reused. `doc/mesh_memory.md` describes the pools themselves; this document
states the invariant they maintain and the trap that sits beside it.

## The invariant

**A mesh-memory range is never reused - allocated, written - while any
submitted frame may still read it.** The rule is enforced by construction:
`erhe::buffer::Buffer_allocation` has no path to an immediate free.

Without it, destroying a `Buffer_mesh` would free its pool ranges
immediately, and a worker task for another mesh could be handed the same range
and write into it while the GPU is still executing the previous frame, which
still draws from it. Vulkan makes that worse in two ways: host-visible pool
memory (common on integrated GPUs) takes the direct memcpy path in
`upload_to_buffer` with no GPU ordering at all, and the staging path records
the copy with only a post-copy barrier, no write-after-read barrier against
the previous frame's vertex / index reads. The symptom would be a mesh drawn
with another mesh's vertices for a frame ("out of place"), or partially
written data ("broken geometry"). The hazard applies to any mesh delete, undo
or scene close, not only to loading.

## How the deferral works

- `erhe::buffer::Buffer_allocation_owner` is the interface a pool implements
  (`retire_allocation(byte_offset, byte_count) noexcept`). `Buffer_allocation`
  stores an owner pointer rather than a `Free_list_allocator*`, and its
  `release()` calls `retire_allocation()`. `Free_list_allocator::free()`
  remains, but only owners call it.
- `retire_allocation` pushes the retirement onto a mutex-protected pending
  list and never touches the free list. The mutex is required: workers may
  destroy `Buffer_mesh` temporaries.
- `Mesh_memory::flush(command_buffer)` (main thread, once per tick) moves all
  pending retirements from every vertex and index pool into ONE
  `Device::add_completion_handler` registered for the current frame. When the
  frame completes, the handler takes `buffer_mesh_allocation_mutex()` and
  applies the frees to each pool's allocator in retirement order. One handler
  across all pools plus the mutex is what keeps the lockstep invariant: the
  stream pools of a format keep identical allocate / free histories, and no
  worker allocation transaction interleaves mid-drain.
- The handler does not free directly; see "Frame-safe frees, and the loader
  free gate" in `doc/mesh_memory.md` for the second gate, the loader
  watermark.
- Why it is safe: a range's last GPU use is recorded on the main thread before
  the `Buffer_mesh` is destroyed (workers never record draws), and destruction
  happens-before the drain, so the last-use frame is at most the frame index
  at drain time, and frames complete in order.
- **On GL, `Device_impl::add_completion_handler` must set `m_need_sync`**, so a
  frame with a registered handler always gets a fence. Otherwise the handler
  waits until `wait_idle()`.
- At shutdown `wait_idle()` drains handlers; retirements not yet drained are
  dropped when `Mesh_memory` is destroyed, because the pools die with it.

Ring buffers (primitive, indirect) are deliberately outside this: they grow by
adding ring buffers, never reallocate, and release by frame sync entry.

## Shared primitives and draw-list records

Deferred frees close one hazard. A second, independent one lives in the
draw-list path:

**Every site that swaps a shared `Primitive`'s render shape in place must
re-register every mesh sharing that primitive, not only its own.** glTF
instances (the importer clones the template mesh per node) share one
`Primitive`, and a draw-list record caches the drawn variant's `base_vertex`,
index range and buffer set. A sharer left unregistered keeps drawing from
ranges the swap retired and the pool later hands to another mesh - which looks
like an instanced object drawn large, displaced, or with another object's
texture, for as long as it takes that sharer's own task to commit. Without
draw lists the renderer reads the live buffer mesh each frame, which is why
the classic path never showed it.

The deferred finalize commit therefore collects every mesh in the scene
sharing a committed primitive (`Scene_root::collect_meshes_sharing_primitives`),
detaches all their raytrace instances before the swap, and rebuilds and
re-registers all of them afterwards
(`async_raytrace_kickoff_operation.cpp`).

## Verifying a change here

- Build the OpenGL and Vulkan configurations (Debug) with Vulkan validation
  on, and load a large scene (Bistro) several times on Vulkan, watching for
  transient artifacts during the load. Also delete meshes, undo, and close a
  scene while rendering - the same hazard class.
- Run the Vulkan synchronization validation layer during the load: no buffer
  write-after-read / read-after-write reports on the mesh vertex and index
  pool buffers.
- Pool statistics: freed bytes match retired bytes after loads, and
  pending-retire does not grow across idle frames (no leaked handlers).
- Load time is unchanged by the deferral - the frees only move to frame
  completion.

## Future work

- [Mesh memory and primitive shapes](plans/mesh_memory.md) - the pre-copy
  Vulkan upload barrier, and making the draw list robust by construction
  against any in-place swap of a shared primitive.
