# Frame Pacer - Requirements

Stability: stable

The requirements the frame pacer meets. The interface and the algorithm are backend agnostic so that the pacer can serve both Windows and Android; the implemented target is desktop Windows Vulkan. The `FR` labels below are cited from the other frame pacing documents and from source comments, and keep their numbering.

The design that satisfies these requirements is [control_model.md](control_model.md) (control-theory model) and [algorithm.md](algorithm.md) (normative algorithm); [inputs.md](inputs.md) names where each input is sourced from, [behavior.md](behavior.md) the observable behavior per scenario, [capability_tiers.md](capability_tiers.md) the tiers and what each enables, and [user_interface.md](user_interface.md) the in-editor verification UI.

## Scope

In scope:

- Windows Vulkan, fixed refresh rate displays.
- Both windowed (DWM-composited) and fullscreen presentation. The design states, per capability tier, what pacing quality is achieved in each presentation path.
- FIFO-family present modes (vsync-locked, no tearing). `VK_EXT_present_mode_fifo_latest_ready` is part of the capability tiers below.
- The frame records the profiling UI and analysis tools consume (see Data requirements).

Out of scope:

- Variable refresh rate displays and G-Sync.
- MAILBOX / IMMEDIATE present modes.

## Functional requirements

The frame pacer shall:

**FR1 - Frame duration decision.** Decide how many display refresh periods (vsyncs) each rendered frame shall be visible.

- This decision is made solely by the frame pacing algorithm. The application shall not choose the frame rate directly; it may only set a *minimum* vsyncs-per-frame (e.g. a higher value for a paused screen).
- The decision policy shall adapt to varying application load, on the CPU, on the GPU, or both.
- Downshifting (increasing vsyncs per frame, reducing frame rate) shall happen quickly when load demands it.
- Upshifting (decreasing vsyncs per frame, restoring frame rate) shall happen within a reasonable time once load again allows it, so that available refresh rate is not underutilized for an unreasonable duration. The time headroom must be sustained before upshifting is an adjustable parameter (see Tunable parameters).
- Steady state means each frame is visible for the same number of refresh periods. Alternating cadences (e.g. 1-2-1-2 judder) and oscillation between vsyncs-per-frame values count as failure; the policy shall include hysteresis or an equivalent mechanism to prevent them.

**FR2 - Frame scheduling.** Decide which future display refresh slot each rendered image shall target, and release the CPU to the application to start per-frame work such that:

- a. The chain of CPU work -> GPU work that produces the image finishes in time for the presentation engine to present it at the chosen slot. This is the primary concern.
- b. The CPU starts as late as possible, to minimize input latency. This is a secondary concern, considered only after (a) is satisfied. In practice this means "deadline minus a safety margin"; the safety margin shall be adaptive, with an adjustable initial value (see Tunable parameters), not implicit.

**FR3 - Present timing.** Make a best effort to present each frame at the decided presentation time, by passing the target presentation time to the presentation engine when an API for that is available (see Capability tiers). Where the presentation engine accepts a target and ignores it, the present request itself is held back on the CPU until the target slot's own refresh cycle, so that the earliest feasible vsync is the target (claim C15, [algorithm.md](algorithm.md) section 8).

**FR4 - Predicted display time to the application.** The predicted display time and visible duration of the frame (the result of FR1 + FR2) shall be passed to the application before its per-frame work begins, so that animation and logic simulation can target the time the frame will actually be shown, instead of sampling the wall clock.

**FR5 - Queue depth control.** Ensure that as few frames as possible are queued to the display. Without this, the CPU can burst-fill the present queue, which then stays full and adds unwanted latency. FR2b is the mechanism that normally achieves this, but FR5 is a hard constraint that shall be enforced even when FR2's estimates are wrong (e.g. by throttling on `VK_KHR_present_wait`).

The swapchain image count has a driver-set hard minimum and cannot be lowered below it. That minimum can be large enough that keeping all images queued would add unwanted latency. The pacer shall therefore control the number of *queued* images independently of the swapchain image count, and shall treat the swapchain image count as a fixed input, not something it can reduce.

**FR6 - Capability tiers and off mode.** The design shall enumerate the presentation-timing capabilities it uses and specify pacer behavior for each combination actually encountered on the target platforms. Extensions to take into account:

- `VK_GOOGLE_display_timing` (required for the Android path; shall be accounted for in the backend-agnostic interface)
- `VK_EXT_present_timing`
- `VK_KHR_present_wait`
- `VK_EXT_present_mode_fifo_latest_ready`
- `VK_EXT_calibrated_timestamps` (for correlating GPU timestamps with the CPU clock, see Data requirements)

