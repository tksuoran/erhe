# Frame Pacer — Capability Tier Matrix

This document is deliverable 5 of the planning phase defined in [frame_pacing.md](frame_pacing.md): which presentation-timing capabilities are used on Windows and (later) Android, what pacer behavior each tier enables — including the off mode of FR6 — and the achievable pacing quality per presentation path (windowed vs. fullscreen). Sourcing details per input are in [frame_pacing_inputs.md](frame_pacing_inputs.md); per-scenario behavior in [frame_pacing_behavior.md](frame_pacing_behavior.md).

## 1. Capability inventory

What each extension contributes to the pacer, independent of platform:

| Capability | Provided by | Pacer function it enables |
|---|---|---|
| Refresh period query | `VK_EXT_present_timing` (swapchain timing properties) / `vkGetRefreshCycleDurationGOOGLE` | Vsync grid period `T` (grid *phase* always comes from feedback) |
| Target present time request | `VK_EXT_present_timing` (target time at a present stage) / `VkPresentTimesInfoGOOGLE.desiredPresentTime` | FR3: present-no-earlier-than `V̂(j_k)`; makes slot targeting authoritative instead of inferred |
| Presentation feedback (achieved time per id) | `VK_EXT_present_timing` past timing (per-stage) / `vkGetPastPresentationTimingGOOGLE` | Grid phase tracker, in-flight-aware anchoring, `δ̂_pe` estimation, queue bookkeeping, FR4 accuracy |
| Present wait (block until id displayed) | `VK_KHR_present_id` + `VK_KHR_present_wait` (or the *2 variants) | FR5 hard queue clamp `q ≤ Q* + F` |
| Latest-ready queue semantics | `VK_KHR/EXT_present_mode_fifo_latest_ready` | Stale queued images dropped at vsync: shortens miss cascades (§3 of the behavior spec) without tearing |
| GPU↔CPU clock calibration | `VK_KHR/EXT_calibrated_timestamps` | `gpu_end` in the reference clock → `L_k`, `s_k`, the entire inner loop |

## 2. Tier definitions and resolution

Per FR6 the design is deliberately binary — **no measurement-only or present-wait-only middle tier**. The tier is resolved once per device at probe time and confirmed per swapchain (a capability that fails at swapchain granularity demotes that swapchain to OFF, with a log line).

