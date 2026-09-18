# Frame Pacer — Requirements

Your task is to design a frame pacer for Vulkan. The interface and the algorithm shall be backend agnostic so that the pacer can be implemented on both Windows and Android; the first implementation target is desktop Windows Vulkan. This document specifies requirements for the design; implementation is out of scope for this phase.

## Scope

In scope:

- Windows Vulkan, fixed refresh rate displays.
- Both windowed (DWM-composited) and fullscreen presentation shall be considered. The design must state, per capability tier, what pacing quality can be achieved in each presentation path.
- FIFO-family present modes (vsync-locked, no tearing). `VK_EXT_present_mode_fifo_latest_ready` is part of the capability tiers considered below.

Future work (design for it, do not implement or plan in detail now):

- Android Vulkan backend.
- CPU-sleep-before-present fallback for platforms without a present-timing request API (see FR3).
- Handling display changes and display mode changes (monitor switch, refresh rate change). Detecting and logging such events is desirable but not required.
- The profiling UI / analysis tools that consume frame records (the data they need is in scope, see Data requirements).

Out of scope:

- Variable refresh rate displays and G-Sync.
- MAILBOX / IMMEDIATE present modes.

## Functional requirements

The frame pacer shall:

**FR1 — Frame duration decision.** Decide how many display refresh periods (vsyncs) each rendered frame shall be visible.

- This decision is made solely by the frame pacing algorithm. The application shall not choose the frame rate directly; it may only set a *minimum* vsyncs-per-frame (e.g. a higher value for a paused screen).
- The decision policy shall adapt to varying application load, on the CPU, on the GPU, or both.
- Downshifting (increasing vsyncs per frame, reducing frame rate) shall happen quickly when load demands it.
- Upshifting (decreasing vsyncs per frame, restoring frame rate) shall happen within a reasonable time once load again allows it, so that available refresh rate is not underutilized for an unreasonable duration. The time headroom must be sustained before upshifting is an adjustable parameter (see Tunable parameters).
- Steady state means each frame is visible for the same number of refresh periods. Alternating cadences (e.g. 1-2-1-2 judder) and oscillation between vsyncs-per-frame values count as failure; the policy shall include hysteresis or an equivalent mechanism to prevent them.

**FR2 — Frame scheduling.** Decide which future display refresh slot each rendered image shall target, and release the CPU to the application to start per-frame work such that:

- a. The chain of CPU work → GPU work that produces the image finishes in time for the presentation engine to present it at the chosen slot. This is the primary concern.
- b. The CPU starts as late as possible, to minimize input latency. This is a secondary concern, considered only after (a) is satisfied. In practice this means "deadline minus a safety margin"; the safety margin shall be adaptive, with an adjustable initial value (see Tunable parameters), not implicit.

**FR3 — Present timing.** Make a best effort to present each frame at the decided presentation time, by passing the target presentation time to the presentation engine when an API for that is available (see Capability tiers). The fallback of inserting a CPU sleep before present is deferred to future work and shall not be part of the initial design beyond noting where it would attach.

**FR4 — Predicted display time to the application.** The predicted display time and visible duration of the frame (the result of FR1 + FR2) shall be passed to the application before its per-frame work begins, so that animation and logic simulation can target the time the frame will actually be shown, instead of sampling the wall clock.

**FR5 — Queue depth control.** Ensure that as few frames as possible are queued to the display. Without this, the CPU can burst-fill the present queue, which then stays full and adds unwanted latency. FR2b is the mechanism that normally achieves this, but FR5 is a hard constraint that shall be enforced even when FR2's estimates are wrong (e.g. by throttling on `VK_KHR_present_wait`).

The swapchain image count has a driver-set hard minimum and cannot be lowered below it. That minimum can be large enough that keeping all images queued would add unwanted latency. The pacer shall therefore control the number of *queued* images independently of the swapchain image count, and shall treat the swapchain image count as a fixed input, not something it can reduce.

**FR6 — Capability tiers and off mode.** The design shall enumerate the presentation-timing capabilities it uses and specify pacer behavior for each combination actually encountered on the target platforms. Extensions to take into account:

- `VK_GOOGLE_display_timing` (required for the Android path; shall be accounted for in the backend-agnostic interface)
- `VK_EXT_present_timing`
- `VK_KHR_present_wait`
- `VK_EXT_present_mode_fifo_latest_ready`
- `VK_EXT_calibrated_timestamps` (for correlating GPU timestamps with the CPU clock, see Data requirements)

