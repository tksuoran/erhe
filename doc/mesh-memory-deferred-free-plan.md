# Mesh memory: frame-safe frees and uploads (plan + results)

Status: IMPLEMENTED 2026-08-16 (change 1 below; change 2 not done). Motivation:
transient glitches during bistro load (objects out of place / broken geometry).

## Results (2026-08-16)

Change 1 (deferred frees) alone did NOT fix the glitch - the user saw it
unchanged / worse. Captured afterwards with the editor MCP `capture_screenshot`
burst during the load and a RenderDoc fork capture inside the finalize window
(`Indirect sub-command` under `draw list 31 dynamic translucent`, pixel history
at the "confetti" flower planter): geometry / positions sane, texture = the
pansy atlas, object drawn large and displaced; the SAME planter in a settled
capture is a small basket further right. Every broken object was a repeated
(instanced) one - hanging planters, lanterns, the hanging sign.

Actual root cause: **shape-level swap + shared primitives + draw list records**.
The deferred finalize commit swaps the render shape's proxy buffer mesh for
the full one and called `update_rt_primitives()` (-> draw list re-register)
only on the task's OWN mesh. glTF instances (importer clones the template
mesh per node) share the `Primitive`, so the sharers' cached draw list
records kept the proxy's base_vertex / index range / buffer set - memory that
the swap freed and other meshes' uploads reused - until their own task
committed, seconds later on bistro (Debug). Without draw lists the renderer
reads the live buffer mesh each frame, which is why the classic path never
showed it. Fix: `deferred_finalize_mesh_items` commit now collects every mesh
in the scene sharing a committed primitive (`collect_meshes_sharing_primitives`),
detaches all their raytrace instances before the swap, and rebuilds /
re-registers all of them after (`async_raytrace_kickoff_operation.cpp`).
Verified: MCP screenshot burst through the whole finalize window (2823 -> 2
queued) shows no artifacts.

Change 1 stays: it closes a real (independent) hazard - a Buffer_mesh freed
while frames in flight still read it - and cost nothing measurable.

Follow-up (not done): make the draw list robust by construction against ANY
in-place render-shape swap of a shared primitive (e.g. a buffer-mesh
generation on `Primitive_render_shape` checked at `flush_pending`, or a
shape->meshes back-reference), instead of relying on each swap site to
notify all sharers.

Also fixed on the way: `apply_renderdoc_override_env()` no longer sets the
DISABLE key the override RenderDoc layer declares for itself (the fork moved
from 1.46 to 1.45 and the hardcoded list disabled it) - `renderdoc_capture.cpp`.
Note: RenderDoc layer + ray query = `VK_ERROR_DEVICE_LOST` at frame 4 on the
890M; capture with `"disable_ray_tracing": true` in `erhe_graphics.json`.

## Hypothesis

`Async_raytrace_kickoff_operation` swaps the fill-only proxy `Buffer_mesh`
for the full one on the main thread (`primitive.cpp
Primitive_render_shape::commit_geometry_buffer_mesh`). The move-assign
frees the proxy's pool ranges **immediately**
(`buffer_mesh.cpp:41-47` -> `Buffer_allocation::release()` ->
`Free_list_allocator::free`). Nothing waits for frames in flight. A worker
task for another mesh can then be handed the same range and its upload
lands in that memory while the GPU is still executing frame N-1, which
still draws the proxy from it. Vulkan makes this worse:

- host-visible pool memory (common on the AMD 890M iGPU) takes the direct
  memcpy path in `vulkan_command_buffer.cpp upload_to_buffer` - no GPU
  ordering at all;
- the staging path records the copy with only a post-copy barrier, no
  WAR barrier against the previous frame's vertex/index reads.

Result: mesh A drawn with mesh B's vertices for a frame (looks "out of
place"), or partially written data ("broken"). Not a regression: the
allocator has never deferred frees. The same hazard applies to any mesh
delete / undo / scene close, not only loading.

## Design goal

Make it impossible for a mesh-memory range to be reused (allocated,
written) while any submitted frame may still read it. The rule is enforced
by construction: `Buffer_allocation` has no path to an immediate free.

## Changes

### 1. Deferred (retired) frees in the pool layer

