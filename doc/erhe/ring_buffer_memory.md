# Ring buffer memory

Stability: mostly stable

How the device's staging ring buffers stay bounded while a scene loads.
`Ring_buffer_pool` (`erhe_graphics/ring_buffer_pool.hpp`) is the allocator,
shared by the Vulkan, OpenGL and Metal backends: each `Device_impl` owns one
(minimum buffer size 2 MiB on Vulkan and OpenGL, 4 MiB on Metal), forwards
`allocate_ring_buffer_entry()` to it after deriving the buffer target's
alignment, and calls its `frame_completed()` for every completed frame.
`Ring_buffer` and `erhe::Circular_ring_buffer_algorithm` are the storage and
the arithmetic under it.

## The allocator

An acquisition is served in three passes. Pass 1 and pass 2 look for an
existing `Ring_buffer` of the requested usage class with free space right now.
When none has space, a new ring buffer is created and added to the pool.

Sizing distinguishes the first buffer of a usage class from a spill: the first
gets `max(m_min_buffer_size, 4 * byte_count)` headroom, a spill - created
because every existing buffer was momentarily full of in-flight ranges - is
sized to `max(m_min_buffer_size, byte_count)`. The 4x multiplier on spills is
what turned a load's 16 MiB staging acquisitions into a pile of 64 MiB buffers
and left the process holding gigabytes of host staging for its lifetime.

Space inside a ring buffer is reclaimed by `Ring_buffer::frame_completed()`:
sync entries are stamped with the frame index at `make_sync_entry`, so
reclamation is at frame-fence granularity.

## Idle reclaim

Ring buffers are erased again, not just reused. After the per-buffer
`frame_completed` loop, `Ring_buffer_pool::frame_completed()` erases every
ring buffer that is idle - the circular algorithm holds no live sync entry and
no open acquired range (`Ring_buffer::is_idle()`) - and has been unused for 16
completed frames (`get_last_used_frame()`), keeping one warm buffer per
`Ring_buffer_usage` class so the steady state does not thrash. Destruction is
deferred-safe: an idle buffer has no in-flight range, and on Vulkan
`Buffer_impl::~Buffer_impl` also registers a completion handler for the VMA
free. The post-load working set therefore returns to the warm set
instead of staying at the load's peak.

## Forward progress without frames

A load that runs while the render loop keeps producing frames paces itself
against frame completions, through `gpu_upload_bytes_per_frame` and the
frame-recording `Image_transfer` mode (doc/editor/async_asset_loading.md).

A load that runs with no frames in flight - a synchronous import, a headless
import, a startup load - gets no frame completions at all, so it has to
reclaim its own staging. `Image_transfer` in `blocking_drain` mode therefore
owns a private 64 MiB `Ring_buffer` and its own transfer command buffer: when
the ring fills it ends the command buffer, submits it through
`Device::submit_command_buffer_and_wait()` (which waits on the command
buffer's implicit fence and fires no completion handlers, so it is safe
mid-frame, unlike `wait_idle()`), then resets the private ring with
`Ring_buffer::complete_all_syncs()`. An image larger than the staging ring
takes a dedicated one-shot staging `Buffer` and an immediate flush.

The private ring is what makes this correct, and it is why a device-wide
"completion tick" was rejected in its place: every consumer of the private
ring is inside the command buffer that was just flushed, which is not true of
the shared pool, whose frame-stamped entries may be consumed by the frame's
still-unsubmitted command buffer.

## Diagnostics

- Every ring-buffer creation logs at info: size, usage class, the new buffer
  count and the new total bytes.
- Crossing 8 buffers of one usage class logs a warning naming the count and
  the bytes - the runaway growth this document is about stayed invisible until
  a VMA out-of-memory dump.
- Idle reclaim logs the reclaimed count and bytes and the remaining buffer
  count.
- A `vmaCreateBuffer` failure dumps every VMA allocation (`vulkan_buffer.cpp`).

## Verification

Load `res/editor/assets/bistro_lights_c.gltf` in a Debug build both ways -
through the interactive editor (frames keep completing) and through a headless
or synchronous import (no frames) - and check `logs/log.txt`:

- ring-buffer totals plateau instead of climbing, and the 8-buffer warning
  does not appear;
- the blocking case shows the `Image_transfer` flush lines, and the load
  completes;
- after the load plus enough frames for the idle threshold, the reclaim line
  brings the pool back to the warm set, and the "VMA stats before destroy"
  dump at device destruction shows only those;
- interactive editing after the load (mesh edits, lightmap prepare) does not
  stutter from ring-buffer churn - the warm-set count and the idle threshold
  exist for this.

## Future work

- [plans/asset_loading.md](../plans/asset_loading.md) - dedicated one-shot staging for oversize uploads, a device-level ring-buffer budget.
