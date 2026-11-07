# World-space bake of static meshes

Stability: experimental

A static mesh node's vertices can be transformed into world space and the
node written under one "World Space" node at the scene root with the
identity transform, so level of detail selection (`doc/editor/lod.md`) and
the renderers see no node scale. The live node tree stays as authored: the
bake makes World-space mesh items in the scene's editor asset
(`doc/editor/prepared_assets.md`), an open scene draws either its source
meshes or its world-space form loaded from those items, and the Scene item
an Export writes holds the World Space node with its meshes naming the
items. The requirements are `doc/plans/prepared_assets.md` R4.6 - R4.8 and
R8.5 - R8.8.

## Export settings

`Scene_export_settings` (editor config codegen unit,
`src/editor/config/definitions/scene_export_settings.py`) is the field
`export_settings` of `Scene_settings`, present or absent, saved through
`ERHE_scene` (`doc/gltf_extensions/ERHE_scene.md`) and shown in the scene's
Properties "Export" group; how a scene gets them, the fields Export reads
and the refusals are `doc/editor/prepared_assets.md` "Export settings". The
fields the bake reads:

| Field | Type | Default | Meaning |
|---|---|---|---|
| `lod` | `Lod_config` | the `Lod_config` defaults | LOD request the chains are generated with for primitives without one |
| `bake_static_to_world_space` | `Bool` | true | Export writes the admitted mesh nodes under the World Space node |
| `instancing` | `Bake_instancing` | `duplicate_within_budget` | What happens to a primitive shared by several admitted meshes (below) |
| `instancing_budget_mib` | `Int` | 256 | Budget of `duplicate_within_budget` |
| `pvs_region_min`, `pvs_region_max`, `pvs_cell_size` | | | PVS bake grid (`doc/editor/pvs.md`) |
| `pvs_mode` | `Pvs_mode` | `off` | What Export does about the PVS (`doc/editor/prepared_assets.md` "PVS") |

`Bake_instancing`:

- `skip`: a mesh with a primitive shared by another mesh keeps its transform.
- `duplicate`: every baked mesh gets its own world-space copy.
- `duplicate_within_budget`: duplicate while the estimated duplicated vertex
  and index bytes (64 bytes per vertex, 4 per index) stay within
  `instancing_budget_mib`, then skip.

## Which meshes are baked

`plan_world_space_bake()` (`src/editor/scene/world_space_bake.{hpp,cpp}`)
admits a mesh that:

- is content, not a render proxy, not skinned, not a skin joint and carries
  no physics (collider shapes live in the node's space);
- has no ancestor (itself included) that is an animation target, moved by a
  rigid body of a motion mode other than none or static, or written as an
  external asset reference;
- has soup-backed, geometry-normative or graph-made primitives (R4.6); the
  duplicated bytes of the latter are estimated from their facet corners;
- is admitted by the instancing setting.

The plan carries per baked mesh its world transform relative to the scene's
root node (`inverse(root.world_from_node) * world_from_node`), and
`kept_reasons` (reason -> count) for the rest; the logs name them.

## Baking a primitive

Positions by `world_from_node`; normals and tangent xyz by its inverse
transpose, renormalized; tangent w and the triangle winding flip when the
determinant is negative. Bounds, content hash and BVH are rebuilt from the
baked data, and the LOD chain is generated in world space (a stored chain is
object space and is not reused). Triangles that become degenerate (positions
bitwise equal after the transform) are dropped, as the content build drops
them. Prepared data: `erhe::primitive::bake_prepared_primitive_transform()`.

## World-space mesh items

A World-space mesh item (kind `world_mesh`) holds one primitive of one
admitted mesh node in world space, in the form of a mesh item
(`doc/erhe/gltf.md` "Mesh items": `write_mesh_item()` of the baked
prepared primitive, its stamp the mesh item stamp), with a Raytrace BVH item
of its own while `raytrace_bvh` is on (`doc/editor/prepared_assets.md`
"Keys").

- Key (`make_world_mesh_key`, `src/editor/assets/item_keys`, R4.7): kind,
  code version `c_world_mesh_code_version`, the mesh item stamp, the key of
  the primitive's Local-space mesh item (its content hash, LOD request and
  optimizer options) and the plan's transform as text
  (`format_transform_hexfloat`: the 16 floats as `%a`, so every bit counts).
  It names no node: two nodes placing one mesh at one transform share an
  item, and moving the scene root moves every key.
- Info (R4.8): `{"nodes":[{"path":[names],"uids":[uids]}, ...]}`, the nodes
  from the scene root down that the item was last made or looked up for; a
  lookup that finds other nodes rewrites it (`Write_transaction::set_info`).

`Scene_export::Impl::ensure_world_mesh_items()` (`src/editor/assets/scene_export`)
makes the missing ones: per shape one preparation (`prepare_primitive_for_export()`
or `prepare_derived_primitive_for_export()`), then per missing key a copy
baked with the key's transform, its LOD chain generated and the item and BVH
item `put`, one executor task per shape. A shape that does not prepare gets
no World-space mesh item (logged); its mesh is written by the exporter's own
bake (a soup) or unbaked, and counted as `inline_primitive_count`.

