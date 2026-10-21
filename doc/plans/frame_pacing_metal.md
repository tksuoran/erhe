# Frame pacing on Metal: tier M to a reliable default

Status: in progress

Make frame pacing tier M (`doc/erhe/metal_backend.md` "Frame pacing") good
enough to be the `auto` tier on Metal, by iterating between the pacer
simulation and live measurements until both agree and the live gate passes.
The pacer algorithm is `doc/frame_pacing/algorithm.md`; its executable models
are `scripts/frame_pacing_sim.py` and `erhe_frame_pacing_tests`
(`src/erhe/frame_pacing/test/`), which must both stay green.
`doc/plans/metal_backend.md` item 5 holds the Metal API probe this builds on.

## Goal and gate

The live gate (`scripts/frame_pacing_live_gate.py`, below) on the windowed
Metal editor, default scene, external 120 Hz display, window visible:

- Tier M at N=1 in 10 of 10 fresh launches, in each of the default
  threading, `ERHE_RENDER_THREAD=0` and `ERHE_FRAME_PIPELINING=0`, in Debug
  and in Release.
- At most 1 % of the sampled frames displayed for two or more refreshes.
- Release-to-display median below tier S in the same build (tier S measures
  29-30 ms in Debug; tier M at N=1 measured 19.4-20.2 ms).
- Both pacer suites green, including a claim whose plant reproduces each
  live failure found on the way.

When the gate passes: `auto` resolves to tier M in
`Device_impl::init_frame_pacing` (`metal_device.cpp`, `frame_pacing_tier`
"w" selects it until then), and `doc/erhe/metal_backend.md` states the
result.

## Metal presentation, as the pacer sees it

From the macOS 27 SDK headers (`QuartzCore/CAMetalLayer.h`,
`CAMetalDisplayLink.h`, `Metal/MTLDrawable.h`, `MTLCommandBuffer.h`):

- Default pacing comes for free: `CAMetalLayer.displaySyncEnabled` (default
  YES) synchronizes presents to the display's vsync, and the swap queue of
  `maximumDrawableCount` (2 or 3) drawables makes `nextDrawable` block until
  one is free - the backpressure tier OFF and tier S live on (tier OFF
  measured 6.9 ms per frame inside `nextDrawable`).
  `allowsNextDrawableTimeout` (default YES) bounds that wait to 1 s.
- Application pacing: `presentDrawable:atTime:` / `presentAtTime:` (absolute
  host time; measured honored windowed), `presentDrawable:afterMinimumDuration:`
  (the previous frame stays on screen at least that long - a cadence of N
  refreshes without a grid estimate; untested), `addPresentedHandler` with
  `presentedTime` (feedback; 0 when not displayed). An application that
  releases its frames late enough never blocks in `nextDrawable` (tier M
  measures 0.01 ms there).
