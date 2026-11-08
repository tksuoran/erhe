# erhe scene serialization

Stability: stable

Reference for how the editor persists scenes: the file format, the save and
open pipelines, every part that participates, and what is (and is not)
persisted. Design history and rationale live in
[`gltf_scene_roundtrip.md`](gltf_scene_roundtrip.md); the wire
format of each vendor extension is specified in
[`gltf_extensions/`](../gltf_extensions/README.md).

## Overview

A scene saves as a **single erhe-authored glTF file** (`.glb` binary by
default; a `.gltf` path selects the JSON form, which is inspection-only - it
writes no buffer URI and cannot be re-imported). One `export_gltf()` call
writes everything: render content as plain glTF 2.x, physics as
`KHR_physics_rigid_bodies` / `KHR_implicit_shapes`, prefab instances as glTF
2.1 `externalAssets` references, embedded texture source images, animations,
and the editor-domain state as `ERHE_*` vendor extensions.

`ERHE_scene` in `extensionsUsed` is the **erhe-authored marker**: a glTF file
carrying it opens as a full scene (fresh `Scene_root`, saved editor state
applied); any other glTF opens as a foreign asset import. All `ERHE_*`
extensions are optional (`extensionsUsed` only, never `extensionsRequired`),
so any stock glTF viewer can open a saved scene and see the render content.

There is no sidecar: no scene JSON, no per-scene imgui ini, no companion
geometry files.

## File anatomy

Standard GLB container: 12-byte header, `JSON` chunk, `BIN` chunk. Inside the
JSON, erhe state attaches at three levels:

| Level | Extensions |
|---|---|
| per object (node / camera / material / mesh primitive) | `ERHE_node`, `ERHE_camera`, `ERHE_light`, `ERHE_material`, `ERHE_geometry` |
| the glTF `scene` object | `ERHE_scene` (per-scene setting overrides, ambient light, the scene item's local property values and style, enable_physics) |
| asset root (`extensions`) | `ERHE_brushes`, `ERHE_node_graphs`, `ERHE_collections`, plus the Khronos physics extensions' shape/material/filter tables |

A glTF node with a `mesh`, a `camera` or a `KHR_lights_punctual` light IS
that prim - an `erhe::scene::Mesh`, `Camera` or `Light`
(`doc/erhe/usd_compatibility_design.md` C5): the glTF node's name, transform, children
and children are the prim's, and the writer inverts it - such a
prim is written as one glTF node with `mesh` / `camera` / the light
extension set, and a `Mesh`, `Camera` or `Light` child of another prim as a
child node of its own. A node that carries two of the three is the prim of
the first in the order mesh > camera > light, and the others become its
child prims with identity transforms, which is the form the writer then
round-trips. The `ERHE_node` payload of a mesh node carries both halves,
`flags` / `properties` for the prim and `mesh_flags` / `mesh_properties` for
its mesh state; the `ERHE_light` payload of a light node is the light prim's
own. The glTF `mesh` and `camera` entries carry no `uid`: such a prim's
identity is its node entry.

A scene's content-library resources - materials, textures, brushes, styles,
physics materials, collision filters, joint settings, animations, skins and
node graphs - are prims of the same tree, under the kind `Scope`s the library
keeps below the scene root or under any other prim
(`doc/erhe/usd_compatibility_design.md` C5, U4). Those scopes and the resource prims
carry no `Item_flags::content`, and the node writer emits a transform-less
prim only when it carries that flag, so they are never written as nodes: a
resource rides its own flat glTF list (materials, images, animations, skins)
or its `ERHE_*` asset-root table (brushes, node graphs), and where it sits
rides `ERHE_scene` `library_folders`, whose `path` names the prim that holds
it - a folder `Scope`, an `Xform`, the `Mesh` that binds it
(`doc/gltf_extensions/ERHE_scene.md`). The load resolves a node path after the
imported nodes are in the scene, so the `library_folders` operation is the
last of an import and of an open, after the node inserts.

A prim of a class that carries no transform - an `erhe::Scope`, or the
`erhe::Typed` a USD `typeName` erhe has no class for becomes - is written as
a glTF node with the identity transform whose `ERHE_node` extension names
its `prim_class` (and, for a `Typed`, its `prim_type_name`); the reader
creates that class and reads no transform for it. A node with none of a
`mesh`, a `camera`, a light or the field is an `Xform`, the class every
other glTF node has.

Cross-references between payloads use glTF indices within the same asset
(node index, material index, mesh index). Item flags serialize as name lists
(see [`gltf_extensions/flags.md`](../gltf_extensions/flags.md)). Geometry-normative
meshes carry a bit-exact geogram dump in `ERHE_geometry` on the mesh
primitive, dual-listed with plain TRIANGLES render data so foreign viewers
still render them (flat-shaded where no fully-present per-vertex NORMAL
could be dual-listed; corner normals stay `ERHE_geometry`-only). A primitive
whose source of truth is an imported triangle soup exports the soup instead
(full vertex attributes: TEXCOORD_n, JOINTS_n / WEIGHTS_n, COLOR_n) with no
`ERHE_geometry` - its geometry is a derived artifact, re-derived on load.

A scene file holds authoring data alone (R1.1 of doc/plans/prepared_assets.md):
Save Scene writes each soup primitive as its raw soup
(`Prepared_mesh_data::none`) - the soup it was imported or saved as, also for
a primitive whose drawn data was loaded from a mesh item (its authoring soup,
doc/erhe/primitive.md "Authoring soup") - with its LOD request alone in
`ERHE_mesh_lod` (R4.5), each image as its source stream, and writes no
`ERHE_mesh_optimized`, `ERHE_mesh_topology`, `ERHE_raytrace_bvh`,
`ERHE_gpu_vertex_streams`, `ERHE_texture_ktx2`, `ERHE_pvs` or LOD chain
(R12.2); no texture of a scene that can be saved has a raw block KTX2
image (a file parse and the texture imports refuse one as a source). The
derived data lives in the file's editor asset (doc/editor/prepared_assets.md
"The editor's own loading"). A file parse reads none of those extensions
(doc/erhe/gltf.md "Authoring data"): a file saved by an earlier build with
them opens as a plain glTF file, its primitives built from their soups
(logged once), and its next save writes authoring data.