## Bake World-Space Meshes

Mesh > Bake World-Space Meshes and MCP `bake_world_space_meshes`
(`doc/agents/mcp_server_usage.md`) queue a job of `Scene_save_service`
(`request_world_space_bake()`, `Scene_export_mode::bake_world_meshes`) with an
Export's staging: the scene is read-only while it runs, the status line shows
"Baking '<scene>': <stage>". It is refused as an Export is (a scene without
export settings or a source file, a USD-backed scene, a prepared run;
`get_export_refusal()`) and plans the bake whatever
`bake_static_to_world_space` says.

1. On the worker: the editor asset's source record, the Local-space mesh
   keys (`compute_mesh_keys()`), the World-space mesh keys and the missing
   items (above).
2. The world-space form is loaded from the items: one document naming every
   World-space mesh item (`Gltf_export_arguments::mesh_item_document`,
   `doc/erhe/gltf.md` "Mesh item documents") is parsed with a resolver over
   the editor asset, and each primitive is built by `run_imported_build_job()`
   from its stored GPU vertex streams, the uploads going through the loader
   queue.
3. On the main thread, once the loader watermark has passed the build's
   last ticket (`is_scene_export_ready()`), `World_space_form::install()`
   replaces the scene's form.

The job reports `export{counts{world_mesh, raytrace_bvh}, world_mesh_node_count,
world_form_mesh_count, ensure_ms, scene_ms}` through `get_save_status`
(`scene_ms` is the form load). It changes neither the node tree nor the undo
stack; a second bake of an unchanged scene makes nothing.

## Two forms

An open scene holds both forms of every admitted mesh node (R8.5):

- the source mesh, drawn through the node's transform;
- the world-space form (`src/editor/scene/world_space_form`): per admitted
  node whose primitives all have items, a render proxy mesh with the item's
  primitives and the source's materials, under the node "World Space Form"
  at the scene root (`content | render_proxy | session_only |
  no_transform_update`). Its identity path gives the proxies
  `Item_flags::world_space`, so they draw with the `WORLD_SPACE_VERTICES`
  variants (`doc/erhe/shader_variants.md` "World-space vertices"). The
  proxies carry `Editor_item_flags::world_space_form` and their sources
  `world_space_source`.

The proxies are derived data: no export, save or item tree shows them, they
take no part in the ID pass or ray queries (picking, selection and editing
act on the sources). A proxy casts shadows as its source did when the form
was installed (`shadow_cast`), and a view's shadow passes draw the casters
of the form the view draws (below), so a shadow is the drawn mesh's at the
drawn level of detail. The PVS bake renders this form (R8.9): a proxy's PVS columns
name its source node (`World_space_form::find_source()`), and a PVS baked on
the form culls only in a view that draws it (R8.10, `doc/editor/pvs.md` D5).

What a view draws: `get_view_filter()` (`renderers/render_context`) leaves
`world_space_form` out of every content pass of a view that draws source
meshes and `world_space_source` out of one that draws the world-space form
(`Render_context::mesh_form`), and its shadow node passes the same filter
over `shadow_caster_filter` as the passes' caster filter
(`Shadow_renderer::Render_parameters::caster_filter`, from the frame's copy
`Scene_view::get_render_mesh_form()`; the bucket path prepares with
`Scene_view::get_drawn_mesh_form()`; `Scene_root::prepare_shadow_bounds()`
gathers the caster bounds of each form, the world-space one while a form is
installed; a form is replaced only by `install()` within one main-thread call,
so a view that draws it, a scene image capture included, always finds its
bounds). The viewport toolbar combo of a scene that has
a form chooses Source Meshes or World-Space Meshes
(`Viewport_scene_view::queue_mesh_form()`); the world-space entry is enabled
while the form is available and otherwise names why ("<n> of <m> missing",
"scene changed", "settings changed"), and a view that asked for it draws
source meshes meanwhile (R8.8). `Viewport_scene_view::prepare_render()`
copies the drawn form into the frame's `Viewport_render_state::mesh_form`.
MCP `render_scene_image` takes `mesh_form` (`source` or `world_space`,
refused while the form is unavailable), and `get_world_space_meshes`
reports the form's state.

## Validity

A node's world-space mesh is valid while the item of its current key is the
one the bake loaded (R8.6). `World_space_validity`
(`src/editor/scene/world_space_validity`) keeps per admitted node what the
bake keyed - its root-relative transform and its primitives' render shapes -
and follows the Scene_host hooks of `Scene_root`, change-driven:

- `on_mesh_transform_changed`: the node's root-relative transform, computed
  as the plan computes it, compared bit for bit with the keyed one;
- `on_mesh_primitives_changed`: its render shapes compared with the keyed
  ones (an edit swaps in a new primitive, its undo restores the original,
  `doc/editor/operations.md` "Undo of an in-place edit");
