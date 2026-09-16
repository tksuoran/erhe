# Box3D physics backend

`ERHE_PHYSICS_LIBRARY=box3d` selects a third physics backend
([erincatto/box3d](https://github.com/erincatto/box3d)), alongside `jolt` and
`none`. The editor builds, links and runs on it.

This document is the working record for that backend: what is done, what is
left, and the Box3D behaviors that were expensive to discover. The reference
material lives elsewhere and is not duplicated here:

- `src/erhe/physics/notes.md` -- the library's own notes, including the
  deferred/unsupported table and the backend design decisions.
- The header comment block in
  `src/erhe/physics/erhe_physics/box3d/box3d_world.hpp` -- the authoritative
  copy of that table. Keep the two in sync.
- The nine `physics: ...` commits (`git log --oneline` from
  `physics: add box3d backend selection` forward). Each records what was
  learned in that phase; several Box3D behaviors were only found by
  instrumenting a failing headless run, and those messages are the record.

## Status

Phases 1-8 are complete and committed. Phase 9 is docs-complete; its
end-to-end verification sweep has NOT been run.

| Phase | Work | State |
| :--- | :--- | :--- |
| 1 | Backend selection and CMake plumbing | done |
| 2 | Pure-logic core (filter table, hull builder, six-DOF classifier) + first physics gtest suite | done |
| 3 | Collision shape descriptors (all 26 `ICollision_shape::create_*`) | done |
| 4 | World and rigid body -- the editor first ran on box3d | done |
| 5 | Physics materials and collision filters | done |
| 6 | Constraints, world anchor, per-pair collision exclusion | done |
| 7 | Event pump (activation + sensor/trigger events) | done |
| 8 | Trial-placement queries | done |
| 9 | Docs | done |
| 9 | End-to-end verification sweep | **REMAINING** |

Unit tests: 58, box3d backend only. Pure logic (hull builder, shape
descriptors, collision filter table, six-DOF classifier) plus world-level
behavior that steps a real `Box3d_world` (activation and sensor events,
trial-placement overlap queries).

## Building and verifying

All three scripts are committed, so they work on any machine with the usual
Windows toolchain:

```bat
scripts\configure_ninja_win_vulkan_box3d.bat -DERHE_BUILD_TESTS=ON
scripts\build_ninja_win_vulkan_box3d.bat editor
build_ninja_win_vulkan_box3d\bin\erhe_physics_tests.exe
```

Headless build for MCP-driven verification and `capture_screenshot`:

```bat
scripts\configure_vs2026_vulkan_headless_box3d.bat
cmake --build build_vs2026_vulkan_headless_box3d --target editor --config Debug
```

Kill any stale `editor.exe` before launching: it holds port 8080 and silently
eats MCP calls. Follow the `erhe-headless-verify` skill for the loop itself.

Note that `erhe_physics_tests` builds ONLY for this backend (the gate is at the
bottom of `src/erhe/physics/CMakeLists.txt`), so a jolt build tree will not
produce it.

## Remaining work -- Phase 9 end-to-end verification

None of this has been run. It is the phase most likely to surface a real bug
rather than confirm one absent, because it reaches areas the unit tests do not:
mass properties, glTF joint import, and teardown ownership.

Full MCP sweep on `build_vs2026_vulkan_headless_box3d`:

1. `create_shape` for box, sphere, capsule, tapered capsule, cylinder, tapered
   cylinder, convex hull and compound; `get_physics_items` confirms each shape
   type survived.
2. `toggle_physics`, step, `capture_screenshot`, and READ the resulting PNG.
3. **The key comparison.** Run the identical scene under
   `ERHE_PHYSICS_LIBRARY=jolt` and compare resting heights and pile shape. This
   is the strongest single signal that shape attachment and mass properties are
   right. Any systematic offset means a shape is the wrong size or the
   mass/inertia path is off.
4. Kinematic-physical `set_world_transform` induces a velocity while `teleport`
   does not.
5. Import a glTF with `KHR_physics_rigid_bodies` joints; confirm sensible joint
   selection, and that `log_physics` emits the expected warnings AND ONLY
   those.
6. Close a scene and grep `logs/log.txt` for `scene-close leak` --
   `Box3d_world` owns hulls, meshes, filter joints and the anchor body.
7. Re-run the jolt headless loop to confirm no regression.

## Remaining work -- interactive verification

These need a live windowed session with a person at the controls; the headless
MCP cannot drive them.

- **Align (Operations window).** The trial-placement queries landed in phase 8;
  before that the stubs always returned false, so the Align search accepted its
  first candidate placement unconditionally. Its search behavior has therefore
  effectively never run. The "Avoid joint pair" dropdown selects between the
  pairwise and whole-world queries (`operations_window.cpp`, the
  `avoid_whole_world` branch).
- **Activation flag behavior.** The activation callbacks fire for the first
  time as of phase 7, and they flip `no_transform_update` on nodes
  (`scene_root.cpp`). Worth exercising: dragging a sleeping body, and waking a
  body by collision.
- **Triggers/sensors** driven by real interaction rather than a stepped test
  world.

## Deferred by decision -- do NOT treat these as bugs

- **`IWorld::debug_draw` is a no-op for box3d.** Its signature names
  `erhe::renderer::Jolt_debug_renderer`, so making it backend-neutral was
  deferred. Box3D does have `b3World_Draw` + `b3DebugDraw`, so this is a wiring
  gap, not a capability gap. Doing it would mean: a neutral signature taking
  `erhe::renderer::Debug_renderer&` plus a settings struct, moving
  `jolt_debug_renderer` out of `erhe::renderer` into `erhe_physics/jolt/`
  (dropping `erhe::renderer`'s Jolt dependency), and deleting
  `App_context::jolt_debug_renderer` plus the `ERHE_PHYSICS_LIBRARY_JOLT` guards
  in `editor.cpp` and `debug_visualizations.cpp`. Around 11 files.
- **`save_state` / `restore_state`.** Box3D exposes no world snapshot API. They
  warn and do nothing, deliberately, rather than returning an empty `State` that
  appears to succeed and hands a future caller silent data loss.
- **Static friction is ignored.** `b3SurfaceMaterial` carries a single friction,
  so erhe's dynamic friction is the one that acts.

## Box3D behaviors worth knowing

Most of these cost a debugging session to find. They are grouped by where they
bite.

### Shapes and hulls

- **Box3D has no standalone shape object.** Shapes are created onto a body, and
  `b3CreateBakedCompoundShape` is rejected for anything but a static, non-sensor
  body. erhe's `ICollision_shape` is created before any body exists and is
  shared between bodies, so the backend's shapes are DESCRIPTORS that
  materialize onto a body via `attach_to_body()`.
- **The hull binding limit is EDGES, not vertices.** `b3CreateHull` rejects a
  hull whose half-edge count exceeds `2 * B3_MAX_HULL_EDGES` (256) long before
  the 128-vertex limit applies. For a UV sphere, E = L * (2A + 1); the
  tessellation constants are chosen against that formula with margin.
- **Latitude ring counts must be ODD** so a ring lands exactly on the equator.
  With an even count the widest sampled ring sits off the equator and every
  generated sphere and capsule comes out about 5% too thin.
- **Scale is applied before rotation**: p' = R * (S * p) + t, verified in
  `b3CloneAndTransformHull`. Composing a child under a scaled parent is exact
  only when the parent scale is uniform or the child adds no rotation;
  otherwise the true result is a shear, which no rigid body engine represents.
  Jolt has the same limitation.
- **A shape's creation transform and scale are BAKED into the shape** at create
  time (see the comment in `b3CreateTransformedHullShape`). So `b3Shape_GetHull`
  and friends return geometry already in the body frame -- which is why the
  overlap queries read the live shapes instead of keeping a parallel list.
- A tapered capsule is the convex hull of its two cap spheres. `b3CreateCone`
  is a different shape (flat caps) and asserts both radii are positive, so it
  cannot express the zero-radius cone erhe uses.

### Bodies and lifetime

- **`b3CreateBody` puts the body in the world immediately**, while erhe creates
  and adds as two steps. Bodies are created with `isEnabled = false` and enabled
  by `add_rigid_body()`.
- **Box3D destroys a body's joints along with the body.** Any joint bookkeeping
  must be purged BEFORE `b3DestroyBody` or a later re-enable passes a dangling
  `b3JointId`.
- Both erhe kinematic modes are Box3D kinematic bodies; they differ only in
  motion. `e_kinematic_physical` uses `b3Body_SetTargetTransform` (converts a
  pose delta into a velocity, Box3D's equivalent of Jolt's `MoveKinematic`);
  `teleport()` always uses `b3Body_SetTransform` and never induces a velocity.
- **Box3D has no `b3ComputeMeshMass`**, so a dynamic body containing a triangle
  mesh is reported and created static instead of simulating with no inertia.

### Filtering and materials

- **erhe's filter semantics are NOT expressible with Box3D's `b3Filter`
  category/mask.** That test is "the pair must share a bit"; erhe's deny list is
  the opposite. They agree only while every body belongs to a single collision
  system. Counter-example, now a test: a body in {debris, terrain} versus a
  filter denying debris must not collide, but any mask that gets the
  single-system cases right says it should.
- **`enableCustomFiltering` is a creation-time shape flag with NO runtime
  setter**, so every shape opts in. A body created without it could never be
  given a collision filter later without destroying and recreating its shapes.
- **`b3Shape_SetFilter` early-returns when the bits are unchanged**, and only a
  real change resets the shape proxy and re-evaluates existing contact pairs.
  `categoryBits` therefore encodes the compiled filter's identity, so assigning
  a different filter to a live body actually takes effect. Without this, whether
  a filter worked depended on whether the contact pair happened to form before
  or after the filter was assigned.
- **Box3D consults the custom filter only for awake dynamic bodies.** The proxy
  reset that `b3Shape_SetFilter` performs wakes them, which is what makes the
  change land.
- **The friction/restitution mixing callbacks take NO context pointer**
  ("this is called from a worker thread"), so the material lookup runs through a
  process-global registry keyed by `b3SurfaceMaterial::userMaterialId`. The
  custom FILTER callback, unlike those, does take a context.

### Joints

- **Joints require two valid bodies** (`src/joint.c` rejects `b3_nullBodyId`),
  so "constrain to world" anchors to a lazily created static, shapeless body at
  the origin. Joint frames are body-origin relative, so a world-space frame
  passes through it unchanged.
- **`b3JointDef` carries a `B3_SECRET_COOKIE`** in `internalValue` that
  `b3Default*JointDef()` sets and Box3D validates at joint creation. Building a
  local `b3JointDef` and assigning it over `joint_def.base` wipes that cookie
  and kills the process silently -- no exception, no minidump, the log just
  stops. Apply common fields ONTO the already-initialized base.
- **Frame conventions differ per joint type**: revolute rotates about frame Z,
  prismatic slides along frame X. The joint frames must be rotated to carry the
  selected erhe axis onto the axis Box3D expects. This is the easiest thing here
  to get silently wrong.
- Point-to-point softness comes from `b3JointDef::constraintHertz` /
  `constraintDampingRatio`, NOT from `b3SphericalJointDef`'s spring, which
  aligns the two frames' rotations rather than their positions.
- `b3CreateFilterJoint` exists precisely to disable collision between two
  bodies -- a direct mapping where the Jolt backend needs a collision sub-group
  hack.

### Events

- **Box3D buffers events in the world rather than calling listeners**, so
  `update_fixed_step()` reads them back after the step. It single-threads the
  step, so the callbacks are invoked straight out of Box3D's arrays -- no
  pending/dispatch marshalling like the Jolt backend needs, and no per-frame
  allocation.
- **There is no activation listener at all.** Activation is synthesized by
  diffing a per-body awake flag against `b3World_GetBodyEvents()`: only bodies
  that MOVED appear, and each carries `fellAsleep`. A body woken WITHOUT moving
  is therefore reported on its first moving step.
- **A disabled body has no velocity state.** Bodies are created disabled (erhe
  adds them to the world as a separate step), and Box3D keeps velocity only in
  the awake set's body state, so `b3BodyDef` velocities and
  `b3Body_SetLinearVelocity` on a disabled body are silently dropped.
  `Box3d_rigid_body` holds the velocity of a body outside the world and applies
  it on entry; `b3Body_Enable` wakes every non-static body, so a body at rest is
  put back to sleep there to enter the world asleep as `IWorld` requires.
- **Sensor touches are reported per SHAPE pair**, so a compound-shaped visitor
  produces one begin event per child shape. They are counted per BODY pair and
  only the 0 -> 1 and 1 -> 0 edges are emitted, matching Jolt. This was verified
  to be load-bearing: with the count check removed, the compound test sees 3
  events instead of 2.
- **`enableSensorEvents` must be set on BOTH shapes** and defaults to false even
  for sensors, so every shape opts in or triggers silently never fire.
- Sensor END events may name already-destroyed shapes; guard with
  `b3Shape_IsValid`. Removal is the ordered path: disabling a body ends its
  overlaps, but Box3D only reports that on the NEXT step, when the wrapper the
  `Trigger_event` names may be gone -- so `remove_rigid_body()` emits the exits
  itself and drops the entries.

### Queries

- **No Box3D query answers "would this penetrate by more than X".**
  `b3Body_OverlapShape` and the `b3Overlap*` family are boolean, and
  `b3ShapeDistance` reports distance 0 for ANY overlap however deep
  (`b3DistanceOutput::distance`, "zero if overlapped"). The pairwise manifold
  functions do: `b3ManifoldPoint::separation` is negative when penetrating.
- **The manifold functions fix the argument order by shape type**: the sphere is
  always B, the hull always A. The dispatch ranks the kinds and swaps the pair
  (inverting the transform) when needed.
- `b3LocalManifold::points` is a POINTER into a caller-owned buffer; set it and
  pass the capacity.
- **There is no public hull-versus-mesh manifold** (only
  `b3CollideHullAndTriangle`, with no public way to iterate a mesh's triangles),
  so a mesh can only be tested with the boolean `b3OverlapMesh` and the
  tolerance is ignored there. Warned once.
- A `b3ShapeProxy` is capped at `B3_MAX_SHAPE_CAST_POINTS` (64) points but a
  hull may carry up to `B3_MAX_HULL_VERTICES` (128), so a dense hull falls back
  to its bounding box for the mesh proxy -- a superset, hence conservative the
  same way the boolean test already is.

## Solver configuration

`b3World_Step` runs with 4 solver sub-steps (Box2D v3's documented default,
stable for stacking at 60 Hz) and `workerCount = 1`. Single-threaded on purpose:
driving Box3D's task callbacks would need a scheduler shared with erhe's, and
the editor steps physics on the main thread.
