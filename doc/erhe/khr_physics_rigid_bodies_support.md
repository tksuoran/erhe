# KHR_physics_rigid_bodies support

Stability: mostly stable

erhe supports the glTF extensions `KHR_physics_rigid_bodies` and
`KHR_implicit_shapes`
(spec: https://github.com/eoineoineoin/glTF_Physics/tree/master/extensions/2.0/Khronos/KHR_physics_rigid_bodies)
on both the import and the export side: the described bodies simulate through
the Jolt wrapper, are exposed and edited in the editor UI, persist in erhe's
scene serialization, and round-trip through glTF.

The support is full-featured: joints carry per-axis limits and drives (via Jolt
`SixDOFConstraint`); physics materials, collision filters and joint settings are
shared content-library assets; triangle-mesh colliders use Jolt `MeshShape` and
are therefore static or kinematic only.

## What carries the data

- `erhe::physics` provides the shapes the extensions need: tapered cylinder,
  mesh, scaled and centre-of-mass-offset shapes, with create-info velocities, a
  gravity factor and `is_sensor`. A mass of 0 means infinite mass.
- `Physics_material`, `Collision_filter` and `Physics_joint_settings` are
  shared items. The Jolt contact listener combines friction and restitution in
  the precedence the spec states; collision filters are data-driven
  (`GroupFilter`, 64-system bitsets, pair exclusion); `IWorld` reports trigger
  enter and exit events.
- `Six_dof_constraint_settings` is the generic six-DOF constraint (frames in
  body node space; axes 0..2 translation, 3..5 rotation), mapped to
  `JPH::SixDOFConstraint` with limits, translation soft limits and
  position / velocity motors.
- The editor holds physics materials, collision filters and joint settings in
  content-library folders, exposes every `Node_physics` accessor (material,
  filter, trigger, gravity factor, initial velocities, centre-of-mass offset)
  and carries a `Node_joint` attachment with a `Scene_root` constraint retry.
- `erhe::scene::Physics_description`
  (`erhe_scene/physics_description.hpp`) is the plain-data carrier both formats
  read and write; `parse_physics()` fills it from glTF and
  `parsers/gltf_physics_import.*` maps it onto the editor's items (body roots,
  compound folding, implicit-shape table, hull and mesh colliders, triggers,
  joints).

## Authoring without an import

`Scene_commands::create_new_rigid_body` and `create_new_joint` are undoable
(`Node_attach_operation`) and reachable from the `scene.create_new_rigid_body`
and `scene.create_new_joint` commands, the Create menu, and the item-tree
context menu ("Attach > Rigid Body / Joint"; a joint auto-connects to another
selected node). The Create menu also creates Physics Material, Collision Filter
and Joint Settings content-library items. An edit of a shared item reaches
the live simulation through the observers `Node_physics` and `Node_joint`
subscribe to it. The MCP tools are `get_physics_items`,
`create_physics_body` / `edit_physics_body`, `create_physics_joint` /
`edit_physics_joint`, `create_physics_material` / `edit_physics_material`,
`create_collision_filter` / `edit_collision_filter`,
`create_physics_joint_settings` / `edit_physics_joint_settings`, plus the
physics fields of `get_node_details`.

## Export design

- `build_gltf_physics_data(scene) -> erhe::scene::Physics_description`
  (`src/editor/parsers/gltf_physics_export.{hpp,cpp}`) walks `Node_physics` and
  `Node_joint` attachments. Collision shape introspection dedups implicit shapes
  into the top-level array; convex hull and mesh shapes become mesh-keyed
  `geometry.mesh` references (the current spec). Compound shape children,
  non-Y shape axes and wrapper scales that differ from the node world scale
  become `Physics_description::synthesized_colliders`: extra glTF child nodes
  the exporter creates, with scale `wrapper_scale / parent_world_scale`. An
  offset-centre-of-mass wrapper unwraps into `motion.centerOfMass`; velocities
  rotate from world into node space; shared materials, filters and joint
  settings dedup by item pointer into the top-level arrays.
- `Gltf_exporter` (`src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp`) takes an
  optional `const erhe::scene::Physics_description*`. `process_node` records an
  erhe-node to glTF-node-index map, and `process_physics()` runs before
  `combine_buffers` (mesh-keyed geometry may export meshes on demand) to fill
  `asset.shapes`, `physicsMaterials`, `collisionFilters`, `physicsJoints`, the
  per-node `node.physicsRigidBody`, and the synthesized collider child nodes
  (a compound trigger becomes a node-list trigger over its synthesized
  children). Both extension names go into `extensionsUsed`, not
  `extensionsRequired`.