## Unique IDs

Every node, and each other object that carries a glTF 2.1 `uid`, has its
`uid` from the moment it exists, the same in every session for the same
content (`doc/plans/prepared_assets.md` R6.2; the library rules are in
`doc/erhe/gltf.md` and `doc/erhe/item.md`):

- an object of a glTF file takes the file's `uid`, or, when the file states
  none, one derived from the file name, its kind and its index, at the
  parse;
- the objects an import of a glTF adds take derived ones too, seeded with
  the file name: the `import_root` node (`<file name>|import_root`) and the
  default key and fill lights (`<file name>|default_key_light`,
  `<file name>|default_fill_light`);
- a node inside a prefab instance takes one derived from the `uid` of the
  instance's carrier node and the `uid` of its template node
  (`<carrier uid>|<template uid>`, `link_instance_to_template()` in
  `prefabs/prefab_library.cpp`), so nested instances chain;
- an object created in the editor takes a random one at creation (a copy or
  clone included), which the next Save persists.

A Save and an Export write the uids the items hold and assign none, as the
PVS bake does, so the Scene item of an unchanged scene is the same in every
session (`doc/editor/prepared_assets.md`). The one case a scene can hold one
uid twice is the same source imported into it twice; the writer then writes
the later one as a uid derived from it, with a warning. Asset keys of the
`builtin` scope carry no uid: builtins are addressed by name, and their
random uids differ per session.

## Native carriers

Where a file format of its own already states a concept - the
`KHR_physics_rigid_bodies` body and joint of a node, the `UsdPhysics`
schemas, `UsdGeomModelAPI`, the `ERHE_node_graphs` `node_bindings` array,
the `erhe:scene` block's `graph_meshes.bound_prims` - that carrier is the
one a save writes and an open reads. The carrier is produced from the value
group's `read_<x>(node)` record and consumed by writing the node's values
(`doc/erhe/property_system.md` section 4.23), so the group and the file stay
one concept with one writer and there is no second, erhe-specific carrier of
the same data to keep in step.

What a native carrier cannot hold rides `ERHE_node` `properties` by the
value's qualified name, with `property_node_refs` for a value naming a node
([`gltf_extensions/ERHE_node.md`](../gltf_extensions/ERHE_node.md)), and
`erhe:Owner:name` custom attributes in USD
([`../erhe/usd_compatibility.md`](../erhe/usd_compatibility.md)) - which of a
body's two kinematic modes is in force is the standing example
([`../erhe/khr_physics_rigid_bodies_support.md`](../erhe/khr_physics_rigid_bodies_support.md),
"Motion modes in a file"). A value that is session state carries no
serialize flag and no carrier at all (`doc/erhe/property_system.md`
section 4.23).

## Save pipeline

Entry points: `editor::begin_scene_save_gltf()`, `write_scene_save_gltf()`
and `finish_scene_save_gltf()` in `src/editor/parsers/gltf.cpp` (run by
`Scene_save_service`, "Saving in the background" below), and the
synchronous `save_scene_gltf()` that runs the three on the calling thread.
The same functions write the Scene item of an Export
(`Gltf_scene_save_kind::scene_item`, doc/editor/prepared_assets.md "Export"). The save assembles one
`erhe::gltf::Gltf_export_arguments` and calls `erhe::gltf::export_gltf()`:

1. **Root node** - the scene's root; export walks the node tree and writes
   every node's local transform as the node holds it, composing no
   transform into another node's (`doc/plans/prepared_assets.md` R6.1). The
   `import_root` node an open or import of a glTF creates is written as no
   node while it carries `Item_flags::world_space` (its path from the scene
   root is the identity), its children in its place, so open/save cycles do
   not nest wrappers; once transformed it is scene content and is written as
   a node with its transform (`import_root` itself is not serialized, so the
   reopened node is a plain one). The flag is derived: a transform edit on
   the root clears it at the edit and setting the identity back restores
   it. Every world transform is therefore bit-identical before a Save and
   after a reopen. A file saved before this rule, with the root's transform
   composed into its children, opens as it was written.
2. **Physics data** - `build_physics_description()`
   (`parsers/physics_export.cpp`, the builder the USD save uses too) converts the rigid-body values /
   `Joint` prims and the content library's physics materials,
   collision filters and joint settings into the plain-data
   `erhe::scene::Physics_description` carrier (`KHR_physics_rigid_bodies` +
   `KHR_implicit_shapes`; spec support notes in
   [`khr_physics_rigid_bodies_support.md`](../erhe/khr_physics_rigid_bodies_support.md)).
   Compound / off-axis shapes export via synthesized child collider nodes;
   the importer folds them back (and removes the carrier nodes when they
   have no other content, keeping save/open/re-save node-identical).
   A settings-less joint (free six-dof) exports as a joint description with
   no limits / drives; reload materializes a `Physics_joint_settings` item
   from it. World-attached joints (no connected node) are skipped with a
   warning - the Khronos extension cannot express them.
   The Joint prim's own state (name, flags, local values such as `active`)
   rides the joint's node as `ERHE_physics_joint`
   ([spec](../gltf_extensions/ERHE_physics_joint.md)); the importer applies
   it to the Joint prim it creates, before the prim enters the scene.
3. **Prefab external assets** - `collect_prefab_external_assets()`
   (`prefabs/prefab_library.cpp`) walks the tree for prims carrying a
   composition arc and maps those nodes to glTF 2.1 `externalAssets` references
   (URIs relativized against the save directory); the instanced subtree is
   NOT flattened into the file. See
   [`plans/gltf_prefabs.md`](../plans/gltf_prefabs.md). The Scene item of
   an Export is the one save that writes the instances' content as the open
   scene holds it (`doc/editor/prepared_assets.md` "Several files").
4. **Image sources** - `make_gltf_image_source_provider()` snapshots the
   content library's retained encoded source images (PNG/JPEG bytes kept
   from import time) so textures re-embed byte-identical; a fallback re-reads
   standalone source image files for textures imported before retention
   existed. Textures with no exportable source (e.g. graph-texture bakes)
   skip the slot - graph-baked slots are reconstructed by re-baking on load.
   Every written image carries its texture's properties (transfer function,
   normal encoding, mipmap generation, wrap modes) in the asset-root
   `ERHE_texture_properties` ([spec](../gltf_extensions/ERHE_texture_properties.md));
   an open takes them from there, an import of a file without them derives
   them once from the material slots that use each image
   (doc/editor/prepared_assets.md "Texture properties").
5. **Animations** - `collect_gltf_export_animations()` exports every
   animation in the content library (samplers + channels, glTF-native).
6. **Editor state** - `add_gltf_editor_state()`
   (`parsers/gltf_extensions_export.cpp`) appends the editor-domain
   extension payloads and `extensionsUsed` entries:
   - per node: node bindings for graph meshes;
   - scene level: `ERHE_scene` - ambient light, the scene item's own
     `properties` map and `style`, `enable_physics`, and the
     per-scene `Scene_settings` overrides, serialized through
     the codegen struct (`scene/definitions/scene_settings.py`). One field
     of it is scene content rather than a setting override:
     `variant_selections`, which variant each variant set of the scene has
     selected (`{prim_path, set_name, enclosing_set_name,
     enclosing_variant_name, variant_name}` per switched set,
     doc/erhe/usd_compatibility_design.md X4); a set without an entry keeps the
     selection the file it came from authored. The two enclosing fields (v2
     of the codegen struct) name the variant block a set is declared inside,
     both empty for a set the prim declares itself - which is what a file
     written before them holds, so it reads unchanged. They are part of the
     name because two blocks of one set may each declare a nested set of the
     same name; an entry naming such a set is applied after the entry naming
     the set that carries its block, because switching the outer set applies
     what the block it selects declares. A glTF save writes the entry
     of the one set it carries (see "material variants" below) with an empty `prim_path`: the
     file's set is carried by the file's own root, which is the prim the
     reloaded scene carries it on. `KHR_materials_variants` itself has no
     selection - the primitives' `material` is what a plain loader binds - so
     this entry is what keeps the selection across an erhe save;
   - material variants: `find_exported_variant_set()` +
     `collect_gltf_material_variants()` write the scene's variant set back as
     `KHR_materials_variants` - the asset's `variants` name list and, per
     primitive, the material each variant binds. The primitive's own
     `material` stays what the scene has bound, which is what a loader shows
     while no variant is selected. A glTF asset holds ONE variant list, so
     the set written is the one the file's own root carries: the scene's root
     prim, or an `import_root` child of it, whose children the writer writes
     in the root's place. Any other set is named in a warning and not
     written;
   - asset root: `ERHE_brushes` (brush library; brush geometry rides as
     extra unreferenced glTF meshes), `ERHE_node_graphs` (graph texture and
     graph mesh assets as embedded node-graph JSON, plus
     `material_bindings` / `node_bindings`), `ERHE_collections` (item
     tags);
   - the **exclusion hook**: meshes and physics controlled by a
     node's geometry graph are excluded from plain export - they
     are baked products, re-derived from the graph on load, and must not be
     double-persisted. An excluded mesh is a prim, so the writer skips the
     whole prim rather than only its glTF `mesh`; writing it as a
     transform-only node would resurrect it beside the rebuilt one.
   The library-domain extensions (`ERHE_node`, `ERHE_camera`, `ERHE_light`,
   `ERHE_material`, `ERHE_geometry`) are written by the exporter itself in
   `src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp`.
