# KHR_physics_rigid_bodies support - status and continuation notes

Goal: full support for the glTF extensions KHR_physics_rigid_bodies and KHR_implicit_shapes
(spec copy: [`doc/KHR_physics_regid_bodies.md`](KHR_physics_regid_bodies.md)): simulate via the
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
| 6 | - | glTF export: `build_gltf_physics_data()` from shape introspection + `Gltf_exporter` physics pass + extensionsUsed | NOT started, blocked (see below) |
| 7 | - | Polish: trigger-event surfacing in editor UI, warning cleanup, notes.md updates, optional cone creation tool parity | NOT started |

Verification done: hinge constraint harness (Phase 3); scene save/load round-trip incl. v2
backward compat (Phase 4); real sample assets from
https://github.com/eoineoineoin/glTF_Physics import and simulate (JointTypes: 11 live
constraints; Materials_Friction; Filtering) (Phase 5).

## Former blocker for Phase 6: export hang (FIXED, verified)

`File > Save Scene` of the default scene used to hang (assert inside
`Free_list_allocator::free` at scope exit of `Gltf_exporter::process_geometry`, surfacing as
a hang). Fixed by 699cee0b (Buffer_mesh declaration-order use-after-free). Verified
2026-06-12 by driving the editor over its MCP server: `save_scene` of the default scene
writes the scene .json + companion .glb + geogram meshes instantly, `export_gltf` produces a
valid .glb, and `import_gltf` loads it back (16 -> 33 nodes). The MCP tools `save_scene` /
`export_gltf` / `import_gltf` (path arguments, no file dialog) were added for this and remain
available for Phase 6 round-trip verification.

## Design

- Editor-side `build_gltf_physics_data(scene) -> erhe::gltf::Gltf_physics_data` (new
  `src/editor/parsers/gltf_physics_export.{hpp,cpp}`): walk Node_physics / Node_joint
  attachments; collision shape introspection (`get_shape_type`, `get_inner_shape`,
  `get_offset`, `get_scale`, ...) -> implicit shapes deduped into the top-level array;
  convex hull / mesh shapes -> `geometry.node` references; compound shapes and non-Y shape
  axes -> synthesized child collider nodes; OCOM / scaled wrappers unwrap into
  `motion.centerOfMass` / node scale; shared items dedup into top-level arrays.
- `Gltf_exporter` (src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp) gains an optional
  `const Gltf_physics_data*`: record erhe-node -> gltf-node-index map in process_node, then a
  physics pass fills `asset.shapes` / `physicsMaterials` / `collisionFilters` /
  `physicsJoints` + per-node `node.physicsRigidBody`, and pushes both extension names into
  `extensionsUsed` (not extensionsRequired). The pinned fastgltf exporter already writes all
  extension JSON.
- Call sites: `operations_window.cpp` File > Export passes built data;
  `scene_serialization.cpp` companion-.glb save keeps passing nullptr (scene JSON remains the
  canonical physics store).
- Export asymmetries noted by the Phase 5 import work: import folds descendant colliders into
  one compound (export must synthesize child nodes from `get_children()` transforms); import
  bakes child world scale into shapes; velocities import node->world so export must rotate
  world->node; COM (OCOM) is the outermost wrapper - unwrap first.

## Known limitations (documented behavior)

- The pinned fastgltf (a31be255) implements an older spec revision: collider geometry uses the
  `"node"` key; the current spec draft and the official sample assets use `"mesh"`.
  Mesh-keyed colliders are skipped on import with a warning. Fix by bumping the fastgltf pin
  when a `mesh`-supporting commit exists; erhe round-trips with itself in the meantime.
- glTF physics material / filter / joint names do not round-trip (the fastgltf types carry no
  name fields); the importer synthesizes "Physics material N" style names.
- Plane shapes: not representable at the pinned fastgltf commit (Shape variant lacks plane).
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
