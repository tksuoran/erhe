# erhe rendering pipeline audit (above the GPU abstraction)

Read-only architectural and API audit, 2026-09-30. Scope: src/erhe/rendergraph,
src/erhe/renderer, src/erhe/scene_renderer, src/erhe/geometry_renderer,
src/erhe/raytrace, src/erhe/texgen, src/editor/{renderers,rendergraph,graphics,
preview} plus the editor frame composition (editor.cpp, app_rendering,
scene/viewport_scene_view, xr/headset_view) and the matching docs. Every claim
below was verified by reading the referenced lines; nothing was built or run.

Slice size (non-test .cpp/.hpp): rendergraph 1.1k, renderer 5.4k, scene_renderer
19.0k, raytrace 5.9k, texgen 4.0k, editor/renderers 23.9k, editor/rendergraph
2.2k, editor/graphics 1.5k, editor/preview 1.4k lines.

--------------------------------------------------------------------------------
## 1. Architecture

### 1.1 The render graph is a node-ordering helper, not a resource graph

- `Rendergraph` owns `std::vector<Rendergraph_node*>`, a `erhe::graph::Graph` for
  topological sort, and a deferred-resource list; that is the whole class
  (src/erhe/rendergraph/erhe_rendergraph/rendergraph.hpp:24-58).
- `execute()` sorts lazily, then loops enabled nodes and calls
  `execute_rendergraph_node(command_buffer)` on each, wrapping each in a debug
  group (rendergraph.cpp:81-136). There is no pass declaration phase, no
  resource table, no lifetime analysis, no aliasing, no barrier derivation.
- Edges carry an integer key (`Rendergraph_node_key`, resource_routing.hpp:5-14)
  and nothing else; a consumer pulls its input texture at execute time by
  walking the FIRST link of the matching input pin
  (rendergraph_node.cpp:36-58, `links.front()`), then calling the producer's
  `get_producer_output_texture(key)` (rendergraph_node.hpp:78-82). A key with
  several producers is silently truncated to one.
- Node "depth" is maintained by `connect()` and propagated by `set_depth()`
  (rendergraph.cpp:224, rendergraph_node.cpp:213-229) but nothing reads it
  except the depth propagation itself; the sort comes from `erhe::graph`.
- Side channels are the norm: shadow data does not flow through the pin, the
  consumer `static_cast`s the input node to `Shadow_render_node` and reads
  `Light_projections` from it (src/editor/scene/viewport_scene_view.cpp:1102-1103,
  src/editor/scene/scene_image_capture.cpp:115,
  src/editor/developer/depth_visualization_window.cpp:80-86). The doc admits
  this ("connection keys guarantee the source node type",
  doc/erhe/rendergraph.md "Data Flow Model").
- Deferred destruction exists (`defer_resource`, rendergraph.cpp:138-141) and has
  exactly one user, `Post_processing_node::update_size`
  (src/editor/rendergraph/post_processing.cpp:126-135).
- `execute()` takes `m_mutex` for the whole frame (rendergraph.cpp:85) and
  `register_node`/`unregister_node` take the same mutex; registering a node
  from inside `execute_rendergraph_node` would deadlock. Nothing does today,
  but nothing documents the rule either.

Verdict: a correct, small DAG scheduler with pull-based texture lookup. It is
not a frame graph; every transient/aliasing/barrier decision lives inside nodes.

### 1.2 Transient resources and barriers

- Each node owns persistent GPU resources: `Render_target` recreates color,
  MSAA color, depth/stencil textures and a `Render_pass` on resize
  (render_target.cpp:50-224); `Post_processing_node` holds per-level texture,
  render pass and timer vectors as public members (post_processing.hpp:78-91).
  No node shares or aliases memory with another.
- Barriers are encoded per attachment in `Render_pass_descriptor`
  (`usage_before/after`, `layout_before/after`, render_target.cpp:160-205) and
  derived inside `erhe::graphics::Render_pass`; cross-node hazards outside
  render passes are hand-placed `command_buffer.memory_barrier(...)` calls
  (viewport_scene_view.cpp:288-291 compute->vertex; :468-471 same for wide
  lines; :486 shadow-map vertex-stage read). doc/editor/post_processing.md
  "Render pass synchronization" documents the mip-chain case in detail.
- Resize races are handled ad hoc: `Render_target::update()` is called from
  inside the node's execute (viewport_scene_view.cpp:508) and simply drops the
  old textures; only post-processing defers.

### 1.3 Per-frame upload model

- Ring buffers: `erhe::graphics::Ring_buffer` + `Circular_ring_buffer_algorithm`
  with per-acquire sync entries stamped with the device frame index and
  reclaimed by `frame_completed()` (ring_buffer.hpp:20-60, ring_buffer.cpp:108-160,
  :229-233). Frames in flight: Vulkan 2 (vulkan_device.hpp:462), GL 3
  (gl_device.hpp:350), Metal 3 (metal_device.hpp:251), null 2.
- Exhaustion returns an empty range (`return {}` at ring_buffer.cpp:117-119);
  callers like `Light_buffer::update` then `ERHE_VERIFY` on span size
  (light_buffer.cpp:620-622). There is no grow path and no stall-and-retry;
  the buffers are sized by `Program_interface_config` maxima
  (program_interface.hpp:23-38) and the per-pass writes are always full size
  ("always fill in data max lights", light_buffer.cpp:616-618).
