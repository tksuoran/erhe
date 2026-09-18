# Physics: outstanding work

Status: in progress

This plan extends `doc/erhe/physics.md`, `doc/erhe/box3d_physics.md` and
`doc/erhe/khr_physics_rigid_bodies_support.md` with the work those documents do not
yet describe as shipping behavior.

## Box3D end-to-end verification sweep

This is the sweep most likely to surface a real bug rather than confirm one
absent, because it reaches areas the unit tests do not: mass properties, glTF
joint import, and teardown ownership. Run it over MCP on
`build_vs2026_vulkan_headless_box3d`.

1. `create_shape` for box, sphere, capsule, tapered capsule, cylinder, tapered
   cylinder, convex hull and compound; `get_physics_items` confirms each shape
   type survived.
2. `toggle_physics`, step, `capture_screenshot`, and READ the resulting PNG.
3. The key comparison: run the identical scene under
   `ERHE_PHYSICS_LIBRARY=jolt` and compare resting heights and pile shape. This
   is the strongest single signal that shape attachment and mass properties are
   right. A systematic offset means a shape is the wrong size or the mass and
   inertia path is off.
4. Confirm that kinematic-physical `set_world_transform` induces a velocity
   while `teleport` does not.
5. Import a glTF with `KHR_physics_rigid_bodies` joints; confirm sensible joint
   selection, and that `log_physics` emits the expected warnings and only
   those.
6. Close a scene and grep `logs/log.txt` for `scene-close leak`: `Box3d_world`
   owns hulls, meshes, filter joints and the anchor body.
7. Re-run the jolt headless loop to confirm no regression.

## Box3D interactive verification

These need a live windowed session with a person at the controls; the headless
MCP cannot drive them.

- **Align (Operations window).** Exercise the search behavior: it depends on
  the trial-placement queries, whose earlier stubs made the search accept its
  first candidate placement unconditionally. The "Avoid joint pair" dropdown
  selects between the pairwise and whole-world queries
  (`operations_window.cpp`, the `avoid_whole_world` branch).
- **Activation flag behavior.** The activation callbacks flip
  `no_transform_update` on nodes (`scene_root.cpp`). Exercise dragging a
  sleeping body, and waking a body by collision.
- **Triggers and sensors** driven by real interaction rather than a stepped
  test world.

## Interactive drag gaps

- An unjointed body flies off after a scripted drag, because the drag point
  keeps its kinematic velocity when the drag ends.
- The Box3D Physics tool rigid path asks for frequency 0, which is a 0 Hz joint
  and therefore does nothing.

## Editing gaps

- Cone creation tool parity with the other shape creation tools.
- A glTF exported from a scene with joints fails to re-import: the parse
  rejects `KHR_physics_rigid_bodies.physicsJoints[].limits`. Colliders and
  motions re-import correctly, so the defect is confined to the limits array.
