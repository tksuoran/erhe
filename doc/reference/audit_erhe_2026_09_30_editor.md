# erhe editor (src/editor) architecture and API audit

Read-only audit, 2026-09-30. Scope: src/editor excluding src/editor/renderers
(covered by another auditor; its composition from editor.cpp is included).
All paths are relative to src/editor unless prefixed with doc/ or
src/erhe. Every claim was checked by reading the code at the cited line.

Size (wc -l, .cpp + .hpp, this slice): mcp 29.9k, scene 20.3k, tools 19.0k,
windows 17.8k, transform 15.3k, operations 12.3k, parsers 11.4k,
geometry_graph 10.2k, texture_graph 10.0k, assets 5.4k, rig 5.2k, xr 4.8k,
brushes 3.6k, graph_editor 3.2k, animation 3.1k, developer 2.9k,
rendergraph 2.2k, graph 1.9k, prefabs 1.9k, physics 1.7k, content_library 1.6k,
top-level files 15.2k (editor.cpp 4482).

--------------------------------------------------------------------------------
## 1. Architecture

### 1.1 App_context: a service locator with 106 pointer members

- app_context.hpp:141-347 is one class with 106 `{nullptr}` pointer members
  (grep count), plus 10 plain flags/ints (lines 144-165), 7 config pointers
  (167-175), the executor and async counters (177-182), and three real
  behaviors: `get_async_in_flight_count()` / `is_scene_load_in_flight()`
  (app_context.cpp:62-75) and `on_item_property_changed()`
  (app_context.cpp:19-60), which is the single place that maps property
  consequence flags to editor actions (asset dirtying, draw-list rebuild,
  lighting-changed message). That last function is a good design; the rest of
  the class is a flat namespace of everything.
- The header itself is light: forward declarations only (lines 7-139),
  `<atomic> <thread> <vector>`. Including app_context.hpp costs little; the
  weight is semantic (every part can reach every other part).
- Every part receives `App_context&` (Tool: tools/tool.hpp:60-61,104;
  Operation::execute(App_context&): operations/operation.hpp:194-195;
  Mcp_server: mcp/mcp_server.hpp:102-106,498). Operations receive the whole
  context on execute/undo, so an operation can touch anything, and the
  dependency of an operation on editor parts is invisible from its type.
- Stale entries: `Imgui_window_scene_views` (app_context.hpp:86) is forward
  declared and defined nowhere in src/editor (grep); `editor::Debug_renderer`
  and `editor::Jolt_debug_renderer` (66, 91) shadow the erhe::renderer types
  actually stored at 242-244 and have no editor definition; `sleep_margin`
  carries `// TODO` (159). `Scene_views` lives in a file named
  viewport_scene_views.hpp (scene/viewport_scene_views.hpp:68), the Editor
  member is `m_viewport_scene_views` (editor.cpp:636), the context field is
  `scene_views` (app_context.hpp:346).

### 1.2 Part construction: documented as taskflow, actually serial