Full pacing requires recent extensions, in particular `VK_EXT_present_timing` (with `VK_EXT_present_mode_fifo_latest_ready` used where available). No measurement-only or `VK_KHR_present_wait`-only tier exists: a device either has the capabilities for display-time sensing or it does not. Where it does not, the backpressure-sensing fallback (tier S) paces from the blocking a plain-FIFO swapchain exerts, and where even that is unavailable the pacer runs in an off mode: the interface stays in place and frame records are still collected where possible, but no pacing decisions are enforced.

**FR7 - Disturbance recovery.** After a disturbance - a missed present deadline, cold start with no history, swapchain recreation - the priority is to return to steady state; the design shall minimize disruption time. Recovery behavior shall be specified for each of these cases.

## Non-functional requirements

**Control-theoretic foundation.** The FR1/FR2 decisions rest on a model of the problem in the framework of control theory ([control_model.md](control_model.md)), and the algorithm is verified against that model - stability, convergence, and bounded response to disturbances (load spikes, load drops, missed presents). Oscillation in steady state counts as failure. Every change to the algorithm keeps both verification suites green: the Python reference `scripts/frame_pacing_sim.py` and the C++ parity suite `erhe_frame_pacing_tests`.

**Acceptance criteria.** The design shall define measurable criteria at least covering:

- Steady state: every frame visible exactly N refresh periods when the workload fits the budget.
- Bounded convergence time after a load change (both directions; see FR1 for the asymmetry between downshift and upshift, and Tunable parameters for the targets).
- No oscillation between vsyncs-per-frame values in steady state.

**Isolation and testability.** The frame pacing algorithm shall be independent of other engine code so that it can be unit and smoke tested. It shall be drivable by a simulated clock and event source, so the control loop can be tested deterministically against synthetic scenarios (steady load, CPU spike, GPU spike, load drop, missed present, cold start) without a GPU or display.

**Clock domain.** All timestamps in the frame records shall share a single monotonic clock domain (QPC on Windows). GPU timestamps shall be calibrated into that domain (`VK_EXT_calibrated_timestamps`).

## Data requirements

The frame pacer shall keep a record of event timestamps for a bounded number of recent frames, primarily to provide data for its own decision making, and secondarily to feed profiling data to UI and analysis tools. Per frame:

- CPU slot begin - when work on the frame in flight starts
- CPU slot end - when work on the frame in flight ends
- Frame pacer wait begin - when the frame pacer starts waiting for the right time to let the app start per-frame work
- Frame pacer wait end - when the frame pacer wait is done and the app starts
- Fence wait begin - when the CPU starts waiting for the frame-in-flight fence
- Fence wait end - when the CPU is done waiting for the frame-in-flight fence
- Acquire next image begin - just before the call to `vkAcquireNextImageKHR()`
- Acquire next image end - right after the call to `vkAcquireNextImageKHR()`
- Command buffer submit - when the `vkQueueSubmit()` call is made
- Present holdback begin / end - when the frame pacer sleeps before requesting present, to prevent presenting too early on an engine that ignores the requested target time (FR3)
- Request to present - when present is requested
- GPU frame begin - first render pass begin
- GPU frame end - last render pass end

Where the platform provides presentation feedback (e.g. actual present time from `VK_GOOGLE_display_timing` / `VK_EXT_present_timing`), that shall be recorded as well.

The measured frame records are available to profiling UI and analysis tools; the editor's own consumer of them is [user_interface.md](user_interface.md).

## Tunable parameters

Every quantitative bound below is an adjustable parameter. These are the requirement-level targets; the consolidated tunable set the algorithm carries, with its verified defaults, is [algorithm.md](algorithm.md) section 7.

| Parameter | Default | Notes |
|---|---|---|
| Downshift reaction time | <= 2 missed-budget frames | Downshift quickly; sustained overload must not cause repeated misses. |
| Upshift confirmation time | 500 ms of sustained headroom | Prevents oscillation; keeps higher refresh rates from being underutilized for long. |
| Convergence time after disturbance | <= 1 s back to steady state | Applies to FR7 recovery scenarios as well as load changes. |
| Latency safety margin (initial) | 20 % of refresh period | Adaptive thereafter (FR2b); initial value used at cold start. |
| Minimum vsyncs per frame | 1 | Application-settable (FR1), e.g. raised on a paused screen. |
| Target queued present images | 1 | FR5; independent of swapchain image count. |
| Frame record history length | 120 frames | Bounded history for decisions and profiling (Data requirements). |

## Future work

- [../plans/frame_pacing.md](../plans/frame_pacing.md) - Android tier A, VRR, display-change handling, asynchronous present, driver verification, UI capture export.
