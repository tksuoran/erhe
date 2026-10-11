# Render phase audit: what R read and wrote outside its own state (2026-10-09)

The inventory taken at commit c06738609 (2026-10-09), when the editor's
render phase still read live editor state, before the ImGui overlap and the
frame pipelining of `doc/editor/render_thread.md` were built. Three
read-only passes over the tree inventoried every read the render phase (R,
`Editor::frame_render_phase`) made of state the main thread writes, every
write R made into main-thread state, and every GPU recording or
render-object mutation reachable from an ImGui window's `imgui()`. Every
site listed here has since been moved into the prepare phase, given a
per-frame copy, or deferred out of the ImGui iteration under the rules of
`doc/editor/render_thread.md` section 2.6; the document is kept as the
classification method and the list of sites to re-check when a new render
read appears. Line numbers are from the audited commit and drift; the
function names are the stable reference.

Kinds: **S** scalar copy (tears, undefined behavior as a race but no crash);
**C** container iteration, span or raw pointer into a container (crash on
reallocation); **P** `shared_ptr` / `weak_ptr` copy, lock or reset (crash);
**DO** `Dependency_object::get_value()`, a binary search over the object's
entry vector that a main-thread `set_value()` of another property of the
same object may `insert` into (in practice C); **RMW** read-modify-write
from both threads.

## 1. Main-thread work that runs inside R

These are not reads to snapshot; they are M2 work in the wrong phase. R
sends synchronous messages and mutates scene, tool and renderer state.

| Site | What runs in R | Writes |
|---|---|---|
| `Viewport_scene_view::execute_rendergraph_node` sends `render_scene_view` (sync) | `Transform_tool::on_render_scene_view` -> `update_for_view`; `Hotbar::on_render_scene_view_message` | `Transform_tool` shared entries, anchor, component mode, `m_transform_target_note` (read by its window); `Lattice_tool::m_active`; `Handle_visualizations` view scale and selection box; hotbar node transform (OpenXR) |
| `Headset_view::render_headset` | `scene.update_node_transforms()`; sends `hover_scene_view` and `render_scene_view` (sync) to 16 subscribers; `Hover_tool::on_message` queues `hover_mesh` on the bus from R; `view_resources->update` sets camera projections and parents | whole-scene world transforms; every tool's hover scene view; message bus queue |
| `Tools::render_viewport_tools` -> every `Tool::tool_render` | transform tool (handles, rays via raytrace BVH queries, readouts), rotate tool, hover tool, paint and weight paint, physics tool, brush tool, create tool, lattice tool, mesh component selection tool, grid tool, draw mode renderer, HUD | `Hover_tool::update_ancestor_hover_flags` sets item flag bits on scene nodes and rewrites its ancestor list; `Mesh_component_selection::prune` erases entries; `Handle_visualizations::get_octant_signs` writes a `mutable` member also written by `pick` on main; `Debug_renderer::set_line_bias_margin` |
| `App_rendering::render_viewport_renderables` -> `Renderable::render` | `Debug_visualizations::render`, DDGI and RC probe overlays, hand tracker, headset rays | `Debug_visualizations::m_selection_bounding_volume` (read by its window); `m_shadow_fit_dump_requested` cleared |
| `Grid::render`, `App_rendering::update_sky_parameters` | write `Composition_pass::data` (`enabled`, grid and sky parameters) of the grid and sky passes | the same fields `Composition_pass::imgui` (Composer window) edits |
| `Rendertarget_mesh::render_done` | `m_material->set_slot_sampler` | a live `Material` the material set syncs and Properties reads |
| `Thumbnails::update` -> `Material_preview` / `Brush_preview::render_preview` | preview scene edits (`set_primitive_material`, `set_mesh_parent`), preview `Material_set::sync_library` over the whole content library, `Mesh_memory::flush(frame_cb)` | preview scenes shared with M2 callers (Properties material preview, geometry graph node previews); `Thumbnails` slot `callback` / `item` / `observer` (assigned by `Thumbnails::draw` from ImGui, read and reset by R without a lock) |
| `Joint_buffer::update` | writes `Skin::skin_data.joint_buffer_index` into the live skin | skin objects |
| `Light_set::resolve` (shadow node, `fit_trace_light_projections`) | rebuilds the per-scene `Scene_root::get_light_set()` | the shared light set |
| `Post_processing_node::update_size`, `Viewport_scene_view::m_render_target.update`, `update_overlay_render_target`, `Id_renderer::update_framebuffer` | recreate textures and render passes when the viewport size changed | textures the Viewport window hands to `Imgui_renderer::image` |
| `Shadow_render_node::execute_rendergraph_node` | `reconfigure(..., command_buffer)` when the preset changed | shadow textures |