- `Scene_pass_resources::begin_pass()` writes camera/joint/light ranges per
  pass and returns them in a `Pass_state` (scene_pass_resources.hpp:139-156);
  `Material_set` is persistent and only rewritten on change serial
  (doc/erhe/scene_renderer.md "When Material_set::update() writes"). Primitive
  records and indirect commands are written per bucket / chunk per pass
  (forward_renderer.cpp:238-239, draw_list_scene.cpp:1452-1457).
- Mesh vertex/index memory: `Buffer_pool` slabs with free-list allocators; freed
  ranges are RETIRED and applied from a frame-completion handler
  (buffer_pool.hpp:30-72), and pools are keyed by `Vertex_stream` pointer for
  the multi-draw base_vertex lockstep invariant (buffer_pool.hpp:85-121).

### 1.4 Where the editor bypasses the graph

All of these record GPU work into the frame command buffer BEFORE
`m_rendergraph->execute()` (editor.cpp:1084-1087) and therefore have no node,
no pin, no debug group at graph level and no way to be reordered by the sort:

- material set updates (editor.cpp:851-857),
- radiance cascades tick and DDGI tick + irradiance query (editor.cpp:858-891),
- lightmap partitioner / baker tick and streamer update (editor.cpp:894-1081),
- the ID pass, run from inside the viewport node but outside its render pass
  (viewport_scene_view.cpp:220-231),
- debug-line and content-wide-line compute pre-passes, atmosphere LUT
  generation, and the GPU ray tracer (viewport_scene_view.cpp:277-506),
- previews and thumbnails render their own render passes at their call sites
  (src/editor/preview/scene_preview.cpp:149-221, material_preview.cpp:164-173,
  graphics/thumbnails.hpp:57-94), and Headset_view renders inside an XR frame
  callback that ends and resubmits the command buffer mid-graph
  (headset_view.cpp:905-1593, `submits_command_buffer()` contract at
  rendergraph_node.hpp:66-73).

The graph's actual content per viewport is Shadow -> Viewport -> Post -> Overlay
-> Imgui host (viewport_scene_views.cpp:370-435).

### 1.5 Coupling between erhe::scene_renderer and editor types

The library layer is clean of editor includes: `Forward_renderer`,
`Shadow_renderer`, `Draw_list_scene`, `Scene_pass_resources` name only
erhe::graphics / erhe::scene / erhe::primitive types. The editor couples the
other way, heavily:

- `Render_context` (src/editor/renderers/render_context.hpp:44-67) carries
  `App_context&`, `Scene_view&`, `Viewport_config&`, `Viewport_scene_view*` and
  is handed to every tool, renderable and pass.
- `Composition_pass::render` reaches through `context.app_context` to
  `app_rendering`, `editor_settings`, `time`, `app_settings`,
  `forward_renderer`, `draw_list_renderer`, `content_wide_line_renderer`
  (composition_pass.cpp:120, 262, 313-314, 332, 358-360, 374).
- `App_context` exposes 36 renderer-ish raw pointers among ~106 fields
  (app_context.hpp:170-247), the de-facto service locator.
