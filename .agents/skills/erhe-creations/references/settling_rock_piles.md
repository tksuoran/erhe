# Deterministic settling, staged construction & rock piles

The manual simulation clock, physics-settled debris, staged (course by
course) construction, and the creation-17 desert props (dunes, cacti).
Reference code: creation 17 (rockfall). Swept-blade plants (agaves) are
in references/blades_sweep.md.

## Manual simulation clock

The `advance_time` MCP tool (2026-08-09) is a manual simulation clock:
`{seconds}` queues exactly that much SIMULATION time (drained at
`max_step_ms`, default 250, per rendered frame - bypasses the 25 ms
wall-clock dilation cap and the hidden-window pause), `{mode}` switches
wall_clock | paused | manual (paused/manual freeze the sim except queued
advances). Wrappers: `c.advance_time(...)`, `c.run_simulation(seconds)`
(queue + poll + restore wall_clock), `common.sim(c, seconds)` (mid-settle
tick, no mode switch).

- **Settle recipe**: build everything with physics DISABLED, then
  `advance_time mode=manual` -> `set_physics(True)` -> `wake_physics()`
  -> advance N seconds -> `set_physics(False)` -> `mode=wall_clock`.
  Identical result windowed or headless, any frame rate; ~10 simulated
  seconds settle in under a second of wall time.
- **Scope the wake on iterative re-settles** (2026-08-09):
  `c.wake_physics(node_name=<subtree>)` / `(node_ids=[...])` wakes only
  that scope - an unscoped wake during an `--only <pile>` re-settle
  wakes EVERY dynamic body and can topple already-built structures
  (the cairn). Staged construction wakes each body as it is laid
  (`node_ids=[stone]`); contact wakes whatever it lands on.
- **Staged construction**: with the sim frozen between explicit ticks,
  you can PLACE BODIES MID-SETTLE: creation 17's cairn lays one flat
  slab per course, ticks 1.2 s, reads the stone's landed pose
  (node_world_pose) and lays the next course on the MEASURED top,
  following the stack as it drifts. Wake each stone after placement
  (bodies spawn asleep). Dropping a whole pre-stacked tower at once
  collapses - twice confirmed.
- The sim is deterministic in TIME but not bit-identical across runs
  (Jolt threading): a staged course can still slide off on an unlucky
  run - VERIFY each stage (landed y vs expected) and retry, delete +
  re-place, like a person would.

## Rock piles

- Physics ROCK PILES: rock = convex hull of a jittered fibonacci-sphere
  point cloud (9-14 points angular, 22-24 worn; round the coords so the
  shape pool key is stable); a few archetypes as pooled brushes,
  instanced with QUANTIZED number bake scales (collision follows; a
  multiplicative scale ladder caps per-brush primitive count -
  `common.quantize_scale`) + power-law sizes (`common.power_law_size`);
  pre-heap with sphere-drop packing (largest first, each rested on the
  heap) so the settle compacts instead of exploding; batch
  `edit_physics_body` friction 0.9 / restitution 0.02 /
  angular_damping 0.35 so rocks pile instead of scattering. Boulders
  > 1 m want the ANGULAR archetypes (an evenly-jittered 30-point hull
  reads as a geodesic ball at boulder scale).
- Chamfer the visible rocks AFTER the settle: one `chamfer` call with a
  `node_ids` batch (~50/call, bevel_ratio ~0.22) on every rock >= 0.45 m
  - each instance silently goes private (deliberate; pebbles keep
  sharing), pose + physics attachment survive, frozen bodies keep the
  slightly-proud original hulls (invisible). If a staged builder DELETES
  a body (cairn retry), prune its id from the chamfer list or the batch
  fails on the missing node.

## Dunes & cacti (creation 17 presentation research)

- **Smooth sand mounds / dunes**: ONE pooled high-tessellation
  `uv_sphere` brush (slice 48 / stack 24, smooth normals), instanced per
  mound with node TRS scale [rx, h, rz] (motion_mode none), yawed to a
  shared wind heading and SUNK below grade (drifts 0.42*h, real-crest
  dunes 0.30*h) so the skirt melts into the ground. Do NOT reuse the
  ground texture on them: a mound's sphere UV spans the whole sphere, so
  the ground's scale-90 fbm renders as microscopic bright grain that
  detaches the mound (bread-loaf look) - make a second graph with the
  SAME gradient at fbm scale ~5x3 and bind it to a dedicated dune
  material with the ground's base_color. Rejected smooth-mound
  alternatives: lattice-deformed stepped boxes crease on the control
  grid, catmull_clark / smooth on hulls stays polygonal at the
  silhouette.
- **Cacti**: saguaro = smooth capsule trunk + arms as 3-capsule chains
  (out-and-up, steeper, vertical - align_y_quaternion, equal radii so
  the hemisphere caps read as one limb); barrel = squat SUNK uv_sphere
  ([0.44, 0.34, 0.44]*s at y 0.28*s - taller reads as a green ball);
  prickly pear = thin uv_sphere pads ([0.38, 0.44, 0.07]*s) that FAN
  with real outward lean (0.35-0.65) AND pad-plane tilt
  (quat_mul(yaw, tilt)) - small-lean untilted pads stack into a ball
  totem. Quantize capsule/sphere parameters for brush pooling; all parts
  motion_mode none. GOTCHA: create_shape capsule takes bottom_radius /
  top_radius (a 'radius' argument is REJECTED since 2026-08-09 -
  create_shape / place_brush / place_brush_instances now error on any
  key outside the shape's schema instead of silently defaulting) and
  requires length > |bottom - top|. Saguaro ribs rejected: hulls cannot
  go concave, per-rib CSG costs a boolean pass each; matte green
  material reads right at scene scale.
