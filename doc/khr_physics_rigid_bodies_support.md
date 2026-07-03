# KHR_physics_rigid_bodies support - status and continuation notes

Goal: full support for the glTF extensions KHR_physics_rigid_bodies and KHR_implicit_shapes
(spec: https://github.com/eoineoineoin/glTF_Physics/tree/master/extensions/2.0/Khronos/KHR_physics_rigid_bodies): simulate via the
Jolt wrapper, expose and edit in the editor UI, persist in editor scene serialization, and
round-trip through glTF import/export.

Confirmed scope decisions: glTF import AND export; full joints (per-axis limits + drives via
Jolt SixDOFConstraint); physics materials / collision filters / joint settings as shared
content-library assets; triangle-mesh colliders via Jolt MeshShape (static/kinematic only).

## Status (2026-06-12)

| Phase | Commit | Content | State |
|---|---|---|---|
| 1 | `6c7c479d` | erhe::physics: tapered cylinder / mesh / scaled / COM-offset shapes; create-info velocities, gravity factor, is_sensor; mass==0 = infinite-mass convention | done |
| 2 | `fecd9665` | Shared `Physics_material` / `Collision_filter` / `Physics_joint_settings` items (Item_type bits 38-40); Jolt contact-listener friction/restitution combine (spec precedence); data-driven collision filters (GroupFilter, 64-system bitsets, pair exclusion); trigger enter/exit events on IWorld | done |
| 3 | `45e406d2` | Generic six-DOF constraint: `Six_dof_constraint_settings` (frames in body node space; axes 0..2 translation, 3..5 rotation) -> JPH::SixDOFConstraint with limits, translation soft limits, position/velocity motors | done |
| 4 | `cd5bc619`, `13a1fdcb` | Editor: content-library folders (physics_materials, collision_filters, physics_joints); Node_physics accessors (material, filter, trigger, gravity factor, initial velocities, COM offset); new `Node_joint` attachment with Scene_root constraint retry; scene JSON serialization (Scene_file v3, Node_physics_data v2, Collision_shape_data v3 + new defs); properties UI for all of it | done |
| 5 | `6fa07a4a` | glTF import: extension bits enabled, `erhe_gltf/gltf_physics.hpp` plain-data carrier, `parse_physics()`, editor mapping `parsers/gltf_physics_import.*` (body roots, compound folding, implicit-shape table, hull/mesh colliders, triggers, joints) | done |
| 5.5 | - | fastgltf spec-compliance patch (`cmake/patches/fastgltf_khr_physics.patch` via CPM `PATCHES`): mesh-keyed collider geometry (current spec) for parse + write, spec inertia key names, missing member initializers (convexHull, combine modes, drive maxForce/targets), exporter JSON fixes (extension name, booleans, malformed motion arrays, missing rigid-body close brace, collisionFilters trailing commas, omit infinite maxForce). Editor side: mesh-keyed import via `build_shape_from_mesh()` (builds Geometry from Triangle_soup on demand) | done |
| 6 | - | glTF export: `parsers/gltf_physics_export.*` `build_gltf_physics_data()` (shape introspection, wrapper unwrap, shared item dedup, synthesized child colliders) + `Gltf_exporter` physics pass + extensionsUsed | done |
| 7 | - | Polish: trigger events surfaced in the Physics window (bounded per-scene log on Scene_root fed by the IWorld trigger callbacks; count also in MCP list_scenes), joint warnings identify settings + node (were empty Node_joint names), notes.md updates. Cone creation tool parity remains optional/not done | done |
| 8 | - | In-editor creation (no import needed): Scene_commands::create_new_rigid_body / create_new_joint (undoable Node_attach_operation; commands scene.create_new_rigid_body / scene.create_new_joint, Create menu); item-tree context menu "Attach > Rigid Body / Joint" (joint auto-connects to another selected node); Create menu entries for Physics Material / Collision Filter / Joint Settings content-library items (operations_window); Node_physics::set_collision_shape; shared reapply/rebuild helpers in scene/physics_edits.{hpp,cpp}; MCP tools: get_physics_items, create/edit_physics_body, create/edit_physics_joint, create/edit_physics_material, create/edit_collision_filter, create/edit_physics_joint_settings, plus physics details in get_node_details | done |

Verification done: hinge constraint harness (Phase 3); scene save/load round-trip incl. v2
backward compat (Phase 4); real sample assets from
https://github.com/eoineoineoin/glTF_Physics import and simulate (JointTypes: 11 live
constraints; Materials_Friction; Filtering) (Phase 5). Phase 6 verified over the editor MCP
tools (export_gltf / import_gltf with explicit paths): default scene exports valid JSON with
both extensions declared (checked with a JSON parser); .glb round-trip re-imports 7 bodies
from 13 glTF rigid-body nodes (synthesized compound children fold back); official samples
ShapeTypes / Triggers / JointTypes import with mesh-keyed colliders (10 bodies + 2 triggers;
3 triggers; 10 joint settings + 11 joints) and re-export with materials, filters, joints,
drives, triggers and implicit-shape dedup intact.