Debug and text renderer recording happens only in R today (every `get()` /
`print` call site is inside the functions above); nothing records during
ImGui or M2.

## 2. State R reads that the ImGui windows write

### 2.1 Settings structs (S unless noted)

`App_context::editor_settings` sections, written in place by the Settings
window (`add_config_section`), the Properties window and MCP: `sky`, `grid`,
`shadow_frustum_fit`, `selection_outline`, `render_style_appearance`,
`selected_render_style_appearance`, `debug_visualizations_style`,
`transform_tool`, `mesh_component_style`, `content_edge_lines`, `ddgi`,
`radiance_cascades` (the DDGI and RC renderers hold `const&` to these),
`exclude_unlit_primitives`, `indirect_diffuse_source`, `headset`. Readers:
composition passes, render style, shadow node, sky renderer, grid, DDGI and
RC ticks and renders, `frame_render_phase` itself.

`App_settings::graphics`: `current_graphics_preset` is assigned as a whole
struct (with a `std::string` name) by `apply_preset` from the Settings
window, and read field by field in `Composition_pass::render`,
`Shadow_render_node`, `fit_trace_light_projections` (shadow technique, bias,
filter, cull, light count limits). `headlight_when_unlit`, `sky_enabled`,
`grid_visible`, `get_use_draw_lists()` (optional override): Settings, MCP.

`Scene_root::get_scene_settings()` optional overrides (`sky`, `grid`,
`shadow_frustum_fit`): `scene_settings_resolve` returns a reference into an
optional the Properties window engages and resets (P-like).

### 2.2 Per-view state (`Scene_view`, `Viewport_scene_view`, `Headset_view`)

| Field | Writer | Kind |
|---|---|---|
| `m_projection_viewport` / `m_window_viewport` (read several times per frame; sizes the render target and the camera view) | `Viewport_window::imgui_viewport` -> `set_window_viewport` | S struct |
| `m_scene_root` (weak, locked about 10 times per frame) | Scene View Config window `set_scene_root`; `Headset_view::imgui` | P |
| `m_camera` (weak) | Scene View Config window `set_camera`; `resolve_pending_scene_and_camera` | P |
| `m_viewport_config` (`Render_context.viewport_config` is a reference) | Visual Style popup `Viewport_config_window::imgui`; `shadow_mode` in Viewport Config window | S struct |
| `m_shader_debug` | toolbar combo `set_shader_debug` | S |
| `m_is_scene_view_hovered`, `m_position_in_viewport` | `Viewport_window::imgui_viewport`, `on_mouse_move`; MCP | S |
| `m_pick_probe_frames` | MCP `arm_pick_probe`; R decrements | RMW |
| `m_debug_visualizations` settings (`shadow_debug`, `skins`, every toggle) | Debug Visualization popup | S |
| `get_shadow_fit_override_camera()` (weak) | Scene View Config window | P |
| `m_hover_entries[]` (`Hover_entry`: weak mesh, weak grid, `shared_ptr<Geometry>`, optionals), `m_world_from_control` | M2 hover (`update_hover_info`), headset pointer, MCP pick; read by hover, paint, weight paint, transform readout, headset rays in R | C + P |
| `Scene_image_view::m_camera` (shared, a live scene camera when MCP named one) | MCP | P |
| `Headset_view::m_viewport_config`, `m_selected_shader_debug`, `m_request_renderdoc_capture` (R clears), `m_finger_inputs`, `m_head_attached_ray_mode`, `Xr_actions` poses | `Headset_view::imgui`, menu, commands, M2 `update_actions` | S / C / RMW |