7. **PVS database** - a scene file carries none: a bake stores it as the
   PVS item of the editor asset, and a Scene item names that item
   (doc/editor/prepared_assets.md "PVS"; doc/editor/pvs.md D1).
8. `erhe::gltf::export_gltf()` produces the GLB/JSON string;
   `erhe::file::write_file()` writes it.

Save entry points. There is ONE save shape: full editor state. Saving a
prefab source is Save Scene on the scene opened from that source.

- **File > Save Scene** (`operations_window.cpp` `Operations::save_scene`):
  a scene opened/loaded from a glTF file (`Scene_root::get_source_path`)
  saves back to that file without confirmation; a scene with no source file
  writes `<scene name>.glb` under `res/editor/scenes` (created on demand),
  with an Overwrite/Cancel modal when the file exists, and is then
  associated with that file (further saves write back silently).
- **MCP `save_scene`** (`mcp/mcp_server_file_io.cpp`): optional path
  (default: same resolution as File > Save Scene); an explicit path with a
  missing/other extension is normalized to `.glb`.
- Both run `editor::save_scene_gltf(App_context&, ...)`: a save triggers
  `Scene_saved_message` (Asset Browser rescan), and when the written file is
  a loaded prefab source the prefab is reloaded so every instance in every
  scene reflects the edit - the prefab editing round-trip is open the
  source as a scene, edit, Save Scene. The reload is the change site of the
  keys of every scene it refreshes (`doc/editor/prepared_assets.md`
  "Several files", R10.6).
- **File > Export (PC)** and **File > Export (Quest)**
  (`Operations::export_scene`) and **MCP `prepare_scene`** (`target` `pc`
  or `quest`; `save_scene_for_quest` is `prepare_scene` with `target`
  `quest`) run an Export into the scene's editor asset and the target's
  prepared asset (doc/editor/prepared_assets.md "Export"); it is an
  export: the scene's source file binding and dirty state are not changed.
  The prepared asset is the only prepared format (R12.1 of
  doc/plans/prepared_assets.md). A `<scene name>_quest.glb` or `_pc.glb`
  that earlier builds wrote with Prepare (Quest) / Prepare (PC) - a glTF
  file with raw block KTX2 images (`ERHE_texture_ktx2`) and GPU-ready
  vertex streams - opens as a source like any other erhe glTF file, with
  none of those extensions read (its raw block textures have no image); the
  editor no longer writes one.
- **MCP `export_gltf`** exports plain interchange (same call minus
  `add_gltf_editor_state` unless `editor_state: true` is passed) - use it
  for handing content to other tools, `save_scene` for full state.

### Saving in the background

File > Save Scene, File > Export (PC) / Export (Quest) and the MCP save
tools hand the save to `Scene_save_service` (`src/editor/scene/
scene_save_service.hpp`), which runs one save at a time with the editor
running. An Export into the store (doc/editor/prepared_assets.md) runs
through the same steps, with `begin_scene_export()`, `run_scene_export()`
and `finish_scene_export()` in place of the three glTF save functions:

1. The request waits until the scene is quiescent: nothing queued on the
   `Operation_stack`, no async operation, scene commit or asset load in
   flight (`App_context::is_scene_load_in_flight()`), no mouse drag, live
   component edit or transform animation. A request for a scene that was
   closed, or has a close pending, fails.
2. `begin_scene_save_gltf()` runs on the main thread: it collects the
   export arguments and the overwritten-source handling below. Then the
   scene is read-only and `write_scene_save_gltf()` (export and file write)
   runs on a worker thread; the export's parallel stages use the executor.