- `register_mesh` / `unregister_mesh` of a content mesh that is not a proxy:
  a node the bake did not key entered the scene, or an admitted node left it
  ("scene changed"); the node leaving again, or entering again (an undo of the
  add or the removal), ends it;
- `Scene_root::on_export_settings_changed()`, called by the scene's
  Properties "Export" rows and by MCP `set_scene_settings` when the export
  settings change: the settings the keys and the admission read (the LOD
  request, `instancing`, `instancing_budget_mib`;
  `make_world_space_settings_signature()`) compared with the ones the bake
  keyed with ("settings changed" while they differ, so setting a value back
  ends it).

So moving a node, editing its mesh, adding or removing a mesh or editing a
setting invalidates it at the edit, and the undo makes it valid again
(R3.3); nothing is compared per frame. The form is available when every
admitted node is valid and neither the scene nor the settings changed since
the bake. A new bake makes the missing items and reinstalls the form. The
status of `doc/editor/prepared_assets.md` "Status" follows the same change
sites and counts the World-space mesh items present and missing.

## Export (the Scene item)

With `bake_static_to_world_space` on, an Export (`doc/editor/prepared_assets.md`
"Export") plans the bake with geometry-normative primitives admitted and makes
the missing World-space mesh items in its Ensure step, before the Local-space
mesh items; a Local-space mesh item is asked for only where a mesh the bake
does not admit draws the shape (R9.3). The exporter
(`Gltf_export_arguments::world_bake` and `world_mesh_item_provider`) writes
each baked mesh under the World Space node with a glTF mesh of its own whose
primitives read its World-space mesh items, and nothing is prepared for it.
A baked mesh moves out of its parent; every other node stays where it is
with its transform. When something stays below the baked mesh - a child that
is not baked, or a baked child that leaves a placeholder itself - a
transform-only node with the mesh's name and transform takes its place and
holds those children; a baked mesh whose children are all baked leaves
nothing behind. The walk visits the node tree in child order, so the result
does not depend on the order of the source file's node array.

A baked mesh and the World Space node carry the transform channel locks
(`lock_translation_*`, `lock_rotation_*`, `lock_scale_*`), written by name in
`ERHE_node`. The baked node keeps its glTF uid, so PVS columns
(`doc/editor/pvs.md` "Terms") follow it. Names are not identities: siblings
that share a name under the World Space node are renamed by the load
(`<name>_1`, ...).

A prepared run (`doc/editor/prepared_assets.md` "Prepared run") loads the
World Space node and its meshes like any other: the Scene item names the
World-space mesh items as `erhe-item:` buffers, and Copy brings each baked
node's World-space mesh item alone into the prepared asset.

## Verification

`scripts/prepared_asset_verify.py` ("World-space meshes"; `--only-world`
runs these checks alone) writes the hand-written scene - a mirrored node
(scale -1.5, 1, 0.7), a mirrored parent (1, -1.2, 1) holding a mesh, a baked
mesh with a baked child and a non-mesh child holding a baked mesh, three
nodes sharing one mesh, a floor - and the same scene with every child listed
before its parent, opens each as a foreign glTF, saves it and reloads the
erhe-saved file:

- added export settings have `instancing` `duplicate_within_budget`;
- Bake World-Space Meshes makes nine World-space mesh items and nine BVH
  items, every item records its node's path and uids, the node tree is as
  before, all nine nodes are valid; `render_scene_image` of the world-space
  form differs from the source meshes' in at most 0.5 % of the pixels by
  more than 2/255 (the rounding of the baked vertices: a level or two of
  shading over up to about 1 % of the pixels on Bistro, more at single
  silhouette pixels); a second bake makes nothing;
- Export makes no item, copies the nine World-space mesh items and asks for
  no Local-space mesh item; the Scene item holds the nine nodes under World
  Space, the placeholder `parent_mesh` holding `holder`, and every World
  Space mesh reads a World-space mesh item;
- moving a baked node makes one world-space mesh missing (the form
  unavailable, `render_scene_image` `world_space` refused, the status
  counting one World-space mesh item and one BVH item missing with as many
  keys asked as before); undo makes it valid and the status matches with the
  Export's Scene key again; removing a baked mesh ends the form's
  availability and undoing the removal restores it; a bake
  after the move makes one item; instancing `skip` makes one Local-space mesh
  item for the shared mesh and drops the three shared World-space mesh items
  from the prepared asset;
- a second Export in a new process makes nothing (and writes the prepared asset anew); with the source
  moved away the prepared run holds the World Space node and renders, within
  the same bound, as the open scene's world-space form and as the source
  meshes.

Bistro: `res/editor/assets/bistro_lights_compressed/bistro_lights_c.gltf`,
headless Release, Export with the bake on (counts, times and the prepared
run's image against the source scene's are in `doc/editor/prepared_assets.md`
"Verification").

## Not verified

- The viewport toolbar combo interactively; the MSVC build.
- Export (Quest) with the bake on the headset.
