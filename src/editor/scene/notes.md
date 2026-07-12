# scene/

## Purpose

Manages 3D scene data for the editor: scene roots (the top-level scene container), scene views (camera + viewport rendering), viewport management, scene commands (create camera/light/rendertarget), physics-scene coupling, raytrace integration, and scene serialization.

## Key Types

- **`Scene_root`** -- Owns an `erhe::scene::Scene`, a physics world (`erhe::physics::IWorld`), a raytrace scene, mesh layers (`Scene_layers`), and a `Content_library`. Implements `erhe::scene::Scene_host`. Registers/unregisters nodes, cameras, meshes, lights, and skins. Manages `Node_physics` instances and rendertarget meshes. Multiple `Scene_root` instances can coexist (managed by `App_scenes`). Constructor takes only `App_message_bus*`, `Content_library`, name, and `enable_physics`; UI for the content library is provided separately by `Content_library_window` (in `content_library/`).

- **`Scene_layers`** -- Defines mesh layers (content, brush, tool, controller, rendertarget) and a light layer. Each layer has an ID used for filtering during rendering.

- **`Scene_view`** -- Abstract base for anything that provides a camera view into a scene. Holds a weak reference to `Scene_root`, viewport configuration, control ray state (for pointing/picking), and hover entries (per-slot raytrace hit results). Subclasses: `Viewport_scene_view`, `Headset_view`.

- **`Viewport_scene_view`** -- Concrete `Scene_view` that is also a `Texture_rendergraph_node`. Renders scene content into a texture consumed by downstream rendergraph nodes (post-processing or direct display). Handles 2D pointer position, hover detection (via raytrace or ID renderer), and shader variant selection.

- **`Scene_views`** (`viewport_scene_views.hpp`) -- Manages the collection of `Viewport_scene_view` instances. Tracks which view is hovered, creates new viewport views, and responds to graphics settings changes.

- **`Scene_commands`** -- Provides commands to create cameras, empty nodes, lights, and rendertargets. Hosts the corresponding `Command` objects.

- **`Scene_builder`** -- Constructs an initial scene with cameras, lights, and brush meshes (platonic solids, spheres, tori, etc.). Used during startup to populate the default scene.

- **`Hover_entry`** -- Per-slot raytrace/pick result storing the hovered mesh, geometry, position, normal, UV, triangle index, and facet.

- **`Frame_controller`** -- Camera controller with 6DOF input axes (translate XYZ, rotate XYZ). Used by `Fly_camera_tool`.

- **`Node_physics`** -- `Node_attachment` wrapping a Jolt rigid body. Synchronizes physics transforms with scene node transforms.

- **`Node_raytrace`** -- Handles raytrace instance creation/destruction for mesh nodes.

## Scene persistence (erhe-authored glTF, phase 4)

Scenes are saved as a **single glTF file** (`<scene name>.glb` under
`res/editor/scenes`, no file dialog; Overwrite/Cancel modal when the file
exists): one `export_gltf()` call carrying the render content plus physics
data, prefab external-asset references, embedded texture sources, animations
and the editor-domain `ERHE_*` extensions (`parsers/gltf.hpp
save_scene_gltf`; `ERHE_scene` in `extensionsUsed` marks the file as
erhe-authored). File > Load Scene is a `.glb`/`.gltf` file picker; the
`load_scene_file` message handler opens an erhe-authored file as a full
`Scene_root` (`open_scene_gltf`: not undoable, empty content library, saved
editor state applied) and routes a foreign glTF to `Scene_open_operation`
(undoable "open foreign glTF as new scene"). The Asset Browser branches its
context menu on `Asset_file_gltf::extensions_used` the same way. See
`doc/gltf-scene-roundtrip-plan.md`.

### Removed: legacy scene serialization (directory bundles, #241)

The superseded `.erhescene` **directory bundle** format
(`scene_serialization.{hpp,cpp}`: `scene.json` + `data.glb` +
`mesh_<i>_p<p>.geogram` inside a `<name>.erhescene` directory) was removed in
phase 5 of `doc/gltf-scene-roundtrip-plan.md`, together with its
scene.json-only codegen serial types under `definitions/` (only
`gltf_source_reference.py` and `scene_settings.py` remain) and the Asset
Browser's `Asset_file_scene` bundle handling. Existing bundles were migrated
by loading and re-saving as `.glb`.

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