3. When the worker is done, `finish_scene_save_gltf()` runs on the main
   thread: source rebinding, the save log lines, `Scene_saved_message`, and
   for a scene save the clean container record and the prefab reload. A
   USD-backed scene saves on the main thread in step 2 instead.

While a save runs the editor keeps rendering, and nothing writes the scenes
the worker reads:

- the `Operation_stack` holds queued operations (also `execute_now()`) until
  the save ends and declines undo / redo; scene commits, geometry graph
  evaluation, lightmap and PVS bake steps (except the PVS bake an Export
  with `pvs_mode` `rebake` runs, doc/editor/prepared_assets.md "PVS"),
  animation playback and transform animations are held;
- every ImGui window's contents and the main menu are disabled
  (`erhe::imgui::Imgui_windows::set_window_contents_disabled()`), hover and
  item-tree hover flags are not written;
- the saved scene's physics does not step, its viewports find no hover
  (the pointer reaches them, so camera navigation works), the fly camera
  does not move a camera of that scene unless the camera node is
  `session_only` (the editor's own viewport camera, which no save writes
  until the camera is keyed: keying clears `session_only`, so an animated
  default camera and its animation are saved),
  and `Scene_root::request_close()` refuses it;
- MCP refuses every tool but the status and save tools
  (doc/agents/mcp_server_usage.md "Saving scenes").

The status line shows the running save: the scene, the export stage
(`erhe::gltf::Gltf_export_stage`: reading mesh data, preparing meshes,
writing scene, transcoding textures, encoding file), the elapsed time and
the stage's progress.

### Saving onto the source file

A scene loaded from a file keeps reading from it: released triangle soups
read back their byte ranges, and image sources are byte ranges of it
(doc/erhe/gltf.md "Image source streams"). A save onto that file replaces it
(through a temporary file), so `save_scene_gltf()` first finds the scene's
primitives whose soup source references the target file, or for a text
.gltf its buffer file (`erhe::gltf::get_gltf_buffer_file_path()`)
(`Triangle_soup_source::references_file()`), and the content-library
textures whose image source does (`erhe::gltf::is_same_file()`), and then:

- a scene file writes every soup as it is (`Gltf_written_soup::soup`, or
  `authoring` for a primitive's authoring soup, whose source is the one that
  references the file), so every such primitive is rebound below; a Scene
  item makes the soup of each such primitive that the export will not write
  from its retained prepared core (its prepared data is stale after an
  edit) resident and drops its source
  (`Primitive::detach_triangle_soup_source()`);
- gives each such texture a resident copy of its stream for the export;
- after the file is written, `erhe::gltf::rebind_sources_to_written_file()`
  points the written primitives (their soup source, or their authoring soup
  source, and their optimized variants sharing the source) at their
  accessors in the new file and makes file-backed sources
  for the images written unchanged, which replace the resident copies; an
  image the export did not write keeps its resident copy.

A reload that read the old source just before the rebind retries once
through the new source (`Primitive_shape::get_triangle_soup()`). The save
logs `'<path>' was the scene's source: ...` and `get_save_status` reports the
counts as `sources{overwrote_sources, rebound_primitive_count,
detached_primitive_count, rebound_image_count}`.

## Open pipeline

Single load entry: the `Load_scene_file_message` handler (subscribed in the
`Operations` constructor, `operations_window.cpp`). Every load source funnels
through it:

- File > Load Scene (`.glb`/`.gltf` file picker, defaults to
  `res/editor/scenes`)
