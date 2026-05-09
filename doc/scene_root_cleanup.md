# Scene_root cleanup plan

## Context

`Scene_root` (src/editor/scene/scene_root.{hpp,cpp}) has accumulated responsibilities
beyond "host an `erhe::scene::Scene`": ImGui browser windows, drag/drop content-library
UI built inline in the constructor, a selection->physics bridge, rendertarget-mesh
tracking with its own mutex, and a handful of dead members. The constructor body is
~260 lines for what should be a thin scene container, and the constructor takes 7
parameters where 3 of them (`imgui_renderer`, `imgui_windows`, `context`) exist only
to build the content-library tree window and are passed as nullptr at 3 of the 6
instantiation sites.

Goal: reduce Scene_root to its core responsibilities (Scene_host, physics world,
raytrace scene, layers, content library reference) without changing any of the
load-bearing API used by 30+ callers.

**Scope of this plan:** Steps 1 and 2 only. Step 3 (constructor create-info /
factory) is rejected. Steps 4 and 5 are deferred -- re-evaluate after 1 and 2
land.

## What is staying untouched

These have many callers and are working as intended:

- `get_scene()` (~80 callers), `layers()` (~50), `get_content_library()` (~30),
  `get_physics_world()`, `get_raytrace_scene()`.
- `register_to_editor_scenes` / `unregister_from_editor_scenes`.
- `register_node_physics` / `unregister_node_physics`, all `Scene_host` overrides.
- `before_physics_simulation_steps` / `update_physics_simulation_fixed_step` /
  `after_physics_simulation_steps`.
- `sort_lights()` -- one real caller in `src/editor/rendergraph/shadow_render_node.cpp`.
- `begin_mesh_rt_update` / `end_mesh_rt_update` -- callers in `gltf.cpp` and
  `operations_window.cpp`.
- `camera_combo` overloads -- one caller in `scene_view_config_window.cpp`,
  not worth extracting in isolation.

## Recommended approach (in execution order)

### Step 1 -- Dead-code sweep (low risk, ~30 LOC)

Files: `src/editor/scene/scene_root.hpp`, `src/editor/scene/scene_root.cpp`.

- Delete `m_camera` -- declared, never read or written.
- Delete `m_camera_controls` -- declared, never read or written.
- Drop the now-unused `#include "scene/frame_controller.hpp"` from the header.

No caller impact. Verify with a build before moving on.

### Step 2 -- Extract `Content_library_window` (low risk, ~140 LOC moved)

The block at `scene_root.cpp` lines ~180-311 (Item_tree_window construction with
drag/drop handler and material context-menu callbacks for the content library) only
depends on the constructor's `imgui_renderer`, `imgui_windows`, `context`,
`m_content_library`, and the scene's `name`. Move it out:

- New files: `src/editor/content_library/content_library_window.{hpp,cpp}`.
- Constructor signature:
  `Content_library_window(Imgui_renderer&, Imgui_windows&, App_context&, std::shared_ptr<Content_library>, std::string_view scene_name)`.
- Owns the `Item_tree_window`, the drag/drop closure, the material-create context
  menu closure, and the title formatting.

After this extraction, drop these from `Scene_root`'s constructor entirely:
`imgui_renderer`, `imgui_windows`, `context`, and `m_content_library_tree_window`.
Construct `Content_library_window` next to `Scene_root` at the three sites that
actually want it (`editor.cpp`, `scene_serialization.cpp`, and `asset_browser.cpp`'s
`Scene_open_operation::execute`). Tools, scene_preview, and the dead `#if 0` block
in `item_tree_window.cpp` simply lose nullptr noise.

Reuse: existing `Item_tree_window` (src/editor/windows/item_tree_window.{hpp,cpp}),
existing `Content_library` (src/editor/content_library/).

### Step 3 (rejected, do not do)

A `Scene_root_create_info` struct + `make_scene_root()` factory was considered to
kill the footgun "caller must call `register_to_editor_scenes` after `make_shared`".
Decided not worth the disruption to all 6 instantiation sites. The post-construction
registration pattern stays.

### Step 4 (deferred) -- Extract `Physics_selection_freezer`

Would move `m_selection_subscription`, `m_physics_disabled_nodes`, the constructor
closure that disables physics on selected nodes, and `Scene_root::imgui()`'s body
into a new `src/editor/scene/physics_selection_freezer.{hpp,cpp}`. Re-evaluate
after steps 1 and 2 land. The closure uses `item->get_item_host() != this` to scope
itself to one scene root, so the new helper would need a `Scene_host*` filter
parameter.

### Step 5 (deferred) -- Extract `Rendertarget_mesh_registry`

Would move `m_rendertarget_meshes`, `m_rendertarget_meshes_mutex`, the
`is<Rendertarget_mesh>` branches in `register_mesh` / `unregister_mesh`, and
`update_pointer_for_rendertarget_meshes` to a new helper class. Re-evaluate after
steps 1 and 2 land. Kills one of Scene_root's two mutexes.

## What NOT to do

- **Do not extract `make_browser_window` / `remove_browser_window` /
  `m_node_tree_window` into a separate `Scene_tree_window` class.** Three callers
  rely on the returned `shared_ptr<Item_tree_window>`, and the closures use
  `shared_from_this()` plus the scene root's identity for hover messages. Net
  complexity does not drop.
- **Do not inline `Scene_host` register/unregister forwarders into the header.**
  They need full `erhe_scene/{node,camera,mesh,skin,light}.hpp` includes; pulling
  those into the header slows editor rebuilds.
- **Do not narrow `App_context*` separately.** It exits Scene_root's constructor
  entirely as part of step 2; nothing further to do.

## Critical files

- `src/editor/scene/scene_root.hpp` -- header
- `src/editor/scene/scene_root.cpp` -- implementation
- `src/editor/editor.cpp` -- default-scene instantiation
- `src/editor/scene/scene_serialization.cpp` -- deserialization instantiation
- `src/editor/tools/tools.cpp` -- tool-scene instantiation, simplifies in step 2
- `src/editor/preview/scene_preview.cpp` -- preview-scene instantiation
- `src/editor/windows/item_tree_window.cpp` -- dead `#if 0` block, args still need updating
- `src/editor/asset_browser/asset_browser.cpp` -- `Scene_open_operation::execute`
- `src/editor/app_scenes.{hpp,cpp}` -- registration target, `imgui()` caller
- `src/editor/rendergraph/shadow_render_node.cpp` -- `sort_lights` caller (regression check)
- `src/editor/scene/notes.md` -- update after each step lands

New files:
- `src/editor/content_library/content_library_window.{hpp,cpp}` (step 2)

Each new pair must be added explicitly to `src/editor/CMakeLists.txt` (no globbing,
per CLAUDE.md).

## Verification

After each step:

1. Configure with `scripts\configure_vs2026_opengl.bat`. Build the `editor` target.
2. Run `editor.exe` and confirm:
   - Scene tree window opens / closes.
   - Content library window shows materials; drag-drop across libraries copies (step 2).
   - Material context-menu still creates new materials (step 2).
   - Selecting a physics-enabled node freezes / un-freezes its physics (step 1).
   - Shadow rendering looks correct (step 1 regression -- the sweep is in the same file as `sort_lights`).

Log output should show no new warnings. No new mutexes added, no existing ones
removed.

## Out of scope

- `gltf.cpp` / `operations_window.cpp` direct uses of `begin_mesh_rt_update` /
  `end_mesh_rt_update`.
- `Scene_layers` -- already a clean nested helper.
- Anything in `erhe::scene` itself.
