# World-space bake of static meshes

Stability: experimental

A static mesh node's vertices can be transformed into world space and the
node moved under one "World Space" node at the scene root with the identity
transform, so level of detail selection (`doc/editor/lod.md`) and the
renderers see no node scale. Prepare (PC / Quest) does it at export time
(the live scene stays as it is); the undoable Bake Static To World Space
operation does it on the live scene. Both share the plan and the vertex
transform, and both read the scene's export settings.

## Export settings

`Scene_export_settings` (scene codegen unit,
`src/editor/scene/definitions/scene_export_settings.py`) is the field
`export_settings` of `Scene_settings` (v2), saved through `ERHE_scene`
(`doc/gltf_extensions/ERHE_scene.md`) and shown in the scene's Properties
"Export" group:

| Field | Type | Default | Meaning |
|---|---|---|---|
| `lod` | `Optional(Lod_config)` | null | LOD request Prepare generates chains with for primitives without one; null: the editor config `lod` |
| `bake_static_to_world_space` | `Bool` | true | Prepare writes static mesh nodes in world space |
| `instancing` | `Bake_instancing` | `duplicate` | What happens to a primitive shared by several baked meshes (below) |
| `instancing_budget_mib` | `Int` | 256 | Budget of `duplicate_within_budget` |
| `pvs_region_min`, `pvs_region_max`, `pvs_cell_size` | | | PVS bake grid (`doc/editor/pvs.md`) |

A v1 settings object (an `ERHE_scene` file, USD `customLayerData`, MCP
`set_scene_settings`) carries the PVS fields at the top level;
`migrate_scene_settings_json()` (`scene/scene_settings_resolve.cpp`) moves
them into `export_settings` before the codegen parse.

`Bake_instancing`:

- `skip`: a mesh with a primitive shared by another mesh keeps its transform.
- `duplicate`: every baked mesh gets its own world-space copy.
- `duplicate_within_budget`: duplicate while the estimated duplicated vertex
  and index bytes (64 bytes per vertex, 4 per index) stay within
  `instancing_budget_mib`, then skip.

## Which meshes are baked

`plan_world_space_bake()` (`src/editor/scene/world_space_bake.{hpp,cpp}`)
admits a mesh that:

