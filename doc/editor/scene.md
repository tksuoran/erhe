# scene/

Stability: mostly stable

## Purpose

Manages 3D scene data for the editor: scene roots (the top-level scene container), scene views (camera + viewport rendering), viewport management, scene commands (create camera/light/rendertarget), physics-scene coupling, raytrace integration, and scene serialization.

## Key Types

- **`Scene_root`** -- Owns an `erhe::scene::Scene`, a physics world (`erhe::physics::IWorld`), a raytrace scene, mesh layers (`Scene_layers`), and a `Content_library`. Implements `erhe::scene::Scene_host`. Registers/unregisters nodes, cameras, meshes, lights, and skins. Manages `Node_physics` instances and rendertarget meshes. Keeps the scene's shape-to-meshes index: for every `erhe::primitive::Primitive` a registered mesh names, the meshes that name it, maintained at the three change sites (`register_mesh`, `unregister_mesh` and `on_mesh_primitives_changed`, which re-indexes only when the mesh's primitive list actually differs) under its own mutex, since two of them run on worker threads during an async load. `collect_meshes_sharing_primitives()` is the query, clearing and filling a caller-owned buffer; the deferred raytrace commit uses it to refresh the sharers of a swapped shape (`operations/async_raytrace_kickoff_operation.cpp`). Multiple `Scene_root` instances can coexist (managed by `App_scenes`). Constructor takes only `App_message_bus*`, `Content_library`, name, and `enable_physics`; UI for the content library is provided separately by `Content_library_window` (in `content_library/`).

- **`Scene_layers`** -- Defines mesh layers (content, brush, tool, controller, rendertarget) and a light layer. Each layer has an ID used for filtering during rendering.

- **`Scene_view`** -- Abstract base for anything that provides a camera view into a scene. Holds a weak reference to `Scene_root`, viewport configuration, control ray state (for pointing/picking), and hover entries (per-slot raytrace hit results). Subclasses: `Viewport_scene_view`, `Headset_view`.

  `update_hover_with_raytrace()` is the only per-frame site that commits the
  scene's raytrace top level acceleration structure (`IScene::commit()`); the
  MCP `raycast` and `pick_at` tools commit on demand, at the moment their
  caller asks for a trace. It hovers only while the editor is settled: it asks
  `App_context::is_scene_load_in_flight()` first, and while that is true it
  clears every hover slot and returns without committing or tracing. A load
  attaches, detaches and rebuilds raytrace instances continuously, so a hover
  that traced through it would rebuild the acceleration structure on nearly
  every frame and never reuse it, and a hover entry it produced would name a
  mesh and a primitive index the load is still swapping underneath. Both
  subclasses inherit the rule, the XR view included. The gate is logged to
  `editor.controller_ray` once at each edge, never per frame.

- **`Viewport_scene_view`** -- Concrete `Scene_view` that is also a `Texture_rendergraph_node`. Renders scene content into a texture consumed by downstream rendergraph nodes (post-processing or direct display). Handles 2D pointer position, hover detection (via raytrace or ID renderer), and shader variant selection.

- **`Scene_views`** (`viewport_scene_views.hpp`) -- Manages the collection of `Viewport_scene_view` instances. Tracks which view is hovered, creates new viewport views, and responds to graphics settings changes.

- **`Scene_commands`** -- Provides commands to create cameras, empty nodes, lights, and rendertargets. Hosts the corresponding `Command` objects.

- **`Scene_builder`** -- Constructs an initial scene with cameras, lights, and brush meshes (platonic solids, spheres, tori, etc.). Used during startup to populate the default scene. Its constructor calls `make_brushes()`, which creates every palette brush the `scene_config` flags enable - each with its name, flags and folder placement, a geometry generator and no geometry - and returns: no mesh is built and nothing is uploaded, so no mesh memory flush is needed, and the order of the `Brushes` scope's children is the order the makers created them in. `ensure_brushes()` is a no-op afterwards, because the constructor set `m_brushes_built`. The builder also owns the palette's `Brush_geometry_queue` (`doc/editor/brushes.md` G5) and hands it to every brush it makes. `make_mesh_nodes()` is the tier 1 consumer that the `scene.add_*` startup commands reach: it sorts the brushes it was given by name, requests all of them first when there are more than a handful, so the workers prepare the tail of the list while the main thread prepares the head, and then instantiates them in order, each `Brush::make_instance()` preparing its brush on the calling thread if no worker got there first. Startup therefore prepares exactly the brushes `commands.json` places.

