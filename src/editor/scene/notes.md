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

## Scene serialization (directory bundles, #241)

`scene_serialization.{hpp,cpp}` saves/loads a scene as a **directory bundle** named
`<name>.erhescene` (dedicated extension on the directory, like an `.xcodeproj`),
rather than a loose set of sibling files. `save_scene()` / `load_scene()` take the
bundle directory; inside it the fixed layout is:

- `scene.json` -- root JSON (`Scene_file`, codegen `definitions/scene_file.py`, version 5)
- `data.glb` -- meshes + materials (only when the scene has meshes)
- `mesh_<i>_p<p>.geogram` -- geometry-normative primitives
- `imgui.ini` -- window docking layout (`scene_imgui_ini_path()`)

The default bundle location is `res/editor/scenes`. Save/Load use a native *folder*
dialog (`SDL_ShowOpenFolderDialog` / `erhe::file::select_folder`); the selected
folder is the bundle root and its name is normalized to carry `.erhescene`. The
Asset Browser lists each `.erhescene` directory as a single `Asset_file_scene` leaf
with a "Load" action (raw glTF import/open stays on `Asset_file_gltf`). Loading is
bundles-only; the JSON schema is unchanged from the previous flat-file format.

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