- `Shadow_render_node` takes `App_context&` and reads editor settings each
  frame (shadow_render_node.hpp:39-50, :123 "refreshed from editor settings
  each frame").

### 1.6 How passes reuse the same code

- Viewport, XR, shadow, ID, preview and MCP capture all end in
  `Forward_renderer::render()` / `Draw_list_scene::draw_*` /
  `Forward_renderer::draw_primitives()` through the shared
  `Base_render_parameters` prologue (scene_pass_resources.hpp:39-93). Multiview
  is a `std::span<const Camera_view_input>` (views.size() >= 2 selects the
  `SHADER_MULTIVIEW_COUNT` axis, forward_renderer.cpp:122-127) so the same
  render call serves desktop and XR (headset_view.cpp:993-1010,
  viewport_scene_view.cpp:191-213).
- The ID pass duplicates the bucket walk instead of reusing it
  (id_renderer.cpp:282-330 `render_buckets`), and borrows the joint buffer
  through `Forward_renderer::get_joint_buffer()` (id_renderer.hpp:118-122;
  acknowledged in doc/plans/draw_list_renderer.md item 7).
- `Shadow_renderer` still owns its own `Joint_buffer`, `Light_buffer`,
  `Camera_buffer`, `Primitive_buffer`, `Draw_indirect_buffer`
  (shadow_renderer.hpp:255-262) although `Scene_pass_resources` exists for
  exactly this (also in doc/plans/draw_list_renderer.md item 7).
- Scene image capture reuses the viewport chain minus the ImGui consumer
  (doc/editor/rendergraph.md "Scene image capture"), which is the strongest
  evidence the graph composition is reusable.

### 1.7 Material / shader / program-interface model

- One uber shader pair, variants keyed by `Shader_key` (int axes for light
  counts, shadow filter/bias/technique/depth bits, debug mode, multiview
  count; bool axes via `ERHE_SHADER_BOOL` X-macro, shader_key.hpp:113-160),
  compiled through `Shader_variant_cache` and prewarmed
  (forward_renderer.cpp:381-526).
- `Program_interface` defines every UBO/SSBO layout at runtime from
  `Shader_resource` blocks and holds the one bind group layout
  (program_interface.hpp:40-72); buffer classes (`Camera_buffer`, `Light_buffer`,
  ...) mirror the block offsets (light_buffer.hpp:29-124).
- Materials: persistent slot spaces (`Material_set`), one per Scene_root for
  the bucket path and one per `Draw_list_scene` for the cached path
  (draw_list_scene.hpp:102-121); textures through a `Texture_heap` owned by the
  set. The two sets are the same C++ type, so a mix-up compiles
  (doc/plans/draw_list_renderer.md item 7).
- Draw-list entries cache the resolved `Reloadable_shader_stages` per
  (multiview count, sub-variant) and invalidate on a `Color_environment` change
  (draw_list_scene.hpp:39-57, draw_list_scene.cpp:1294-1351).

### 1.8 Culling

None on the color path. `Forward_renderer::render` buckets every mesh of every
span each pass (forward_renderer.cpp:136-151); `Draw_list_scene::draw_color`
iterates all lists and only filters by item flags (draw_list_scene.cpp:1519-1594,
1436-1446). The plan acknowledges it: doc/plans/draw_list_renderer.md item 1
"Frustum culling on the entry AABB ... Q6 deferred culling". The only culling
is the per-light caster contribution cull inside the shadow frustum fit
(shadow_renderer.cpp:300-306 comment). A grep for frustum/occlusion in the
renderer code finds only material occlusion-texture fields.

### 1.9 Draw submission model

- Both paths issue `multi_draw_indexed_primitives_indirect` per bucket
  (forward_renderer.cpp:244-250) or per chunk of a draw list
  (draw_list_scene.cpp:1473-1479), with commands written CPU-side into a ring
  range each pass (`Draw_indirect_buffer::update`, forward_renderer.cpp:239,
  draw_list_scene.cpp:1457). Per-primitive data is indexed by draw id
  (`ERHE_DRAW_ID`, draw_list_scene.cpp:1425-1427).
- It is indirect in form only: the indirect buffer is CPU authored every
  frame; there is no GPU-side command generation, no count buffer, no
  culling compute. The chunk limit is `Primitive_buffer::get_max_primitive_count()`
  (draw_list_scene.cpp:1427).

### 1.10 "No per-frame allocation" rule: honored and violated

Honored (the scratch pattern):
- `Shadow_renderer::m_caster_world_aabbs/m_receiver_world_aabbs/
  m_caster_vertex_extents` members cleared per call (shadow_renderer.hpp:265-270,
  shadow_renderer.cpp:308-320).
- `Composition_pass::m_mesh_spans` reused (composition_pass.hpp:156,
  composition_pass.cpp:237-241).
- `Content_wide_line_compute_renderer::m_dispatches` is a member
  (content_wide_line_compute_renderer.cpp:134).
- `Material_set::update()` gated by change serial; `Draw_list_scene::
  check_material_changes()` costs one compare per material when idle
  (draw_list_scene.cpp:793-826); its `changed` / `to_reregister` locals only
  allocate on the rare change.

Violated (allocations on the steady-state frame path):
- `std::vector<Render_bucket> buckets;` as a local per pipeline per pass:
  forward_renderer.cpp:136 (every composition pass on the bucket path),
  shadow_renderer.cpp:170 (per light per frame), id_renderer.cpp:296 (per
  hovered frame), plus `Render_bucket` itself holds vectors of entries.
- `fmt::format(...)` into `erhe::utility::Debug_label` (which owns a
  `std::string`, debug_label.hpp:23-50) per bucket per frame:
  forward_renderer.cpp:210-222, shadow_renderer.cpp:180-190; per draw list per
  frame: draw_list_scene.cpp:1576-1581 (the doc lists this as remaining cost,
  doc/plans/draw_list_renderer.md item 8).
- `std::vector<std::shared_ptr<Mesh>> filtered_meshes` per ID render
  (id_renderer.cpp:250-262).
- Headset path: `std::vector<Camera_view_input> view_inputs` and
  `std::vector<erhe::renderer::View> debug_views` per XR frame
  (headset_view.cpp:993-1036).
- editor tick: `std::vector<glm::ivec3> override_values` rebuilt from scene
  settings every frame while a baker exists (editor.cpp:960-967).
- `Draw_list_scene::flush_pending` and `Material_set::flush_pending` swap the
  pending vector into a local (draw_list_scene.cpp:1180-1183,
  material_set.cpp:391-395), which discards the member's capacity every frame
  that had ops, so the next enqueue reallocates.
- `Composition_pass::render` mutates ANOTHER pass's `primitive_settings`
  every call (composition_pass.cpp:118-151, "TODO This is a bit hacky"), and
  viewport_scene_view.cpp:373-383 does the same thing again for the wide-line
  feed; both are per-frame pushes of editor settings, which AGENTS.md "No
  update each frame" forbids. Same pattern: content edge-line config pushed
  each frame at viewport_scene_view.cpp:253-260 (comment says "each frame")
  and headset_view.cpp:1083-1087; lightmap options pushed each frame at
  editor.cpp:911-929 (`set_tile_config`, `set_cell_size`, `set_options`) and
  `set_lightmap_bicubic` at editor.cpp:897.

--------------------------------------------------------------------------------
## 2. API issues

### 2.1 Boolean arguments (17 in slice headers, excluding tests)

- `Composer::render(context, bool include_content, bool include_overlay)`
  (composer.hpp:38) and `App_rendering::render_composer(...)` (app_rendering.hpp:144),
  `App_rendering::render_viewport_main(context, bool include_overlay)`
  (app_rendering.hpp:139), called as `render_viewport_main(context,
  !m_post_processing_enabled)` (viewport_scene_view.cpp:528).
- `Shadow_render_node::reconfigure(device, cb, int, int, int, bool
  distance_technique, int, int)` (shadow_render_node.hpp:61): eight positional
  scalars including a bool.
- `Viewport_scene_view` ctor `int msaa_sample_count, bool enable_post_processing`
  (viewport_scene_view.hpp:95-106).
- `App_rendering::make_composition_pass(name, data, bool selected)`
  (app_rendering.hpp:162-166), `set_grid_visibility(bool)` (:148).
- `Light_buffer::get_sampler(bool compare)` (light_buffer.hpp:158).
- `Id_renderer::render_buckets(..., bool use_id_ranges, ...)` called with a bare
  `true` (id_renderer.cpp:276).
- Public mutable flags: `Id_renderer::enabled` (id_renderer.hpp:60),
  `Composition_pass_data::enabled/ignore_exposure/overlay` (composition_pass.hpp:42-53).

### 2.2 Over-wide and hazardous parameter structs

- `Forward_renderer::Render_parameters` has 20 fields, and two are REFERENCE
  members with braced temporaries as default initializers:
  `const glm::uvec4& debug_joint_indices{0xffffffffu, 0, 0, 0};` and
  `const std::span<glm::vec4>& debug_joint_colors{};`
  (forward_renderer.hpp:137-138). A reference bound to a temporary in a default
  member initializer is ill-formed per CWG 1696 and dangles where compilers
  accept it; every call site happens to set both so the defaults never bind,
  which is why it has not bitten.
- `Shadow_renderer::Render_parameters::mesh_spans` and `Id_renderer::
  Render_parameters::content_mesh_spans` are `const std::initializer_list<...>&`
  members (shadow_renderer.hpp:108-112, id_renderer.hpp:108). The aggregate is
  built at the call site with `.mesh_spans = { layers.content()->meshes }`
  (shadow_render_node.cpp:661) so the initializer_list's backing array lives to
  the end of the full expression only; it works because `render()` is that
  expression. Storing the struct would dangle.
- `Shadow_renderer::Render_parameters` has 30 fields (shadow_renderer.hpp:87-200),
  `Composition_pass_data` 21 fields including five `std::function`s
  (composition_pass.hpp:85-99) evaluated per pass per frame.
- `Base_render_parameters` mixes per-pass GPU inputs with `Grid_parameters` and
  `Sky_parameters` that only two passes read (scene_pass_resources.hpp:84-91).

### 2.3 Hidden ordering requirements

- Debug renderer: `begin_frame` -> tools record -> `compute()` (compute encoder)
  -> barrier -> render pass -> `render()` -> `end_frame()`; deferring `end_frame`
  across nodes is only safe because "each view's chain executes contiguously"
  (viewport_scene_view.cpp:530-541, and the pending-frame release at :237-241).
  This relies on `Graph::sort()` picking the earliest-registered eligible node,
  an undocumented property of a different library.
- `Renderable::render(const Render_context&)` is called twice per frame, once
  with `encoder == nullptr` (record lines) and once inside the render pass
  (viewport_scene_view.cpp:271 and :529); the interface (renderable.hpp:7-14)
  says nothing about the two-phase contract; doc/editor/rendering.md 54-70
  does.
- `Light_buffer::update` must run before `bind_*` (light_buffer.hpp:448 comment).
- `Content_wide_line_renderer`: "Call once per frame between begin_frame() and
  the first ..." (content_wide_line_renderer.hpp:85).
- `Rendergraph_node` pins must be registered after the base constructor and
  before `connect()` (doc/erhe/rendergraph.md "Pin Registration Timing");
  nothing enforces it beyond a logged error.
- `App_scenes::flush_draw_lists()` / `update_material_sets()` must precede the
  graph (editor.cpp:828-857 comments); the dependency is only a code position.

### 2.4 God classes

- `App_rendering` (app_rendering.hpp:103-249): shadow node factory, capture,
  grid state, sky parameters, renderable registry, composition pass factory,
  public `shared_ptr<Composition_pass>` fields other classes mutate per frame.
- `Lightmap_baker` (lightmap_baker.hpp 997 lines, ~88 methods, ~116 members;
  lightmap_baker.cpp 6120 lines with `tick` 479 lines and `bake_gbuffer` 300).
- `Radiance_cascades_renderer` (~99 members, 2909-line cpp), `Ddgi_renderer`
  (~76 members, 1939 lines).
- `Viewport_scene_view` is at once a `Scene_view`, a `Texture_rendergraph_node`
  and an `enable_shared_from_this` (viewport_scene_view.hpp:89-93), with a
  387-line execute (viewport_scene_view.cpp:169-556).
- `Headset_view::render_headset` is 688 lines (headset_view.cpp:905).

### 2.5 Header weight

- `rendergraph_node.hpp` pulls `erhe_graphics/texture.hpp`, `erhe_graph/node.hpp`
  (Item hierarchy) and `<mutex>` into every node header (rendergraph_node.hpp:3-16).
- `forward_renderer.hpp` includes nine sibling headers (20 includes) so any
  consumer sees every buffer class; `light_buffer.hpp` includes
  `erhe_graphics/device.hpp`, `erhe_scene/camera.hpp`, `erhe_scene/light.hpp`
  (light_buffer.hpp:3-13).
- `scene_view.hpp` includes `<geogram/mesh/mesh.h>` (scene_view.hpp:12) for one
  `GEO::index_t` field, dragging geogram into every scene-view consumer.

### 2.6 Duplicated concepts

- Viewport/camera descriptions: `erhe::math::Viewport`, `Camera_view_input`
  (camera_buffer.hpp:178), `erhe::renderer::View` (view.hpp), the XR
  `Render_view`, `Render_context::viewport` + `::views` + `::camera`, and
  `Scene_view::get_camera_viewport()`; headset_view.cpp:1013-1035 converts one
  to another per eye per frame.
- Render-target description: `Render_target_create_info`,
  `Texture_rendergraph_node_create_info` (same fields plus key), `Warmup_target`
  (forward_renderer.hpp:173-186), `Render_pass_descriptor`.
- Two material slot spaces of one type (see 1.7); two draw paths with one
  eligibility predicate re-evaluated per pass (composition_pass.cpp:312-326).
- Two ways to say "render only some passes": `Render_content` enum
  (render_context.hpp:38-42) AND the `include_content/include_overlay` bools.
- Two per-frame timers hand-rolled the same way (`Cpu_timer_scope` in
  composition_pass.cpp:80-100 and shadow_render_node.cpp:408-428).

### 2.7 Ownership

- Nodes are raw `Rendergraph_node*` in the graph, owned elsewhere by
  `shared_ptr`; destruction order is made safe by the `m_is_registered`
  sentinel (rendergraph.cpp:28-37), not by ownership.
- `Post_processing_node` exposes all pyramid resources as public members
  (post_processing.hpp:78-97); `Composer::composition_passes` and its mutex are
  public ("TODO Move to children", composer.hpp:42-44).
- `App_rendering::composition_passes()` returns the vector without the mutex
  and documents "do not call from the render thread" (app_rendering.hpp:178-185).

--------------------------------------------------------------------------------
## 3. Code health

### 3.1 Largest files / functions (non-test)

Files: lightmap_baker.cpp 6120, radiance_cascades_renderer.cpp 2909,
ddgi_renderer.cpp 1939, draw_list_scene.cpp 1652, primitive_renderer.cpp 1515,
mesh_memory.cpp 1281, id_renderer.cpp 1247, lightmap_partitioner.cpp 1142,
post_processing.cpp 989, light_buffer.cpp 964, shadow_renderer.cpp 950.

Functions over 250 lines: `Headset_view::render_headset` 688
(headset_view.cpp:905), `Shadow_renderer::render` 534 (shadow_renderer.cpp:277),
`Lightmap_baker::tick` 479 (lightmap_baker.cpp:5217),
`Viewport_scene_view::execute_rendergraph_node` 387 (viewport_scene_view.cpp:169),
`Lightmap_baker::bake_gbuffer` 300 (:3257), `Primitive_renderer::add_capsule` 293
(primitive_renderer.cpp:690), `Shadow_render_node::execute_rendergraph_node` 290
(shadow_render_node.cpp:403), `Id_renderer::render` 279 (id_renderer.cpp:762),
`Primitive_renderer::add_torus` 265 (:1247), `Shadow_render_node::reconfigure`
252 (shadow_render_node.cpp:108).

### 3.2 Duplication

- Bucket walk + pipeline lookup + bind + MDI: forward_renderer.cpp:153-254,
  shadow_renderer.cpp:196-274, id_renderer.cpp:282-380 are three copies.
- Environment key construction duplicated between render and prewarm
  (forward_renderer.cpp:110-128 vs :403-415) and reimplemented in
  `Color_environment::make_environment_key` (draw_list_scene.hpp:53-56).
- Content-wide-line feed lambda `feed_pass` duplicated between
  viewport_scene_view.cpp:301-363 and headset_view.cpp:1094-1140.
- Selection outline animation computed twice per frame in two files
  (composition_pass.cpp:124-150, viewport_scene_view.cpp:369-383).

### 3.3 Dead code

- `src/erhe/renderer/erhe_renderer/texture_renderer.hpp` includes
  `erhe_renderer/base_renderer.hpp` and `erhe_scene/viewport.hpp`, neither of
  which exists; `texture_renderer.cpp` is 0 bytes; neither is in
  src/erhe/renderer/CMakeLists.txt. `renderer_message_bus.{hpp,cpp}` are 0
  bytes. `src/erhe/scene_renderer/erhe_scene_renderer/format_pools.hpp` is 0
  bytes.
- doc/erhe/renderer.md still lists `Texture_renderer` and `Draw_indirect_buffer`
  as erhe::renderer key types (renderer.md "Key Types"); `Draw_indirect_buffer`
  lives in scene_renderer.
- 215 commented-out code lines in the slice (lines starting with `//` that end
  in `;` or mention `gl::`), e.g. composition_pass.cpp:182-191, 224-230,
  editor.cpp:1121-1142 (old GL swap logic), forward_renderer.cpp:328.
- Stencil constants 2 and 3 retired but kept (app_rendering.hpp:252-254).

### 3.4 Counters

- TODO/FIXME/XXX/HACK: 50 in the slice. Load-bearing ones:
  forward_renderer.cpp:95 "TODO is this ok?" (early-out), :117 "TODO proper
  conversion", :178 "TODO Implement other blending modes", shadow_renderer.cpp:172
  and :182 (force_disable / blending policy), composition_pass.cpp:119 "a bit
  hacky", shadow_render_node.hpp:34-35 (node/renderer relationship),
  post_processing.hpp:215 and app_rendering.hpp:241 (missing GPU timers),
  ring_buffer.cpp:112 "TODO Cleanup this API".
- `struct` keyword: 20 hits, mostly forward declarations of generated config
  types (`struct Viewport_config;`, `struct Ddgi_config;`, render_context.hpp:23,
  composition_pass.hpp:21-22) plus real definitions at post_processing.hpp:170,
  :178, program_interface.hpp:23, primitive_renderer.cpp:987,
  lightmap_tile_io.cpp:16. The generated config layer uses `struct`, so every
  consumer must forward-declare it as `struct`, against the AGENTS.md rule.
- `auto` locals (`auto x =` / `auto& x =`, excluding trailing returns): 258
  (editor/renderers 91, scene_renderer 58, renderer 39, raytrace 34,
  editor/graphics 16). Examples: composition_pass.cpp:153, :238-239,
  forward_renderer.cpp:100-101, :130.
- Boolean parameters in headers: 17 (section 2.1).

### 3.5 Doc / code mismatches found

- doc/erhe/rendergraph.md shows `rendergraph.execute()` and
  `void execute_rendergraph_node() override` with no command buffer; both take
  `erhe::graphics::Command_buffer&` (rendergraph.hpp:32, rendergraph_node.hpp:64).
- doc/erhe/rendergraph.md "Render_target handles MSAA resolve internally when
  sample_count > 0"; code branches on `m_sample_count > 1` (render_target.cpp:104).
- doc/erhe/rendergraph.md and doc/editor/rendering.md node tables omit
  `Viewport_overlay_node`, `Scene_image_view`, `Scene_image_readback_node`
  (present in viewport_scene_views.cpp:420-426 and scene_image_capture.hpp:96,163;
  doc/editor/rendergraph.md does describe the capture nodes).
- doc/editor/rendering.md:72-80 says the shadow pass "Culls front faces: only
  back faces write shadow depth"; the default is `Shadow_cull_mode::cull_back`
  (shadow_renderer.hpp:161) and doc/erhe/shadows.md agrees with the code.
- doc/editor/renderers.md lists `Mesh_memory` as an editor renderers type; it is
  `erhe::scene_renderer::Mesh_memory` (src/erhe/scene_renderer/.../mesh_memory.hpp).
- doc/erhe/scene_renderer.md "Dependencies: erhe::renderer (Draw_indirect_buffer)"
  is inverted; draw_indirect_buffer.cpp is in scene_renderer.
- doc/editor/rendering.md carries the header "This document was mostly written
  by Claude and may contain inaccuracies", which the doc/README.md rules
  (documents describe the present) do not allow for a "stable" document.

--------------------------------------------------------------------------------
## 4. Strengths (specific)

- `Scene_pass_resources` + `Base_render_parameters`: the pass prologue is
  described without naming a renderer, so bucket, draw-list and fullscreen
  paths share one bind sequence (scene_pass_resources.hpp:39-111). This is the
  right seam for everything in section 6.
- `Draw_list_scene`: incremental, main-thread-owned, enqueue-from-anywhere
  design with cached shader resolutions, change-serial gated material
  re-derivation and explicit counters for every maintenance path
  (draw_list_scene.hpp:123-250, draw_list_scene.cpp:793-826); each decision is
  cross-referenced to a numbered requirement in doc/erhe/draw_list_renderer.md.
- Frame-safe GPU memory: `Pool_block` retires instead of frees, applied from a
  frame-completion handler, and pool identity is chosen to make the MDI
  base-vertex lockstep invariant hold by construction (buffer_pool.hpp:30-121).
- Ring buffer completion is driven by the device frame fence
  (ring_buffer.cpp:229-233, vulkan_device.cpp:1625-1628), one mechanism for
  every per-frame buffer, including CPU readback shadows (ring_buffer.cpp:134-145).
- Shader variants: one uber shader with an X-macro key, a variant cache, and
  an init prewarm that mirrors the runtime key exactly (forward_renderer.cpp:381-526).
- Multiview as a `std::span<const Camera_view_input>` reaching the camera UBO
  and the debug/wide-line compute passes (doc/erhe/multiview.md,
  forward_renderer.cpp:122-127) - XR did not fork the renderer.
- Defensive lifetime rules that are explained where they live:
  `m_light_projections.clear()` before any early exit (shadow_render_node.cpp:430-438),
  `defer_resource` for mid-frame resizes (post_processing.cpp:126-135),
  `Composition_pass_result` so a skipped pass is distinguishable in a capture
  (composition_pass.hpp:103-117), breadcrumbs in `Rendergraph::execute`
  (rendergraph.cpp:114-117).
- Verification culture in docs: shadows.md, draw_list_material_set.md,
  post_processing.md record measured numbers, gates and the barrier rationale.
- Shadow scratch buffers are the textbook example of the allocation rule
  (shadow_renderer.hpp:265-270).

--------------------------------------------------------------------------------
## 5. Test coverage of the slice

- erhe_rendergraph: no tests (no src/erhe/rendergraph/test).
- erhe_renderer: `erhe_renderer_gpu_tests` (test_debug_line_width_gpu.cpp,
  833 lines) covers wide-line widths, AA profiles and an AA cost benchmark;
  device-gated by `erhe::gpu_test_support` (renderer/test/CMakeLists.txt).
  Text_renderer, Primitive_renderer shapes, Jolt adapter: untested.
- erhe_scene_renderer: deviceless `erhe_scene_renderer_tests` (Material_set
  bookkeeping, test_material_set.cpp 399 lines) and `erhe_scene_renderer_gpu_tests`
  (shadow fixture 793 + 230 lines, test_shadow_gpu 309, content line width 372,
  material set GPU 584). Forward_renderer bucketing, Draw_list_scene, Camera /
  Light / Primitive / Joint buffers, Mesh_memory / Buffer_pool, Program_interface:
  no direct unit tests (Draw_list_scene is exercised only through the editor's
  mcp_server_tests and gi/shadow verify scripts, per doc/testing.md:13,150).
- erhe_raytrace: 7 test files (scene, geometry, instance, hierarchy, masking,
  bvh_scene 630 lines).
- erhe_texgen: 8 test files (~1.7k lines).
- editor/renderers: only test_radiance_cascades_layout.cpp (408 lines). ID
  renderer explicitly has no automated coverage (doc/plans/id_renderer.md);
  Post_processing, Composer/Composition_pass, DDGI, Sky, Lightmap_baker (6k
  lines) have none beyond MCP-driven scripts (gi_verify.py, shadow_verify.py).
- Golden-image coverage lives in erhe_graphics_gpu_tests, below this slice.

--------------------------------------------------------------------------------
## 6. Future development options (prioritized)

1. Frustum culling on draw-list entries (low cost, high value).
   Helps: `Draw_list_entry` already carries a world AABB (doc/plans/
   draw_list_renderer.md item 1); the transform hook path exists
   (draw_list_scene.hpp:189-194). Hinders: AABB goes stale for dynamic objects;
   bucket path has no entry structure to cull. Start with the draw-list path,
   then let `Shadow_renderer` read the same AABBs instead of walking every mesh
   per light (shadow_renderer.cpp:308-330).

2. Remove the steady-state allocations (low cost).
   Make `buckets` a member scratch in Forward_renderer / Shadow_renderer /
   Id_renderer (pattern at shadow_renderer.hpp:265-270); build debug labels
   into a fixed buffer or drop per-bucket formatting in release; keep
   `m_pending` capacity in the two flush_pending swaps; move the three
   "push settings each frame" sites to the ImGui edit site as AGENTS.md
   requires (composition_pass.cpp:118-151, viewport_scene_view.cpp:253-260,
   editor.cpp:897-929).

3. GPU-driven / indirect rendering (medium cost, high value once culling exists).
   Helps: draws are already MDI with draw-id indexed primitive records
   (draw_list_scene.cpp:1425-1479); records are contiguous GPU layout
   (doc/erhe/draw_list_performance_improvements.md); static mobility flag exists
   (R10a). Hinders: indirect commands and primitive records are re-uploaded per
   pass through ring ranges; no count buffer in the encoder API; two draw
   paths to keep in sync. Step 1: persistent per-list record + command buffers
   for static lists; step 2: compute cull writing count + compacted commands;
   step 3: retire `bucket_primitives` for covered passes (plan item 5).

4. Real transient-resource render graph (high cost, medium value today).
   Helps: node/pin/key skeleton, `submits_command_buffer` contract,
   `Render_pass_descriptor` already carries usage_before/after so barrier
   derivation has a target. Hinders: nodes own textures (Render_target,
   Post_processing_node public vectors), pull-based `get_*_texture` at execute
   time, side-channel `static_cast` to Shadow_render_node, pre-graph GPU work
   in editor.cpp:851-1081, previews rendering outside. A realistic first step
   is not aliasing but declaring resources: give edges a typed resource
   descriptor (texture, or a `Light_projections` handle) so the shadow side
   channel and the input-key lookup become one mechanism, and move the DDGI /
   RC / lightmap ticks into nodes so RenderDoc and the sort see them.

5. Unify Shadow_renderer and Id_renderer onto Scene_pass_resources (low cost).
   Both duplicate the six buffers or borrow one (shadow_renderer.hpp:255-262,
   id_renderer.hpp:118-122); the doc already lists this as owed work.

6. Post-processing stack (medium cost).
   Today one bloom+tonemap node with a public pyramid; the overlay pass is a
   special case (issue #230 plumbing across Viewport_scene_view,
   Viewport_overlay_node, Post_processing::composite_input). A pass list with
   ping-pong targets owned by the node and a per-pass parameter ring would also
   host TAA and exposure. Helps: the parameter buffer + texture heap pattern
   (post_processing.hpp:194-207). Hinders: overlay/debug deferral state
   (`m_debug_renderer_frame_pending`) tied to the node order.

7. Temporal AA / jitter (medium).
   Helps: `Camera_buffer` already writes `frame_number` and has a jitter field
   (camera_buffer.hpp), reverse-Z and view-relative precision are done
   (doc/erhe/shadows.md). Hinders: no motion vectors anywhere; MSAA resolve is
   inside `Render_target`; ID pass and picking read the unjittered viewport.

8. Clustered / forward+ lighting (medium-high).
   Helps: lights already live in one UBO with type buckets and a resolved
   `Light_set` partition (light_buffer.hpp:29-124, forward_renderer.cpp:108-121).
   Hinders: light counts are SHADER VARIANT axes, so the shader is specialized
   per light count; a cluster list would replace six axes with a runtime loop,
   which also shrinks the variant space. `max_light_count 32`
   (program_interface.hpp:30) is the practical ceiling today.

9. Bindless materials / textures (medium).
   Helps: `Material_set` already owns a `Texture_heap` and slot indices in
   records (draw_list_scene.hpp:102-121); handles are written into records
   (material_buffer.cpp:256-290). Hinders: the GL sampler-array heap path is
   the weakest and least verified (doc/plans/draw_list_renderer.md item 7).

10. Unify the DDGI and radiance-cascade probe fields (already half done).
    `Probe_field` is published in one place and both producers reduce into the
    same atlas format (editor.cpp:874-884, doc/editor/radiance_cascades.md);
    the remaining step is a single `Indirect_diffuse_producer` interface with
    `tick`/`get_field`/`reset_history` so editor.cpp:858-891 stops branching.

11. Virtual / clipmap shadow maps (high).
    Helps: the tight fit, footprint, derived bias and per-light render passes
    are all parameterized (shadow_renderer.hpp:87-200). Hinders: a fixed 2D
    array + cube array layout, per-light full re-rasterization each frame, no
    caster caching (would follow from item 1's AABB path).

12. Better XR path (medium).
    Helps: multiview via views span; `submits_command_buffer`. Hinders: a
    688-line `render_headset` that re-derives `View`s per eye per frame and
    duplicates the wide-line feed (headset_view.cpp:993-1140); the XR path
    skips post-processing entirely (headset_view.cpp:961-972 comment "no
    post-processing"). Extracting the per-view setup into a
    `Render_context` builder shared with `Viewport_scene_view` would let the
    same overlay/post nodes serve both.

13. Mesh shaders (low priority).
    Nothing in the current model helps; meshlets would need a new
    `Buffer_mesh` layout and a third draw path. Defer until 1 and 3 land.

--------------------------------------------------------------------------------
## Appendix: key evidence table

| Topic | File:line |
|---|---|
| Graph execute loop | src/erhe/rendergraph/erhe_rendergraph/rendergraph.cpp:81-136 |
| Pull-based input lookup, first link only | rendergraph_node.cpp:36-58 |
| Shadow side channel static_cast | src/editor/scene/viewport_scene_view.cpp:1102-1103 |
| Pre-graph GPU work | src/editor/editor.cpp:851-1081, execute at :1086 |
| Viewport node execute (387 lines) | src/editor/scene/viewport_scene_view.cpp:169-556 |
| Per-frame settings push (rule violation) | composition_pass.cpp:118-151; viewport_scene_view.cpp:253-260, 373-383; editor.cpp:897-929 |
| Per-frame bucket vector + fmt label | forward_renderer.cpp:136, 210-222; shadow_renderer.cpp:170, 180-190 |
| Reference members with temporary defaults | forward_renderer.hpp:137-138 |
| initializer_list reference members | shadow_renderer.hpp:108-112; id_renderer.hpp:108 |
| Duplicated pass buffers | shadow_renderer.hpp:255-262 |
| MDI submission | forward_renderer.cpp:244-250; draw_list_scene.cpp:1473-1479 |
| Ring buffer exhaustion returns empty | ring_buffer.cpp:117-119 |
| Frames in flight | vulkan_device.hpp:462 (2), gl_device.hpp:350 (3), metal_device.hpp:251 (3) |
| Dead header with missing includes | src/erhe/renderer/erhe_renderer/texture_renderer.hpp:3-4 |
| Bool args | composer.hpp:38; app_rendering.hpp:139,144,148,165; shadow_render_node.hpp:61 |
| Doc mismatch, shadow cull | doc/editor/rendering.md:78 vs shadow_renderer.hpp:161 |
| Doc mismatch, execute signature | doc/erhe/rendergraph.md "Creating and Wiring a Graph" vs rendergraph.hpp:32 |
