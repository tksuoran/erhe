# Ring buffer memory plan: bounding growth during scene loads

Status: IMPLEMENTED 2026-08-16 (compile pending user verify). What was
built deviates from the phase-2 sketch below in one deliberate way:

- Instead of a device-wide "completion tick", forward progress lives in
  `Image_transfer` (the load path's only heavy ring consumer): it owns a
  PRIVATE 64 MiB `Ring_buffer` staging ring plus its own transfer
  command buffer. When the ring fills it flushes - ends the cb, submits
  via the new `Device::submit_command_buffer_and_wait()` (Vulkan: waits
  the cb's implicit fence; fires NO completion handlers, safe mid-frame,
  unlike `wait_idle()`), then `Ring_buffer::complete_all_syncs()` resets
  the private ring. This is exactly correct because every consumer of
  the private ring is in the flushed cb - which is NOT true of the
  shared pool, whose frame-stamped entries may be consumed by the still
  unsubmitted frame cb; that is why the shared-tick design was dropped.
  Images larger than the staging ring use a dedicated one-shot staging
  `Buffer` + immediate flush.
- Phase 1 idle reclaim (16-frame threshold, 1 warm buffer per usage
  class) + creation/reclaim logging: Vulkan `Device_impl` only.
- Spill-sizing fix (`max(min, byte_count)` instead of `4 * byte_count`
  when the usage class already has buffers): Vulkan + GL + Metal.
- No config budget knob (constant in image_transfer.cpp); mesh vertex
  uploads were already fine (Buffer_transfer_queue uses dedicated
  self-freeing staging).
- New: `Circular_ring_buffer_algorithm::complete_all()/is_empty()` (+
  unit test), `Ring_buffer::complete_all_syncs()/is_idle()/
  get_last_used_frame()/get_ring_buffer_usage()/get_capacity_byte_count()`.

Original plan below, kept for context.

---

Status: PLAN ONLY (2026-08-16, revised same day for the blocking-load
case). Nothing below is implemented.

## Problem

`Device_impl::allocate_ring_buffer_entry()` (vulkan_device.cpp ~1911, same
logic in gl_device.cpp / metal_device.cpp) grows `m_ring_buffers` without
bound and never shrinks it:

1. Pass 1/2 look for an existing `Ring_buffer` with free space *right now*.
2. If none has space, a NEW ring buffer of `max(m_min_buffer_size, 4 *
   byte_count)` is created and pushed onto `m_ring_buffers` — forever.

Ring buffer space is only reclaimed by `Ring_buffer::frame_completed()`
(sync entries are stamped with the frame index at `make_sync_entry`), i.e.
at frame-fence granularity. Worse, the glTF load path records every
upload copy into the caller's `current_command_buffer` for the WHOLE
parse (`Image_transfer` holds one `Command_buffer&`; see
editor/parsers/gltf.cpp ~578): the copies are not even submitted until
that command buffer is, so during a load the total staging demand is the
whole scene at once. Every acquisition that lands while all existing
buffers hold in-flight ranges mints another buffer.

Measured while loading the bistro scene (VMA stats dump at OOM,
2026-08-16): **81 × 67.1 MB `Ring_buffer` allocations** — 78 in host
memory (5.2 GB, dominating the editor's 9.9 GB working set) and 3 in the
224 MB BAR heap (device-local + host-visible), which was left with a 4 MB
budget. The 67.1 MB size is the `4 * byte_count` multiplier applied to
16 MiB acquisitions. None of this memory is ever returned.

## Design requirement: loading must make its own progress

Two load scenarios must both stay within budget:

- **Concurrent**: the render loop keeps producing frames during an async
  load. Frame completions reclaim ring space, so the loader can pace
  itself against them (per-frame upload budget).
- **Blocking**: the application waits for the load before rendering new
  frames (synchronous import, headless import, startup load). No frame
  completions arrive. The loader must be able to FLUSH its own uploads —
  submit the accumulated transfer commands with its own fence, wait, and
  reclaim — without depending on the render thread. Any wait the loading
  thread performs must be on something the loading thread itself can
  trigger.

## Phase 1 — Reclaim idle ring buffers (retention fix; smallest, do first)

- Add `Ring_buffer::is_idle()` — true when the circular ring buffer
  algorithm has no live sync entries and no open (acquired, un-released)
  range. Add `m_last_used_frame`, updated in `acquire()`.
- In `Device_impl::frame_completed()`, after the existing
  `ring_buffer->frame_completed(...)` loop: erase ring buffers that are
  idle AND unused for N completed frames (suggest N = 16), keeping up to
  K warm buffers per `Ring_buffer_usage` class (suggest K = 1) so steady
  state does not thrash.
- Destruction is already deferred-safe: `Buffer_impl::~Buffer_impl`
  registers a completion handler for the VMA free.
- Outcome: the post-load working set returns to ~K × 64 MiB per usage
  class instead of holding 5.4 GB for the process lifetime.

## Phase 2 — Upload epochs: decouple ring reclamation from frames

This is the enabler for both load scenarios.

- Generalize the sync-entry key from "frame index" to a monotonic
  **completion tick** owned by the device. Rendered frames advance it
  (frame fence completion, exactly as today), and explicit transfer
  flushes advance it too. `Ring_buffer::frame_completed(frame)` becomes
  `tick_completed(tick)`; `make_sync_entry` stamps the current tick.
- Add a device-level transfer flush for the loading path, e.g.
  `Device::flush_transfers(Command_buffer&)`: end the command buffer,
  submit it with a fence on the same queue, wait for the fence, advance
  the completion tick, run ring reclamation, and hand back a fresh
  command buffer to continue recording. Same-queue submission order
  keeps copy-before-use ordering for everything recorded later.
- `Image_transfer` (and the mesh/vertex-pool upload path) must tolerate
  the command buffer being split at upload boundaries. Today each
  `upload_to_texture` creates a short-lived `Blit_command_encoder`, so
  any point between two uploads is a safe flush boundary; the flush must
  simply never land inside one image's multi-level copy loop.

## Phase 3 — Budget + throttle in the allocator and the load path

- Track `m_ring_buffer_total_bytes` per device; add a budget knob
  (config: `erhe_graphics.json` e.g. `ring_buffer_budget_mb`, default
  256). Count the BAR heap (CPU_write prefers device_local|host)
  toward it; BAR is only 224 MB and was fully consumed.
- Loading-path pacing (uses phase 2):
  - **Concurrent case — per-frame upload budget**: the loader tracks
    bytes acquired since the last observed completion tick; past the
    budget it waits for the tick to advance (condition variable
    signalled from `Device_impl::frame_completed` / flush completion)
    and continues.
  - **Blocking case — self-flush**: if the tick does not advance within
    a short timeout (no frames coming), or unconditionally when the ring
    allocator reports no space at budget, the loader calls
    `flush_transfers()` on its own command buffer. This both executes
    the pending copies and frees the staged ranges — forward progress
    is guaranteed by the loader itself, never by the render thread.
- Allocator backstop (`allocate_ring_buffer_entry`): when pass 1/2 fail
  AND `total + new_size > budget`, do NOT silently create a buffer.
  Return a "would exceed budget" outcome so the caller can flush/wait
  (loading thread), EXCEPT when the caller is the render thread mid-
  frame, where waiting on its own frame would deadlock: allocate anyway
  and log a warning with the new total. Correctness first, budget
  best-effort.
- Sizing fix: a single request larger than any existing buffer must
  still succeed, but size it `max(m_min_buffer_size, byte_count)` — the
  current `4 * byte_count` turns 16 MiB requests into 64 MiB buffers and
  quadruples the waste exactly when memory is tightest. (Keeping 4x for
  the FIRST buffer of a usage class is fine; it is the spill buffers
  that must stop being 4x.)

## Phase 4 — Right-size the scene-load staging path (optional follow-up)

With phases 2-3 the budget already holds; these reduce flush frequency:

- Dedicated one-shot staging: for very large uploads (image mip chains,
  mesh vertex/index pool block init), allocate a plain host-visible
  `Buffer`, record the copy, and free it via `add_completion_handler` —
  transient, never joins the permanent ring pool. (The
  `upload_to_buffer staging` allocations already work like this.)
- Requests larger than the whole budget (a single >256 MiB acquisition)
  must take this path rather than forcing a giant ring buffer.

## Phase 5 — Diagnostics (do together with phase 1)

- Log at info on every ring buffer creation: size, usage class, new
  count, new total bytes. Warn when the count crosses a threshold (e.g.
  8) — this failure mode stayed invisible until a VMA OOM dump.
- Log each loading-path `flush_transfers()` with bytes flushed, so load
  pacing is visible in the log.
- The `vmaCreateBuffer` failure path now dumps all VMA allocations
  (vulkan_buffer.cpp, added 2026-08-16); keep it.

## Verification

- Load `res/editor/assets/bistro_lights_c.gltf` (Debug build) BOTH ways:
  through the interactive editor (concurrent) and through a headless /
  synchronous import (blocking). In both, grep the log: ring-buffer
  totals must plateau at the budget, flush lines must appear in the
  blocking case, and the load must complete (forward progress).
- After load + N frames the pool must shrink back to the warm set;
  process working set (`Get-Process editor`) no longer holds multi-GB of
  host staging.
- The device-destroy VMA stats dump ("VMA stats before destroy") must
  show only the warm ring buffers.
- Regression check: interactive editing after load (mesh edits, lightmap
  prepare) must not stutter from ring-buffer churn — the warm-set K and
  idle N exist for this; tune with the profiler if needed.