The baseline requires recent extensions, in particular `VK_EXT_present_timing` (with `VK_EXT_present_mode_fifo_latest_ready` also assumed available where useful). This is an accepted constraint of this research project: no measurement-only or `VK_KHR_present_wait`-only middle tier shall be designed. When the baseline requirements are not met, the frame pacer shall operate in a "dummy / placebo / off" mode: the interface stays in place and frame records are still collected where possible, but no pacing decisions are enforced — we simply do not do frame pacing on such devices.

**FR7 — Disturbance recovery.** After a disturbance — a missed present deadline, cold start with no history, swapchain recreation — the priority is to return to steady state; the design shall minimize disruption time. Recovery behavior shall be specified for each of these cases.

## Non-functional requirements

**Control-theoretic foundation.** Making the FR1/FR2 decisions well requires a good understanding of control theory. The first step of planning is to produce a model of the problem in the framework of control theory. The frame pacing algorithm must be verified against that model — stability, convergence, and bounded response to disturbances (load spikes, load drops, missed presents) — before the plan can be approved. Oscillation in steady state counts as failure.

**Acceptance criteria.** The design shall define measurable criteria at least covering:

- Steady state: every frame visible exactly N refresh periods when the workload fits the budget.
- Bounded convergence time after a load change (both directions; see FR1 for the asymmetry between downshift and upshift, and Tunable parameters for the targets).
- No oscillation between vsyncs-per-frame values in steady state.

**Isolation and testability.** The frame pacing algorithm shall be independent of other engine code so that it can be unit and smoke tested. It shall be drivable by a simulated clock and event source, so the control loop can be tested deterministically against synthetic scenarios (steady load, CPU spike, GPU spike, load drop, missed present, cold start) without a GPU or display.

**Clock domain.** All timestamps in the frame records shall share a single monotonic clock domain (QPC on Windows). GPU timestamps shall be calibrated into that domain (`VK_EXT_calibrated_timestamps`).

## Data requirements

The frame pacer shall keep a record of event timestamps for a bounded number of recent frames, primarily to provide data for its own decision making, and secondarily to feed profiling data to UI and analysis tools. Per frame:

- CPU slot begin — when work on the frame in flight starts
- CPU slot end — when work on the frame in flight ends
- Frame pacer wait begin — when the frame pacer starts waiting for the right time to let the app start per-frame work
- Frame pacer wait end — when the frame pacer wait is done and the app starts
- Fence wait begin — when the CPU starts waiting for the frame-in-flight fence
- Fence wait end — when the CPU is done waiting for the frame-in-flight fence
- Acquire next image begin — just before the call to `vkAcquireNextImageKHR()`
- Acquire next image end — right after the call to `vkAcquireNextImageKHR()`
- Command buffer submit — when the `vkQueueSubmit()` call is made
- Display floor begin / end — in some cases the frame pacer might sleep before requesting present, to prevent presenting too early (when the device does not support requesting presentation at a specific time; deferred, see FR3)
- Request to present — when present is requested
- GPU frame begin — first render pass begin
- GPU frame end — last render pass end

Where the platform provides presentation feedback (e.g. actual present time from `VK_GOOGLE_display_timing` / `VK_EXT_present_timing`), that shall be recorded as well.

The measured frame records shall be made available to profiling UI / analysis tools. The tools themselves are out of scope.

## Deliverables of the planning phase

1. A control-theory model of the frame pacing problem.
2. The frame pacing algorithm design, verified against the model (stability, convergence, disturbance response).
3. An inventory of the inputs the algorithm needs (see Data requirements) and where each is sourced from, per capability tier.
4. A behavior specification for the scenarios: steady state, CPU/GPU load spike, load drop, missed present, cold start, swapchain recreation, and off mode.
5. A capability tier matrix: which extensions are used on Windows and (later) Android, and what pacer behavior each tier enables, including the off mode of FR6 and windowed vs. fullscreen differences.

## Tunable parameters

All quantitative bounds below are adjustable parameters. The values given are proposed defaults; the planning phase may revise them with justification, but the parameters themselves shall remain adjustable.

| Parameter | Proposed default | Notes |
|---|---|---|
| Downshift reaction time | ≤ 2 missed-budget frames | Downshift quickly; sustained overload must not cause repeated misses. |
| Upshift confirmation time | 500 ms of sustained headroom | Prevents oscillation; keeps higher refresh rates from being underutilized for long. |
| Convergence time after disturbance | ≤ 1 s back to steady state | Applies to FR7 recovery scenarios as well as load changes. |
| Latency safety margin (initial) | 20 % of refresh period | Adaptive thereafter (FR2b); initial value used at cold start. |
| Minimum vsyncs per frame | 1 | Application-settable (FR1), e.g. raised on a paused screen. |
| Target queued present images | 1 | FR5; independent of swapchain image count. |
| Frame record history length | 120 frames | Bounded history for decisions and profiling (Data requirements). |
