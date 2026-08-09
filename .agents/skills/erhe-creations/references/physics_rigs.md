# Physics rigs: joints, ragdolls, load-bearing motors

Joint plumbing and motor-driven standing rigs. Reference code:
creations 7-8 (ragdoll rumble, glass audience) and 14 (spider
sentinel - motor-held STANDING ragdoll). Plant sway rigs live in
references/vegetation.md; deterministic settling in
references/settling_rock_piles.md.

## Physics (creations 7-8 carry reference code)

- Shapes with `motion_mode="dynamic"`; per joint create TWO coincident
  anchor child nodes (`common.anchor`, world positions), `settle()`,
  then `create_physics_joint node=anchorA connected=anchorB
  settings_name=<library settings>`. Limits: lock linear = all axes
  0..0; hinge = lock 2 angular + range on one; ball = range on all 3;
  weld = everything locked; pendulum-to-world = no connected node.
- `toggle_physics` takes an explicit `enabled` bool since 2026-08-08
  (omit = old toggle behavior) - no toggle-and-verify dance. Bodies
  still spawn DEACTIVATED unless created with `wake: true` on
  `create_physics_body`; `wake_physics()` remains for waking a whole
  scene after enabling (scoped variants: see the settle recipe in
  references/settling_rock_piles.md). Freeze an aftermath pose by
  `toggle_physics enabled=false` before save.
- Pure-visual child parts: `strip_physics(node_id)`.
- `apply_physics_force` pokes a dynamic body (force / torque / impulse +
  optional world `point`). Impulses act immediately; forces last one
  fixed step (re-apply for a sustained push).
- `edit_physics_body` initial-velocity gotcha: `linear_velocity` only
  stores into the create info, and a shape edit in the SAME call
  recreates the body BEFORE it lands. Two calls: first
  `{linear_velocity, mass, ...}`, then `{shape, ...}` to trigger the
  recreation that applies it.

## Load-bearing motor rigs (creation 14 carries reference code)

Rest-pose motor drives are strong enough to act as MUSCLES, not just
sway springs: creation 14's 50-part ragdoll spider STANDS under full
gravity on motorized leg joints (and staggers + recovers from an
apply_physics_force shove).

- Explicit masses are essential for motor sizing. Create every part
  `motion_mode="none"`, pose it, then attach the body with
  `create_physics_body shape="auto" mass=<explicit>` (gravity_factor
  stays 1 - the point is to carry the weight). (`edit_physics_body`
  mass edits DO rescale inertia since 2026-08-08, so post-hoc mass
  tuning is safe too.)
- Size stiffness per joint from its static hold torque = (weight share
  at the contact point) x (horizontal lever from joint to contact), for
  ~0.02 rad of sag; graduate along the limb (spider hip 2400 -> toe 100
  Nm/rad). `max_force` ~5x hold torque makes a real shove yield the
  motors visibly before recovery. Drives on all 3 angular axes,
  position_target 0, limits ~+/-0.45 rad, linear locked.
- Feet/contact ends: place the authored pose so free-end tips just touch
  the floor (compute the drop analytically from the limb tables); the
  closed contact chain makes the stance much stiffer than open-chain
  spring sag suggests (measured: 24 mm sag on a 0.46 m stance).
- Pose probes: "tilt from world up" is useless for capsules rotated off
  +Y (reads 90 deg forever). Gate standing on height + drift from the
  REST rotation, and gate recovery on the body axis ELEVATION only
  (yaw-insensitive) - a shoved creature legitimately re-plants facing a
  new heading. A shove also slides it 1-2 m: re-frame the aftermath
  camera on the body's actual position, not the build position.