- OS pacing: `CAMetalDisplayLink` (macOS 14) calls its delegate with the
  drawable, `targetTimestamp` (when the frame's work must be submitted) and
  `targetPresentationTimestamp`; `preferredFrameLatency` and
  `preferredFrameRateRange` set latency and cadence. The application gives
  up its own loop timing - it inverts erhe's M1 -> M2 -> R control flow - but
  the two timestamps state the system's own presentation deadline, a
  reference for `delta_pe` and for the latency the pacer should match.
- Command buffer completion is at GPU end; `GPUStartTime` / `GPUEndTime` are
  host time. A drawable must be complete about one refresh period plus
  1.5 ms before its vsync windowed (claim C22).

## What is known (2026-10-20, Debug, M4 Pro)

- Tier M holds N=1 when it starts there: 348/348 one-refresh frames,
  release-to-display 19.4-20.2 ms, deadline offset estimate ~9.1-9.3 ms.
- A startup downshift (scene load) to N=2 can lock it there. Live outcome
  varied between batches: 4/4 runs returned to N=1 in one batch, 0/5 in
  another, 1/2 in a control with the pre-fix anchor - conditions outside the
  editor (other processes, GPU state) move it.
- The GPU cost is not a fixed input: the presenting command buffer's span
  depends on the cadence and on where in the refresh period its GPU work
  starts (phase 0 = a display time, gate script "span by start phase"):

  | case | start phase | median span | spans > T |
  |---|---|---|---|
  | tier M, N=1 | 0.75-0.875 | 5.5-6.2 ms | 0 % |
  | tier S, N=1 | 0.25-0.5 | 5.4 ms | 0 % |
  | tier M, N=2 locked | 0.125-0.375 | ~9.5 ms in the worst bins | 34-68 % of those bins |
  | tier M, N=2 locked | 0.875 | | 0-12 % |
  | tier M, N=2 pinned, after 15 s | 0.375-0.625 | 4.9 ms | 0 % |

  The per-pass GPU timers grow with the span (real GPU time, not a wait
  outside the passes). A Metal System Trace shows WindowServer GPU work
  overlapping 2-3.5 ms of each editor command buffer, but Instruments
  induces a fixed GPU performance state while recording, which hides any
  frequency effect - use untraced frame records for the cost model, the
  trace only for overlap structure. Leading hypothesis: at N=2 the GPU idles
  half the time and clocks down, and work starting shortly after a display
  time also contends with the compositor; neither happens at N=1.
- The simulator's plant takes stage durations as fixed inputs; C23 stands in
  with a fixed 12 % tail at 7.9 ms, which the band allowance handles, while
  the live tail reaches 10-14 ms (beyond the period), which it cannot.
- Pacer state at the start of this plan: the deadline offset estimator
  (C22), the in-flight anchor fix (deviation 16), the hysteresis-band upshift
  allowance (deviation 15, C23); Python model 81/81, C++ 28/28.

## Compositor properties per display mode (measured 2026-10-20)

Tier M depends on two properties of the macOS compositor that differ by
display mode, measured with the default scene (request bias `b`: the
request is `presentDrawable:atTime:(V - b*T)` for a target vsync `V`;
deadline: the completion margin `V - GPUEndTime` a frame needs to display
at `V`):

| display mode | request window (on target) | early | late | GPU deadline | tier S release-to-display |
|---|---|---|---|---|---|
| Dell U2725QE, fixed 120 Hz | `b` = 0.25 (only value run) | - | - | ~T + 1.5 ms (C22) | 29-30 ms |
| built-in, fixed 60 Hz | `b` = 0.25, 0.75 | `b` = 1.25: 45 % one refresh early | - | ~T + 0.3-1 ms (estimator 16-17.6 ms) | - |
| built-in, ProMotion (120 Hz) | `b` = 1.25, 1.5 | - | `b` = 0.25, 0.75: 100 %; `b` = 1.0: 20 % | 2T + 0.5-1.5 ms (margins >= 17-18 ms on time, <= 16 ms late) | 37.6 ms |

ProMotion behaves like the fixed-rate compositor with one more period of
pipeline: both the request cutoff and the GPU deadline lie a period
earlier. With the hard-coded `b` = 0.25 every ProMotion frame displays a
refresh late, the estimator saturates at its 2T ceiling, and the pacer
locks at N=2 with ~80 ms release-to-display (5/5 launches); with `b` =
1.25-1.5 and the 2T ceiling, N=1 still slips 20-27 % (the deadline lies
beyond the ceiling).

Neither property can be fixed in advance, so the device measures them:

### Present calibration at device init

Built: `doc/erhe/metal_backend.md` "Frame pacing" describes it (grid,
request window, deadline; probes as a steady stream one every second
vsync) with the measured results per display mode. Tier M requests at the
calibrated offset, and the pacer starts `delta_pe` at the calibrated
deadline (floor) with the ceiling half a period above. The raw probes are
in MCP `get_frame_pacing_status` `present_calibration`: the simulator
plant's request cutoff and deadline per display mode come from those
reports (collect the Dell at 120 Hz when it is connected). Open: a display
change after init keeps the calibrated request offset (the pacer side
drops the calibrated deadline when the period changes); re-calibrating on
the live layer is not built.

## Iteration

Each iteration runs these steps in order and appends one entry to the log
below (what changed, sim results, gate results, the next hypothesis):

1. Measure. Run the gate for the failing configuration with `--out` and
   reduce the records to a plant property: GPU cost against cadence and
   start phase, the deadline (C22), whatever the failure exposes. Isolate
   one variable per experiment: pin the cadence with `--pin-min-vsyncs`,
   compare tiers with `--graphics frame_pacing_tier=...`, compare threading
   modes with `--env`. Never trust a single launch - outcomes vary between
   batches; 5 runs minimum per configuration.
2. Model. Extend the plant of both suites with that property, calibrated
   from step 1 (first iteration: GPU duration as a function of cadence and
   start phase - the start phase is known to the plant from the true grid),
   and add or revise a claim that reproduces the live failure with the
   current pacer. The claim must fail before the pacer change.
3. Pacer. Change the pacer against that claim, root cause first. Candidates
   in order: (a) upshift probing with backoff - at N > N_min, after a quiet
   interval and with the median stage time within the next period, try N-1;
   a probe reverted by pressure doubles the interval (the C10 backoff);
   (b) cost samples per cadence - judge an upshift by the stage times last
   measured at N-1; (c) a schedule phase that keeps the GPU work out of the
   costly window, if step 1 shows the phase, not the cadence, is the cause;
   (d) `presentDrawable:afterMinimumDuration:` for N>1. Every change keeps
   C1-C23 green in both suites; a claim that has to change states why in
   `doc/frame_pacing/algorithm.md`.
4. Gate. Build, run the gate in all configurations; append the result. A
   failure goes back to step 1 with the new evidence.

Before the first iteration (optional, one experiment): a measurement-only
`CAMetalDisplayLink` probe that logs `targetPresentationTimestamp -
targetTimestamp` and the achieved cadence for the default scene - the
system's deadline and the latency reference.

### Next: GPU pre-warm before an upshift (user decision 2026-10-21)

Evidence (Dell, CPU and GPU clocks normalized; gate N=1 in 47/60: Debug
26/30, Release 21/30): every failing run upshifted at about frame 73 and
reverted. Over the first 6-7 frames at N-1 the GPU clock ramps from
0.45-0.53 to ~0.8 of its maximum with spans of 9-11 ms, frames display 1-2
refreshes late and the reactive path downshifts; or the ramp frames stay
in the cadence window and the predictive test fires once it holds 30
samples, though every current frame fits.

After an upshift the Apple GPU needs about 5 frames to ramp its clock
(Debug trace: 0.56, 0.62, 0.69, 0.78, 0.89 of the maximum; spans 8.7 to
5.4 ms), so the first frames at N-1 can slip - visible judder. Absorbing
those slips in the miss attribution was rejected (the user sees them);
pre-warming is user-invisible:

1. Pacer: when the upshift dwell completes, keep N for a pre-warm phase
   and ask the host for extra GPU work per frame, sized so the GPU's busy
   share matches the one expected at N-1; switch to N-1 once the measured
   GPU clock reaches the low decile recorded at N-1, or after a frame cap
   (about 8 frames). A downshift or pressure cancels the phase.
2. Host (Metal): an ALU-only compute kernel (register-resident FMA loop,
   one small write, minimal memory traffic) dispatched after the frame's
   work, its duration calibrated against GPU timestamps.
3. Plant and claim C26: without pre-warm the first N-1 frames slip, with
   it none do and the cadence holds.

Probe (Release, Dell, pinned N=2, an ALU-only kernel - FMA loop over
65536 threads - in its own command buffer after the presenting one):
without ordering the kernel overlapped the frame's work (clock 0.98, but
the frame span stayed 7.3 ms); waiting on an `MTLEvent` the presenting
command buffer signals at its end, the kernel runs in the idle gap: none
-> clock 0.51, span 9.9 ms; 2.3 ms -> 0.97, 4.9 ms; 4.8 ms -> 0.99, 5.2 ms.
The governor is not a plain utilization controller (2.3 ms of 16.7 raised
the clock although the busy share fell from 59 % to 43 %), so the plant
models it phenomenologically: low clock at the longer cadence unless a
pre-warm of at least the calibrated fill runs, ramping with the measured
time constant.

