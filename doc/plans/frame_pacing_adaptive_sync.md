# Frame pacing on Adaptive-Sync displays (Metal tier M)

Status: proposed

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
