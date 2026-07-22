# Frame Pacer — Input Inventory and Sourcing

This document is deliverable 3 of the planning phase defined in [frame_pacing.md](frame_pacing.md): an inventory of every input the algorithm ([frame_pacing_algorithm.md](frame_pacing_algorithm.md)) needs, and where each is sourced from, per capability tier. File/line references are to the erhe tree as of 2026-07-21 and name the integration sites the implementation phase will touch.

## 1. Capability tiers (sourcing view)

The full tier matrix with behavior per tier is deliverable 5; sourcing needs only these three:

| Tier | Platform | Required capabilities |
|---|---|---|
| **W** (first target) | Windows Vulkan | `VK_EXT_present_timing`, `VK_KHR_present_id` + `VK_KHR_present_wait`, `VK_KHR_calibrated_timestamps` (or EXT alias), FIFO-family present mode; `VK_*_present_mode_fifo_latest_ready` used when present |
| **A** (future) | Android Vulkan | `VK_GOOGLE_display_timing`, `VK_KHR_present_id`/`present_wait` where available, calibrated timestamps (`CLOCK_MONOTONIC` domain) |
| **OFF** | any | Baseline unmet → no pacing decisions; frame records still collected from whatever sources exist |

Note on `VK_EXT_present_timing`: the extension is recent; the descriptions below use its concepts (swapchain timing properties, explicit time domains, target time at a chosen present stage, past-presentation timing with per-stage timestamps). Exact entry-point and structure names must be verified against the shipping `vulkan_core.h` at implementation time. `VK_KHR_present_wait2`/`present_id2` are newer variants of the KHR pair; probe both, prefer the newer when present.

## 2. Clock domains

Requirement: one monotonic clock domain for all frame records. On Windows this is QPC; on Android `CLOCK_MONOTONIC`.

| Domain | Used for | Bridge |
|---|---|---|
| QPC / `CLOCK_MONOTONIC` (the *reference domain*) | All CPU events, all pacer decisions, frame records | — |
| GPU ticks (`vkCmdWriteTimestamp`, `timestampPeriod`) | GPU frame begin/end | `vkGetCalibratedTimestampsKHR` with `VK_TIME_DOMAIN_DEVICE` + `VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER` (Windows) / `CLOCK_MONOTONIC` (Android); recalibrate periodically, monitor `maxDeviation` |
| Presentation engine domain | Achieved present times, refresh period | Tier W: `VK_EXT_present_timing` exposes explicit time domains — select/calibrate to the reference domain. Tier A: `VK_GOOGLE_display_timing` timestamps are `CLOCK_MONOTONIC` already |

Calibration error folds into the margin `m` (model §10.6); no separate control path.

## 3. Input inventory