### 2.3 Cameras (live `erhe::scene::Camera` through `Camera_view_input` raw pointers)

Every consumer dereferences the live `Projection` and `Node` during the
frame (`Camera_buffer::update`, `Draw_list_renderer` frustum planes,
`Content_wide_line_renderer::set_view_params`, `Sky_renderer`,
`Id_renderer::render`, `Light_projections::apply` for the directional fit,
`Shadow_render_node` diagnostics). Reads: `projection()` fields,
`world_from_node_transform()`, `node_from_world()`, `get_exposure()` (DO),
`get_shadow_range()` (DO), `position_in_world()`. Writers: Properties window
camera and transform rows (`set_value`, immediate), the navigation gizmo in
`Viewport_window::imgui`, fly camera, transform tool. `fit_trace_light_projections`
iterates `scene.get_cameras()` (C).

### 2.4 Lights

`Light_set::resolve` iterates the light layer and copies `shared_ptr<Light>`
(C + P); `Light_projections::apply` and `Light_buffer::update` read type,
frame, world transform, intensity, color, temperature, range, spot angles,
`casts_shadow` through raw `Light*` (S); `Sky_renderer::resolve_sun_direction`
loops the light layer (C); `copy_shadow_projections` copies `get_name()`
(C: a `std::string` copy). Writers: Properties light rows (mirrored scalars,
immediate), transform edits, light add / remove (M2 operations).

### 2.5 Scene containers, meshes, skins (non-draw-list paths)

The draw-list path (`use_draw_lists`, default on for the desktop, off for
OpenXR) renders from `Draw_list_scene` records, a main-thread snapshot.
Everything else reads the live scene:

- `Composition_pass::render` -> `Forward_renderer::render` ->
  `bucket_primitives`; the edge-line `feed_pass`; `Id_renderer::render`;
  `Shadow_render_node` (empty checks, spans to `Shadow_renderer`);
  `Shadow_renderer::render` AABB gathering; `Scene_tlas::update` (DDGI, RC,
  ray trace); `Content_bounds` (DDGI, RC): `get_mesh_layer_by_id`, the
  `meshes` vectors held as spans, `get_skins()`, `mesh->get_primitives()`,
  `Mesh_primitive.material` (P), `world_from_node()`, flag bits (visible,
  selected, hovered, active item), `skin` and `skin_data`, `point_size`,
  `line_width`, `lightmap_uv_scale_offset`, `get_double_sided()` (DO on the
  material), `Scene::get_ambient_light()`. Bucket entries keep raw `Mesh*`
  between bucketing and upload (C).
- `Shader_key::derive` reads material properties (DO) and the normal map
  texture reference (P) per primitive on the bucket path; `exclude_unlit`
  reads `get_bxdf_model()` (DO); `Scene_tlas` reads `get_transmission()`
  (DO). On the draw-list path material slot spaces are uploaded in M2 by
  `update_material_sets` and R only binds them.
- `Joint_buffer::update` iterates `skin_data.joints` and reads joint world
  transforms (C + S).
- `Debug_visualizations::render` walks nodes, mesh layers, lights, cameras,
  layouts, skins, lightmap tiles and `selection->get_selected_items()` (C).
- Writers: operations (queued to `Operation_stack::update` in M2 except
  `execute_now` in the Lightmap and Inventory windows, and Undo / Redo
  buttons and menu entries, which run immediately); Properties rows
  (immediate `set_value`); selection and flag edits from the Item tree
  (immediate); brush drag-and-drop preview nodes added and removed from the
  scene inside `Viewport_window` drag handling.

### 2.6 Tool and renderer state read by `tool_render` / `Renderable::render`