Built (2026-10-21, not yet run live): `Gpu_prewarm` (metal_gpu_prewarm.cpp;
kernel ordered after the presenting command buffer by an MTLEvent, duration
requested in seconds, iteration count from the measured rate),
`Device::set_present_prewarm`, the pacer's pre-warm phase
(`Schedule_decision::prewarm_seconds`) and claim C26 in both suites
(Python 97/97, C++ 31/31). The init calibration measures only the kernel
rate: an init sweep of the fill failed - with clear-only frames the
governor stayed at its lowest state (0.21, 338 MHz) even under 8 ms of
ALU work per frame (the app not yet active at init is not the cause:
gate-launched editors are never active, and the runtime probe worked) -
so the fill is learned online (user decision): it starts at 2 ms (editor
default when the kernel exists); a pre-warm ending with the GPU clock
below 0.9 doubles it (bounded by half the cadence period) without a
cadence switch, one that reached the clock but does not fit backs off.

Gated live 2026-10-21 (Log): the Dell gate passes, every upshift
pre-warmed, and the upshift transition is clean in Debug and Release
(C28). Next: the gate on fixed 60 Hz and on ProMotion with this build
(the calibration's majority vote and probe spacing have not run there
yet).

## Tools

- `scripts/frame_pacing_live_gate.py` - fresh launches, settle, sample,
  table; config overrides restored afterwards. Example:
  `python3 scripts/frame_pacing_live_gate.py --editor build_xcode_metal/bin/Debug/editor --runs 5 --graphics 'frame_pacing_tier="w"' --out <scratch>`.