- Asset Browser context menu ("Load scene" on an erhe-authored file)
- MCP `load_scene` (queued; poll `list_scenes` to observe completion)
- `--scene <file>` command-line option (startup, instead of the procedural
  startup script). `--no-scene` instead starts with an empty scene
  ("Scene 1", the File > New Scene scene: a default camera and its
  viewport, no lights, no meshes; under OpenXR no desktop viewport, as for
  any opened scene) and loads nothing; it takes precedence
  over `--scene`. The tools (Hud, Hotbar, OpenXR headset) home in that
  empty scene, so a scene loaded later opens beside it; closing "Scene 1"
  or MCP `set_headset_scene` moves them (`doc/editor/tools.md` "Headset
  scene").
- `scene.load_scene` in `config/editor/commands.json`

The handler scans the file (`editor::scan_gltf` -> `Gltf_scan_summary`,
JSON-only, no buffer decode) and branches on
`is_erhe_scene(extensions_used)`:

- **Foreign glTF** -> `Scene_open_operation` (undoable operation: new scene
  root + content library + browser + viewport, then a normal glTF *import*
  with default camera/lights and an `import_root` wrapper).
- **erhe-authored scene** -> `editor::open_scene_gltf()`
  (`parsers/gltf.cpp`), which is NOT undoable and does not appear on the
  operation stack:
  1. Parse into a temporary container (`erhe::gltf::parse_gltf`), because
     the `ERHE_scene` payload must be read before the `Scene_root` can be
     constructed (`enable_physics` is a construction-time property).
  2. Construct the `Scene_root` with a **fresh, empty `Content_library`** -
     the file carries the scene's own brushes / materials / textures /
     animations / graphs; nothing leaks in from other scenes.
  3. Apply the rest of the `ERHE_scene` payload: ambient light, the scene
     item's `properties` map (its complete local set, so a style-held
     ambient color does not become local) and the per-scene
     `Scene_settings` overrides (codegen deserialize). The `style` member
     is assigned further down, once the file's style items exist.
  4. `finalize_imported_meshes()` - build GPU vertex/index buffers
     (skinned variant when needed), build edges for wireframe rendering
     when the geometry arrived without them (geometry restored from
     `ERHE_geometry` already has them, byte-exact), and register raytrace
     primitives.
  5. Resolve glTF 2.1 external assets: each referenced prefab is parsed
     (once, cached by `Prefab_library`) and instantiated under its carrier
     node.
  6. Execute inline (built as operations, executed and dropped - same
     ordering as the import compound): content-library attaches (textures /
     materials / skins / animations), `import_gltf_physics()` -> `import_physics()` (Khronos
     payload -> `Node_physics.*` values / `Joint` prims, compound folding, carrier
     node removal) and `import_gltf_editor_state()`
     (`parsers/gltf_extensions_import.cpp`: flags, layouts, rigs (IK
     settings), tags, brushes, node graphs + their bindings; graph meshes
     re-bake).
  7. Reparent the parsed top-level nodes directly under the new scene's
     root - no `import_root` wrapper, no injected default camera / lights
     (an erhe-authored scene has exactly what it was saved with).
  8. Fill the scene's `Variant_table` from the asset's
     `KHR_materials_variants` list (one set named `materials`, carried by
     the scene's root prim) and apply the `ERHE_scene` selection.
     A file carries no PVS: a scene opens with the PVS state `absent`
     (the PVS item of the editor asset is not read at the open).
  9. Kick off `Async_raytrace_kickoff_operation` (BVH builds on worker
     threads, synchronized via the scene's `Item_host` mutex).
- The handler then wires UI for the returned scene root: a browser window,
  a viewport (an existing empty viewport is repurposed when present, else a
  new one is created; either way the viewport's dock tab is selected, so the
  scene is in front), and `Scene_created_message` homes the global tools
  (Hud / Hotbar / Headset_view) when no scene owned them yet.

Legacy pre-extension files: node `extras.erhe_flags` and the material extras
carrier (`roughness_y`, `bxdf_model`, `blending_mode`, ...) are still parsed
(never written) so files exported before the `ERHE_*` extensions existed
keep their state on import.

## Parts map

| Part | Location | Role |
|---|---|---|
| `save_scene_gltf` / `open_scene_gltf` | `src/editor/parsers/gltf.{hpp,cpp}` | scene save / open entry points |
| `scan_gltf`, `is_erhe_scene` | `src/editor/parsers/gltf.cpp` | cheap scan; erhe-authored detection |
| `add_gltf_editor_state` | `src/editor/parsers/gltf_extensions_export.cpp` | editor-domain `ERHE_*` payloads + exclusion hook |
| `import_gltf_editor_state` | `src/editor/parsers/gltf_extensions_import.cpp` | editor-domain `ERHE_*` apply on open/import |
| physics export / import | `src/editor/parsers/physics_export.cpp` / `physics_import.cpp` (`gltf_physics_import.cpp` = the glTF entry) | format-neutral physics description <-> erhe::physics |
| `export_gltf`, `parse_gltf` + library `ERHE_*` writers/readers | `src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp` | core glTF I/O (fastgltf fork: physics extensions, glTF 2.1 externalAssets, generic extension passthrough) |
| prefab externalAssets | `src/editor/prefabs/prefab_library.cpp` | collect on save, resolve + instantiate on open |
| `Scene_settings` / `Gltf_source_reference` codegen | `src/editor/scene/definitions/*.py` | per-scene overrides payload (its `Scene_export_settings` is in `src/editor/config/definitions/`); content-library item -> source-file back-references |
| `Load_scene_file_message` handler | `src/editor/operations/operations_window.cpp` | single load entry, erhe-vs-foreign branch, UI wiring |
| `Scene_open_operation` | `src/editor/operations/scene_open_operation.cpp` | undoable "open foreign glTF as new scene" |
| MCP tools (`save_scene`, `load_scene`, `open_scene`, `export_gltf`, `import_gltf`) | `src/editor/mcp/mcp_server_file_io.cpp` | scripted access to the same paths |
| extension specs + JSON schemas | `doc/gltf_extensions/` | wire-format reference (schemas double as test fixtures) |

## What is not persisted (known limitations)

- **Content-library materials referenced by no mesh are not exported**
  (glTF materials exist only where meshes reference them). Consequently a
  graph-texture binding on an unused material is dropped at save with a
  warning (`"binding dropped"`). Bind materials that meshes use.
- Textures whose source cannot be reconstructed (embedded in a source
  .glb/.gltf imported before source retention, or graph bakes) do not embed
  an image; graph bakes are re-derived on load, others lose the slot with a
  warning.
- **`Brush_placement.*` values are not persisted** (the brush *library*
  is, via `ERHE_brushes`): the three values are registered without the
  serialize flag, so a placed-brush node reloads as a plain mesh node
  without the link back to its source brush. The legacy scene.json format
  did not persist them either.
- A static rigid body's mass is not persisted (KHR_physics_rigid_bodies has
  no `motion` object for static bodies); the value is meaningless for
  statics and is shape-derived on reload.
- Window layout / imgui state is not part of the scene; only the global
  editor layout is persisted, and it is not per scene.
- Undo/redo history, selection, and other transient session state are not
  saved.
- **A file carries the cameras it authored plus the ones the user created.**
  The default camera the editor injects so that a camera-less file has
  something to render through - the foreign-glTF import's, and the one
  `open_scene_gltf` and `open_scene_usd` add - is session state, flagged
  `erhe::Item_flags::session_only`: the glTF exporter and the USD writer both
  leave it out, and the next open of the file injects a fresh one fitted to
  the content. A file therefore saves as the prim tree it authored; a
  camera-less file stays camera-less and its top-level prim stays its root.
- Corner normals of geometry-normative meshes live only in `ERHE_geometry`;
  foreign viewers render such meshes flat-shaded.
- **Prefab templates ignore editor-domain payloads.** Saving over a prefab
  source writes the full editor state (the file becomes erhe-authored:
  `ERHE_scene` in `extensionsUsed`), but instantiating it as a prefab parses
  only the render and physics content -
  `ERHE_*` payloads (layouts, tags, brushes, node graphs) do not transfer
  into instances. In particular, a mesh controlled by a
  node's geometry graph is excluded from the save (re-derived
  from the graph on scene load) and prefab instances do not rebuild it, so
  graph-baked products are missing from instances of such a prefab.