`Tool::m_enabled`, `Tool::m_hover_scene_view` (raw pointer), transform tool
`shared.settings` and `shared.entries` (vector of `shared_ptr<Node>`,
rebuilt by `update_target_nodes` from selection messages fired by the Item
tree), hover and active handles, rotate tool members, `Grid_tool::m_grids`
(vector; "Add Grid" / "Remove Grid" buttons push and erase: C), grid
members, `Hover_tool` checkboxes, `Mesh_component_selection` entries and
mode and `m_external_hover` (Geometry Spreadsheet window `set_external_hover`),
`Brush_tool::m_drag_and_drop_brush` and hover frame (written by Viewport
window drag-and-drop), `Create` shape parameters (Create window),
`Lattice_tool::m_active`, `Physics_tool` properties, `Paint_tool` and
`Weight_paint_tool` properties, `Selection::get_selected_items()` and
`get_active_scene_root()` (C / P), `Debug_renderer::m_anti_aliasing`
(Settings window), `App_rendering::debug_target_joint` (weak) and
`debug_joint_colors` (Weight display), `App_rendering::m_renderables`
(iterated without its mutex), `App_rendering::m_trigger_capture` (RMW),
`Post_processing_node` parameters and `weights` vector (Post Processing
window sliders), `Ray_trace_renderer::m_enabled` and config (Ray Trace
window), `Ddgi_renderer` / `Radiance_cascades_renderer` requests and merge
mode, `Lightmap_baker` options, tile config and tiles (Lightmap window,
including `clear_tiles` which `ERHE_VERIFY`s no tick plan is open),
`Depth_to_color_rendergraph_node::m_light_index` and its render target,
`Brdf_slice_rendergraph_node::m_material` (P), angles and render target,
`Rendertarget_imgui_host::m_enabled` (HUD "Mesh visible", Hotbar),
`Rendertarget_mesh` LOD bias (HUD slider).

### 2.7 ImGui infrastructure

`GImGui` is a plain global switched by `Scoped_imgui_context` on both
threads; `Imgui_renderer::m_mutex` (recursive) serializes
`draw_imgui_windows` (held per host for the whole host draw) against R's
host nodes, so today they block rather than race. `ImGui::NewFrame`
invalidates the `ImDrawData` R reads; `ImFontAtlasUpdateNewFrame` in
`Imgui_renderer::begin_frame` mutates the font atlas texture status R's
`update_draw_data_textures` consumes; `Imgui_renderer::image` pushes into
`m_draw_texture_references` that R's `render_draw_data` moves out;
`Window_imgui_host::begin_imgui_frame` recreates the swapchain render pass
R's host node uses. All of these are safe under step 11's ordering (the
host nodes run after the main thread published the frame's draw data, and
the next `NewFrame` runs after the join); they are listed because that
ordering is the argument, and a TSan run confirms it.

## 3. State R writes that the ImGui windows read