- `scripts/frame_pacing_summary.py` - one running editor, loop / acquire /
  cadence medians.
- MCP: `get_frame_pacing_status` (`pacer.vsyncs_per_frame`,
  `deadline_offset_ms`), `get_frame_pacing_frames` (per frame records incl.
  `gpu_begin` / `gpu_end` / `achieved_present_time` / `planned_vsyncs`),
  `set_frame_pacing_min_vsyncs`, `get_gpu_timers` (latest per-pass GPU
  times).
- `editor.frame_pacing` at debug level in `config/editor/logging.json`
  logs cadence changes and a summary every 600 frames (restore the file).
- Metal System Trace: `xcrun xctrace record --template 'Metal System Trace'
  --attach <pid> --time-limit 3s --output <file>.trace`, then `xcrun xctrace
  export --input <file>.trace --xpath '/trace-toc/run[@number="1"]/data/table[@schema="metal-gpu-intervals"]'`
  (rows use id/ref-compressed XML; per command buffer group by
  `cmdbuffer-id`, other processes by `process`); also
  `gpu-performance-state-intervals`.
- Python model: `python3 scripts/frame_pacing_sim.py` (about 5 minutes);
  single scenarios by importing it (`Scenario`, `Tunables`, `run`).

Traps: an occluded editor window gets `presentedTime` 0 and no feedback
(the log says "drawables were not displayed"; the gate flags it); unfocused
fullscreen counts as hidden; an agent-launched editor has no focus.

## Log

- 2026-10-20, iteration 0 (state at plan start): see "What is known". Gate
  not yet run in Release or with 10 launches per configuration.
