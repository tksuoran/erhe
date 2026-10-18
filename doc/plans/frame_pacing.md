# Frame pacing: outstanding work

Status: proposed

This plan holds the work that the frame pacer described in
[../frame_pacing/requirements.md](../frame_pacing/requirements.md) and its
companion documents does not do yet. The pacer itself, its capability tiers,
its verification UI and its measured behavior are described there; only the
open items live here.

## Tier S in the GPU-bound regime

Measured 2026-10-18 with `scripts/bistro_frame_bench.py` (windowed Release
editor, Bistro, a 120 Hz display, tier S, plain FIFO with 2 swapchain
images, the pipelined render thread). The device's own frame records
(`get_frame_pacing_frames`: GPU bracket, slot fence wait, submit and present
times) give the picture; Tracy's GPU zones on this device sit a few
milliseconds off the CPU clock and cover only the timed passes.

**What happens.** The scene is GPU-bound: the GPU bracket runs back to back
(gap between frames about 0.1 ms) whether vsync is on or off. With IMMEDIATE
present the bracket is the pure work (about 21.5 ms here) and the tick is
steady. With FIFO the bracket grows by about one third of a refresh period
and locks to three periods (25 ms): the frame command buffer waits for the
swapchain image at `COLOR_ATTACHMENT_OUTPUT`, a submit-wide stage, so the
shadow and viewport passes wait for the image too, and the image of a
2-image FIFO swapchain is released only at the vblank that displays the
previous frame. The GPU frame start is therefore vblank-locked, which is
also what makes the cadence steady on screen. The main thread's "alternating
slow and fast frames" is the CPU tick differencing the GPU end jitter on
that locked grid: tick(N) = 25 ms + w(N) - w(N-1) with w the per-frame GPU
duration jitter, which gives the measured lag-1 autocorrelation near -0.5
while the present interval stays at three periods. It is a readout
artefact, not a control loop oscillating.

**Where the main thread blocks.** `Device::acquire_frame_slot` in M2 waits
for the GPU frame two ids back (`vkWaitSemaphores`, about 20 ms per frame
here). Moving that wait later would change nothing in this regime: the GPU
queue is already full, so throughput is set by the GPU and the wait only
decides where the CPU parks. What the position does cost is latency: the
frame is released at the earliest possible moment (the slot's previous use
retired), so its work sits in the queue behind a whole GPU frame; input to
display is about three frame periods. In a CPU-bound regime the wait never
blocks, so its position does not matter there either.

**Why the slop servo did not pace.** Two defects, the first fixed
(88b03400a): a device without the present timing capability never set the
display refresh period, so the observer created no pacer (no decisions, no
Frame Pacing capture) and the servo kept its construction constant. The
second is structural: the servo's overshoot test compares the loop period
with one refresh period, so at any cadence above one vsync per frame every
frame is an overshoot, the sleep is halved every frame, and the servo is
inert. Tier S as designed only paces loads lighter than the refresh period.

**Status 2026-10-18.** Items 1 and 2 landed: the servo paces against the
tracked plant loop period (`Slop_servo_pacer`, claim C19b) and the editor's
single input poll per frame follows the pacing wait (`run()` runs the frame
start, then polls, then `frame()`). A second poll inside `frame()` was tried
first and dropped: `poll_events` publishes a new event batch, so the second
poll discarded the first poll's events (mouse button events were lost), and
a modal resize loop entered from a poll inside `frame()` refuses the
live-resize redraws. Measured on the scene above: the
sleep settles near the former slot wait, the slot fence wait falls from
about 20 ms to under 1 ms, the GPU stays saturated and the cadence stays at
three periods, the GPU end-to-end interval loses its alternation (lag-1
autocorrelation near zero), and release-to-GPU-end latency falls by about
one frame of queueing. Items 3 and 4 are open.

**Proposed, in order, keeping two frames in flight:**

1. **Cadence-aware slop servo.** Measure the achieved cadence N as the loop
   period rounded to refresh periods (FIFO backpressure makes it a whole
   number) and use N x T as the overshoot reference; servo the sleep so the
   residual involuntary blocking (slot fence wait plus present blocking)
   settles at the headroom. The sleep then replaces the 20 ms slot wait,
   the frame is released just in time for its GPU slot, latency drops by
   about one frame of queueing at the same throughput, and the tick becomes
   steady because the sleep absorbs the GPU end jitter. The hard waits stay
   where they are as the safety net. Verify with the benchmark: fence wait
   near the headroom, GPU gap unchanged, present interval unchanged, tick
   lag-1 autocorrelation near zero, input-to-GPU-begin shorter.