- **`Hover_entry`** -- Per-slot raytrace/pick result storing the hovered mesh, geometry, position, normal, UV, triangle index, and facet. Entries from an analytic source carry no mesh; `analytic_provider` names their provider and `get_name()` returns its name.
- **`Analytic_hover_provider`** (`analytic_hover_provider.hpp`) -- Third hover source next to raytrace and ID render, for tools hit tested analytically (the transform gizmo). Providers are registered in `App_context::analytic_hover_providers` after part construction. `Scene_view::update_hover_with_analytic_tools()` runs last in each hover update and merges every provider's entry into `tool_slot` by ray-t. `Scene_view::reset_hover_slots()` clears slots only (the sources refill them in the same update); `reset_hover()` also calls every provider's `clear_analytic_hover()` and is used where a view stops picking.

- **`Frame_controller`** -- Camera controller with 6DOF input axes (translate XYZ, rotate XYZ). Used by `Fly_camera_tool`.

- **`Node_physics`** -- `Node_attachment` wrapping a Jolt rigid body. Synchronizes physics transforms with scene node transforms. A convex hull / triangle shape keeps no reference to the geometry it was built from, so the attachment remembers it in `collision_mesh` (a bridged weak object reference, `Node_joint::connected_node`'s form): no value names the body's own mesh, and a value names a `Mesh` prim below the body, which is where both exporters then state the collider (`parsers/physics_export.cpp` makes it a physics entry of its own on that prim). `physics_import.cpp` sets it when a mesh collider of another prim folds into the body. The value records where the shape came from - setting it does not rebuild the shape - and a mesh that leaves the scene leaves the built shape standing, with the export falling back to the body's own mesh and one warning.

- **`Variant_table`** (`variant_table.hpp`) -- The variant sets one scene carries (doc/erhe/usd_compatibility_design.md X4), owned by its `Scene_root` and dying with it. One `Variant_set` is the prim carrying it, the set name, its variants with their material bindings and property opinions, the selected variant, the prims each variant adds, the base values, and how many opinions the file authored that erhe has no place for (a property the reader could not express, a prim a variant adds somewhere the reader's hoist does not reach). The prim and the materials are weak references, and `Scene_root` drops a set whose prim an `items_removed` message names, so an undone import stops offering its sets. A variant's opinions are `erhe::scene::Instance_override` entries in the neutral name / text form the file reader recorded them in, so a variant nobody selected still has its opinions, which no item of the scene holds; `base_values` is what the prims held for every path and property name any variant authors, before the selected variant reached them at load. `Scene_root::select_variant()` switches a set: `Variant_select_operation` records the selection in the table and in `Scene_settings::variant_selections`, one `Property_set_operation` (or `Node_transform_operation`) writes each opinion any variant of the set authors - the chosen variant's value where it authors one, the base value where it does not, so switching never leaves the previous variant's opinion standing - one `Property_set_operation` per prim any variant of the set adds writes its `active` - the chosen variant's prims hold no local value, which is active, and every other variant's are false, which prunes each one and its subtree from the render, the pick and the simulation - and one `Mesh_material_assign_operation` per binding assigns the materials, all in one compound so a single undo reverts the switch. Every variant's prims are in the scene whichever variant is selected (`Variant::prims`, by the path each has below the carrying prim and the name the file's variant block gave it): the reader puts them there, so a switch never builds or destroys a prim. `resolve_variant_prim()` is the path-to-item lookup the opinions use, and `resolve_variant_binding()` is where a binding path becomes mesh primitives: a path that names a mesh covers the primitives the same variant does not bind by subset, and a deeper path names one primitive by its GeomSubset name. Both resolve a path the tree has no item for through the clone of every composition arc it crosses (`erhe::scene::find_instance_item`), which is how a switch reaches the prim an opinion behind an arc was applied to at load (doc/erhe/usd_compatibility_design.md C6). A `variantSet` a variant block declares is a set of the same prim, tabled beside the set carrying the block and named by it (`Variant_set_key`: prim path, set name, enclosing set name, enclosing variant name - two blocks of one set may each declare a nested set of the same name, which `full_assets/Teapot/DrawModes.usd` does), and its blocks contribute only while that block is the selected one: `Variant_table::is_live()` is that test, and a switch of a set takes the sets its old block declares off - their opinions back to their base values, every prim of theirs inactive, their own nested sets first - and brings the sets its new block declares on, each applying the selection it holds, recursively. A set whose enclosing block is not the selected one is switched by recording the selection alone, which the branch brings with it when a switch of the enclosing set lets it in; the Properties combo shows such a set indented, named by its block and disabled. A set that only starts contributing then has no base values captured yet - the reader captures them for the contributing branch alone - so `capture_variant_base_values()` captures them as the set comes on.


- **`Draw_mode`** (`draw_mode_properties.hpp`) -- `UsdGeomModelAPI` as a value group of the prim itself (doc/erhe/property_system.md section 4.24, doc/erhe/usd_compatibility.md, "Draw modes"): the request that a model prim's subtree be drawn as a proxy. `Draw_mode` is a registration holder with static members only; every attribute of the schema is an attached property of `erhe::scene::Node` named as the mapping table names it, under the owner type `Draw_mode`, which is the name a file's opinion of one addresses it by. `Draw_mode.apply_draw_mode` is the group's key property: the prim carries a draw mode exactly while it is true, which is the erhe form of `GeomModelAPI` being applied to the prim, and authoring any other value of the group sets it. None of the values inherits: `Draw_mode::inherited` is USD's own deferral token, and `read_draw_mode()` walks the ancestor prims for it, with the root fallbacks `default` and `full`, returning a plain `Draw_mode_data` record every consumer reads. `get_draw_mode_description()` / `set_draw_mode_description()` are the `erhe::scene::Draw_mode_description` halves the importers and exporters use, with a value's authored flag being whether the node holds it locally (D32). `Draw_mode.source_directory` is session state (no serialize flag): which file spelled the prim's relative card-texture paths, so `resolve_card_texture_path()` resolves one against it; an instance holds none of its own and reads the template's through the reference layer, which is where the variant block that authored the path lives.

- **`Draw_mode_renderer`** (`draw_mode_renderer.hpp`) -- The background `Tool` that submits the line proxies per viewport: the 12 edges of the extent box for `bounds`, three axis lines from the prim's origin for `origin`, both in the prim's own space, in `draw_mode_color`, depth-tested the way the adapter draws them as geometry. `cards` submits no lines: its proxy is the quad geometry `Draw_mode_system` owns, which the ordinary content passes render. It iterates the rendered scene's draw-mode system entries, so no pass scans the tree, and its line buffer is a persistent scratch cleared at the point of use.


- **`Draw_mode_system`** (`draw_mode_system.hpp`) -- The per-scene owner of the draw modes' runtime state (doc/erhe/scene.md "Node systems"), owned by `Scene_root` and added to its `Scene`, which drives it from the three node-system change sites. It keeps one `Draw_mode_entry` per node carrying a draw mode, keyed by a raw `Node*` and erased when the node leaves the scene, so a scene close releases what it holds. The entry holds the cached extent - the authored `extentsHint` when there is one, else the bounds of the meshes at and below the prim, measured once - and the card proxy. The system writes `Item_base::set_prunes_children()` from the prim's own mode, so the prim's children leave render, pick and simulation while it asks for a proxy. A `cards` mode owns generated quad geometry as well (`draw_mode_cards.hpp`): one `Mesh` child prim of the model prim with one primitive and one unlit, single-sided material per drawn face. A face that has an image is cut out along the image's alpha channel - the material is `alpha_test` with the cutoff 0.1 reading the alpha of the base color slot, which is the erhe form of the `opacity` connection and `opacityThreshold` `UsdImagingDrawModeAdapter` gives a card - and a face with no image is the flat draw-mode color over its whole quad. The proxy carries `Item_flags::draw_mode_proxy`, which exempts it from its own parent's pruning (`Hierarchy::is_pruned_by_parent`), keeps it out of the extent measurement and redirects a viewport pick of it to the model prim, and `Item_flags::session_only`, which keeps every exporter from writing it. Building it inserts a prim, so the change sites only queue the node and `App_scenes::rebuild_draw_mode_proxies()` calls `flush_proxy_rebuilds()` once per frame on the main thread, the way a display-color change is handled. An inactive prim owns no proxy: a prim its own opinion or an ancestor's pruning took out of render, pick and simulation is drawn by nothing, so `on_node_active_changed` takes the proxy away with the bit and builds it when the bit comes back - which is what keeps the clones an instance puts below a pruning prim from each owning a set of cards. The card images are read through `erhe::graphics::Image_loader` and shared per scene by their file path (`Scene_root::find_card_texture`).
- **`Node_raytrace`** -- Handles raytrace instance creation/destruction for mesh nodes.

Raytrace instances carry their own world transform, and a freshly built
instance starts at the identity, uncommitted. `Mesh::update_rt_primitives()`
therefore seeds the rebuilt instances with the node's current world transform
and commits them, exactly as a node move does, before it announces the
change. Every primitive swap on an already-placed node goes through it - a
geometry graph re-bake, the deferred raytrace commit, the initial bind of a
placed node - and without the seeding those swaps would leave the raytrace
hits at the origin: the mesh renders at its node, while hover and picking
miss it until the node next moves.

## Scene persistence (erhe-authored glTF)

Scenes are saved as a **single glTF file**, no file dialog: a scene
opened/loaded from a glTF file saves back to its own source file without
confirmation (when that file is a loaded prefab source the prefab reloads,
refreshing every instance, so one Save Scene covers prefab sources too); a
scene with no source file saves to
`<scene name>.glb` under `res/editor/scenes` (Overwrite/Cancel modal when
the file exists) and is then associated with that file. One `export_gltf()`
call carries the render content plus physics data, prefab external-asset
references, embedded texture sources, animations and the editor-domain
`ERHE_*` extensions (`parsers/gltf.hpp save_scene_gltf`; `ERHE_scene` in
`extensionsUsed` marks the file as erhe-authored). File > Load Scene is a
`.glb`/`.gltf` file picker; the
`load_scene_file` message handler opens an erhe-authored file as a full
`Scene_root` (`open_scene_gltf`: not undoable, empty content library, saved
editor state applied) and routes a foreign glTF to `Scene_open_operation`
(undoable "open foreign glTF as new scene"). The Asset Browser branches its
context menu on `Asset_file_gltf::extensions_used` the same way. Full
reference (pipelines, parts map, limitations): `doc/editor/scene_serialization.md`;
design record: `doc/editor/gltf_scene_roundtrip.md`.

## Public API / Integration Points

- `Scene_root::register_to_editor_scenes()` -- registers with `App_scenes`
- `Scene_root::make_browser_window()` -- creates an `Item_tree_window` for this scene
- `Scene_root::before/update/after_physics_simulation_steps()` -- physics tick cycle
- `Scene_view::set_world_from_control()` -- sets the control ray (pointer direction)
- `Scene_view::get_hover()` / `get_nearest_hover()` -- query hover results
- `Scene_views::create_viewport_scene_view()` -- factory for new viewport views
- `Scene_views::hover_scene_view()` -- returns the currently hovered view

## Dependencies

- erhe::scene, erhe::physics, erhe::raytrace, erhe::geometry, erhe::primitive
- erhe::rendergraph, erhe::imgui, erhe::commands
- editor: App_context, App_message_bus, Content_library, Mesh_memory, Tools