- 2026-10-20, iteration 1 (display: built-in panel, the Dell not connected):
  on ProMotion tier M locked at N=2 in 5/5 launches with every frame a
  refresh late (~80 ms release to display); the bias sweep and fixed 60 Hz
  runs located the per-mode request window and deadline (table above).
  Built the present calibration. Calibrated on ProMotion: 270/272 frames on
  target, 35 ms, still N=2 - GPU span p90 9-10.7 ms beyond T (the C23
  tail); at fixed 60 Hz also N=2 (span median 12-15 ms, p90 21-24 ms).
  Then: startup histories showed tier M did upshift to N=1 and returned
  within 2-3 frames - first through a slot collision (the dropping-queue
  anchor cap after a skipped-forward target), then, with a FIFO anchor,
  through the predictive downshift judging N=1 by the window's N=2 GPU
  tail and its near-misses. Pacer: FIFO anchor (deviation 18) and
  cadence-scoped predictive downshift (deviation 17), claim C24 (plant:
  drawable pool latched 2T - 0.8 ms before display, cadence-dependent GPU
  tail); C23's control rephrased. Python 86/86, C++ 29/29.
  Gate, Debug, ProMotion: N=1 in 10/10 (default), 10/10
  (`ERHE_RENDER_THREAD=0`), 10/10 (`ERHE_FRAME_PIPELINING=0`); two-refresh
  frames 1, 1 and 4 of 3450; release to display median 27.1-27.6,
  28.3-28.8 and 27.3-28.6 ms (tier S 37.6 ms).
  Gate, Release, ProMotion: N=1 in 10/10 in each of the three threading
  modes; two-refresh frames 2, 1 and 1 of 3450; release to display median
  28.3-28.9, 27.8-28.8 and 28.3-28.8 ms (tier S Release 37.5-37.8 ms).
  The gate passes on the ProMotion panel.
- 2026-10-21: after a system sleep no tier M drawable displayed: the
  recorder clock (`steady_clock`) and Metal's (`mach_absolute_time`)
  diverged by the sleep (~3.7 h), so every target lay hours ahead. The
  recorder reads `mach_absolute_time` on Apple now. Fixed 60 Hz, Debug:
  N=1 in 5/5, two-refresh frames 1-2 of ~270, release to display 39.5-42.6
  ms (tier S 58.5 ms). `auto` resolves to tier M on Metal. Open: the Dell
  at 120 Hz with the calibration (collect its probes for the plant); a
  display change after init keeps the calibrated request offset.
- 2026-10-21, Dell U2725QE at 120 Hz (HiDPI 1920x720, backing 3840x1440):
  calibration request 0.38-0.5 T, deadline 9.2-9.3 ms. Tier S 31.5-34.2 ms.
  Tier M at N=1: 18.7-21 ms. But tier M locks at N=2 (some N=3) in a
  varying share of launches: Debug 3/30; Release batches 8/10, 5/10, 0/10
  (thermal state nominal throughout, `pmset` no warnings). At N=2 the GPU
  clocks down and 26-74 % of the spans exceed T, so the upshift dwell
  never completes; at N=1 the same work takes 5.2 ms. Nothing measured at
  N=2 predicts N=1: next is upshift probing with backoff (Iteration step
  3 candidate (a)), judged by C24's cadence-scoped tests.
- 2026-10-21, iteration 2 (user decision: measure the GPU clock rather than
  probe blind; a probe broke C2/C3/C4/C10 - stage times cannot tell a
  clocked-down GPU from heavy load). IOReport ("GPU Stats" / "GPU
  Performance States", channel GPUPH: OFF + P1-P15, residency per state;
  DVFS table `voltage-states9` of the `pmgr` IORegistry entry, 338-1578 MHz
  on the M4 Pro; no root needed) on the Dell, Release: at N=1 the GPU runs
  at 1470-1540 MHz, 78-81 % busy, span 5.2-5.6 ms; at N=2 at 1020-1160 MHz
  median (dips to ~650 MHz), 55 % busy, span 6.4-8.1 ms median with the
  tail beyond T. A sample costs 160-230 us (up to 1.7 ms): a sampler
  thread, not a frame thread. Design: the device records each presented
  frame's average active GPU clock as a ratio of the DVFS maximum
  (`Frame_time_record::gpu_clock_ratio`, 0 = unknown); the pacer's upshift
  headroom test judges max(c, g * ratio) - the work at full clock - while
  the predictive and reactive downshifts keep the real spans. Plant: GPU
  clock by cadence, GPU time = work / clock; claim C25.
  Built: the clock monitor (now `Clock_monitor`, clock_monitor_apple.mm), C25 in both
  suites (Python 90/90, C++ 30/30). Live the work (span * clock ratio) is
  4.8-5.05 ms median at N=1 and N=2 alike, Debug and Release. Found and
  fixed: the editor converted the render thread's steady_clock join waits
  to recorder time assuming the recorder is steady_clock - wrong since the
  recorder reads mach_absolute_time, so after a system sleep join waits
  counted as CPU service. Dell gate, Release: N=1 in 10/10 (default),
  9/10 (`ERHE_RENDER_THREAD=0`), 10/10 (`ERHE_FRAME_PIPELINING=0`), ~19 ms
  release to display. Debug: N=1 in 6/10 - the CPU slows down at N=2 too
  (service time median 3.1 ms, p90 5.1, max 15.5 against 1.3 ms at N=1),
  and frames beyond the period reset the upshift dwell.
