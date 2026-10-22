# Frame pacing on Adaptive-Sync displays (Metal tier M)

Status: in progress

Built and gated: detection, pacer and editor wiring; loops Medium and
clustered hold 120 Hz fullscreen on the Adaptive-Sync display in every
session, windowed unchanged (see "Verification"; claims C30 and C31,
deviations 24 and 25). The goal is met; open: the ProMotion and 60 Hz
re-gates and a continuous VRR cadence for loads beyond one period.

Tier M (`doc/erhe/metal_backend.md` "Frame pacing", algorithm in
`doc/frame_pacing/algorithm.md`, Metal iteration log in
[frame_pacing_metal.md](frame_pacing_metal.md)) assumes a periodic vsync
grid. A variable refresh rate display in fullscreen breaks that assumption,
and the editor then runs well below the display rate while the GPU frame
fits it. [frame_pacing.md](frame_pacing.md) "VRR-aware tier" records that
no tier paces a variable-rate display on purpose; this plan is that work for
Metal.

## Observed behaviour (2026-10-22, Release Metal, M4 Pro, Bistro)

- Display: external 4K panel driven at 3840x1440 @ 120 Hz. `NSScreen`
  reports `maximumFramesPerSecond` 120, `minimumRefreshInterval` 8.333 ms,
  `maximumRefreshInterval` 20.833 ms: a 48-120 Hz Adaptive-Sync range.
- Windowed, the compositor runs a fixed 8.344 ms grid and tier M holds
  120 Hz exactly (present delta 8.344 ms in 1061-1063 of 1062 frames).
- Fullscreen (`config/editor/window.json` `"fullscreen": true`, SDL picks the
  3840x1440 @ 120 Hz mode, direct to display), `presentedTime` deltas are
  not multiples of 8.33 ms: steady 10.6-11.8 ms, or 16-20 ms stretches. The
  display shows each frame when the pacer asks for it, the pacer's grid
  estimate (`Frame_time_record::grid_period`) follows the presented deltas
  (9.5-11.8 ms), and tier M paces to its own drifting estimate.
- The outcome depends on the pacer's history, not on GPU time. Once the grid
  estimate sits at 8.344 ms, 120 Hz holds up to about 6.5 ms of GPU work per
  frame; once it has drifted, it stays drifted across configuration
  switches. The same configuration ran at 120 fps in one session and 86 fps
  in another.
- At HEAD c8c3a775e, Bistro loops Medium needs 7.2 ms and clustered 7.6 ms
  of GPU per frame (0.7-1.1 ms margin to 8.33 ms) and presents at 85-94 fps.
- N=2 trap: when frames land at two refreshes, the GPU clock drops to about
  0.8 (IOReport), the frames get slower, and the pacer stays at 50-60 fps.

## Mechanism

The drift enters through two channels in `Frame_pacer::on_present_feedback`.

- **PLL creep (bounded).** On a VRR display feedback is never earlier than
  the target: an on-time frame displays at its target, a late one when it is
  ready. The PLL's period branch therefore only sees non-negative
  innovations and creeps the period up, to 8.338-8.359 ms in the plant -
  bounded by its clamp of 0.5 % of the reference period.
- **Gross-error re-seed (unbounded).** The re-seed
  (`doc/frame_pacing/algorithm.md` deviation 12, claim C16) takes a period sample from each
  feedback delta, `dt / round(dt / period)` for `1 <= round(dt / period)
  <= 4`, and re-seeds when the median of its 16-sample window deviates from
  the tracked period by more than 2 %. It replaces the period *and* the
  reference period (`m_grid_period_ref`), so the clamp re-centres on what it
  measured. On a fixed grid every delta is a multiple of the true period, so
  the samples are the true period whatever the load does.
- On a VRR display the deltas are not multiples of anything. A late frame
  displays when it is ready, not at the next vsync: a 20 ms frame at N=1
  gives `dt` = 20 ms, `round(20 / 8.33)` = 2, a 10 ms sample; 11 ms frames
  give 11 ms samples. A cadence below the display's 48 Hz floor (N=3 at
  120 Hz, 25 ms targets) makes the display repeat the previous image
  20.833 ms after it and hold the next image to 20.833 + 8.333 = 29.17 ms.
  29.17 / 8.333 = 3.5 exactly, a rounding boundary: the delta counts as
  three periods (a 9.72 ms sample) only because the PLL creep already put
  the tracked period above 8.333 ms. Once such samples hold a majority of
  the 16-sample window, its median re-seeds the period to 10-11 ms or
  9.72 ms. The disturbances that produce them are a scene load, a heavy
  configuration, or the cold start's downshift to N=3.