## Former blocker for Phase 6: export hang (FIXED, verified)

`File > Save Scene` of the default scene used to hang (assert inside
`Free_list_allocator::free` at scope exit of `Gltf_exporter::process_geometry`, surfacing as
a hang). Fixed by 699cee0b (Buffer_mesh declaration-order use-after-free). Verified
2026-06-12 by driving the editor over its MCP server: `save_scene` of the default scene
writes the scene .json + companion .glb + geogram meshes instantly, `export_gltf` produces a
valid .glb, and `import_gltf` loads it back (16 -> 33 nodes). The MCP tools `save_scene` /
`export_gltf` / `import_gltf` (path arguments, no file dialog) were added for this and remain
available for Phase 6 round-trip verification.

## Design (as implemented)

- Editor-side `build_gltf_physics_data(scene) -> erhe::gltf::Gltf_physics_data`
  (`src/editor/parsers/gltf_physics_export.{hpp,cpp}`): walks Node_physics / Node_joint
  attachments; collision shape introspection -> implicit shapes deduped into the top-level
  array; convex hull / mesh shapes -> mesh-keyed `geometry.mesh` references (current spec);
  compound shape children, non-Y shape axes and wrapper scales differing from the node world
  scale -> `Gltf_physics_data::synthesized_colliders` (extra glTF child nodes created by the
  exporter, scale = wrapper_scale / parent_world_scale); OCOM wrapper unwraps into
  `motion.centerOfMass`; velocities rotate world -> node space; shared materials / filters /
  joint settings dedup by item pointer into top-level arrays.
- `Gltf_exporter` (src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp) takes an optional
  `const Gltf_physics_data*`: process_node records an erhe-node -> gltf-node-index map, then
  process_physics() (before combine_buffers, since mesh-keyed geometry may export meshes on
  demand) fills `asset.shapes` / `physicsMaterials` / `collisionFilters` / `physicsJoints`,
  per-node `node.physicsRigidBody`, synthesized collider child nodes (compound triggers
  become node-list triggers over their synthesized children), and pushes both extension
  names into `extensionsUsed` (not extensionsRequired).
- Call sites: `operations_window.cpp` File > Export and the MCP `export_gltf` tool pass
  built data; `scene_serialization.cpp` companion-.glb save keeps passing nullptr (scene
  JSON remains the canonical physics store).
- Import folds synthesized children back: dynamic bodies via the motion-root compound fold;
  static synthesized children become individual static bodies (physically equivalent).

## Known limitations (documented behavior)

- The fastgltf pin (a31be255, the latest upstream commit) needs
  `cmake/patches/fastgltf_khr_physics.patch` (applied by CPM at configure time) for current
  spec compliance; see the Phase 5.5 table row. Drop the patch when upstream catches up.
  When iterating on the patch note that the CPM cache key hashes the patch path, not its
  content: delete `.cpm_cache/fastgltf/<new-hash>` after editing the patch file.
- glTF physics material / filter / joint names do not round-trip (the fastgltf types carry no
  name fields); the importer synthesizes "Physics material N" style names.
- Plane shapes: not representable in fastgltf (Shape variant lacks plane; not added by the
  patch).
- Export: compound children with convex hull / mesh shapes are skipped with a warning (the
  baked compound carries no source mesh reference); direct hull / mesh shapes reference the
  owning node's mesh.
- Export: world-attached joints (no connected node) are skipped with a warning; multiple
  Node_joints on one node export only the first (glTF carries one joint per node).
- Export: inertia overrides are not exported (Node_physics does not expose them; matches the
  scene .json, which also drops them); mass is exported for dynamic bodies only.
- Export: kinematic non-physical vs physical is collapsed to isKinematic=true (re-import
  yields kinematic physical).
- The text (.gltf) export variant writes no buffer URI, so it cannot be re-imported
  (pre-existing exporter property; use .glb for round-trips, .gltf for JSON inspection).
- Friction: dynamic friction only for now (no velocity-threshold static/dynamic selection).
- Acceleration-mode drives are approximated as force mode (warn once).
- Multi-axis joint limits are applied per axis (box approximation of radial limits; warn).
- Angular soft limits: Jolt SixDOF springs are translation-only; angular stiffness falls back
  to a hard limit with a warning.
- Collision systems: at most 64 interned system strings per world (uint64 bitsets).
- Compound bodies are baked at import/creation; editing a child node transform does not
  rebuild the compound.
- A sleeping body resting inside a sensor fires a trigger exit (and enter again on wake) -
  standard Jolt sensor behavior.
- Joint settings edits require pressing "Rebuild Joint" on the using Node_joint(s).