| R writer | Main reader | Kind |
|---|---|---|
| `Composition_pass` statistics (`m_last_*`) | `Composer::imgui` | S |
| `Shadow_render_node::m_light_projections` (cleared and rebuilt), `m_last_draw_statistics`, `m_last_cpu_time_us` | Depth Visualization window, `Debug_visualizations`, MCP | C |
| `Post_processing_node::level_widths`, `weights`, textures (`update_size`) | Post Processing window | C |
| `Viewport_scene_view` content, overlay and depth textures; `Depth_to_color` and `Brdf_slice` render targets | `Viewport_window`, Depth Visualization and Properties windows (`Imgui_renderer::image`) | P |
| `Frame_log_window::on_frame_end` swaps `m_frame_entries` | Frame log window | C |
| `Gpu_timer_impl::store_last_result_ns` (device completion in R's `end_frame`) | Performance window | S |
| `Id_renderer` transfer entries (completion handler in R's `end_frame`) | `update_hover_with_id_render` (M2, after the join) and MCS scan results | ordered by the join |
| `Transform_tool` shared state and note, `Lattice_tool::m_active`, `Mesh_component_selection` entries, item hover flags, `Debug_visualizations::m_selection_bounding_volume` | Transform tool window, Geometry Spreadsheet, Selection window, Item tree, Debug Visualizations popup | C / RMW (all section 1) |

## 4. GPU recording and render-object mutation reachable from `imgui()`

Immediate today; each races R once the windows draw during R.

| Site | Reached from | What it does |
|---|---|---|
| `Properties::material_properties` | Properties window, selected material | `Material_preview::resize` + `update_rendertarget` (texture and render pass recreation) + `render_preview(*current_command_buffer)` (a render pass into the upload cb, preview material set writes); the same object is driven by R's thumbnails |
| Properties "BRDF Slice" | Properties window | `Brdf_slice_rendergraph_node::set_material`, `set_enabled`, `set_area_size` -> `Render_target::update` on a node R executes |
| `Scene_commands::create_new_rendertarget` | Create menu (host begin callback), Create window, Hierarchy context menu (`deferred_operations` run at the end of `Item_tree::imgui_tree`, still inside ImGui) | `Rendertarget_mesh` texture and mesh upload into the cb, a clear pass, `Rendertarget_imgui_host` and rendergraph node registration, `create_viewport_scene_view` (shadow node creation into the cb, post-processing and overlay nodes, `rendergraph.connect`) |
| `create_viewport_scene_view` via Open Four View, Hierarchy "Open Editor" | Window menu, Hierarchy | as above; "deferred" through `Imgui_windows::queue`, whose `flush_queue` runs at the end of `draw_imgui_windows`, inside the ImGui phase |
| Undo / Redo buttons and menu entries | Operations window, Edit menu | run `Operation::undo` / `execute` immediately: viewport resources operation (view and shadow node create / destroy), scene open and glTF import (blocking `Image_transfer` slot-0 `submit_command_buffer_and_wait`), mesh operations |
| Lightmap window "Save All Tiles", "Clear All Tiles", "Revert Tiling", "Batch Process All Tiles" | Lightmap window | bake-slot command buffers, `submit_command_buffers` + `wait_idle`; layout, tiles and uv mapping rewrites R's `record_tick` reads |
| Asset browser "Reference Material into Scene", "Import" (.usd), "Instantiate as prefab" (synchronous paths); graph node `Asset_ref::resolve` retries; Save glTF over a loaded prefab | Asset browser, texture and geometry graph windows, Operations window | blocking container loads with slot-0 submit and wait |
| Depth Visualization window | `set_shadow_renderer_node` (unlocked `rendergraph.connect` / `disconnect`), `set_enabled`, `Render_target::update` |
| DDGI and Radiance Cascades windows | `set_indirect_diffuse_source` -> renderer `set_selection` (releases probe and state textures), `set_merge_mode` (`release_state_textures`) |
| Viewport size | `Viewport_window::imgui_viewport` -> `set_window_viewport` writes `m_projection_viewport` at once; R recreates the content, post-processing and overlay textures from it |
| `Window_imgui_host::begin_imgui_frame` | every frame | `update_render_pass` recreates the swapchain render pass on resize |
| Menu commands | `App_windows::viewport_menu` is the host begin callback | `Command::try_call()` runs inside `Imgui_windows::begin_frame` |

Deferred today and fine: `Operation_stack::queue` (applied in `update`, M2),
`Imgui_windows::queue` (but see above), graphics preset application
(queued `graphics_settings` message; the preset struct itself is assigned
immediately), texture graph and geometry graph evaluation (`update` in M2),
`Hotbar::set_slots` (`rebuild_if_needed`, M2), `Texture_file_loader`
previews and imports (tick in M2), `Mesh_memory` interactive uploads
(`flush` in M2), asynchronous glTF loads (`Asset_manager::tick`, M2),
scene creation and loading (queued messages, bus pump in M2).

Lock order: an ImGui window that creates a host or rendergraph node holds
the ImGui renderer mutex and then takes the rendergraph mutex; R holds the
rendergraph mutex across `execute` and takes the ImGui renderer mutex in
its host nodes.