- Once re-seeded, the display honors every target on the pacer's own grid
  (`presentDrawable:atTime:`), so every delta equals the tracked period and
  confirms it. A delta shorter than the tracked period never arrives (the
  targets are spaced by it), so nothing pulls the period back to 8.33 ms.
  The estimate only ratchets upward, and the outcome depends on the
  session's history: the same configuration holds 120 Hz in a session
  without the disturbance and runs at 1 / (re-seeded period) in one with it.
- The cadence budgets quantize by the tracked period (deviation 12), so a
  re-seeded 9.72 ms period holds N=1 at 103 fps and N=2 at 19.44 ms
  (51 fps).

The N=2 trap at the live load (GPU 7.2-7.6 ms) is a separate mechanism and
is not VRR-specific in the plant: the cadence tests add `guard` (1 ms) to
the stage times (`W = max(q_p(c), q_p(g)) + guard`, the reactive
attribution `streak_attr + guard > N * period`), so with q95(g) above
7.33 ms N=1 is infeasible by the pacer's rule. The margin AIMD keeps
producing near-misses (its ~1 % equilibrium rate), which count as pressure,
and a miss streak after an upshift attributes to load: the pacer returns to
N=2. On a fixed grid this is correct and stays: a frame that misses its
vsync by a little costs a whole refresh there. On a VRR display such a
frame costs only its own lateness, so the fix for the trap is a guard
allowance scoped to VRR. The live windowed result ("Observed behaviour")
does not contradict the fixed-grid trap: the windowed viewport is smaller
(1334x1120 against 2224x1360 fullscreen) and its GPU span was 4.5-6.1 ms,
against 7.36-8.10 ms fullscreen.

Claim C30 (`scripts/frame_pacing_sim.py`, `erhe_frame_pacing_tests`
`DISABLED_c30_variable_refresh_display`) reproduces both with a VRR display
model in the plant (`vrr_min_interval` / `vrr_max_interval`, 8.333 /
20.833 ms, 1 ms deadline, GPU clock 0.8 at N>=2): after a startup spike or
a heavy configuration at 6.5 ms of GPU work the period re-seeds to 9.72 ms
and the pacer stays at N=1 with 9.72 ms deltas; at the live load of 7.2 ms
it stays mostly at N=2 with 16.7 ms deltas. The controls: the same 6.5 ms
load and spike on the windowed compositor's fixed grid and the VRR display
without a disturbance hold 120 Hz, and the 7.2 ms load on the fixed grid
stays at N=2 (the guard rule).

## Fix

The pacer takes the display kind from the host
(`Frame_pacer_tunables::display` `Present_display_kind::variable_refresh`
and `max_refresh_interval`, set through the observer's
`Frame_pacing_observer::set_display`, whose change recreates the pacer;
`doc/frame_pacing/algorithm.md` deviation 24). On a VRR display the period
is the minimum refresh interval for good (no re-seed, no PLL period
branch), late displays re-anchor the slot cursor only, the cadence stays
no target lies more than the maximum refresh interval after the previous
one (the display would repeat the previous image in between), the cadence
tests drop the `guard` allowance, and a frame the display could not show at
its target (within one period of the previous display, or a repeat in
between) is no deadline offset evidence. C30 passes in both suites: every
VRR arm at N=1 with 8.333 ms deltas and the deadline offset at its initial
value, no repeats during the heavy configuration; the fixed-grid claims are
unchanged.

The Metal backend detects the VRR presentation (the NSScreen
refresh-interval range of the window's screen, and the fullscreen state;
`doc/erhe/metal_backend.md` "Frame pacing"), and the editor passes it to the
pacer with `set_display` (`doc/frame_pacing/inputs.md` "Display kind"). On
VRR fullscreen the true deadline is about 1 ms and the deadline offset
estimator never decays below its initial value, which is also its floor, so
the editor seeds `Frame_pacer_tunables::deadline_offset_initial` there
instead of the windowed compositor's calibration (T + 1.3 ms = 9.63 ms at
120 Hz).

