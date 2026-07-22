# erhe::frame_pacing

Frame pacing algorithm core: decides how many display refresh periods each
frame is visible (cadence), when the CPU may start per-frame work (release
scheduling), which vsync slot each frame targets, and how many images may be
queued for presentation.

## Design documents

The library implements the planning-phase design in `doc/`:

- `doc/frame_pacing.md` - requirements
- `doc/frame_pacing_control_model.md` - control-theory model, claims C1..C9
- `doc/frame_pacing_algorithm.md` - the algorithm (normative for this code)
- `doc/frame_pacing_inputs.md` - input definitions and engine sourcing
- `doc/frame_pacing_behavior.md` - behavior specification per scenario
- `doc/frame_pacing_capability_tiers.md` - capability tiers (Vulkan extensions)
- `doc/frame_pacing_implementation_plan.md` - phased work order

`scripts/frame_pacing_sim.py` is the executable Python reference model; this
library is its 1:1 C++ port. Keep them in sync: any algorithm change must keep
BOTH the Python suite (`py -3 scripts/frame_pacing_sim.py`) and the C++ parity
suite (`erhe_frame_pacing_tests`) green.

## Key types

- `Frame_pacer` - the pure algorithm. No OS / Vulkan / clock access: all times
  arrive as arguments (seconds, one monotonic clock domain), all decisions are
  returned as values. Steady state performs no heap allocations (fixed-capacity
  ring storage allocated at construction).
- `Slop_servo_pacer` - the tier S fallback (P4.1, Games-by-Mason method):
  no display-time sensing, no grid, no cadence - servos a pre-input sleep
  from measured backpressure blocking ("slop"). Requires plain-FIFO
  backpressure; on fifo_latest_ready the servo winds to nothing (C21a).
  Pure like `Frame_pacer`; reference model `SlopServo` in the sim.
- `Frame_pacer_tunables` - all quantitative knobs with verified defaults.
- `Schedule_decision` - per-frame output: release time, target slot and
  presentation time (FR3), predicted display time and duration (FR4), and the
  present-wait clamp id (FR5).
- `Sample_window` - fixed-capacity sliding window with order-statistic
  quantile; backs the load / latency estimators.

## Input fidelity contract (important)

- `cpu_duration` / `gpu_duration` fed to `on_cpu_done` / `on_gpu_done` are
  stage SERVICE times: involuntary waits (pacer wait, fence, acquire) and GPU
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

None (standard library only). This is deliberate: the isolation requirement
in `doc/frame_pacing.md` (unit and smoke testable without GPU or display).
