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

## Roles and threads

Every `Ring_buffer_pool` and `Ring_buffer` method may be called from any
thread: the pool's mutex guards its buffer lists, each buffer's mutex its
allocation state (a range can be released on another thread than the one that
acquired it, for example in a completion handler). The pool keeps its buffers
per thread role (`erhe::utility::Thread_roles::is_render_role()`): a ring
buffer reclaims space in acquisition order, which holds only while no range
of an older frame follows a range of a newer one, and with frame pipelining
the main role acquires for frame N+1 while the render role acquires for frame
N. Each role's frames are in order, so each role allocates from its own
buffers; worker threads share the main role's, and in inline mode (or while
the roles are unset) every allocation is the main role's. The log line of a
new buffer names its role.

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

## Upload staging cap

Uploads that may wait a frame - the budgeted asset-load uploads - stage
through `Device::allocate_upload_staging(target, byte_count,
capacity_byte_count)` instead of `allocate_ring_buffer_entry`. Behind it,
`Ring_buffer_pool` keeps one more `CPU_write` ring of exactly
`capacity_byte_count` bytes, shared by those uploads and never spilled:

- **No room means wait.** A request the ring cannot hold right now gets an
  empty range; the caller records nothing and retries next frame, after
  frame completion retired older ranges. Users: `Image_transfer` in
  `frame_recording` mode (glTF texture residency, standalone image files)
  and `Buffer_transfer_queue::flush_budgeted` (the `Mesh_memory` loader
  queue, through `Command_buffer::try_upload_to_buffer`).
- **A request larger than the cap still makes progress.** It gets a
  one-shot `CPU_write` buffer sized to it, but only while the ring and any
  earlier one-shot buffer are idle, and the ring takes nothing while the
  one-shot buffer is live; the pool destroys it at the first frame
  completion that finds it idle. So the staged bytes in flight never exceed
  max(cap, largest single request).
- **Large requests are not starved.** A request refused for lack of room
  leaves its size as a mark, and smaller requests are refused until one at
  least that large is served. Every owner retries the refused request first
  (the loader queue is FIFO and keeps unrecorded entries at its front;
  `Gltf_image_residency` retries the same image), so the ring drains for it.
  The mark expires after `upload_staging_wait_expiry_frames` completed
  frames without such a retry (a cancelled load).
- **An idle ring restarts at offset 0** (`Ring_buffer::complete_all_syncs`),
  so a caught-up but mid-buffer write position does not refuse a request the
  empty ring can hold.
- **The capacity is a parameter of the request.** Callers pass their
  setting with every request (the editor's `load.max_upload_staging_bytes`,
  doc/editor/async_asset_loading.md "Budget"); a different value replaces
  the ring once it is idle, and requests wait until then.
- **Main role only:** like every ring, it retires space in acquisition
  order, which holds for one role's frames.

Backends that stage nothing need no room: on Metal and for a host-visible
Vulkan buffer, `try_upload_to_buffer` is a memcpy and always succeeds.
Interactive uploads (`Command_buffer::upload_to_buffer`, the full-drain
`Mesh_memory::flush`) keep their per-upload staging - their contract is
"recorded this frame", which a cap cannot honor - and so does
`Image_transfer` in `blocking_drain` mode, whose private ring is sized by
the same staging capacity.

`Device::get_upload_staging_usage()` reports the capacity, the bytes in
use, the high-water mark, and the refused and oversize request counts; the
editor logs them when a load becomes resident and MCP `get_memory_usage`
returns them as `upload_staging`.

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

- [plans/asset_loading.md](../plans/asset_loading.md) - outstanding asset loading work.