- `erhe_buffer`: introduce `class Buffer_allocation_owner` (interface) with
  `virtual void retire_allocation(std::size_t byte_offset, std::size_t
  byte_count) noexcept = 0;`. `Buffer_allocation` stores
  `Buffer_allocation_owner*` instead of `Free_list_allocator*` and its
  `release()` calls `retire_allocation()`. `Free_list_allocator::free()`
  remains, but only owners call it.
- `erhe_scene_renderer/buffer_pool`: `Pool_block` (or `Buffer_pool`)
  implements `Buffer_allocation_owner`. `retire_allocation` pushes
  `{block, byte_offset, byte_count}` onto a mutex-protected pending list
  (workers may destroy `Buffer_mesh` temporaries; must be thread-safe).
  It never touches the free list.
- `Mesh_memory::flush(command_buffer)` (main thread, once per tick,
  `editor.cpp:753`) gains a drain step: move all pending retirements from
  every vertex/index pool into ONE `Device::add_completion_handler`
  registered for the current frame. The handler, when the frame completes,
  takes `buffer_mesh_allocation_mutex()` and applies the frees to each
  pool's `Free_list_allocator` in retirement order. One handler across all
  pools + the mutex keeps the lockstep invariant: stream pools of a format
  keep identical alloc/free histories and no worker allocation transaction
  interleaves mid-drain.
- Safety argument: a range's last GPU use is recorded on the main thread
  before the `Buffer_mesh` is destroyed (workers never record draws), and
  destruction happens-before the drain, so the last-use frame <= the frame
  index at drain time; frames complete in order.
- GL: `Device_impl::add_completion_handler` must set `m_need_sync = true`
  (currently only ring-buffer acquire does) so a frame with a registered
  handler always gets a fence; otherwise the handler waits until
  `wait_idle()`.
- Shutdown (`editor.cpp:2852-2869`): `wait_idle()` already drains
  handlers; pending retirements not yet drained must be dropped when
  `Mesh_memory` is destroyed (pools die with it, nothing to free).
- Diagnostics: pool `describe()` reports pending-retire bytes; a debug
  counter of "retired bytes freed per frame".

### 2. Vulkan upload path: no unordered writes into pool memory

- `upload_to_buffer` staging path: add a pre-copy
  `VkBufferMemoryBarrier2` (src = the buffer's consumer stages/access from
  `buffer_usage_to_vk_stage_access(usage)`, dst = COPY / TRANSFER_WRITE)
  covering the same range, so an in-place overwrite of a range read by
  earlier commands (paint tool, live edits) is ordered. Cheap, keeps the
  general API safe independent of (1).
- Host-visible direct memcpy path: only safe for ranges no in-flight frame
  reads. With (1) that holds for freshly allocated pool ranges, but not for
  in-place overwrites of a live mesh. Either route buffers with vertex /
  index / storage usage through the staging copy always, or keep memcpy
  and document that in-place overwrites are the caller's responsibility.
  Decision at implementation time after logging which memory type the
  pools actually get on the 890M (add a one-line log in
  `Buffer_pool::create_new_block`).

### 3. Ring buffers (primitive / indirect) - excluded, not changed

They grow by adding ring buffers, never reallocate, and release by frame
sync entry. Not part of this fix; if the glitch survives (1)+(2), test them
by inflating `m_min_buffer_size` so no wrap occurs during load.

## Commits

1. `erhe_buffer` + `buffer_pool` + `mesh_memory`: retire-and-defer frees
   (with GL `m_need_sync` fix, shutdown handling).
2. `vulkan_command_buffer`: pre-copy WAR barrier; host-visible path
   decision.

## Verification

- Build opengl + vulkan configs (Debug), Vulkan validation on; load bistro
  several times on Vulkan (where the glitch reproduces) and on GL, watch
  for the transient artifacts. Also test: delete meshes / undo / close
  scene while rendering (same hazard class).
- Vulkan synchronization validation layer during load: no buffer WAR/RAW
  reports on "Mesh vertex pool" / "Mesh index pool" buffers.
- Pool statistics: freed bytes match retired bytes after loads; no growth
  in pending-retire across idle frames (no leaked handlers).
- Perf: bistro load time unchanged (frees just move to frame completion).