- editor.cpp:1477-1478 define
  `ERHE_TASK_HEADER(var)` = set init-status line + profile scope, and
  `ERHE_TASK_FOOTER(ops)` = `}` + clear status line. The `ops` argument is
  never expanded, so every `.name("...").succeed(a, b, c)` chain
  (e.g. editor.cpp:2051, 2194-2195, 2406-2416, 2535) is dead text. There is
  no `tf::Taskflow` in editor.cpp beyond the include (line 235);
  `m_executor` (1442) is used for runtime async work only. The construction
  is a straight-line sequence of 25 named blocks (1773-2537) followed by a
  post-construction phase (2545-2950). doc/editor/editor.md "Initialization
  Order" states this correctly ("Initialization is serial"); the stale
  dependency lists in the macros and the comment in
  editor_settings_store.hpp:125-126 ("Parts are constructed in parallel init
  tasks; registration must be thread safe") do not.
- Hidden ordering constraints, all enforced only by comments:
  `fill_app_context()` after every part (2564); `m_input_bindings_store->load()`
  after every part registered commands (2568); `Mcp_server` after fill (2584);
  `m_hotbar->get_all_tools()` / `m_inventory_window->collect_tools()` (2593-2594);
  scene_created subscription before `run_startup_script()` (2790-2795, 2835);
  operation stack drained before `prewarm_all` (2854); the init command buffer
  closed last (2895-2907). The destructor mirrors this with explicit ordered
  teardown (2914-2975) and cites source line numbers that have drifted
  ("line 1501", "line 1471" at 2941-2943).
- The `Editor` class is a 4000-line local class in editor.cpp (313-4328):
  constructor 1367-2950 (~1580 lines), `tick()` 332-1180 (~850 lines),
  `run()` 3947-4062, plus scene-close leak watchdog, startup script
  interpreter (3591-3946) and event handlers. It is the largest single unit
  in the slice.

### 1.3 Lifetime rules (scene close, undo removal)

- The rules in doc/editor/coding_rules.md ("Scene-hosted references") match
  the code: 19 parts subscribe to `close_scene` and 19 to `items_removed`
  (grep lists in section 5 below). `Editor::on_close_scene()` (3288-3442)
  clears selection, operation history (3346), partitioner, baker working set,
  graph window targets, viewport bindings; a 60-frame leak watchdog
  (3444-3590, `k_scene_close_leak_check_frames` 4136) logs survivors with
  holder counts. This is a strength: the bug class is instrumented, not
  merely documented.
- Closing a scene drops the whole undo history (`clear_history`, 3346) - an
  architectural consequence of operations holding strong references to
  content; see 1.6.
- `Operation_stack` verifies the main thread (operation_stack.hpp:306-316,
  425); `Asset_manager` has `verify_main_thread` (assets/asset_manager.hpp:525);
  `s_item_tasks` in items.cpp:39 has none (doc/plans/editor_improvements.md
  section 3 already lists it).

### 1.4 Message bus vs direct calls

- app_message_bus.hpp:261-301: 24 typed buses with per-type dispatch policy
  (sync / queue / both). Messages are documented in place
  (app_message.hpp), including why each is queued (e.g. 154-169, 175-189).
- Direct calls dominate: of the 106 App_context pointers, most are dereferenced
  directly at runtime. The bus carries lifecycle and cross-cutting change
  notifications (selection, hover, scene created/closed, items removed,
  lighting changed); the per-frame pump is `App_message_bus::update()`
  (editor.cpp:759). Reasonable split; the risk is that `Hover_scene_view_message`
  carries raw `Scene_view*` (app_message.hpp:31, 41) with a documented
  use-after-free history (#256) instead of a weak handle.

### 1.5 Tool / command / input-binding layering

- `erhe::commands::Command_host` -> `Tool` (tools/tool.hpp:57) adds priority,
  viewport rendering and tool properties; `Tools` (tools/tools.hpp:153) holds
  the registry and the priority tool. 18 `Tool` subclasses (grep). Commands
  are `erhe::commands::Command` objects bound by `Commands`;
  `Input_bindings_store` (input_bindings_store.hpp:19-34) persists overrides
  and is change-driven (no polling), a clean example.
- tools/tools.hpp includes 12 concrete tool headers (117-134) although it only
  needs `Tool*`; 30 files include it (grep), so a change to any tool header
  rebuilds them all.
- `Tools::set_priority_tool` calls `set_priority_boost` (tools/tools.cpp:62,70)
  before `sort_bindings()` (98), the ordering issue doc/plans/editor_improvements.md
  section 6 records.

### 1.6 Undo/redo

- `Operation` (operations/operation.hpp:189-243): execute/undo plus
  reference-retention hooks (`collect_item_references`, `on_lossless_undo`,
  `drop_payload`) for the asset manager. 34 direct subclasses + 29
  Mesh/Compound-derived (grep). `Operation_stack` (operation_stack.hpp:317-444)
  has queue / execute_now / queue_from_thread, undo groups for MCP batch
  (359-361), history clearing and `free_undone_loads`.
- No merging/coalescing of consecutive operations exists (grep for
  merge/coalesce/absorb in operation_stack.cpp: none). Granularity is one
  operation per completed drag (dependency_property_rows: "one operation per
  completed drag", doc/editor/editor.md), which is the right unit.
- `Compound_operation` (compound_operation.hpp:10-29) has no rollback on
  partial failure (doc/plans/editor_improvements.md section 1 agrees).
- UI and MCP share the operation types but not always the path:
  - MCP `create_light` builds the Light directly then inserts via
    `Item_insert_remove_operation` (mcp_server_scene_action.cpp:3120-3140): undoable.
  - MCP `edit_light` (3207-3310) and `edit_camera` (3336-3365) mutate the
    item under `item_host_mutex` with no operation - not undoable - while the
    same edits from the Properties window go through
    `Property_set_operation` (dependency_property_rows.cpp, 19 operation
    references). Same document field, two different undo behaviors.
  - MCP `set_item_property` does use `Property_set_operation`
    (mcp_server_properties.cpp:378), so a reflective path already exists.
- Undo holes in the UI: lightmap_window.cpp:605-609 writes
  `scene_root->get_scene_settings().lightmap_tile_overrides` (scene document
  state, persisted) directly; item_tree_window.cpp:855-880
  `fork_brush_with_material` adds a new Brush to the content library
  (`library.add(forked)` / `set_parent`) without an operation, called from
  the material drop at 1034.

### 1.7 Selection and hover

- `Selection` (tools/selection_tool.hpp:170) is the single mutation API,
  host-scoped, with an active item and active scene (doc/editor/selection.md
  matches). Additional selection notions: `Mesh_component_selection`
  (tools/mesh_component_selection.hpp:144, its own message pair),
  graph-editor node selection (graph_editor_window_base.hpp:58
  `collect_selected_nodes`), `Range_selection` inside Selection,
  `Operation_stack_selection` (a redo-target chooser, not a selection:
  operations/operation_stack_selection.hpp) - the last is a naming collision.
- Hover: `Scene_view` owns six `Hover_entry` slots (scene/scene_view.hpp:46-107,
  310) filled by raytrace, ID render and analytic providers
  (`App_context::analytic_hover_providers`, app_context.hpp:217), merged by
  ray-t (263). Clear and documented.

### 1.8 Window system

- `erhe::imgui::Imgui_window` (src/erhe/imgui/erhe_imgui/imgui_window.hpp:21)
  is the base; 39 editor subclasses (grep). Hosts: `Window_imgui_host`
  (desktop) and `Rendertarget_imgui_host` (rendertarget_imgui_host.hpp:23,
  in-scene UI for Hud/Hotbar/XR). `Viewport_window` (windows/viewport_window.hpp:26)
  shows a `Viewport_scene_view`; `Scene_views` (scene/viewport_scene_views.hpp:68)
  creates/destroys/persists them; `Editor_windows` (windows/editor_windows.hpp:43)
  owns dynamically created extra windows with deferred creation via
  `Imgui_windows::queue()`.
- Imgui_window's constructor takes `bool developer = false`
  (imgui_window.hpp:29), against the no-bool-args rule (library code, but the
  editor is its main client).
- `Headset_view` inherits `Scene_view + Renderable + Imgui_window +
  enable_shared_from_this` (xr/headset_view.hpp:131-135): one object is a
  view, an overlay renderer and a window.

### 1.9 Settings / config persistence

- Code-generated config structs (config/definitions/*.py -> config/generated),
  loaded via `erhe::codegen::load_config` (editor.cpp:1371-1375).
  `Editor_settings_store` (editor_settings_store.hpp:54-137) owns
  editor_settings.json + user_state.json, collect callbacks, change detection
  by string compare, `touch()` from edit sites (change-driven, matches
  AGENTS.md). Versioning/migration exists (`_version`, pre-v4 seeding
  comment at 28-30; settings v5 migration in memory bank).
  AI-driven runs are read-only (main.cpp:31-33).
- `App_settings` (app_settings.hpp:110-173) mixes settings root, graphics
  limits, presets and ephemeral UI state (`node_tree_show_all`, 155); its
  API uses bool parameters (`update(bool allow_save)`, `read(bool openxr)`,
  `save_presets_if_dirty(bool, bool)`: 39-40, 66, 122, 131).

### 1.10 MCP server

- 272 statically dispatched tools (mcp/mcp_server.cpp:558-832 table, 272
  entries; config/editor/mcp_tools.json 272 descriptors) plus every
  registered editor command as a fallthrough tool (835-845). Dispatch is a
  member-function-pointer table (mcp_server.hpp:148-154) chosen to avoid MSVC
  C1061 (comment 140-142).
- `Mcp_server` is one class with ~300 handler declarations
  (mcp_server.hpp:195-497) implemented across 17 .cpp files; the handlers
  include 237 distinct editor/erhe headers (grep over mcp/*.cpp) - the server
  knows every subsystem's internals. Coupling is mostly through App_context
  and public APIs; the only `friend class Mcp_server` is Operation_stack
  (operation_stack.hpp:418). It also keeps many per-tool deferral state
  members (mcp_server.hpp:596-781: drag steps, gesture steps, screenshot
  annotations, irradiance/RC/scene-image queries) inside the server object.
- Threading: HTTP thread enqueues, main thread drains in tick
  (editor.cpp:684-687); queue depth and timeouts are bounded
  (mcp_server.hpp:544-551). The design is sound; the size is the issue:
  30k lines, of which ~24k is handler code that mostly re-implements argument
  parsing and result serialization per tool by hand. doc/agents/mcp_api_guidelines.md
  (no dependence on UI-panel state) is followed by the handlers sampled.

### 1.11 Three node-graph editors

- geometry_graph (10.2k) and texture_graph (10.0k) share
  `Graph_editor_window_base` (graph_editor/graph_editor_window_base.hpp:35,
  986-line .cpp) plus templated serialization/operations/asset CRTP helpers
  (graph_editor/graph_serialization.hpp:34,125; graph_operations.hpp:58-197;
  graph_asset.hpp:26). Each window overrides 17-18 virtuals (grep). Editing
  goes through operations (`execute_now`: 6 and 7 uses).
- graph/ (Graph_window, Shader_graph, 1.9k) does NOT use the shared base
  (graph/graph_window.hpp:42 derives from Imgui_window directly), has its own
  node set (add/subtract/...), is constructed at editor.cpp:2155 and is the
  "experiments"-grade shader graph. It is a third, unshared implementation.
- texture_graph/nodes/texture_node_descriptors.cpp (4269 lines) is a
  hand-written table of `Node_descriptor` builders (e.g. `build_japanese_glyphs`
  is 918 lines, 3150-4068); 2852 quoted strings. This is data expressed as C++.

### 1.12 XR integration

- `ERHE_XR_LIBRARY_OPENXR` appears at 117 sites in 40 files (grep). Nine
  parts take `Headset_view&` in their constructors (grep, outside xr/) and 20
  runtime call sites read `context.headset_view`. Headset_view owns quad
  views, the shadow node, controller visualization, pick camera, perf plots
  (headset_view.hpp members). XR is woven through Hud, Hotbar, Selection
  (`setup_xr_bindings`, editor.cpp:2558), Brush_tool and Physics_tool
  headers (brushes/brush_tool.hpp:35, physics/physics_tool.hpp:35).

### 1.13 Asset pipeline

- `Asset_manager` (assets/asset_manager.hpp:230, 2175-line .cpp) covers
  identity, ownership, container records, userships, pins, load tasks,
  dirty marking and removal announcements. Async loads are
  `Asset_load_task::tick()` state machines (assets/asset_load_task.hpp:128-139)
  advanced under `Frame_load_budget` from `Editor::tick` (editor.cpp:600-618),
  with `Scene_commit_queue` landing worker results first (593). This is a
  deliberate, well-documented pipeline (doc/editor/async_asset_loading.md).
- `Content_library` (content_library/content_library.hpp:74) is an
  `Item_host` with kind scopes and per-item metadata; one per Scene_root
  (scene/scene_root.hpp:568).

--------------------------------------------------------------------------------
## 2. API issues

1. God classes: `Editor` (editor.cpp:313-4328); `App_context` (106 pointers);
   `Scene_root` (scene/scene_root.hpp:192, 45 members: layers, physics world,
   raytrace scene, draw lists, material/light sets, five node systems, USD
   records, variant table, browser window, trigger log; scene_root.cpp 2733
   lines, 99 member functions); `Mcp_server` (~300 handlers);
   `Operations` window class (operations/operations_window.cpp, 86 methods:
   every geometry op, save/load scene, export glTF, create material /
   physics material / joint settings / brush, modal dialogs).
2. Boolean parameters: 76 in this slice's headers (grep), e.g.
   operations_window.hpp:129,155-182 (`remesh(..., bool regenerate_attributes)`,
   `catmull_clark(bool generate_facet_texcoords)`), transform/subtool.hpp:52-58
   (`get_basis(bool world)`), transform_tool.hpp:335-338 (`bool local`),
   app_rendering.hpp:139,144 (`bool include_content, bool include_overlay`),
   app_settings.hpp:39-131. `Viewport_scene_view::update_hover(bool ray_only = false)`
   (scene/viewport_scene_view.hpp:141). Compare `Node_transform_first_execute`
   (node_transform_operation.hpp:48) which does it right.
3. `struct` vs `class`: 48 `struct` declarations (22 in app_message.hpp:27-238,
   mixed with `class` message types at 76, 124, 199 in the same file), 2 in
   mcp_server.hpp:74,533, 2 in editor.cpp:2669.
4. `auto` locals: 1018 `auto x =` / `const auto` in non-renderer .cpp files
   (grep) against "prefer explicit types".
5. Naming: getters without `get_` next to ones with it
   (`Scene_views::hover_scene_view()/last_scene_view()` scene/viewport_scene_views.hpp:181-182
   vs `get_viewport_config_data()` 76; `App_settings::config()/user_state()/settings_store()`
   app_settings.hpp:139-146; `Grid_frame::world_from_grid()`; `Handle_visualizations`
   `has_target()` fine but `initial_twist()`). `Operation_stack_selection` is
   not a selection. `Scene_views` vs file `viewport_scene_views.hpp` vs member
   `m_viewport_scene_views`.
6. Header weight: scene/scene_view.hpp includes `<geogram/mesh/mesh.h>` (line 12)
   and tools/debug_visualizations.hpp (7) for a `GEO::index_t facet` field and a
   `Debug_visualizations` member; 37 files include it (grep). scene_root.hpp is
   included by 128 files and pulls app_message.hpp, message_bus, scene_host,
   light_set, material_set, draw_list deps, variant_table (3-12).
   tools/tools.hpp pulls 12 tool headers (117-134) into 30 includers.
7. Duplicated concepts: viewport/camera: `erhe::math::Viewport` (window and
   projection viewports, viewport_scene_view.hpp:163-164), `Viewport_config`,
   `Viewport_window`, `Viewport_scene_view`, `Scene_views`, `Quad_view`,
   `Four_view`, `Headset_view`, `Scene_preview` (preview/scene_preview.hpp:44),
   `Render_context::views`. Light creation exists in four places with
   different defaults: scene/scene_builder.cpp:1327-1385, scene/scene_commands.cpp
   (`create_new_light`, called at 137), mcp_server_scene_action.cpp:3120-3134,
   windows/item_tree_window.cpp:2316-2321 (new-scene default light).
8. Immediate-mode UI writing document state without operations: 1.6 above
   (lightmap_window.cpp:605; item_tree_window.cpp:876-880; MCP edit_light /
   edit_camera). Settings writes (physics_window.cpp:53-63 into
   `editor_settings->physics`) are settings, not document, and are acceptable.
9. "Update each frame" patterns:
   - editor.cpp:955-961 rebuilds `std::vector<glm::ivec3> override_values`
     from scene settings and calls `m_lightmap_baker->set_tile_overrides()`
     every tick whenever a baker and an active scene exist;
     lightmap_window.cpp:611-613 confirms ("the editor tick mirrors the scene
     settings only next frame"). The change site already exists (the window),
     so the per-frame mirror is exactly what AGENTS.md forbids.
   - editor.cpp:939-946 compares `is_offline_bake_active()` against a cached
     bool per frame (edge detection by comparison).
   - `Scene_view::tick_scene_and_camera_restore()` (scene_view.hpp:207-211)
     is a per-frame no-op-once-resolved poll, documented as such.
10. Raw `Scene_view*` in messages and Tool caches (app_message.hpp:31,41;
    tool.hpp:112-113) with explicit "destroyed" notifications instead of a
    weak handle.

--------------------------------------------------------------------------------
## 3. Code health

- Largest files (lines): mcp/test/mcp_server_tests.cpp 5810, editor.cpp 4482,
  texture_node_descriptors.cpp 4269, mcp_server_scene_action.cpp 4075,
  parsers/usd.cpp 4037, operations_window.cpp 2997, transform_tool.cpp 2930,
  scene_root.cpp 2733, debug_visualizations.cpp 2678, mcp_server_scene_query.cpp 2563,
  item_tree_window.cpp 2552, mcp_server_ui.cpp 2429, asset_manager.cpp 2175,
  headset_view.cpp 2166, mcp_server.cpp 2116.
- Longest functions (brace-matched, non-test): `build_japanese_glyphs` 918
  (texture_node_descriptors.cpp:3150), `Headset_view::render_headset` 689
  (xr/headset_view.cpp:905), `Lightmap_window::imgui` 668 (windows/lightmap_window.cpp:768),
  `Settings_window::imgui` 579 (windows/settings_window.cpp:145),
  `Lightmap_texture_window::imgui` 564, `Handle_visualizations::render` 502
  (transform/handle_visualizations.cpp:843), `Animation_window::curve_canvas` 455,
  `Mcp_server::query_node_details` 426, `Rotate_tool::render` 401
  (transform/rotate_tool.cpp:331), `Operations::imgui` 389,
  `Viewport_scene_view::execute_rendergraph_node` 388, `Mcp_server::action_create_shape` 384.
  Plus `Editor::tick` ~850 and the Editor constructor ~1580 (class-local,
  not caught by the scan).
- TODO/FIXME/XXX/HACK: 186 in this slice (34 in physics/physics_window.cpp,
  13 in xr/headset_view.cpp, 8 rendertarget_imgui_host.cpp, 7 editor.cpp).
- Commented-out code: 257 lines matching code-like `//` patterns; 17 `#if 0`
  blocks; e.g. editor.cpp:2611-2626 (dead status-bar code), 700-701,
  1139-1151 (old swap/sleep blocks). net_test.cpp (607 lines) is a separate
  terminal net test program built from the editor CMakeLists (line 1317).
- printf family: crash_handler.cpp:42-109 (`fprintf(stderr)` inside the
  crash handler, defensible) and main.cpp:67 (`--help` output). No other
  violations of the logging rule found.
- Per-frame allocations in render paths (AGENTS.md "steady-state frames
  perform no allocations"): `Rotate_tool::render` builds
  `std::vector<vec3> disc_positions`, `disc_indices`, `inside_major_ticks`,
  `inside_minor_ticks`, `inside_segments`, `sector_positions`, `sector_indices`
  every frame while dragging (rotate_tool.cpp:446-590);
  `Handle_visualizations::render` builds `visible_lines`, `lines`, `solid_tips`
  (handle_visualizations.cpp:1020, 1118, 1145) every frame the gizmo is
  visible; `Lattice_tool::tool_render` builds `std::vector<Line> lines`
  (tools/lattice_tool.cpp:299); `Debug_visualizations::selection_visualization`
  builds `ndc_points` / `projected_convex_hull_points` (debug_visualizations.cpp:1587,1603);
  `Selection_tool` builds `entry_hosts` per range update (selection_tool.cpp:146);
  editor.cpp:957 (`override_values`, allocates when overrides exist).
  Counter-examples that follow the rule: `m_geogram_progress` (editor.cpp:2637),
  Mcp_server `Input_gesture_steps::clear()` keeping capacity (mcp_server.hpp:661-671),
  Selection's retained scratch views (doc/editor/selection.md).
- Thread-safety notes already tracked in doc/plans/editor_improvements.md
  section 3 (Tools priority pointer, Transform_tool_shared atomic, s_item_tasks).

--------------------------------------------------------------------------------
## 4. Strengths

- Lifetime discipline is engineered, not hoped for: scene-close leak watchdog
  (editor.cpp:3444-3590), `Items_removed_message` with O(1) lookup contract
  (app_message.hpp:231-251), operation reference collection for the asset
  manager (operation.hpp:197-229), main-thread verification in
  Operation_stack and Asset_manager, `get_editor_references` MCP tool and
  `scripts/undo_reference_clearing_smoke_test.py`.
- The property system integration: one consequence-flag mapping
  (app_context.cpp:19-60), `Property_set_operation` used by both the
  Properties rows and MCP `set_item_property`, one operation per completed drag.
- Change-driven persistence: `Editor_settings_store::touch()` + compare +
  autosave (editor_settings_store.hpp:83-100), `Input_bindings_store`
  (input_bindings_store.hpp:17-18 "nothing polls for changes"),
  `Graphics_settings::apply_active_preset` replacing a former per-frame apply
  (app_settings.hpp:43-49).
- Message policies are explicit per type with the reason documented at the
  message (app_message.hpp:154-169, 175-189, 231-251).
- The MCP server is a real test surface: bounded queue, deferral protocol for
  multi-frame tools (mcp_server.hpp:556-571), `reset_editor_state`, undo
  groups for `batch`, health endpoint semantics (mcp_server.hpp:530-537),
  bearer auth; the headless editor makes end-to-end verification scriptable
  (doc/testing.md:36-70).
- The construction rule "constructors store App_context& only; siblings are
  explicit constructor arguments" is applied consistently in the blocks read
  (editor.cpp:2348-2401 passes 8-10 explicit references per part) and is
  documented in three places (coding_rules.md, editor.md, editor_windows.hpp:40-42).
- Async asset loading with frame budgets and a single commit point at the top
  of tick (editor.cpp:588-618) keeps worker results from racing the frame.
- Operation-stack re-entrancy and undo-group semantics are spelled out
  (operation_stack.hpp:306-361).

--------------------------------------------------------------------------------
## 5. Test coverage

- mcp_server_tests: 87 `TEST_F` cases (mcp/test/mcp_server_tests.cpp),
  covering tools/list, scenes, materials (many round trips), batch/undo
  grouping, JSON-RPC edge cases, auth, IK/rig verbs, undo of imports clearing
  references, input injection and ImGui driving, screenshot annotation,
  shadow acne gates. Editors run as CTest fixtures on ports 3773/3774
  (mcp/test/CMakeLists.txt:93-141; doc/testing.md:36-70).
- Unit tests in-slice: transform/test 55 (IK solver), rig/test 37,
  assets/test 17, brushes/test 15, renderers/test 17.
- Python verification scripts driving the headless editor: 57 scripts in
  scripts/, including scene_roundtrip_verify.py (full glTF round trip with
  schema validation), undo_reference_clearing_smoke_test.py, gi_verify.py,
  shadow_verify.py, ik_*_verify.py, texture_graph_smoke_test.py,
  viewport_input_focus_verify.py.
- Parts with close_scene subscriptions (19): animation_player, animation_window,
  asset_manager, brush_tool, brdf_slice, depth_visualization_window, editor,
  thumbnails, grid_tool, operations_window, physics_tool, brush_preview,
  material_preview, bone_visualization, material_paint_tool, weight_display,
  transform_tool, properties (+ the test). items_removed (19): similar set
  plus create, scene_root, selection_tool, geometry_spreadsheet_window,
  item_tree_window.
- Untested or thinly tested: Operation_stack itself has no unit test (only
  `operation_stack_selection` is dependency-free and unit-testable by design,
  operation_stack_selection.hpp:22-24); Compound_operation partial failure;
  the undo holes in 1.6 (edit_light/edit_camera undo, lightmap override undo,
  brush fork undo) have no test because no test expects them to be undoable;
  Editor::tick ordering; XR paths (no headless XR); the shader graph
  (graph/); Settings window / graphics preset apply; most `windows/` ImGui
  code except what mcp UI-driving tests touch; `Scene_commands` and the
  startup-script interpreter (editor.cpp:3676-3946) beyond what default
  scenes exercise.

--------------------------------------------------------------------------------
## 6. Future development options (prioritized)

P1. Route every document mutation through operations, from both entry points
    (small-medium). Helps: `Property_set_operation` + property metadata
    already exist and MCP `set_item_property` uses them
    (mcp_server_properties.cpp:378). Do: make MCP `edit_light` / `edit_camera`
    build Property_set_operations (or one Compound) instead of direct setters
    (mcp_server_scene_action.cpp:3251-3310, 3336-3365); wrap
    `lightmap_tile_overrides` writes (lightmap_window.cpp:605) and the brush
    fork (item_tree_window.cpp:876-880) in operations; add a test that every
    mutating MCP tool leaves exactly one undo entry (the `batch` test at
    mcp_server_tests already asserts this pattern for one case). Benefit:
    closes undo holes, makes UI and MCP behave identically, and is a
    precondition for P3.

P2. Delete the vestigial taskflow annotations or make them real (small).
    editor.cpp:1477-1478 ignore the `ops` argument; either remove the
    `.name().succeed()` text from all 25 blocks so the code says what it
    does, or keep them and build a `tf::Taskflow` from them once the GL
    worker-context question in doc/editor/editor.md "Initialization Order"
    is settled. Also fix the stale comment at editor_settings_store.hpp:125.

P3. Reflection-driven MCP layer (medium, high payoff on 30k lines).
    The property system already provides typed, named, flagged properties
    and `get_item_properties` / `set_item_property` tools. Move item edits
    (`edit_light`, `edit_camera`, `edit_material`, `edit_physics_body`,
    `edit_joint`, `set_scene_settings`) onto a generic
    "set properties on item" verb with schema generated from property
    metadata, and generate `mcp_tools.json` descriptors for those from the
    same metadata instead of hand-maintaining 272 JSON entries checked by
    `validate_tool_list_against_dispatch` (mcp_server.hpp:160). Hinders:
    tools that are workflows, not edits (bake, import, gestures) stay
    hand-written; keep them but they become the minority.

P4. Split `Editor` (editor.cpp) into: Frame_loop (tick + pacing, 332-1180),
    Editor_bootstrap (construction sequence), Scene_lifecycle (on_scene_created /
    on_close_scene / leak watches, 3220-3590), Startup_script (3591-3946),
    Window_events (3109-3162) (medium). Helps: the tick already has named
    breadcrumb phases (`set_breadcrumb`, e.g. 592, 599, 619) that are natural
    method boundaries. doc/plans/editor_improvements.md section 2 already
    lists this.

P5. Scoped contexts instead of one App_context (medium-large). Group the 106
    pointers into a handful of typed facades (Graphics_context: device,
    renderers, mesh_memory; Scene_context: app_scenes, selection,
    operation_stack, asset_manager; Ui_context: imgui, windows, icon_set,
    hotbar; Tools_context) and have parts take the facades they need. Helps:
    constructors already take explicit sibling references (editor.cpp:2348-2401),
    so the dependency graph is mostly visible at construction; the runtime
    reads are the part to migrate. Hinders: `Operation::execute(App_context&)`
    is a universal escape hatch used by 63 operation classes; give operations
    a narrower `Operation_context` first.

P6. Fold graph/ (shader graph) into graph_editor or delete it (small).
    graph/graph_window.hpp:42 bypasses `Graph_editor_window_base`; the node
    set (add/sub/mul/div/constant/load/store/passthrough) is 349 lines of
    real logic. Either port it to the base to prove the base is general, or
    move it to experiments/ and out of the editor build.

P7. Data-driven texture node descriptors (small-medium).
    texture_node_descriptors.cpp (4269 lines, 2852 string literals, a 918-line
    function) is a table; move it to JSON/TOML under res/ or a codegen
    definition, loaded by `erhe::texgen` at startup. Removes the largest
    non-test .cpp in the editor and makes node authoring a data change.

P8. Zero-allocation gizmo rendering (small). Give `Rotate_tool`,
    `Handle_visualizations`, `Lattice_tool`, `Debug_visualizations` persistent
    scratch members cleared at point of use (the pattern AGENTS.md prescribes
    and `m_geogram_progress` / Mcp_server gesture scratch already follow).
    Sites listed in section 3.

P9. Remove the per-frame lightmap override mirror (small). Have
    `Lightmap_window` (the change site, lightmap_window.cpp:605-620) and scene
    load / undo be the only callers of `set_tile_overrides`; delete
    editor.cpp:955-961.

P10. Header hygiene (small): drop `<geogram/mesh/mesh.h>` from
    scene/scene_view.hpp:12 (store `uint32_t facet` or forward-declare via a
    small `Geo_index` alias header), drop the 12 concrete includes from
    tools/tools.hpp:117-134, remove dead forward declarations in
    app_context.hpp:66,86,91.

P11. Extract a reusable editor framework from erhe-specific code (large,
     speculative). Candidates that are already generic: Imgui_window/hosts,
     Editor_settings_store + codegen configs, Operation/Operation_stack,
     Message_bus, Commands/Input_bindings_store, Editor_windows, Mcp_server
     transport (mcp_server.cpp routing/queue, mcp_server_shared.hpp helpers).
     What hinders: App_context threading through everything (P5 first),
     `Operation::execute(App_context&)`, and Scene_root owning the physics
     world, raytrace scene, draw lists and node systems together (extract
     `Physics_selection_freezer` etc. per doc/plans/editor_improvements.md
     section 7 as a first step).

P12. Multi-document is largely present (multiple Scene_root, host-scoped
     selection, per-scene content library, viewport unbinding) except undo:
     `clear_history()` on close (editor.cpp:3346) throws away every scene's
     history because the stack is global. A per-Scene_root operation stack
     (or history entries tagged by host and dropped selectively) would make
     closing one scene keep the others' undo. Cost medium; the reference
     collection machinery (operation.hpp:207) already knows which items each
     operation retains.

P13. Scripting: the startup script interpreter (editor.cpp:3676-3946) and
     the MCP dispatcher are two command surfaces over the same operations.
     Making the startup script a sequence of MCP tool calls (JSON, same
     dispatcher via `dispatch_tool_call`, mcp_server.hpp:123) would delete
     one interpreter and give scripts the whole tool set. Depends on P1/P3
     so that scripted mutations are undoable and reproducible.

--------------------------------------------------------------------------------
## Appendix: doc vs code checks

- doc/editor/coding_rules.md "Part construction": consistent with the
  construction blocks read (editor.cpp:2348-2401) and with Editor_windows
  (editor_windows.hpp:40-42). Enforced by convention only; no static check.
- doc/editor/editor.md "Initialization Order: Initialization is serial":
  correct; the ERHE_TASK_FOOTER `.succeed(...)` text contradicts it visually.
- doc/editor/editor.md lists `Debug_renderer`, `Text_renderer`, etc. under
  GPU subsystems: matches editor.cpp:1789-1886.
- doc/editor/operations.md: matches operation.hpp / operation_stack.hpp;
  does not mention that MCP `edit_light` / `edit_camera` bypass operations.
- doc/agents/mcp_api_guidelines.md: handlers sampled (create_light,
  edit_light, edit_camera, set_item_property) take explicit arguments and do
  not read UI-panel state; consistent.
- memory-bank/topics/editor.md retention traps (scratch retention,
  self-sustaining pins, async task handles) match the code patterns seen
  (items.cpp:39-43 purge, Selection scratch views).
