# Frame Pacer - Input Inventory and Sourcing

Stability: stable

An inventory of every input the algorithm ([algorithm.md](algorithm.md)) needs and where each is sourced from, per capability tier. The `erhe integration site` columns name where in the engine each input is produced; file/line references are indicative and go stale, the type and function names do not.

## 1. Capability tiers (sourcing view)

The full tier matrix with behavior per tier is [capability_tiers.md](capability_tiers.md); sourcing needs only these three:

| Tier | Platform | Required capabilities |
|---|---|---|
| **W** | Windows Vulkan | `VK_EXT_present_timing`, `VK_KHR_present_id` + `VK_KHR_present_wait`, `VK_KHR_calibrated_timestamps` (or EXT alias), FIFO-family present mode; `VK_*_present_mode_fifo_latest_ready` used when present |
| **A** (planned, see Future work) | Android Vulkan | `VK_GOOGLE_display_timing`, `VK_KHR_present_id`/`present_wait` where available, calibrated timestamps (`CLOCK_MONOTONIC` domain) |
| **OFF** | any | Baseline unmet -> no pacing decisions; frame records still collected from whatever sources exist |

The descriptions below use `VK_EXT_present_timing` concepts: swapchain timing properties, explicit time domains, a target time at a chosen present stage, and past-presentation timing with per-stage timestamps. `VK_KHR_present_wait2`/`present_id2` are newer variants of the KHR pair; both are probed and the newer one is preferred where present.

## 2. Clock domains

Requirement: one monotonic clock domain for all frame records. On Windows this is QPC; on Android `CLOCK_MONOTONIC`.

| Domain | Used for | Bridge |
|---|---|---|
| QPC / `CLOCK_MONOTONIC` (the *reference domain*) | All CPU events, all pacer decisions, frame records | - |
| GPU ticks (`vkCmdWriteTimestamp`, `timestampPeriod`) | GPU frame begin/end | `vkGetCalibratedTimestampsKHR` with `VK_TIME_DOMAIN_DEVICE` + `VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER` (Windows) / `CLOCK_MONOTONIC` (Android); recalibrate periodically, monitor `maxDeviation` |
| Presentation engine domain | Achieved present times, refresh period | Tier W: `VK_EXT_present_timing` exposes explicit time domains - select/calibrate to the reference domain. Tier A: `VK_GOOGLE_display_timing` timestamps are `CLOCK_MONOTONIC` already |

Calibration error folds into the margin `m` (model section 10.6); no separate control path.

## 3. Input inventory