A load just inside one period (Bistro clustered) stayed at N=2 on the
upshift test's hysteresis band; on VRR the cadence tests take the forms of
`doc/frame_pacing/algorithm.md` deviation 25, verified by claim C31 in both
model suites.

## Verification

Live, 2026-10-22, Release Metal (`build_xcode_metal`), M4 Pro, external
48-120 Hz panel at 3840x1440 @ 120 Hz, Bistro at the bench camera; per
config 10 s settle, then a 5 s pacing capture (about 600 frames at N=1).
Fullscreen detection log: "display presentation: variable refresh
(fullscreen; display refresh interval 8.333 ... 20.833 ms = 48.0 ... 120.0
Hz)"; windowed: "fixed grid (windowed; ...)". The display's minimum
interval is 8.344 ms; "exact" is the share of present deltas within
0.1 ms of it.

| session | config (in order) | present delta median (p5 / p95), ms | exact | N | GPU span median (p5 / p95), ms | GPU clock |
|---|---|---|---|---|---|---|
| D fullscreen, clustered first | clustered | 8.344 (8.344 / 8.344) | 100 % | 1 | 7.75 (7.70 / 7.78) | 1.00 |
| | loops Medium | 8.344 (8.344 / 8.344) | 99.8 % | 1 | 6.35 (6.32 / 6.38) | 0.99 |
| | clustered | 8.344 (8.344 / 8.344) | 100 % | 1 | 7.74 (7.69 / 7.78) | 1.00 |
| E fullscreen, heavy first | loops 43 lights | 16.639 (16.32 / 16.94) | 0 % | 2 | 10.38 (10.26 / 10.48) | 0.99 |
| | loops Medium | 8.344 (8.344 / 8.344) | 99.8 % | 1 | 6.29 (6.25 / 6.51) | 0.99 |
| | clustered | 8.344 (8.344 / 8.344) | 100 % | 1 | 7.65 (7.60 / 7.68) | 1.00 |
| | loops Medium | 8.344 (8.344 / 8.344) | 100 % | 1 | 6.29 (6.25 / 6.40) | 0.99 |
| F fullscreen | loops Medium | 8.344 (8.344 / 8.344) | 100 % | 1 | 6.31 (6.26 / 6.43) | 0.99 |
| | clustered | 8.344 (8.344 / 8.344) | 99.7 % | 1 | 7.66 (7.61 / 7.69) | 1.00 |
| | loops 43 lights | 16.663 (16.20 / 17.06) | 0 % | 2 | 10.44 (10.26 / 11.61) | 0.99 |
| | clustered | 8.344 (8.344 / 8.344) | 100 % | 1 | 7.66 (7.61 / 7.69) | 1.00 |
| G fullscreen, clustered first | clustered | 8.344 (8.344 / 8.344) | 100 % | 1 | 7.73 (7.69 / 7.76) | 1.00 |
| | loops 43 lights | 16.667 (15.99 / 17.04) | 0.7 % | 2 | 10.50 (10.37 / 10.66) | 0.99 |
| | clustered | 8.344 (8.344 / 8.344) | 100 % | 1 | 7.73 (7.69 / 7.77) | 1.00 |
| windowed | loops Medium | 8.344 (8.344 / 8.344) | 100 % | 1 | 3.97 (3.87 / 4.14) | 0.98 |
| | clustered | 8.344 (8.344 / 8.344) | 99.8 % | 1 | 4.58 (4.46 / 4.79) | 0.98 |
| H fullscreen, clustered first | clustered | 8.344 (8.344 / 8.344) | 100 % | 1 | 7.63 (7.59 / 7.67) | 1.00 |
| | loops Medium | 8.344 (8.344 / 8.344) | 99.7 % | 1 | 6.28 (6.24 / 6.41) | 0.99 |
| | clustered | 8.344 (8.344 / 8.344) | 100 % | 1 | 7.63 (7.59 / 7.67) | 1.00 |
| I fullscreen, heavy first | loops 43 lights | 16.663 (16.10 / 17.07) | 0 % | 2 | 10.36 (10.24 / 10.81) | 0.98 |
| | clustered | 8.344 (8.344 / 8.344) | 99.8 % | 1 | 7.67 (7.62 / 7.69) | 1.00 |
| | loops Medium | 8.344 (8.344 / 8.344) | 100 % | 1 | 6.31 (6.27 / 6.47) | 0.99 |
| windowed | clustered | 8.344 (8.344 / 8.344) | 100 % | 1 | 4.73 (4.60 / 4.97) | 0.98 |