Grouped by production site. "Consumer" names the pacer interface call ([frame_pacing_algorithm.md §1](frame_pacing_algorithm.md)) that ingests the input. "Latency" is when the input becomes causally available relative to the frame it describes — the design tolerates every latency listed (verified by the simulation's event-delivery model).

### 3.1 Static / per-swapchain inputs

| Input | Symbol | Tier W source | Tier A source | erhe integration site | Latency |
|---|---|---|---|---|---|
| Refresh period | `T` | Swapchain timing properties (`VK_EXT_present_timing`) | `vkGetRefreshCycleDurationGOOGLE` | Query at swapchain creation, `vulkan_swapchain.cpp` / `vulkan_surface.cpp`; re-query on recreation | Static per swapchain |
| Frames in flight | `F` | Engine constant | Engine constant | `Device_impl::get_number_of_frames_in_flight()`, `vulkan_device.cpp:1999` | Static |
| Swapchain image count | — (fixed input, FR5) | Swapchain creation result | same | `vulkan_swapchain.cpp` | Static per swapchain |
| Present mode in use | — | Present-mode selection (FIFO family; `fifo_latest_ready` preferred, already scored highest) | same | `vulkan_surface.cpp:557-567` | Static per swapchain |
| Capability tier | — | Extension + feature probes | same | `check_device_extension` table `vulkan_device.cpp:911-945`, feature chains `vulkan_device_init.cpp:747/1168` (follow the `fifo_latest_ready` pattern; **probes for `present_id`, `present_wait`, `present_timing`, `calibrated_timestamps` are new work — none exist today**) | Static per device |
| Deadline offset initial | `δ̂_pe` | Constant (2.5 ms), then refined from feedback (§3.4) | `presentMargin` / `earliestPresentTime` from GOOGLE past timing | Pacer-internal | — |
| Application minimum cadence | `N_min` | Application | Application | App layer (e.g. paused-screen state in the editor) → `set_min_vsyncs()` | On change |

### 3.2 Per-frame CPU events (reference clock, same thread, immediate)

All produced by instrumentation in the reference domain; consumed as frame-record entries and, where noted, by pacer calls.

**Normative — `c_k` is a service time.** The value fed to `on_cpu_done()` is the CPU slot span **minus** the involuntary waits recorded inside it (pacer wait, fence waits, acquire wait). The raw span and each wait are recorded separately in the frame records; the subtraction happens at the pacer boundary. Feeding the raw span would contaminate the outer-loop statistic `W` with wait time caused by pacing and GPU state — a cross-loop feedback coupling whose failure mode is spurious downshift. The verification simulation uses pure service times; this definition is a parity requirement for the C++ port.

| Input / record event | Source | erhe integration site | Consumer |
|---|---|---|---|
| `now` at schedule | QPC read | Pacer call site in the editor main loop | `schedule()` |
| Pacer wait begin/end (= release `r_k` actual) | Around the pacer-mandated wait | Editor run loop, before per-frame work — the retired sleep-throttle experiment at `src/editor/editor.cpp:578-589` marks the intended attachment point | `schedule()` return → wait; wait-end timestamp feeds `L_k` |
| CPU slot begin/end (`c_k`) | Tick bracket | `Device::begin_frame`/`end_frame` (`device.hpp:336-343`) bracket the graphics frame; CPU slot spans the app tick around them | `on_cpu_done()` |
| Fence wait begin/end | Around `vkWaitForFences` | `vulkan_command_buffer.cpp:228` (pre-submit wait), `vulkan_device.cpp:92` (frame-in-flight fence) | Frame record; part of measured `L_k` |
| Acquire begin/end | Around `vkAcquireNextImageKHR` | Swapchain path in `Command_buffer::begin_swapchain` (`vulkan_command_buffer.cpp:714-754`) | Frame record |
| Submit timestamp | At `vkQueueSubmit` | Submit path `vulkan_device.cpp:1174-1232` | Frame record |
| Present request timestamp + id chaining | At `vkQueuePresentKHR`, with `VkPresentIdKHR` (and target-time struct, §3.5) chained | Present drive `vulkan_device.cpp:1232` area | Frame record; id correlates feedback |
| Display floor begin/end | Deferred (FR3 fallback) — record slots reserved, not produced initially | — | Frame record (future) |

### 3.3 GPU events

| Input | Symbol | Source | erhe integration site | Consumer | Latency |
|---|---|---|---|---|---|
| GPU frame begin/end | `g_k`, `gpu_end` | **Dedicated frame-spanning timestamp bracket**: `vkCmdWriteTimestamp` in the frame's first submitted command buffer and in the last one before present (**new work** — the existing `Gpu_timer` objects are all per-render-pass; none spans the frame); results via non-blocking `vkGetQueryPoolResults` (`WITH_AVAILABILITY`, no `WAIT`) **polled at every schedule point**; ticks → reference clock via `timestampPeriod` + calibrated timestamps | Slot machinery reusable: query pool `vulkan_device.hpp:465-471`, write `vulkan_device.cpp:2066-2102`, lazy collect `vulkan_device.cpp:1334-1370` (per-pass timers stay on the lazy path — they feed profiling, not control). New work: frame bracket, per-frame polling, QPC calibration | `on_gpu_done()` | **Nominal: first schedule point after GPU finish (≈ 1 frame) — what claims C1–C8 assume.** Lazy fence-read fallback (≈ `F` frames) degrades gracefully: verified C9 (step disruption 7 vs. 6 frames, settle 227 ms vs. 193 ms, no oscillation) |
| Chain latency | `L_k` | Derived: calibrated `gpu_end` − pacer-wait-end | Pacer-internal | `on_gpu_done()` | with `gpu_end` |
| Believed slack | `s_k` | Derived: `d_k − gpu_end` | Pacer-internal | margin AIMD, miss taxonomy | with `gpu_end` |

Notes on `g_k` fidelity:

- Measured as the frame *span*, `g_k` includes GPU idle bubbles when the CPU submits late — a conservative (safe-side) overestimate of GPU service time. Acceptable by default; if it ever causes premature downshifts in practice, the refinement is summing the existing per-pass `Gpu_timer` spans to approximate busy time.
- The existing `VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT` for both begin and end queries is acceptable for the frame bracket: a bottom-of-pipe *begin* fires when previously submitted work has drained — i.e. when the GPU actually becomes free to start this frame — and bottom-of-pipe *end* is the correct chain-completion point. Keep a code comment on this choice; begin-at-top vs. begin-at-bottom is the difference between queued span and execution span.

### 3.4 Presentation feedback

| Input | Tier W source | Tier A source | Consumer | Latency |
|---|---|---|---|---|
| Achieved present time (per id) | Past-presentation timing (`VK_EXT_present_timing`), stage = latch / first-pixel-visible | `vkGetPastPresentationTimingGOOGLE` → `actualPresentTime` | `on_present_feedback()` → grid phase tracker, in-flight-aware anchor, `q` bookkeeping | 1–4 frames after display; polled once per frame at the schedule point |
| `δ̂_pe` refinement | Latch-stage timestamp vs. calibrated `gpu_end` for hit frames | `presentMargin` / `earliestPresentTime` directly | Pacer-internal estimator (conservative initial constant until enough samples) | trailing |
| Presentation-path change hint (windowed ↔ fullscreen) | Not directly queryable — appears as a level shift in `δ̂_pe` residuals; optionally platform hints | n/a | Log only (future-work scope) | trailing |

### 3.5 Actuators (decision outputs and their mechanisms)

| Output | Mechanism, Tier W | Mechanism, Tier A | erhe integration site |
|---|---|---|---|
| Release gating (FR2) | Wait until `release_time` on a high-resolution waitable timer (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`); residual jitter ≈ 0.5–1 ms is the `guard` tunable | `clock_nanosleep`-class wait | `erhe_time/sleep.hpp` exists; its actual precision on Windows must be measured during implementation and `guard` set accordingly |
| Target present time (FR3) | Present-timing info chained to `vkQueuePresentKHR`: target = `V̂(j_k)` at the chosen present stage | `VkPresentTimesInfoGOOGLE.desiredPresentTime = V̂(j_k)` ("no earlier than" semantics) | Present drive, `vulkan_device.cpp:1232` area |
| Present-wait clamp (FR5) | `vkWaitForPresentKHR(swapchain, presentId = k − 1 − Q*, timeout)` before releasing frame k | same where available | Executed as part of the pacer wait, editor loop |
| Predicted display time (FR4) | `schedule()` return `{predicted_display, predicted_duration}` | same | Editor tick: replaces wall-clock sampling for animation/simulation time |
| OFF mode | release = now, no target time, no waits | same | Pacer-internal degradation |

### 3.6 Validation signals (not control inputs)

Two CPU-timeline observations come for free, need no clock calibration, and serve as cross-checks on the primary sources. They shall be recorded and compared, feeding the degradation diagnostics of [frame_pacing_capability_tiers.md](frame_pacing_capability_tiers.md) §5; they are never fed to the control loops.

| Signal | Source | Validates |
|---|---|---|
| Fence-signal observation time | When the CPU observes the frame's fence signaled (upper bound on `gpu_end`, reference clock) | Calibrated-timestamp drift; sanity of `gpu_end` |
| Present-wait unblock time | Return of the FR5 `vkWaitForPresentKHR` clamp wait ≈ display time of the waited frame | Presentation-feedback loss or excess latency |

## 4. Requirements data-list coverage

Every event in the requirements document's data list maps to a §3 row: CPU slot begin/end, pacer wait begin/end, fence wait begin/end, acquire begin/end, submit, present request (§3.2); display floor begin/end (§3.2, deferred with FR3's fallback); GPU frame begin/end (§3.3); achieved present feedback (§3.4, "recorded where the platform provides it"). The ring buffer holding these records is the same structure backing the estimator windows (design §2) — one data path for control, profiling UI, and analysis tools.

## 5. Gaps — new work the implementation phase must add

1. Extension/feature probes and enables for `VK_KHR_present_id`, `VK_KHR_present_wait` (and the *2 variants), `VK_EXT_present_timing`, `VK_KHR_calibrated_timestamps` — following the existing `fifo_latest_ready` probe pattern (`vulkan_device.cpp:911-945`, `vulkan_device_init.cpp:747/1168`). Tier resolution from the probe results.
2. Swapchain-creation opt-in for present timing (per-swapchain enable) and re-query of `T` on recreation, wired to `notify_swapchain_recreated()`.
3. The frame-spanning GPU timestamp bracket (§3.3 — all existing `Gpu_timer`s are per-render-pass), its non-blocking per-frame result polling at the schedule point, and GPU-timestamp calibration into QPC with periodic recalibration. Include the begin-stage code comment (§3.3 notes).
4. `VkPresentIdKHR` chaining and an id↔frame-index correlation table in the present drive.
5. Timestamp instrumentation at the §3.2 sites that do not yet record times (acquire, submit, present request, fence waits record profiling scopes but not pacer-consumable QPC stamps), and the `c_k` service-time subtraction at the pacer boundary (§3.2, normative).
6. Measurement of the Windows waitable-timer wake-up jitter to set `guard`; `timeBeginPeriod` interaction documented.
7. The pacer call site in the editor run loop (attach where the retired sleep throttle was), including routing `predicted_display` to the simulation clock (FR4).
8. Recording of the §3.6 validation signals and their comparison against the primary sources.

Deliverables 4 (behavior specification per scenario) and 5 (capability tier matrix incl. windowed vs. fullscreen quality statements) remain.
