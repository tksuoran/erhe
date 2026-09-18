# Frame Pacer - Implementation Map

Stability: stable

How the frame pacer is layered across erhe, and what each layer owns. Source
comments cite the step labels below (`P0.2`, `P2.1`, `P4.1`, ...), so the
labels and their meanings are stable; they name pieces of the integration,
not a schedule. The design itself is [requirements.md](requirements.md),
[control_model.md](control_model.md), [algorithm.md](algorithm.md),
[inputs.md](inputs.md), [behavior.md](behavior.md) and
[capability_tiers.md](capability_tiers.md).

Standing principles of this layering:

- **The pacer core is pure.** No OS, Vulkan or clock access: all times arrive
  as arguments, all decisions are returned as values. That is what makes it
  testable against a virtual clock and portable across backends.
- **Observer before actuation.** The pacer computes and logs a full schedule
  decision whether or not the host enforces any part of it, so input fidelity
  can be validated against reality independently of the actuators.
- **Parity is the gate.** The C++ pacer reproduces the Python reference
  model's verdicts; any algorithm change keeps both suites green.
- **Instrumentation behind a facade.** Record sites go through
  `erhe::frame_pacing::Frame_time_recorder` so the frame loop can be
  refactored without touching the pacer.

## Layers

### P0 - erhe preparation (instrumentation and probes, no pacing)

| Step | Owns |
|---|---|
| **P0.1 Capability probes + tier resolution** | Probes and feature enables for `VK_KHR_present_id` + `present_wait` (and the *2 variants), `VK_EXT_present_timing`, `VK_KHR/EXT_calibrated_timestamps`, alongside the `fifo_latest_ready` probe; resolution of the tier as a `Device` capability, logged at startup |
| **P0.2 Frame-record ring + CPU stamps** | The 120-frame record ring in the reference clock domain, the CPU-side stamps (CPU slot, fence waits, acquire, submit, present request) and the normative `c_k` service-time subtraction |
| **P0.3 Frame-spanning GPU bracket** | One dedicated frame timer (begin in the frame's first submitted command buffer, end in the last one before present), non-blocking `WITH_AVAILABILITY` polling at the schedule point, and calibration of GPU ticks into the reference clock |
| **P0.4 Swapchain timing, present ids, feedback** | Present timing enabled on the swapchain (`VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT`), `VkPresentIdKHR` chaining with the id-to-frame correlation, per-frame past-presentation polling, refresh-period re-query on recreation |
| **P0.5 Timer jitter measurement** | `erhe_frame_pacing_timer_probe`, the standalone measurement that sets the `guard` default (measured waitable-timer wake error p99 0.59 ms; `std::this_thread::sleep_for` p99 15.3 ms, which is why it is not used) |

P0 has standalone value: it is the profiling data path the requirements ask
for, independent of pacing.

### P1 - pacer core (no erhe dependencies)

| Step | Owns |
|---|---|
| **P1.1 `Frame_pacer`** | The pure C++ port of `scripts/frame_pacing_sim.py` behind the interface of [algorithm.md](algorithm.md) section 1 |
| **P1.2 Parity suite** | `erhe_frame_pacing_tests`: the simulation plant (pipelined stages, `F` frames in flight, FIFO or `fifo_latest_ready` presentation on a true vsync grid the pacer does not know, causal event delivery, plain-FIFO acquire backpressure) and the claim scenarios |

### P2 - integration

| Step | Owns |
|---|---|
| **P2.1 Observer** | `Frame_pacing_observer`, wired at the editor tick and fed from the frame records; it enforces nothing itself and emits decisions and periodic summaries to the `editor.frame_pacing` log category. The host enforces the parts of the decision the actuation steps below cover |
| **P2.2 Queue clamp (FR5)** | `Device::wait_for_displayed_frame` down to `vkWaitForPresentKHR` on `Schedule_decision::wait_id`; a bounded timeout is a logged no-op, device or surface loss marks the swapchain invalid |
| **P2.3 Release gating + target present time (FR2/FR3)** | The pacer wait on `erhe::time::Waitable_timer` and the target present time chained into `vkQueuePresentKHR`, plus the C15 present-request holdback. Kill switches: `frame_pacing_enforce`, `frame_pacing_present_holdback` |
| **P2.4 FR4 routing** | The simulation clock advances by the delta between successive `predicted_display` values instead of sampling the wall clock (`Time::prepare_update`'s `simulation_advance_ns`) |

### UI - verification window

`Frame_pacing_window`, collected at the tick right after the observer: the
simulated CPU workload knob, the cadence / frame-delta / input-latency
graphs, the CPU / GPU / display timelines and the shared pan-zoom axis.
Requirements and the `U` labels: [user_interface.md](user_interface.md).

### P3 - validation on real hardware

Reproducing the [behavior.md](behavior.md) scenarios with induced load
(P3.1), comparing the windowed and fullscreen presentation paths (P3.2), and
reviewing the tunables against what the hardware shows (P3.3). The part of
P3.2 that needs a driver honoring target present times is open; see Future
work.

### P4 - tier S

| Step | Owns |
|---|---|
| **P4.1 `Slop_servo_pacer`** | The pure slop-servo pacer and its reference model `SlopServo` in the simulation, with the plain-FIFO acquire-backpressure plant |
| **P4.2 Tier owns the swapchain configuration** | The resolved tier, not an independent present-mode ranking, decides present mode and image count: tier W selects `fifo_latest_ready` with present timing, tier S plain FIFO with the minimum image count |
| **P4.3 Method benchmark** | The A/B methodology and metrics behind the tier W vs tier S table in [capability_tiers.md](capability_tiers.md) section 7 |

## Risks this layering answers

| Risk | How the layering answers it |
|---|---|
| A driver lacks `VK_EXT_present_timing`, so tier W is unreachable | Tier resolution (P0.1) is independent of everything else: records and observer mode work at any tier, and tier S paces without the extension |
| Frame-loop refactoring moves the instrumentation sites | The recording facade (P0.2); the pacer references no engine internals |
| Real input fidelity differs from the simulation (service times, delivery latency) | Observer mode (P2.1) validates the inputs before any actuator is enabled; claim C9 bounds the delayed-delivery case |
| Windowed / DWM feedback semantics surprise | The validation signals of [inputs.md](inputs.md) section 3.6 cross-check feedback, and [capability_tiers.md](capability_tiers.md) section 5 defines the degraded states |
| Timer wake-up jitter larger than assumed | P0.5 measures it, and `guard` is a tunable |

## Future work

- [../plans/frame_pacing.md](../plans/frame_pacing.md) - tier A, VRR, display-change detection, asynchronous present, FR3 verification on capable hardware, UI capture export.