Grouped by production site. "Consumer" names the pacer interface call ([algorithm.md section 1](algorithm.md)) that ingests the input. "Latency" is when the input becomes causally available relative to the frame it describes - the design tolerates every latency listed (verified by the simulation's event-delivery model).

### 3.1 Static / per-swapchain inputs

| Input | Symbol | Tier W source | Tier A source | erhe integration site | Latency |
|---|---|---|---|---|---|
| Refresh period | `T` | Swapchain timing properties (`VK_EXT_present_timing`) | `vkGetRefreshCycleDurationGOOGLE` | Query at swapchain creation, `vulkan_swapchain.cpp` / `vulkan_surface.cpp`; re-query on recreation | Static per swapchain |
| Frames in flight | `F` | Engine constant | Engine constant | `Device_impl::get_number_of_frames_in_flight()`, `vulkan_device.cpp:1999` | Static |
| Swapchain image count | - (fixed input, FR5) | Swapchain creation result | same | `vulkan_swapchain.cpp` | Static per swapchain |
| Present mode in use | - | Present-mode selection (FIFO family; `fifo_latest_ready` preferred, already scored highest) | same | `vulkan_surface.cpp:557-567` | Static per swapchain |
| Capability tier | - | Extension + feature probes | same | `check_device_extension` table and feature chains in `vulkan_device.cpp` / `vulkan_device_init.cpp`, covering `fifo_latest_ready`, `present_id`, `present_wait`, `present_timing` and `calibrated_timestamps` | Static per device |
| Deadline offset initial | `delta_hat_pe` | Constant (2.5 ms), then refined from feedback (section 3.4) | `presentMargin` / `earliestPresentTime` from GOOGLE past timing | Pacer-internal | - |
| Application minimum cadence | `N_min` | Application | Application | App layer (e.g. paused-screen state in the editor) -> `set_min_vsyncs()` | On change |

### 3.2 Per-frame CPU events (reference clock, same thread, immediate)

All produced by instrumentation in the reference domain; consumed as frame-record entries and, where noted, by pacer calls.

**Normative - `c_k` is a service time.** The value fed to `on_cpu_done()` is the CPU slot span **minus** the involuntary waits recorded inside it (pacer wait, fence waits, acquire wait). The raw span and each wait are recorded separately in the frame records; the subtraction happens at the pacer boundary. Feeding the raw span would contaminate the outer-loop statistic `W` with wait time caused by pacing and GPU state - a cross-loop feedback coupling whose failure mode is spurious downshift. The verification simulation uses pure service times; this definition is a parity requirement for the C++ port.

| Input / record event | Source | erhe integration site | Consumer |
|---|---|---|---|
| `now` at schedule | QPC read | Pacer call site in the editor main loop | `schedule()` |
| Pacer wait begin/end (= release `r_k` actual) | Around the pacer-mandated wait | Editor run loop (`src/editor/editor.cpp`), before per-frame work | `schedule()` return -> wait; wait-end timestamp feeds `L_k` |
| CPU slot begin/end (`c_k`) | Tick bracket | `Device::begin_frame`/`end_frame` (`device.hpp:336-343`) bracket the graphics frame; CPU slot spans the app tick around them | `on_cpu_done()` |
| Fence wait begin/end | Around `vkWaitForFences` | `vulkan_command_buffer.cpp:228` (pre-submit wait), `vulkan_device.cpp:92` (frame-in-flight fence) | Frame record; part of measured `L_k` |
| Acquire begin/end | Around `vkAcquireNextImageKHR` | Swapchain path in `Command_buffer::begin_swapchain` (`vulkan_command_buffer.cpp:714-754`) | Frame record |
| Submit timestamp | At `vkQueueSubmit` | Submit path `vulkan_device.cpp:1174-1232` | Frame record |
| Present request timestamp + id chaining | At `vkQueuePresentKHR`, with `VkPresentIdKHR` (and target-time struct, section 3.5) chained | Present drive `vulkan_device.cpp:1232` area | Frame record; id correlates feedback |
| Present holdback begin/end | Around the FR3 holdback sleep before `vkQueuePresentKHR` (claim C15) | Present drive, after `vkQueueSubmit2` | Frame record; stamped as an involuntary wait, excluded from CPU service time |

### 3.3 GPU events

| Input | Symbol | Source | erhe integration site | Consumer | Latency |
|---|---|---|---|---|---|
| GPU frame begin/end | `g_k`, `gpu_end` | **Dedicated frame-spanning timestamp bracket**: `vkCmdWriteTimestamp` in the frame's first submitted command buffer and in the last one before present (the `Gpu_timer` objects are per-render-pass and span no frame; this bracket is separate); results via non-blocking `vkGetQueryPoolResults` (`WITH_AVAILABILITY`, no `WAIT`) **polled at every schedule point**; ticks -> reference clock via `timestampPeriod` + calibrated timestamps | Dedicated query-pool ring in the Vulkan device; per-pass timers stay on the lazy collect path, because they feed profiling, not control | `on_gpu_done()` | **Nominal: first schedule point after GPU finish (~= 1 frame) - what claims C1-C8 assume.** Lazy fence-read fallback (~= `F` frames) degrades gracefully: verified C9 (step disruption 7 vs. 6 frames, settle 227 ms vs. 193 ms, no oscillation) |
| Chain latency | `L_k` | Derived: calibrated `gpu_end` - pacer-wait-end | Pacer-internal | `on_gpu_done()` | with `gpu_end` |
| Believed slack | `s_k` | Derived: `d_k - gpu_end` | Pacer-internal | margin AIMD, miss taxonomy | with `gpu_end` |

Notes on `g_k` fidelity:

- Measured as the frame *span*, `g_k` includes GPU idle bubbles when the CPU submits late - a conservative (safe-side) overestimate of GPU service time. Acceptable by default; if it ever causes premature downshifts in practice, the refinement is summing the existing per-pass `Gpu_timer` spans to approximate busy time.
- The existing `VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT` for both begin and end queries is acceptable for the frame bracket: a bottom-of-pipe *begin* fires when previously submitted work has drained - i.e. when the GPU actually becomes free to start this frame - and bottom-of-pipe *end* is the correct chain-completion point. Keep a code comment on this choice; begin-at-top vs. begin-at-bottom is the difference between queued span and execution span.

### 3.4 Presentation feedback

| Input | Tier W source | Tier A source | Consumer | Latency |
|---|---|---|---|---|
| Achieved present time (per id) | Past-presentation timing (`VK_EXT_present_timing`), stage = latch / first-pixel-visible | `vkGetPastPresentationTimingGOOGLE` -> `actualPresentTime` | `on_present_feedback()` -> grid phase tracker, in-flight-aware anchor, `q` bookkeeping | 1-4 frames after display; polled once per frame at the schedule point |
| `delta_hat_pe` refinement | Latch-stage timestamp vs. calibrated `gpu_end` for hit frames | `presentMargin` / `earliestPresentTime` directly | Pacer-internal estimator (conservative initial constant until enough samples) | trailing |
| Presentation-path change hint (windowed <-> fullscreen) | Not directly queryable - appears as a level shift in `delta_hat_pe` residuals | n/a | Absorbed by the estimator; not a control input | trailing |

### 3.5 Actuators (decision outputs and their mechanisms)

| Output | Mechanism, Tier W | Mechanism, Tier A | erhe integration site |
|---|---|---|---|
| Release gating (FR2) | Wait until `release_time` on `erhe::time::Waitable_timer` (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`); measured wake error p99 0.59 ms, which is what sets `guard` = 1 ms. `std::this_thread::sleep_for` is unusable here (p99 wake error 15.3 ms) | `clock_nanosleep`-class wait | Editor run loop |
| Target present time (FR3) | Present-timing info chained to `vkQueuePresentKHR`: target = `V_hat(j_k)` at the chosen present stage, plus the C15 request holdback where the engine ignores the target | `VkPresentTimesInfoGOOGLE.desiredPresentTime = V_hat(j_k)` ("no earlier than" semantics) | Present drive in the Vulkan device |
| Present-wait clamp (FR5) | `vkWaitForPresentKHR(swapchain, presentId = k - 1 - Q*, timeout)` before releasing frame k | same where available | Executed as part of the pacer wait, editor loop |
| Predicted display time (FR4) | `schedule()` return `{predicted_display, predicted_duration}` | same | Editor tick: replaces wall-clock sampling for animation/simulation time |
| OFF mode | release = now, no target time, no waits | same | Pacer-internal degradation |

### 3.6 Validation signals (not control inputs)

Two CPU-timeline observations come for free, need no clock calibration, and serve as cross-checks on the primary sources. They shall be recorded and compared, feeding the degradation diagnostics of [capability_tiers.md](capability_tiers.md) section 5; they are never fed to the control loops.

| Signal | Source | Validates |
|---|---|---|
| Fence-signal observation time | When the CPU observes the frame's fence signaled (upper bound on `gpu_end`, reference clock) | Calibrated-timestamp drift; sanity of `gpu_end` |
| Present-wait unblock time | Return of the FR5 `vkWaitForPresentKHR` clamp wait ~= display time of the waited frame | Presentation-feedback loss or excess latency |

## 4. Requirements data-list coverage

Every event in the requirements document's data list maps to a section 3 row: CPU slot begin/end, pacer wait begin/end, fence wait begin/end, acquire begin/end, submit, present request, present holdback begin/end (section 3.2); GPU frame begin/end (section 3.3); achieved present feedback (section 3.4, where the platform provides it). The ring buffer holding these records is `erhe::frame_pacing::Frame_time_recorder`, the same structure backing the estimator windows (design section 2) - one data path for control, profiling UI, and analysis tools.

## Future work

- [../plans/frame_pacing.md](../plans/frame_pacing.md) - tier A (Android) sourcing and actuators.