- The call sites are `operations_window.cpp` (File > Export), the MCP
  `export_gltf` tool and scene save (`save_scene_gltf` in `parsers/gltf.cpp`).
  The scene file itself is the canonical physics store
  (`doc/editor/scene_serialization.md`; the erhe-specific remainder rides in the
  `ERHE_physics` node extension).
- Import folds synthesized children back: a dynamic body through the
  motion-root compound fold, a static synthesized child as an individual static
  body, which is physically equivalent.

## Verifying

- The sample assets at https://github.com/eoineoineoin/glTF_Physics import and
  simulate: `JointTypes` (11 live constraints), `Materials_Friction`,
  `Filtering`, `ShapeTypes` and `Triggers`. `ShapeTypes` imports 10 bodies plus
  2 triggers, `Triggers` 3 triggers, `JointTypes` 10 joint settings plus 11
  joints, all with mesh-keyed colliders, and each re-exports with materials,
  filters, joints, drives, triggers and implicit-shape dedup intact.
- The round-trip is driven over the editor's MCP server with explicit paths
  (`export_gltf`, `import_gltf`, `save_scene`), so no file dialog is involved.
  The default scene exports valid JSON declaring both extensions, and a `.glb`
  round-trip re-imports 7 bodies from 13 glTF rigid-body nodes (the synthesized
  compound children fold back).

## Known limitations

- fastgltf is pinned to the fork `tksuoran/fastgltf` (branch
  `khr_physics_rigid_bodies`), which carries the spec-compliance fixes erhe
  needs: mesh-keyed collider geometry for both parse and write, the spec
  inertia key names, the missing member initializers (`convexHull`, combine
  modes, drive `maxForce` and targets) and the exporter JSON fixes (extension
  name, booleans, motion arrays, the rigid-body close brace, `collisionFilters`
  commas, omitting an infinite `maxForce`). The root `CMakeLists.txt`
  `CPMAddPackage` comment states when the fork can be dropped for
  spnda/fastgltf. CPM `PATCHES` is banned repo-wide (AGENTS.md), so the fork is
  how the fixes travel.
- The fastgltf physics material, filter and joint types carry no name field.
  Names ride the `ERHE_scene` `physics_materials` / `physics_joints` /
  `collision_filter_names` entries in erhe-authored files, as do the local
  property values of a physics material and of a joint-settings item; for a
  foreign file the importer synthesizes "Physics material N" style names.
- Plane shapes are not representable in fastgltf (its `Shape` variant has no
  plane, and the fork does not add one).
- Export skips a compound child that carries a convex hull or mesh shape, with
  a warning: the baked compound holds no source mesh reference. A direct hull
  or mesh shape references the mesh it was built from
  (`Node_physics::collision_mesh`; no value means the body's own mesh). When
  that mesh is a node below the body, the collider is exported on that node and
  the body keeps the motion, which is the `KHR_physics_rigid_bodies` rule that
  a collider belongs to its nearest ancestor body.
- Export skips a world-attached joint (one with no connected node) with a
  warning, and exports only the first of several `Node_joint`s on one node,
  because glTF carries one joint per node.
- Export writes no inertia overrides (`Node_physics` does not expose them), and
  writes mass for dynamic bodies only.
- Export collapses kinematic non-physical and kinematic physical to
  `isKinematic = true`, so a re-import yields a kinematic physical body.
- The text (`.gltf`) export variant writes no buffer URI and therefore cannot
  be re-imported; use `.glb` for a round-trip and `.gltf` for JSON inspection.
- Friction is dynamic friction: there is no velocity-threshold selection
  between static and dynamic friction.
- Acceleration-mode drives are approximated as force mode, warned once.
- Multi-axis joint limits are applied per axis, a box approximation of radial
  limits, warned.
- Angular soft limits fall back to a hard limit with a warning, because Jolt
  `SixDOF` springs are translation-only.
- A world interns at most 64 collision system strings (uint64 bitsets).
- A compound body is baked at import or at creation; editing a child node
  transform does not rebuild the compound.
- A sleeping body resting inside a sensor fires a trigger exit, and an enter
  again on wake. This is standard Jolt sensor behavior.
- A joint settings edit takes effect when "Rebuild Joint" is pressed on each
  using `Node_joint`.

## Future work

- [plans/physics.md](../plans/physics.md) - cone creation tool parity and the
  remaining physics editing gaps.
