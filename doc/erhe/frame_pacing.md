# erhe::frame_pacing

Stability: mostly stable

Frame pacing algorithm core: decides how many display refresh periods each
frame is visible (cadence), when the CPU may start per-frame work (release
scheduling), which vsync slot each frame targets, and how many images may be
queued for presentation.

## Design documents

The library implements the design in `doc/frame_pacing/`:

- `doc/frame_pacing/requirements.md` - requirements, `FR` labels
- `doc/frame_pacing/control_model.md` - control-theory model, claims
- `doc/frame_pacing/algorithm.md` - the algorithm (normative for this code)
- `doc/frame_pacing/inputs.md` - input definitions and engine sourcing
- `doc/frame_pacing/behavior.md` - behavior specification per scenario
- `doc/frame_pacing/capability_tiers.md` - capability tiers (Vulkan extensions)
- `doc/frame_pacing/user_interface.md` - the editor's verification window
- `doc/frame_pacing/implementation_plan.md` - how the pacer is layered across erhe, `P` labels

`scripts/frame_pacing_sim.py` is the executable Python reference model; this
library is its 1:1 C++ port. Keep them in sync: any algorithm change must keep
BOTH the Python suite (`py -3 scripts/frame_pacing_sim.py`) and the C++ parity
suite (`erhe_frame_pacing_tests`) green.

## Key types

- `Frame_pacer` - the pure algorithm. No OS / Vulkan / clock access: all times
  arrive as arguments (seconds, one monotonic clock domain), all decisions are
  returned as values. Steady state performs no heap allocations (fixed-capacity
  ring storage allocated at construction).
- `Slop_servo_pacer` - the tier S fallback (Games-by-Mason method):
  no display-time sensing, no grid, no cadence - servos a pre-input sleep
  from measured backpressure blocking ("slop"). Requires plain-FIFO
  backpressure; on fifo_latest_ready the servo winds to nothing (C21a).
  Pure like `Frame_pacer`; reference model `SlopServo` in the sim.
- `Frame_pacer_tunables` - all quantitative knobs with verified defaults,
  and the host's description of the presentation path: `present_queue`,
  `deadline_offset_source`, and `display` (`Present_display_kind`
  `fixed_grid` | `variable_refresh` with `max_refresh_interval`; an
  Adaptive-Sync display driven directly has no grid to track,
  doc/frame_pacing/algorithm.md deviation 24).
- `Schedule_decision` - per-frame output: release time, target slot and
  presentation time (FR3), predicted display time and duration (FR4), and the
  present-wait clamp id (FR5).
- `Sample_window` - fixed-capacity sliding window with order-statistic
  quantile; backs the load / latency estimators.
- `Frame_pacing_observer` - drives a `Frame_pacer` from the engine's frame
  time records and enforces nothing itself; the host enforces whichever
  parts of the exposed `Schedule_decision` its actuators cover. Log lines go
  through a queue the host drains, so the library stays free of logging
  dependencies.
  `set_deadline_ceiling_callback` reports, once per pacer, a deadline
  estimate held at the host-measured ceiling for
  `deadline_ceiling_sustain_samples` consecutive deadline samples
  (`Frame_pacer::is_deadline_offset_ceiling_sustained`): the host's deadline
  measurement is stale-low (the editor then deletes its present calibration
  cache entry, doc/erhe/graphics.md "Present calibration cache").
- `Frame_time_recorder` - the bounded ring of per-frame event timestamps
  (one monotonic clock domain) that feeds both the pacer's estimators and the
  profiling UI. There is no second instrumentation path. Thread-safe: one
  mutex guards every access, because the editor's main thread begins a
  frame's record and runs the pacer while its render thread stamps the
  previous frame. Writers name the field by member pointer (`set`,
  `set_once`, `add`, and a two-field `set` for begin / end pairs); readers
  take copies (`get`). No reference to a record leaves the lock. The display
  refresh duration the swapchain reports is kept here too, under the same
  lock.

## Input fidelity contract (important)

- `cpu_duration` / `gpu_duration` fed to `on_cpu_done` / `on_gpu_done` are
  stage SERVICE times: involuntary waits (pacer wait, present wait, join wait, fence, acquire) and GPU
  idle bubbles are excluded. Feeding raw spans causes spurious downshifts.
- `on_gpu_done` should be driven by per-frame polling of the GPU frame
  timestamp bracket; the lazy fence-read path is a verified graceful
  degradation (claim C9), not the nominal mode.

## Tests

`test/` replicates the Python simulation plant (pipelined CPU/GPU stages,
FIFO or fifo_latest_ready presentation engine on a true vsync grid the pacer
does not know, causally-correct event delivery, plain-FIFO acquire
backpressure for the tier S claims) and asserts model claims C1..C21.
Build with `-DERHE_BUILD_TESTS=ON`; run `erhe_frame_pacing_tests`.

## Dependencies

`erhe::profile` (profiling zones) and `erhe::verify` (`ERHE_VERIFY` on
invalid tunables), both private; nothing that needs a GPU or a display. This
is deliberate: the isolation requirement in
`doc/frame_pacing/requirements.md` (unit and smoke testable without GPU or
display).

## Future work

- [plans/frame_pacing.md](../plans/frame_pacing.md) - tier A (Android), VRR, asynchronous present, verification UI capture export.
