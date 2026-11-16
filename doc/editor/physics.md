# physics/

Stability: stable

## Purpose

Physics-related tools, UI, and collision shape generation for the editor.

## Key Types

- **`Physics_tool`** -- A `Tool` for interacting with physics objects. Three modes:
  - **Drag** -- grab and drag rigid bodies using a physics constraint (point-to-point)
  - **Push** -- apply force in the pointing direction
  - **Pull** -- apply force toward the pointer

  Manages damping overrides and visual feedback (debug lines); the drag constraint itself is a `Physics_drag_constraint`. Supports both desktop mouse and XR controller input.

  The drag start (`begin_drag()`, shared by the right-drag's `acquire_target()` and `begin_scripted_drag()`) picks the pull by the grabbed body: a dynamic body held by a live joint (`Scene_root::is_jointed_rigid_body()`) gets the jointed-body spring (`make_jointed_body_drag_settings()`, the same pull as the Transform tool's drag) with its pivot at the center of mass, no damping / friction / gravity overrides, no grab-time velocity reset and no extra per-frame velocity damping; its drag target is the grab point's goal offset by the current grab point -> center of mass vector. Every other body gets the rigid (frequency 0, unbounded) grab at the grabbed point with the tool's overrides and extra damping, its drag point moved as kinematic.

  Scripted drags (`begin_scripted_drag()` / `step_scripted_drag()` / `release_target()`) run a Drag mode drag from code through the same drag start, per-frame goal (`apply_drag_goal()`) and release; the MCP `physics_drag` tool steps one goal per frame through its deferred request (grab_point, default the body's center of mass; target or translation; frames; release false holds the drag until `action: "release"`). A scripted drag refuses pointer drags and survives hover view changes. Scene close and removal of the dragged mesh release the drag.

  `tool_render()` casts a ray down from the dragged mesh with `project_ray()`, which steps over the mesh's own hits without editing the scene.

- **`Physics_drag_constraint`** -- The pull of an interactive physics drag, shared by the Physics tool's right-drag and the Transform tool's drag of a jointed dynamic body (`transform/physics_driven_drag.hpp`): a collisionless kinematic drag point body plus a point-to-point constraint from a pivot on the dragged dynamic body to it (`Physics_drag_constraint_settings`: frequency 0 = rigid, else a spring with damping ratio and max force; optional Jolt solver iteration overrides). `attach()` wakes the dragged body and keeps it awake (`begin_move`), `move_drag_point()` teleports or kinematically moves the drag point, `detach()` removes constraint then drag point body and lets the body sleep again; the dragged body keeps its velocity. The owner detaches on scene close / removal of the dragged node, before the world or body goes away.

  `make_jointed_body_drag_settings(mass)` is the one definition of the jointed-body pull: 10 Hz, critically damped, force bounded to 3x the body's weight, Jolt island iterations 40 velocity / 20 position, `Drag_point_speed::braking` with a limit margin of 0.1 rad.

  Braking drag point: the drag registers with its `Scene_root` at `attach()` (`register_physics_drag`), and `Scene_root::update_physics_simulation_fixed_step()` calls `on_fixed_step(dt)` before each world step, which advances the drag point body along the joint reach (`Joint_reach::step_toward`) toward the projected target by at most `v_max * dt`, `v_max = sqrt(2 * (max_force / mass - g) * d)`, `d` = reach radius * limit margin for a limited hinge, else 0.05 m. The projected target of a limited hinge stays the margin inside the range. So the bounded pull can always brake the body before a joint limit: without the cap a jumping drag target (fast cursor) accelerated a 2 kg ball on a +-80 deg limited hinge to 4-6 m/s into the limit (12-25 mm hinge separation). A limited hinge of radius 0.64 m gets `v_max` 1.585 m/s; the Newton's cradle hinges are unlimited and get 1.40 m/s. An immediate drag (the unjointed Physics tool grab) places the drag point in `move_drag_point()`.

  Joint-space projection: `attach()` takes the scene's `Joint_system` entries and builds an `erhe::physics::Joint_reach` when exactly one live joint holds the dragged body and its other side is the world or a non-dynamic body (anchor frame captured at attach). `move_drag_point()` then projects every requested position onto what the joint lets the pivot reach - circle (one free / ranged rotation axis, range applied), sphere (two or three, angular limits not applied), box (translation only, per-axis ranges), point (nothing moves the pivot) - so the pull never fights the joint; a target without a unique nearest position keeps the last projected one. Several joints on the body, a joint to a dynamic body, and limit combinations `Joint_reach` does not handle (translation and rotation both movable, a rotation fixed at a non-zero angle) leave the drag point unprojected. `get_projection_description()` names the choice and `get_requested_drag_point()` / `get_projected_drag_point()` report the last move (the MCP `physics_drag` tool returns all three). The force bound stays as the safety net.

  Regression check: `scripts/physics_drag_joint_sweep.py` builds the Newton's cradle (creation 21) in a running editor, drags its balls through `drag_selection` and `physics_drag` (slow, fast, out-of-plane, slack and diagonal targets) and fails when a hinge's two anchors separate by more than a threshold (default 1 mm) during a hold or after release.

- **`Physics_window`** -- ImGui window for physics simulation settings (gravity, time step, debug visualization toggles).

- **`Collision_volume_calculator`** / **`Collision_shape_generator`** -- Function types used by `Brush` to compute collision volumes and create `ICollision_shape` instances from geometry. Defined in `collision_generator.hpp`.

## Collision shapes follow the node's scale

A body carries no scale: the physics backend places it by the node's world
rotation and translation. The scale lives in the body's shape. A node's
`Node_physics_system` entry keeps the source shape (in node space, as a brush,
a mesh hull or a single imported collider gives it) and the body's shape is
`make_node_scaled_collision_shape(source, |world scale|)` - the source itself
at scale 1, a uniform scaling wrapper for a uniform scale, else a non-uniform
scaled wrapper - wrapped in the center-of-mass offset the node's value states.

The shape follows every change of the node's world scale: a Transform tool
drag (each frame of the drag, its undo, redo and cancel), `set_node_transform`,
a `scale` property write, an ancestor's scale, a reparent, a paste or
duplicate, a mirror and a load. All of these reach the node's transform
observer, which queues the body for the shape pass
(`Node_physics_system::apply_pending_shape_scales()`, each frame before the
simulation steps, also while the simulation is off); the pass compares the
node's world scale against the scale the body's shape was built for and, when
it differs, builds the shape from the source again and swaps it into the live
body
(`IRigid_body::set_collision_shape()`: the broadphase bounds follow,
constraints stay). A frame without a scale change builds nothing. The source is
never wrapped twice, so scaling back to 1 gives the source object itself.

A folded import compound (colliders of descendant nodes) already carries the
world scales of its colliders; it is handed over with the root scale it was
built at (`set_collision_shape(node, shape, source_scale)`) and scaled by the
ratio.

A scale the backend cannot represent on the source (`is_valid_scale()`: under
Jolt a sphere or capsule takes only a uniform scale, a cylinder only an equal
scale across its axis) replaces the collider: a sphere or capsule collider
under a non-uniform scale becomes the convex hull of the node's own mesh at
that scale, which is exact for the geometry the node shows. A source without a
mesh to fall back on (an implicit imported collider on a node without
geometry, or a compound whose rotated child cannot take a non-uniform scale)
takes the backend's nearest valid scale and the node is named in a warning. A
scale with a zero component gives the body an empty shape: nothing collides
with it, and a dynamic body without an explicit mass then weighs 1 and falls
through everything. The pass takes the scene's item host mutex before the
physics step mutex, since the hull fallback reads mesh primitives a brush
geometry worker may replace.

Mass: without a `Node_physics.mass` value the body's mass is its shape volume
times the material density, so it follows the scaled shape, and a save writes
no mass for it, so it stays derived after a reload. An explicit mass - an
authored value, a value a brush of a density other than the default writes
for its placed volume, or a mass in a file saved before this rule - stays as
written and does not follow the scale; the inertia follows the new shape. A
brush of the default density writes no mass value. The body -> node writeback
of a dynamic body keeps the node's world scale.

The shape arguments of the MCP `create_physics_body` and `edit_physics_body`
tools (`half_extents`, `radius`, `length`, ...) are in node space: the body's
shape is that shape at the node's world scale.

The direction for static level geometry is collision baked in world space,
after which node transforms no longer matter; until then the shapes follow
the scale as above.

## Public API / Integration Points

- `Physics_tool::acquire_target()` / `release_target()` -- begin/end physics interaction
- `Physics_tool::set_mode()` -- switch between drag/push/pull
- Collision generators are passed to `Brush_data` for physics-enabled brush placement

## Dependencies

- erhe::physics (IWorld, IRigid_body, IConstraint, ICollision_shape)
- erhe::raytrace (for picking)
- editor: App_context, Tools, Scene_view, Node_physics