2. **Poll input after the release sleep.** `run()` polls events before M1,
   so events arriving during the sleep wait a whole frame; with a sleep of
   most of the frame this is the dominant input latency. Poll after the
   pacing wait (or poll again there) and dispatch in M2 as now.
3. **Stop gating the off-screen passes on the swapchain image.** Acquire
   the image lazily before the swapchain pass or split the submit so only
   the swapchain pass waits on the acquire semaphore. This returns the
   image-wait third of a refresh period to the GPU per frame (about 14 %
   here). On its own it also removes the vblank lock of the GPU start, so
   it must land together with item 1 holding the cadence, or the display
   interval alternates between two and three periods; with item 1 the
   recovered time becomes latency reduction and headroom against the
   occasional slot miss (GPU brackets above three periods were observed).
4. **Report the pacing metric, not the tick.** The frame log and status
   readout show the CPU tick; add the GPU end-to-end and present intervals
   from the frame records so a vblank-locked steady display reads as
   steady.

Not proposed: a third frame in flight (adds a frame of latency for no
throughput, the queue is already full) and FIFO_LATEST_READY or mailbox
(drops the backpressure tier S senses). The main thread's slot wait could
move into R behind an upload command pool ring one deeper than the frame
slots, which would let M2 never block on the GPU; it buys nothing while
item 1 is missing and is not needed once it exists.

## Tier W on Linux: open findings

Measured 2026-10-18 with `scripts/bistro_frame_bench.py` (windowed Release
editor, Bistro, NVIDIA proprietary driver on X11, the window on a 120 Hz
external display, tier W, `FIFO_LATEST_READY`, the pipelined render thread).
The machine also has a 240 Hz builtin panel, and the swapchain's
`refreshDuration` query reported that panel's 4.17 ms, not the 8.33 ms of
the display the window is on (item 5 below); because no feedback arrives
(item 1) nothing re-seeds the period, so every "vsync" the pacer counts in
these numbers is half a real refresh. The run found and fixed
one defect (eeb5d595a: the GPU frame bracket end was queued after the
present holdback, so the pacer's `gpu_duration` carried its own holdback and
the cadence climbed to the 200 ms holdback cap). What remains:

1. **No presentation feedback.** `vkGetPastPresentationTimingEXT` never
   returns a completed primary-stage timing on this path ("First present
   timing feedback" is never logged), so `achieved_present_time` stays 0,
   the grid phase is never anchored (`grid_phase` 0.0), FR4 predictions are
   unanchored and the C15 holdback paces against an assumed phase. Find out
   whether the driver reports the stage on X11 at all (the surface's
   stage-query mask is 0x5: queue-ops-end and first-pixel-out), whether the
   request is suppressed, or whether the time-domain conversion rejects the
   sample; log the raw feedback once at startup so the three cases are
   distinguishable from `logs/log.txt`.
2. **The upload-to-frame bubble inflates `g_k`.** The bracket begins at the
   frame's first command buffer (the M2 upload cb, submitted at the start of
   R) and the frame cb follows after R's CPU recording (about 2 ms here), so
   the span reads about 10.8 ms for 8.1 ms of timed GPU passes, with a q95
   near 15 ms. On the pacer's 4.17 ms grid that is the difference between
   4 and 5 slots per frame; the pacer settled at N=5 (20.8 ms, 48 fps, two
   and a half real 120 Hz refreshes) for a GPU that finishes the frame in
   about 9 ms. inputs.md 3.3 accepts the overestimate as conservative; at
   short grid periods it is not cheap. Options: begin the
   bracket at the frame cb when the upload cb is empty or trivially short,
   or measure the two submits as separate spans and sum them.
3. **The holdback blocks R.** `submit_command_buffers` sleeps the holdback
   on the render thread before `vkQueuePresentKHR` (11-12 ms per frame at
   N=5), so R is busy for the whole hold and the next frame's R cannot
   start; with frame pipelining the main thread does not wait, but the
   render thread's own next frame does. A deferred present (the
   "Asynchronous present" section below) removes this.
4. **Observer summaries are debug-only.** `Frame_pacing_observer`'s cadence
   change and summary lines reach the log only when `editor.frame_pacing`
   is at debug level (`config/editor/logging.json` says info), so a
   production log shows the holdback-cap warnings but not the N they came
   from. Log cadence changes at info.
5. **`refreshDuration` follows the wrong display** - addressed. With two
   displays (a 240 Hz builtin panel and a 120 Hz external one) the swapchain
   of a window on the 120 Hz display reported 4.17 ms, and with item 1 the
   C16 re-seed could not correct it, so the pacer ran on a grid twice too
   fine (N=5 on it was 2.5 real refreshes). The period now comes from
   `erhe::graphics::Display_refresh_resolver` (inputs.md 3.1): the window's
   display mode, the query (with `refreshInterval` and the timing properties
   counter), and an init calibration from displayed times that verifies
   both. Left: the init calibration runs once per display; a second
   calibration pass after a display change waits for the next presents like
   the first, and a VRR display is still paced on the minimum cycle.