## USD-backed scenes

glTF is the editor's own format and the one a scene saves in by default. A
scene opened from a `.usd` / `.usda` / `.usdc` / `.usdz` file is USD-backed
instead: `Scene_root::get_source_format()` reports `usd`, and Save Scene
(the menu command, the Asset Browser's "Load scene", MCP `save_scene`)
writes a `.usda` layer back through `erhe::usd` (`save_scene_usd`,
`doc/erhe/usd.md`). A scene never converts between the two formats -
neither direction is offered anywhere
([`usd_compatibility_design.md`](../erhe/usd_compatibility_design.md) G3).

Opening a USD file as a scene (`open_scene_usd`) builds a fresh `Scene_root`
with its own empty content library, puts the file's top-level prims directly
under the scene root - a USD file *is* the scene, so no `import_root`
wrapper is added - and indexes the file's materials and the textures its
image files decode to as the scene's own resources. A material is a prim of
that tree and enters the scene with it, at the place the file gave it; only a
material the file placed nowhere is attached under the `Materials` kind
scope, and a texture always is, because a USD file names image files rather
than texture prims. Importing the same
file as an asset (the Asset Browser's "Import", MCP `import_usd`) keeps
using the wrapper and the target scene's library, unchanged.

A file that authors no camera is looked at through the editor's default
camera, fitted to the content; it is session state and is not written back
(see "What is not persisted"), so the layer saves with the top-level prims
the file authored.

What the written layer carries beyond the USD mapping
([`usd_compatibility.md`](../erhe/usd_compatibility.md)) is the editor's scene state,
as string entries of the root layer's `customLayerData`:

| key | value |
|---|---|
| `erhe:scene` | the same JSON object the glTF `ERHE_scene` block carries, as one string: `ambient_light`, the scene item's `properties` map and `style`, `enable_physics`, the codegen-serialized per-scene `settings` and the `graph_meshes` entries a geometry node graph's prim has no form for ([`usd_node_graphs.md`](../erhe/usd_node_graphs.md) section 4) |
| `erhe:version` | the writer's revision, `"1"` |

An opened file that has no `erhe:scene` entry keeps the editor defaults, so a
USD file written by any other tool opens as a scene without complaint.

Wherever that block names a prim - a `graph_meshes` entry, a
`variant_selections` entry - it carries the path `erhe::usd::plan_usd_prim_paths`
plans for the prim, in the item-path spelling. The save plans the paths from
the arguments it has filled, writes the block with them, and hands the same
arguments to the write, which plans identically; a planned path is where the
prim lands in the file, so it is the path the item has once the file is opened
again and the entry resolves by an exact match.

A prefab instance is carried as the composition arc it came from: a prim
holding composition arcs (`doc/erhe/item.md`, "Composition arcs") is written
as a referencing prim with one `references` (or `payload`) arc per record, in
the record's order, and the instance content below it is not written - the arcs' targets hold it. An
arc names the target file relative to the layer being written, or no file at
all when it targets a prim of that same layer.

Every content-library kind is a prim of the layer where it sits: a style is
a `class` prim ([`usd_compatibility_design.md`](../erhe/usd_compatibility_design.md)
X3), a brush a `Brush` prim holding its geometry as a child `Mesh`, a node
graph of either kind a marked `NodeGraph` prim holding one `Shader` per node
([`usd_node_graphs.md`](../erhe/usd_node_graphs.md)), and a folder
the `Scope` it is (E4). The physics of the scene is the `UsdPhysics` prims
and API schemas of the mapping ([`usd_compatibility.md`](../erhe/usd_compatibility.md),
"Physics"): a body is its prim's `PhysicsRigidBodyAPI`, a physics material,
a collision filter and a joint-settings item are prims where they sit, a
joint is a `PhysicsJoint` child prim of the prim it joins, and the physics
world's gravity is a `PhysicsScene` prim, so the `erhe:scene` block carries
only `enable_physics` of it. Textures are
named by their source image file: a generated texture has no bytes on disk, so
its slot is left out of the material's shading network with a warning - except
a slot fed by a texture graph, which is written as a connection to that
graph's `NodeGraph` prim and needs no image.

## Verifying round-trips

**`scripts/scene_roundtrip_verify.py`** is the standing verification
harness (`doc/editor/gltf_scene_roundtrip.md` phase 6): against a fresh headless editor
session it builds a scene exercising every `ERHE_*` extension (shapes,
imported textured + skinned/animated assets, physics bodies + joint, brush
placement, graph mesh + graph texture bindings, layouts, tags, authored
animation keys, an external-asset prefab instance), saves it, validates
every `ERHE_*` payload against the JSON schemas in
`doc/gltf_extensions/schema/`, reloads it and diffs the MCP-visible state,
round-trips `res/editor/scenes/Prefab test.glb` when present, and runs the
optional foreign-tool checks (Khronos glTF validator via
`--gltf-validator` / `ERHE_GLTF_VALIDATOR`; Blender headless import+render
via `--blender` / `ERHE_BLENDER`). Exit 0 = all executed checks passed.

The graph asset round-trips are additionally covered by
`scripts/geometry_nodes_smoke_test.py` and
`scripts/texture_graph_smoke_test.py` (each parses the saved GLB JSON chunk
directly; run each suite in its own fresh editor session). Ad-hoc checks:
save -> load -> compare via the in-editor MCP (`get_scene_nodes`,
`get_scene_materials`, `get_physics_items`, `get_scene_brushes`, ...) and
`capture_screenshot`. The design behind the format is
`doc/editor/gltf_scene_roundtrip.md`.

`scripts/scene_roundtrip_verify.py` also carries the **USD leg**
(`usd-roundtrip`), which mirrors the glTF sections on USD content and shares
their diff and normalization helpers and nothing else - neither format is
converted into the other. Each `.usda` under `src/erhe/usd/test/data/` opens
as a scene, `authored.usda` takes one MCP edit per item kind (a node name, a
material value, a light value, a `purpose`, and the erhe-only
`Mesh.shadow_cast`), and the scene is saved under `logs/`, closed, reloaded
into a fresh scene and diffed: nodes, materials, lights, cameras, textures
and the set of locally authored property names per item. The reloaded scene
is then saved a second time and the two `.usda` files are compared line by
line - the writer is a function of the scene, so a save of a reloaded scene
reproduces its own input file, and any line that differs is reported as a
failing check. `usdchecker` runs on the first saved file when it is on PATH
or `--usdchecker` / `ERHE_USDCHECKER` names it (the `usdchecker.bat`
wrapper of the OpenUSD binary distribution's `scripts/` folder is what to
name on Windows), and prints SKIP otherwise;
the whole section skips when the editor was built with
`ERHE_USD_LIBRARY=none`.

## Future work

- [plans/background_scene_save.md](../plans/background_scene_save.md) - open
  items of the background save.
- [plans/prepared_assets.md](../plans/prepared_assets.md) - requirements for
  the source / editor / prepared asset split.
- [plans/gltf.md](../plans/gltf.md) - open items of the persistence design,
  including the state listed under "What is not persisted".
- [plans/gltf_prefabs.md](../plans/gltf_prefabs.md) - the remaining prefab phases.