| Tier | Platform | Requires (all of) | Uses opportunistically |
|---|---|---|---|
| **W** — full pacing | Windows (first implementation) | `VK_EXT_present_timing` · `VK_KHR_present_id` + `present_wait` (or *2) · `VK_KHR/EXT_calibrated_timestamps` · FIFO-family present mode | `fifo_latest_ready` (preferred present mode; erhe's selector already ranks it highest) |
| **A** — full pacing, Android path (future work) | Android | `VK_GOOGLE_display_timing` · calibrated timestamps (`CLOCK_MONOTONIC` domain) | `present_id`/`present_wait` where the driver ships them |
| **S** — slop-servo fallback (planned, Phase 4) | any | plain `VK_PRESENT_MODE_FIFO_KHR` + vsync (backpressure must exist) | GPU timestamps for records if present |
| **OFF** — dummy / placebo | any | — (anything less than the above) | GPU timestamps for records if present |

Open item carried into the Android phase (explicitly future work, not designed now): if `present_wait` is absent there, the FR5 clamp needs a GOOGLE-feedback-based gate; until that is designed, absence of `present_wait` on Android means OFF.

**Planned tier S — slop-servo fallback (implementation plan Phase 4).** A middle tier is now planned between W/A and OFF, based on the Games-by-Mason pacer method (reviewed 2026-07-22 from `FramePacer.zig`): no presentation-timing extensions at all — sense *backpressure* instead of display times. Measure per-frame "slop" (time the CPU spent blocked on the GPU/swapchain; erhe's records already isolate this as the involuntary-wait subtraction behind `cpu_service_time`), then servo a **sleep before input polling**: `sleep += (slop − headroom) × gain` per frame, multiplicative back-off on frame-time overshoot, clamped to `[0, period − headroom]`. Equilibrium: residual blocking ≈ a static headroom — the servo self-tunes toward the vblank deadline without ever knowing where it is. Properties: portable to any FIFO swapchain, no cadence control (always targets one frame per vblank), mean-based (no quantiles — heavy-tailed load oscillates or parks conservatively), delta-time by EWMA of loop cadence rather than FR4 prediction. **Hard requirement: plain `VK_PRESENT_MODE_FIFO_KHR`** — on `fifo_latest_ready` superseded images drop instead of exerting backpressure, slop reads ~0, and the servo winds to nothing. This is why the pacer method must own the present-mode and swapchain-image-count decision (Phase 4 wiring): tier W wants `fifo_latest_ready` + present timing; tier S wants plain FIFO + minimum image count. Supporting external data point from the same source: `VK_AMD_anti_lag` / `VK_NV_low_latency2` reportedly act only when their FPS limiter caps *below* the refresh rate — consistent with our issue-#1 finding that the driver's latency machinery is cap-driven, not target-driven.

**VRR (variable refresh rate) is an explicit non-goal of tiers W/A.** The entire control design assumes a periodic vsync grid (`V(j) = phase + j·period`); the C16 gross-error re-seed tracks a *stable* rate change but the grid concept itself does not model per-frame variable refresh. The slop-servo method survives VRR precisely because it never assumes a grid — another argument for tier S as the fallback on VRR displays, until a VRR-aware tier is designed.

## 3. Behavior enabled per tier

| Function | Tier W | Tier A (planned) | OFF |
|---|---|---|---|
| FR1 cadence control | Full | Full | None (`N` fixed = `N_min`, no decisions) |
| FR2 release scheduling | Full | Full | Release = now |
| FR3 target present time | Requested at chosen present stage | `desiredPresentTime` ("no earlier than") | Not requested |
| FR4 prediction to app | Exact in steady state | Exact in steady state | Best-effort wall clock, flagged unpaced |
| FR5 queue clamp | `vkWaitForPresentKHR` hard invariant | `present_wait` if shipped, else tier is OFF (see §2) | Not enforced |
| FR7 recovery behaviors | Full | Full | n/a |
| Frame records | Full incl. presentation feedback | Full incl. feedback (+`presentMargin`) | CPU events always; GPU if timestamps exist; no feedback |
| Miss cascades after a spike | Shortened when `fifo_latest_ready` active (stale images dropped at vsync) | Same where available | n/a |

## 4. Presentation path: windowed vs. fullscreen (Tier W)

Required by the scope statement: achievable pacing quality per path. The control laws are identical in both paths (model §9); what differs is the effective deadline offset `δ_pe`, feedback semantics, and therefore quality:

| Aspect | Fullscreen / independent flip | Windowed, DWM-composited |
|---|---|---|
| `δ_pe` | Small, stable (flip-queue deadline) | Larger and more variable: image must meet the *compositor's* deadline, one composition period ahead |
| Presentation feedback meaning | Scanout (first pixel out/visible stages) | May reflect composition, not scanout; per-stage timing (latch vs. visible) disambiguates where the driver reports it |
| Display latency | Minimum achievable | +1 composition period typically |
| Cadence control (FR1) | Exact | Exact — grid and cadence logic are unaffected by composition |
| Latency minimization (FR2b) | Full | Degraded by `δ̂_pe` inflation: releases earlier by the composition allowance; the adaptive margin absorbs the extra variability |
| FR4 prediction accuracy | = scanout | May be offset by up to one composition period; still exact in *period* (duration prediction unaffected) |
| Slot-miss consequence | Next vsync | Next composition — same cadence math |

Notes:

- Modern Windows can grant independent flip to windowed swapchains (MPO / flip-model promotion). The pacer does not branch on this — the promotion appears as a level shift in `δ̂_pe` residuals and the estimator follows it. A demotion (e.g. an overlay appears) looks like a small step disturbance in timing-only misses and is absorbed per behavior-spec §6b.
- Path changes (fullscreen toggle, monitor move) are display-change events: out of the initial scope, detect-and-log desirable (requirements, Future work). The swapchain recreation that usually accompanies them already triggers the §8 recovery behavior.
- Quality summary: **cadence stability and judder-freedom are path-independent; the paths differ only in absolute latency and in FR4's absolute offset.** All C1–C8 claims apply in both paths; windowed adds a constant-ish latency penalty of about one composition period plus a larger steady-state margin.

## 5. Tier W capability degradations worth logging

Not tiers — diagnostics the implementation shall log when observed, all leaving the pacer in a defined state:

| Observation | Interpretation | Pacer state |
|---|---|---|
| Present-timing feedback stops arriving | Driver/compositor stopped reporting | Grid phase and anchor freeze on last estimates; after a timeout, demote swapchain to OFF |
| Calibration `maxDeviation` grows | GPU/CPU clock drift | Widen `guard` contribution; recalibrate more often |
| Persistent §6b pattern (timing-only misses with load headroom) | `δ̂_pe` or grid wrong for this path | Margin absorbs; log for analysis |
| `vkWaitForPresentKHR` returns device-lost / timeout | Swapchain dying | Propagate to engine recreation path |
| Tier resolves OFF although the driver is known to support present timing | A capture layer is filtering the device-extension list. Verified on the development machine: erhe's in-app RenderDoc capture support (`renderdoc_capture_support` in `config/editor/erhe_graphics.json`) hides `VK_EXT_present_timing` (and video/NV extensions) because RenderDoc whitelists extensions it can capture | Expected: frame pacing Tier W and RenderDoc capture are mutually exclusive per session. Disable `renderdoc_capture_support` to pace; the startup tier log makes the state visible |
| Target present time requests accepted but not honored | **Measured on the development machine (P2.3, NVIDIA 596.83, RTX 3080)**: the surface reports `presentAtAbsoluteTimeSupported = FALSE`, `presentAtRelativeTimeSupported = TRUE` — verified per fullscreen-exclusive mode (the caps query chains `VkSurfaceFullScreenExclusiveInfoEXT`; FALSE in both default and application-controlled exclusive); relative-time targets validate cleanly but the engine still displays each image at its earliest feasible vsync, across **every advertised combination**: with and without `PRESENT_AT_NEAREST_REFRESH_CYCLE`, with the target stage expressed as `IMAGE_FIRST_PIXEL_OUT` and as `QUEUE_OPERATIONS_END` (the only two stages in the surface's query mask), in the swapchain's only time domain (`PRESENT_STAGE_LOCAL` — the enumeration is logged at swapchain creation), and in plain `VK_PRESENT_MODE_FIFO_KHR` as well as `FIFO_LATEST_READY` (present-mode interaction ruled out). Full shareable issue description: [frame_pacing_present_timing_driver_report.md](frame_pacing_present_timing_driver_report.md). A discriminating probe rules out mere rounding ambiguity: with the target biased to MID-cycle (slot + T/2) under `NEAREST_REFRESH_CYCLE`, the one-cycle-early slot sits 1.5 cycles from the target and could never be a "nearest" pick — yet the early-display fraction was unchanged (~60 %), so the target value is inert, not merely rounded — frames whose chain finishes more than one refresh period before their target display a slot early (~20 % windowed, ~50 % exclusive fullscreen, identical in both flag modes). FR3 is therefore a no-op on this driver | Cadence regularity rests on release gating alone: budget slack (margin + quantile-vs-typical latency gap) occasionally lets a frame catch the previous slot. The software holdback of the present *request* into the target slot's cycle is **implemented and verified sim-first (claim C15)**: `vkQueuePresentKHR` is delayed until `target − T + 1 ms` (after GPU submission, so only the present is delayed), stamped as an involuntary wait; config kill switch `frame_pacing_present_holdback` (default true). On a driver that DOES honor targets, the recommended formulation is: strict mode with a small early bias (`target − T/4`, absorbs clock skew across the boundary without reaching the previous cycle), or `NEAREST_REFRESH_CYCLE` with a `+T/4` late bias (makes the intended cycle the unambiguous nearest pick); either lets the engine absorb early readiness and restores the exact scenario-1 cadence signature without trimming release-gating slack — untestable on this machine, verify at P3.2 on capable hardware |
| Queried `refreshDuration` grossly wrong after an app-driven display mode change | **Measured on the development machine (NVIDIA 596.83, RTX 3080)**: after an application-driven (SDL) exclusive-fullscreen mode set to 3840×2160 @ 23.976 Hz from a 120 Hz desktop, `vkGetSwapchainTimingPropertiesEXT` reports `refreshDuration` = 43.478 ms — exactly 1/23 s, as if truncated to integer 23 Hz — while the extension's own achieved-present feedback lies on a 41.72 ms grid (4.2 % error). When the same mode is pre-set through Windows display settings, the query reports the correct 41.708 ms. Driver report issue #2: [frame_pacing_present_timing_driver_report.md](frame_pacing_present_timing_driver_report.md) | `refreshDuration` is a **hint only**. The pacer's grid-period PLL clamp (±0.5 % of nominal) cannot absorb a 4 % seed error, so the pacer re-seeds period and phase from feedback deltas when their median disagrees grossly with the tracked period (claim C16, algorithm deviation 12); cadence budgets follow the tracked period, and the C15 present-request holdback receives the tracked period instead of the queried one |
| Present-timing queue fills on plain `VK_PRESENT_MODE_FIFO_KHR` (`VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT` within ~32 presents of startup) | **Measured on the development machine (NVIDIA 596.83, RTX 3080, P4.2)**: with the swapchain in plain FIFO, chained timing requests enqueue but their entries do not complete/drain the way they do on `FIFO_LATEST_READY` — the same `poll_present_timing` cadence that keeps the queue empty on latest_ready overflows the 32-entry queue on plain FIFO. Driver finding #3 candidate for the driver report | The error is recoverable (marks the swapchain invalid; recreation resets the timing queue) instead of fatal. Tier S does not enable present timing at all — not needing the extension is the method's contract, and this driver behavior makes the combination unusable anyway. The refresh period falls back to the window's display mode |
| Process dies inside `vkQueuePresentKHR` when `VkPresentTimingsInfoEXT` is chained | **Application bug, resolved**: the swapchain must be created with `VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT` (`VUID-vkSetSwapchainPresentTimingQueueSizeEXT-swapchain-12229`); presenting timing requests against a swapchain without the flag crashes silently inside the driver. Initially misdiagnosed as a driver bug after blind bisecting - one validation-layer run identified it immediately | Lesson (now an AGENTS.md rule): keep erhe free of Vulkan validation errors at all times, and run validation when touching Vulkan code. With the flag set, requests + feedback are verified working on the development machine (feedback `complete=true`, achieved-present ~2 ms after GPU end at 120 Hz) |

## 6. Probe integration

Tier resolution slots into the existing erhe probe pattern (`check_device_extension` table at `vulkan_device.cpp:911-945`, feature-chain enable at `vulkan_device_init.cpp:747/1168`, currently covering `fifo_latest_ready` only — the remaining probes are gap item 1 of [frame_pacing_inputs.md](frame_pacing_inputs.md) §5). The resolved tier is a `Device` capability exposed to the pacer at construction; per-swapchain confirmation happens at swapchain creation where present timing is enabled on the swapchain.

## 7. Method quality benchmark: tier W vs tier S (P4.3, measured)

Development machine (NVIDIA 596.83, RTX 3080, 120 Hz exclusive fullscreen, T = 8.33 ms), empty scene (W ≈ 5.2 ms) plus the simulated-workload knob, 1000-frame MCP captures per cell. Tier S has **no presentation feedback by design**, so its display-side numbers use the present-return cadence as a proxy (with 2 images and binding backpressure, `vkQueuePresentKHR` returns on queue-slot boundaries); its latency numbers are the measured *conversion of post-input blocking into pre-input sleep* (model claim C19: production timing does not move — the input poll does).

| Load (added CPU) | Tier W (full pacer) | Tier S (slop servo) |
|---|---|---|
| none (empty scene) | N=1, **100 %** exact 1-slot gaps, 0 early, release→display median **8.13 ms** p95 9.01; slop 0.36 ms | post-input blocking **5.84 → 1.79 ms** median (sleep 2.61 ms = the input-poll latency trim); loop keeps T but p95 14.4 ms; present-return cadence 60 % exact (back-off transients) |
| U(0, 5 ms) | N=1, **99.9 %** exact, 0 early, latency median 12.07 ms (q95 budget prices the tail in) | sleep parks near 0 (median 0, p95 1.6 ms): tails + work ≈ T fire the overshoot back-off; benefit mostly gone; loop bounded, p95 16.1 ms |
| U(0, 10 ms) | pressure downshift to **N=2**, 94.5 % exact 2-slot gaps, 0 early, latency median 17.0 ms — every frame visible ~2T, judder-free | no cadence concept: sleep ≈ 0, loop bounded (97 % under 2T, max 23 ms, no death spiral), display cadence uncontrolled (proxy 78 % 1-slot with 0/2/3 outliers = visible irregularity) |

Where each method stands (matches the sim claims C19–C21):

- **Latency**: tier S's entire value is the blocking→sleep conversion, worth ~`T − work − headroom` per frame — real (~2.6–4 ms here) but only under light, low-variance load. Tier W controls latency through the release gate at *every* load, with the budget (not the queue) setting the floor.
- **Exactness / judder**: tier W holds exact cadence at every operating point (0 early displays throughout — the C15 holdback carrying its weight). Tier S has no display-time sensing, so cadence is whatever the FIFO queue produces; under variance it visibly wanders.
- **Convergence**: the servo converges in ~100 frames on steady load and re-converges after spikes (C20), but its mean-seeking + multiplicative back-off design means *any* tail activity near the period resets it — under U(0,5) it effectively never engages ("parks conservatively", the C21b live confirmation).
- **Heavy tail**: tier W prices tails via the q95 budget and downshifts cadence when the budget demands it; tier S stays *safe* (bounded, no runaway) but stops pacing.

**Recommendation**: tier W wherever the four capabilities exist (it is strictly better at every measured point); tier S as the portable fallback — a real improvement on light steady loads, harmless elsewhere, and the only method that survives VRR (no grid assumption). OFF only when forced.

---

With this document, deliverables 1–5 of the planning phase are complete:

1. [frame_pacing_control_model.md](frame_pacing_control_model.md) — control-theory model
2. [frame_pacing_algorithm.md](frame_pacing_algorithm.md) — algorithm design, verified (C1–C8, `scripts/frame_pacing_sim.py`)
3. [frame_pacing_inputs.md](frame_pacing_inputs.md) — input inventory and sourcing
4. [frame_pacing_behavior.md](frame_pacing_behavior.md) — behavior specification per scenario
5. this document — capability tier matrix