Sessions D-G and the first windowed session ran the upshift rule of
deviation 25 with the earlier downshift rules; H, I and the last windowed
row run the backlog downshift test of deviation 25 as it stands.

Fullscreen, the grid estimate stays 8.333 ms and the deadline offset at
its initial value (2.5 ms) in every config; the display shows each frame
8.344 ms after the previous one (its actual minimum interval), which the
phase tracking of on-time displays follows. Windowed, the grid estimate is
8.344 ms (the tracked fixed grid) and the deadline offset 11.7-12.7 ms
(present calibration 9.62 ms, raised by the estimator).

- Loops Medium and clustered hold 120 Hz in every fullscreen visit (10
  clustered visits: first in a session, after loops Medium, after loops 43
  lights), with no pre-warm frame captured: no visit fell to N=2.
- Loops 43 lights (10.4-10.5 ms of GPU) runs at N=2 with 16.7 ms deltas, as
  its load requires on the integer cadence. A continuous VRR cadence
  (targets spaced at the predicted frame interval when it exceeds T, about
  95 Hz for that load) is not part of this plan's goal and is open.
- Not re-gated here: the built-in ProMotion panel in fullscreen (its range
  makes it a variable refresh display too) and the 60 Hz modes; they belong
  to the tier M re-gate of [frame_pacing_metal.md](frame_pacing_metal.md),
  which needs the user at the machine.

## Goal and gate

Fullscreen on the Adaptive-Sync display, tier M (or the tier `auto` picks)
presents loops Medium and clustered Bistro at 120 Hz (8.33 ms deltas) in
every session, without the drift and without the N=2 trap, while the
windowed fixed-grid behaviour and the existing Dell / ProMotion gates of
[frame_pacing_metal.md](frame_pacing_metal.md) stay as they are. The pacer
models stay green: `scripts/frame_pacing_sim.py` and `erhe_frame_pacing_tests`.

## Starting points

- Detect a variable-rate presentation: the `NSScreen` refresh-interval range
  (`doc/plans/metal_backend.md` table row "Refresh period, VRR range") and
  whether the window is fullscreen / direct to display. On such a display
  the grid period comes from the display's minimum refresh interval (the
  target cadence), not from presented-time deltas; presented deltas are
  feedback on whether the frame met its target, not grid evidence.
- Decide the pacing target on VRR: present at the target cadence
  (`presentDrawable:atTime:` / `afterMinimumDuration:`), with the cadence
  chosen from the GPU frame time as today.
- Reproduce the drift and the N=2 trap in the plant (`frame_pacing_sim.py`)
  with a VRR display model first, so a claim / test fails on the current
  pacer before the fix.
- Display / mode changes (window to fullscreen, moving between displays)
  re-run the detection, not a per-frame poll (`doc/plans/frame_pacing.md`
  "Display and display-mode change handling").

## Verification recipe

- Release Metal, `build_xcode_metal` (re-run `cmake build_xcode_metal` after
  adding sources; the Xcode project does not pick up new generated codegen
  sources otherwise).
- Fullscreen via a restorable `config/editor/window.json` override
  (`"fullscreen": true`, `"refreshrate": 120`); restore it afterwards. The
  display must be awake and unlocked (no human needed otherwise); check with
  `CGSessionCopyCurrentDictionary` (`CGSSessionScreenIsLocked`) and
  `CGDisplayIsAsleep`.
- Bistro at the `scripts/bistro_frame_bench.py` bench camera, loops Medium
  and clustered (`set_graphics_preset` `light_shading`), several sessions,
  including a session that starts in a state that drifted before (e.g.
  a heavy configuration such as loops with all 43 lights first, then back).
  Report the present-interval distribution, the grid estimate, N and the GPU
  clock ratio. The per-machine measurement scripts and earlier numbers are
  indexed in `memory-bank/local/context.md` (`bistro_fullscreen_perf`).
- Windowed regression: 120 Hz exact as before. The 60 Hz and ProMotion
  re-gates (built-in display) need the user to move the window or change
  the display mode.