6. **`vkWaitForPresentKHR` is not a display-time source on this driver.**
   On the NVIDIA/X11 path the wait on the previous present returns 0.3 to
   2 ms after that present was queued (32 samples, median 0.5 ms), so it
   completes at queue processing, not at scanout; the calibration's
   fast-frame gate rejects these and the run stays "unverified". On the
   Intel/Mesa path the same wait wakes on the vblank cadence with about 5 %
   jitter in the median (8.8 to 8.9 ms on a 120 Hz display), which the 10 %
   confirmation tolerance absorbs. Two consequences to follow up: the FR5
   present-wait clamp cannot clamp anything on the NVIDIA path, and the
   clamp's wait on the main thread overlaps the render thread's
   `vkQueuePresentKHR` on the same swapchain, which the specification marks
   externally synchronized (`vk.xml` `externsync="true"` on the swapchain
   parameter); the calibration keeps the clamp off while it runs for that
   reason, the steady state does not.

## Tier A: Android Vulkan backend

The pacer core, the frame records and the tier resolution are
backend-agnostic, but no Android sourcing exists: nothing in
`src/erhe/graphics` calls `VK_GOOGLE_display_timing`. Tier A needs

- `vkGetRefreshCycleDurationGOOGLE` for the refresh period and
  `vkGetPastPresentationTimingGOOGLE` for the achieved-present feedback,
  mapped onto the same `on_present_feedback` path tier W uses,
- calibrated timestamps in the `CLOCK_MONOTONIC` domain,
- `VkPresentTimesInfoGOOGLE.desiredPresentTime` as the FR3 actuator
  ("no earlier than" semantics),
- a decision for the FR5 queue clamp when the device ships no
  `VK_KHR_present_wait`: either a gate built from GOOGLE feedback, or tier A
  resolves OFF on such devices (the state today).

## VRR-aware tier

Tiers W and A assume a periodic vsync grid (`V(j) = phase + j*period`), so
variable refresh rate displays are outside their model. Tier S survives VRR
because it never assumes a grid, and is the fallback there. A tier that
paces a variable-rate display on purpose has no design yet.

## Display and display-mode change handling

Monitor switch, refresh-rate change and window moves between displays are
handled only indirectly, through the swapchain recreation they usually cause
(the pacer resets its presentation-side state and re-queries the period) and
through the gross-error period re-seed. Detecting such an event explicitly
and logging it would make the recovery attributable instead of inferred.

## Asynchronous present

The C15 present-request holdback runs inline on the producer thread, which
makes it structurally lossy once
`budget > (N+1)*T - delta_pe - 2*guard`: tail frames slip (claim C18
accepts a slip rate of about the spike rate). Exact holdback together with
full-budget scheduling needs the present request issued from a separate
thread (same queue family, no ownership transfer). Do this only when a
measurement shows the loss matters.

## Target present times on capable hardware

The development machine's driver accepts target present times and ignores
them (issue #1 of
[../reference/nvidia_present_timing_driver_report.md](../reference/nvidia_present_timing_driver_report.md)),
so the FR3 actuator itself is unverified. On a driver that honors targets,
compare the two recommended formulations - strict mode with a small early
bias (`target - T/4`), and `NEAREST_REFRESH_CYCLE` with a `+T/4` late bias -
and confirm that the C15 holdback can then be switched off without
re-admitting early displays.

## Driver report issue #3

The present-timing queue fills within about 32 presents on plain
`VK_PRESENT_MODE_FIFO_KHR` on the development machine, while the same
polling cadence keeps it empty on `fifo_latest_ready`. erhe survives it (the
error marks the swapchain invalid, and tier S does not enable present timing
at all), but the finding is not written up in the driver report yet.

## Verification UI: simulated GPU workload

The window's workload knob inserts CPU work only. A GPU-side knob (extra
draw or compute work inside the measured GPU frame bracket) would let the
GPU-step scenarios be reproduced from the UI the same way the CPU steps are.

## Verification UI: capture serialization

The window's capture is append-only, in memory, and dropped by Clear. Saving
and loading it would allow offline analysis and comparison across runs. The
capture is a dense frame-id-indexed sequence of fixed-size samples, so the
intended format is columnar and delta-encoded (frame begin times as
inter-frame deltas, the other timestamps as float milliseconds relative to
their frame's begin) rather than a struct dump.