- 2026-10-21, iteration 3 (pre-warm gated live, Dell at 120 Hz). The first
  Release batch with pre-warm reached N=1 in only 7/10. Startup histories
  (`frame_pacing_live_gate.py --history`, the per-frame `prewarm_ms` in
  `get_frame_pacing_frames`) showed three defects: (1) the present
  calibration failed in 4/10 launches ("no request offset displayed on
  target"): the compositor displays stretches of probes a refresh late at
  any offset - whole request passes shifted by one, at the start of the
  stream and in its middle (a warm-up stream before the sweep was tried
  and refuted: offset 0 is the window's edge and the shifts recur) - and
  the sweep required every pass on target; (2) the pre-warm kernel rate was
  measured at the end of the calibration's success path and the editor
  and the observer enabled pre-warm only for a valid calibration, so
  exactly those launches upshifted without pre-warm; (3) their ramp frames
  (clock 0.45-0.78) entered N=1's clock record, its low decile fell to
  ~0.58 and the upshift never completed again (claim C27, deviation 21).
  Also found: deadline probes committed before their predecessor's target
  (margins from ~1.9 T with probes 2 T apart) displayed late again, and
  the one-split fit read that as "everything late". Fixes: majority of
  passes per request offset; probes spaced `floor(margin / T) + 1` vsyncs
  when the margin reaches past 2 T; the kernel rate measured first and
  pre-warm enabled by it alone; clock records from the steady state only
  (`clock_settle_time` 150 ms after the cadence's first GPU end, no frames
  delivered after the cadence was left, no pre-warm frames). Python 100/100
  (C27's control reproduces the lock), C++ 32/32 (C27 fails against the
  previous pacer). Calibration valid in 70/70 launches since; request window
  0-0.75 T in most, deadline 9.2-10.2 ms (1.11-1.22 T).
  Gate, Release: N=1 in 10/10 (default), 10/10 (`ERHE_RENDER_THREAD=0`),
  10/10 (`ERHE_FRAME_PIPELINING=0`); two-refresh frames 6, 9 and 9 of 3480;
  release to display median 17.9-18.0 ms (tier S 31.5-34.2 ms). Debug:
  10/10, 10/10, 10/10; two-refresh frames 12, 9 and 8 of 3480; 19.8-19.9 ms.
  The Dell gate passes (was 47/60). Transitions: all 52 upshifts pre-warmed,
  none reverted; the fill learned 4 ms (one doubling from 2 ms; once 8 ms).
  Release: no slipped frame in the 120 frames after any upshift. Debug:
  for 40-100 frames after the upshift ~20-30 % of the frames are scheduled
  a slot later (a planned two-slot step - every display is on its target):
  the P-cores stay at ~0.55 of their maximum for 0.3-0.8 s after the
  switch while the GPU is already at full clock, release-to-GPU-end latency
  is 25-30 ms (Release 22 ms), and the release scheduler re-anchors one
  slot forward whenever `V(j) - delta_pe - (typical latency + margin)` has
  passed. Next hypothesis: the CPU counterpart of the GPU ramp; measure
  the P-core clock ramp against the latency window first (the plant has no
  CPU clock dynamics yet).
- 2026-10-21, iteration 4 (the Debug upshift transient). The P-core ramp was
  not the cause: in every configuration, Release included, the pacer
  released every frame immediately for 85-120 frames after the upshift
  (no pacer wait) while each frame did ~1.8 ms of work and spent 13 ms in
  involuntary waits - the FR5 clamp (~6 ms), the join (~7 ms) and every
  4th frame `nextDrawable` (9 ms). N=1's latency window still held the
  startup frames (scene load, 30-50 ms) and the waits, counted as latency,
  kept the estimate at the N=2 level: early release justified itself
  (Release: one refresh of extra latency; Debug, render thread slower: a
  skipped slot in 20-30 % of the frames). The plant measured latency after
  the clamp but counted the fence and drawable-pool waits. Fix (claim C28,
  deviation 22): the latency input excludes the involuntary waits between
  release and submit (`Frame_time_record::release_wait_time`; plant
  likewise), and a cadence change clears the entered cadence's latency
  window, seeding a floor until the visit has `K` samples (a seed kept as a
  sample had carried the startup spike down a chain of short visits).
  Python 102/102 (the C28 control reproduces the trap), C++ 34/34 (C28 fails
  on the previous pacer). No CPU performance request was needed (user:
  work without requesting performant cores).
  Gate, Dell 120 Hz: N=1 in 60/60; two-refresh frames 7-10 of 3480 per
  batch; release to display 19.3-19.9 ms Debug, 18.0-18.3 ms Release. After
  the 54 upshifts: 0 skipped slots in the first 60 frames (before: Debug
  20-34 per 10 frames over 10 upshifts), input-to-display latency over
  frames 20-100 after the upshift 0.3-1.1 ms above steady state (before:
  4.6-18.7 ms).
- 2026-10-21, Vulkan on macOS (Dell at 120 Hz, `build_xcode_vulkan`).
  KosmicKrisp exposes neither `VK_EXT_present_timing` nor
  `VK_GOOGLE_display_timing` and stays on tier S. MoltenVK exposes
  `VK_GOOGLE_display_timing`: its `desiredPresentTime` is
  `presentDrawable:atTime:` and `actualPresentTime` the drawable's
  `presentedTime`, both `mach_absolute_time` nanoseconds (the recorder's
  clock), and the targets are honored (at N=2, 99 % of the frames displayed
  two refreshes apart). Tier W now runs on it (`doc/erhe/vulkan_backend.md`)
  with the tier M differences (no holdback, estimated deadline offset, a
  quarter-period early request; no present calibration, no pre-warm). The
  Metal findings carry over: at N=2 the GPU clocks down (0.4-0.65) and the
  pacer stayed at N=2 until the Vulkan device ran the `Clock_monitor` (now
  `clock_monitor_apple.mm`, shared by both backends). Then it upshifted, but
  at N=1 the governor lowered the clock to ~0.72 within a second, the frame
  span read ~T and the predictive downshift returned to N=2 with every frame
  on time. The span held the 1.4 ms (Debug; Release 0.65 ms) GPU idle gap
  between the upload submit and the presenting submit, which does not scale
  with the clock; the bracket now measures that gap and the pacer's `g`
  excludes it (inputs.md 3.3). Work at full clock is ~5.8 ms; a Metal System
  Trace of the MoltenVK editor read the main pass at 8.3 ms because
  Instruments fixes a lower GPU performance state - not a MoltenVK cost.
  Gate, MoltenVK, Dell 120 Hz: N=1 in 60/60 (Debug and Release, default,
  `ERHE_RENDER_THREAD=0`, `ERHE_FRAME_PIPELINING=0`); two-refresh frames
  8-12 of ~3450 per batch; release to display 18.2-20.4 ms Debug,
  16.7-19.7 ms Release; GPU service 6-6.5 ms at 72-80 % clock. Open: the
  Vulkan path has no present calibration (ProMotion needs a request a
  period earlier, see "Compositor properties per display mode") and no
  pre-warm; re-gate on fixed 60 Hz and ProMotion.
