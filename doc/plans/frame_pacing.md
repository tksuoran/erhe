# Frame pacing: outstanding work

Status: proposed

This plan holds the work that the frame pacer described in
[../frame_pacing/requirements.md](../frame_pacing/requirements.md) and its
companion documents does not do yet. The pacer itself, its capability tiers,
its verification UI and its measured behavior are described there; only the
open items live here.

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