- is content, not skinned, not a skin joint and carries no physics (collider
  shapes live in the node's space);
- has no ancestor (itself included) that is an animation target, moved by a
  rigid body of a motion mode other than none or static, or written as an
  external asset reference;
- has only soup-backed primitives (a primitive with authored geometry is
  written as `ERHE_geometry`, not prepared);
- is admitted by the instancing setting.

The plan carries per baked mesh its world transform relative to the root
node, and `kept_reasons` (reason -> count) for the rest; the save logs them
and the operation reports them.

## Baking a primitive

Positions by `world_from_node`; normals and tangent xyz by its inverse
transpose, renormalized; tangent w and the triangle winding flip when the
determinant is negative. Bounds, content hash and BVH are rebuilt from the
baked data, and the LOD chain is generated in world space (a stored chain is
object space and is not reused). Triangles that become degenerate (positions
bitwise equal after the transform) are dropped, as the content build drops
them. Prepared data: `erhe::primitive::bake_prepared_primitive_transform()`;
a soup: `erhe::primitive::transform_triangle_soup()`.

## Flags of a baked mesh

A baked mesh and the World Space node carry the transform channel locks
(`lock_translation_*`, `lock_rotation_*`, `lock_scale_*`), persistent flags
written by name in `ERHE_node`, so interactive editing does not move them.
The baked mesh's path from the scene root is the identity, so it carries
the derived `Item_flags::world_space` and draws with the
`WORLD_SPACE_VERTICES` shader variants (`doc/erhe/shader_variants.md`
"World-space vertices"); a mesh moved anyway (locks cleared) draws through
its node transform, and a reset back to the identity draws it as
world-space again.

## Placeholders

A baked mesh moves out of its parent; every other node stays where it is
with its transform. When something stays below the baked mesh - a child
that is not baked, or a baked child that leaves a placeholder itself - a
transform-only node with the mesh's name and transform takes the mesh's
place and holds those children. A baked mesh whose children are all baked
leaves nothing behind (Bistro: 2670 baked meshes, 0 placeholders).

## Prepare (export)

`Gltf_export_arguments::world_bake` (`Gltf_export_world_bake`) carries the
plan. The exporter prepares each shape once, then per baked mesh primitive
bakes a copy of the prepared core, generates its LOD chain and packs its GPU
streams (`prepare_world_baked_primitives()`). The node pass writes each baked
mesh under the World Space node with a glTF mesh of its own, and a
placeholder only when a child is written under it, with the locks of
"Flags of a baked mesh" on the baked meshes and the World Space node; a
mesh whose primitives did not all prepare is written unbaked in place. The MCP save status
`prepared` block reports `world_bake_mesh_count`,
`world_bake_placeholder_count`, `world_bake_primitive_count` and
`world_bake_failed_count`. The baked node keeps its glTF uid, so PVS columns
(`doc/editor/pvs.md` "Terms") follow it.

Names: the export writes the baked meshes under the World Space node with
the names they have; siblings that share a name there are renamed by the
load (`<name>_1`, ...), as the operation renames them. Names are not
identities (the uid is), so the export does not rename them itself.

## Bake Static To World Space operation

`Bake_world_space_operation` (`src/editor/operations/
bake_world_space_operation.{hpp,cpp}`), Mesh > Bake Static To World Space
(the scene's export settings) and MCP `bake_static_to_world_space` (an
`export_settings` object merged over them for the call;
`doc/agents/mcp_server_usage.md`).

- The constructor plans and records the entries, sorted parents first; the
  first `execute()` builds the world-space primitives, one executor task per
  mesh (each copy is a new shape; distinct shapes build concurrently, as in
  `doc/editor/async_asset_loading.md`). A mesh whose copy cannot be built
  keeps its transform (logged, in `kept_reasons`). Bistro, Release: about
  1.7 s from the request to the operation done.
- `execute()`, per entry, parents first: the mesh moves under World Space
  (world transform kept, children riding along), the placeholder takes its
  place in its parent, the children move into the placeholder keeping their
  local transforms, and the mesh's transform becomes the identity. A baked
  child is therefore taken from its parent's placeholder. The World Space
  node is created when the root has no identity Xform of that name, and
  the locks of "Flags of a baked mesh" are set.
- `undo()` runs the inverse of each step in reverse order: primitives,
  parent and position the step recorded, transform, the placeholder's
  children back into the mesh, the placeholder removed, the name restored
  (renamed beside a sibling of the same name), and the locks the bake set
  cleared. The tree and the locks are restored exactly.
- A mesh that carries `Item_flags::world_space` (its path is the identity
  already) is not planned.

## Verification

Headless Release editor (`doc/agents/editor_runs.md`):

- Bistro (`res/editor/assets/bistro_lights_compressed/bistro_lights_c.gltf`,
  `load_scene`): `bake_static_to_world_space` plans 2670 meshes (13
  animated kept); `render_scene_image` before, after the bake and after
  `undo` from one camera: after differs only by rounding (max 2 / 255),
  after undo identical; `get_node_details` `max_axis_scale` 0.016 -> 1.0.
  `prepare_scene` `target` `pc` writes the same bake; the loaded file
  renders within float noise of the source scene.
- A hand-written glTF (no `ERHE_geometry`, so the primitives are
  soup-backed) with a mirrored node (scale -1.5, 1, 0.7), a mirrored parent
  (1, -1.2, 1), a baked mesh with a baked child and a non-mesh child holding
  a baked mesh, three nodes sharing one mesh, and the same scene with a
  child listed before its parent: every instancing mode, bake / undo / redo
  with the node tree (names, parents, local transforms) restored exactly,
  and Prepare (PC) with the loaded file compared to the source render.
- Debug (GCC, windowed Vulkan): Bistro bake, undo and redo run without a
  VERIFY or error log; the redone node tree matches the baked one.

## Not verified

- The menu command interactively; the MSVC build.
- Prepare (Quest) with the bake on the headset.
