# Overdraw visualization

Status: in progress

Build order steps 1 and 2 are built (2026-10-29, Vulkan; "Built" below); step 3 is next.

This plan extends `doc/editor/rendering.md` (the viewport rendergraph chain
and the composition passes) and `doc/erhe/shader_variants.md` (the
`Shader_key` axes) with a per-viewport overdraw view: every fragment shader
invocation of the scene is counted per pixel, and the viewport shows the
counts as a heat map in place of the post-processed image. It is a desktop
editor tool first; the motivating measurement is the Quest Bistro eye pass,
which shades about 30 M fragments per frame for 5.9 M eye pixels
(`doc/plans/quest_performance.md` "Bistro configuration sweep").

## Goals

- Show, per pixel of a viewport, how many times a counted fragment shader
  ran there in one frame, with the viewport's real rendering configuration:
  the same composition passes, pipelines (depth, stencil, blend, cull), draw
  order, draw-list path, culling classification and occlusion culling as
  when the mode is off.
- Report the same counts as numbers (MCP), so an agent can measure overdraw
  of a scene and camera without reading screenshots, and render the
  overdraw image of an explicit camera at an explicit resolution with no
  viewport involved (O6).
- Leave the mode-off path unchanged: no extra variant, binding, pass or
  buffer when no viewport has the mode on.

## Design

### O1. Counting into a per-pixel storage buffer

The counted shaders add 1 with `atomicAdd` to a per-pixel `uint` in a
storage buffer, indexed by the integer `gl_FragCoord.xy` (and the view
index under multiview, item 6). The fragment's real outputs stay as the
pipeline has them; the visualization node (O4) overwrites the image.

The buffer, not a blended count attachment, because:

- every pipeline keeps its own blend, depth and stencil state, so the
  counted fragments are those of the real configuration; a blended count
  would need an additive blend override or a second color attachment in
  every viewport pipeline;
- the count is per fragment shader invocation, independent of the
  viewport's MSAA sample count (a blended attachment counts per covered
  sample) and color format;
- buffer atomics in the fragment stage are what the occlusion test pass
  already uses on every backend, behind
  `Device_info::fragment_stores_and_atomics`. The mode is offered only where
  that flag is set.

The buffer belongs to the `Viewport_scene_view` (one per view, sized to its
render target, created on the first frame the mode is on and released when
it turns off). It is a render-phase resource: M2 requests the size, R
allocates or resizes it at the start of the node's execute, as
`Texture_rendergraph_node::request_size` does for the render target
(`doc/editor/render_thread.md`). Frames execute in order on the one queue,
so one buffer serves every frame in flight: R clears it before the view's
render pass, with a barrier from the clear to the fragment stage's storage
reads and writes, and a barrier from the fragment writes to the
visualization node's reads.

### O2. The overdraw variant axis

An integer `Shader_key` axis, `OVERDRAW` (`ERHE_OVERDRAW`; off, shaded,
rasterized, "Built" below), is a per-pass axis on both paths:

- the bucket path: `Forward_renderer::Prepare_parameters::overdraw`, per
  composition pass;
- the draw-list path: `Draw_list_scene::resolve_color_stages()` resolves
  the overdraw variants beside the standard resolution of each list, and
  the draw lists, GPU command generation and occlusion culling stay in
  effect. The axis reads no pass-dependent record field, so
  `reads_record_pass_fields()` stays false for it: unlike `SHADER_DEBUG`, it
  never sends a pass to the bucket fallback.

The view's `Viewport_render_state` (per frame slot) carries the flag from
M2 to R, so a toggle takes effect on one frame for both halves.

Which draws count: the content layers of the view (the meshes
`shader_debug_filter` accepts: content and controller), in every
composition pass that draws them with `standard.{vert,frag}` - the opaque,
selected, alpha-tested and translucent fills, solid wireframe, corner points
and centroids. Tool, brush, bone and rendertarget meshes keep their normal
variants and are not counted. `VARIANT_DEPTH_ONLY` draws (the selection
stencil masks) are not counted.

`standard.frag` under `ERHE_OVERDRAW`:

- runs the material inputs that decide a discard (base color alpha for
  `alpha_test` / `screen_door`) and discards exactly as the real variant;
- counts with `erhe_count_overdraw()` from a new include,
  `res/shaders/erhe_overdraw.glsl` (the buffer declaration, the index
  computation and the atomic), which every counted shader shares (item 4);
- skips lighting and writes a constant color.

### O3. What one count means

A count is one fragment shader invocation of a counted shader at the pixel
(helper invocations perform no stores, item 5). The overdraw variant must
not change which invocations run. Storage writes in a fragment shader turn
off the implementation's implicit early depth test, so:

- a variant whose real counterpart cannot discard declares
  `layout(early_fragment_tests) in;`: an invocation runs when the fragment
  passes the depth and stencil tests at that point of the draw order, as
  the real shader with early-Z does;
- a variant whose real counterpart can discard (the `alpha_tested`
  blending class) keeps late tests: every rasterized fragment runs the
  shader before the depth test, which is what the GPU does for a
  discarding shader too.

`erhe_standard_variant.glsl` defines `ERHE_FRAGMENT_MAY_DISCARD` from the
key (the blending mode axis and any other discard site found in
`standard.frag`), and the early-tests declaration keys on it.

The counts model an early-Z GPU drawing in the editor's order. Hardware
hidden-surface removal (Apple GPUs), Adreno's low resolution Z and the
runtime's fixed foveation on Quest remove part of this work on those
devices; the view shows what the draw order and depth test leave for them
to remove.

### O4. The visualization node and the post-processing bypass

`Overdraw_node` (`src/editor/rendergraph/overdraw_node.{hpp,cpp}`), a
`Texture_rendergraph_node` with a `viewport_texture` input and output,
draws one fullscreen pass into its own texture of the view's size: each
pixel's count through a color ramp (0 black, 1 dark blue, 2 green, 3
yellow, 4 orange, 5 red, from `max_count` up white; `max_count` is
kept per viewport, default 8), and over count 0 the scene color at 25 % luminance so
the geometry stays recognizable. The output is display-referred: no
exposure, bloom or tone mapping.

Switching the mode rewires the view's chain on the main thread, at the
change site (the Viewport window's menu item and the MCP tool, queued for
the M2 flush per `doc/editor/coding_rules.md` "ImGui-time work"), with
`Rendergraph::connect` / `disconnect`:

```
off: Viewport_scene_view -> Post_processing_node -> Viewport_overlay_node -> Window_imgui_host
on:  Viewport_scene_view -> Overdraw_node        -> Viewport_overlay_node -> Window_imgui_host
```

For a view created without post-processing (`--no-post-processing`), the
overdraw node goes between the view and the window host, and the
`Viewport_window`'s output node follows it. The post-processing node stays
alive, disconnected, so turning the mode off restores the chain without
re-creating its pyramid. The overlay keeps drawing gizmos and the hotbar on
top, depth-tested against the content depth. The Viewport window shows a
legend of the ramp and the frame's mean count over covered pixels (from O5)
while the mode is on.

### O5. Numbers: the overdraw statistics

A compute pass after the view's render pass reduces the buffer into a small
result: a histogram of counts 0 .. 63 plus an overflow bin, the total of
all counts, the maximum and the covered pixel count (count > 0). The result
is copied to a per-slot readback buffer and decoded when its frame retires
(`Render_frame_results`, `doc/editor/render_thread.md`). It runs only on
frames where the mode is on.

MCP:

- `set_overdraw_visualization {viewport?, mode, max_count?}`: the mode for a
  viewport (default: the first viewport window with a scene view); refused with the reason when the
  device lacks `fragment_stores_and_atomics`.
- `get_overdraw_stats {viewport?}`: the retired frame's result: mode,
  counted_mode, valid, width, height,
  frame index, invocations total, covered pixels, mean per covered pixel,
  mean per pixel, maximum, histogram.

### O6. Overdraw through `render_scene_image`

`render_scene_image` gains `output: "overdraw"`: its capture chain renders
the request's camera at the requested `width` x `height` (required for
every rendering image tool, `doc/agents/mcp_api_guidelines.md` "Image
tools: rendering requests state the resolution, captures do not")
with the overdraw axis on its scene image view,
its own counter buffer (O1) and an `Overdraw_node` in place of its
post-processing node (O4), and writes the heat-map PNG. The reply carries
the statistics of O5 for that image, read back with the request (the
capture already spans the frames its readback needs). `max_count` and the
counted shader set are request arguments with the viewport's defaults. The
other `render_scene_image` arguments (scene, camera, MSAA sample count) keep
their meaning, so a camera sweep is a loop of calls, each fully determined
by its arguments.

## Code map

Where each part attaches:

- Variant axes: `src/erhe/scene_renderer/erhe_scene_renderer/shader_key.hpp`
  (`ERHE_SHADER_INT` list, `OVERDRAW`; `Overdraw_count` enum) and
  `res/shaders/erhe_standard_variant.glsl` (defines derived from axes).
- Bucket path environment key: `Forward_renderer::render()` and
  `apply_render_environment()` in `forward_renderer.cpp` (where
  `SHADER_DEBUG` and `SPECULAR_ANTI_ALIASING` are set);
  `shader_debug_filter` in `bucket_primitives()` (`mesh_memory.cpp`) is the
  content-only filter to reuse.
- Draw-list path: `Color_environment::make_environment_key()` and
  `reads_record_pass_fields()` in `draw_list_scene.cpp`;
  `Draw_list_scene::resolve_color_stages()`.
- Bindings: `program_interface.cpp` and `buffer_binding_points.hpp` in
  `erhe::scene_renderer` (storage blocks at binding 9, 11, 12 are the light
  arrays; the counter buffer takes a free binding, fragment stage).
- Fragment atomics precedent: `res/shaders/occlusion_test.frag` (read
  first, then `atomicOr`) and its gate on
  `Device_info::fragment_stores_and_atomics` in `gpu_cull_classifier.cpp`.
- Discard sites in `standard.frag`: the alpha-test / screen-door block
  ahead of the light loops (search "Alpha-test / screen-door discard").
- Per-frame view state M2 -> R: `src/editor/scene/viewport_render_state.hpp`
  (`shader_debug` is the model), `Viewport_scene_view::prepare_render()`.
- Chain wiring: `Scene_views::create_viewport_scene_view()` in
  `src/editor/scene/viewport_scene_views.cpp` (post-processing node, overlay
  node `src/editor/rendergraph/viewport_overlay_node.hpp`, window host);
  `Rendergraph::connect()` / `disconnect()` in `erhe::rendergraph`.
- Scene image capture chain (O6): `src/editor/scene/scene_image_capture.hpp`,
  `doc/editor/rendergraph.md` "Scene image capture".
- UI: the Shader Debug combo in `Viewport_scene_view` (`viewport_scene_view.cpp`,
  `c_shader_debug_strings`) is where the viewport's overdraw menu item goes.
- MCP: handlers in `src/editor/mcp/`, schemas in
  `config/editor/mcp_tools.json`, usage in `doc/agents/mcp_server_usage.md`;
  `get_viewport_size` gives a viewport's render size for comparisons.

## Build order

1. The axis, the buffer and the counting in `standard.frag` (O1-O3), on
   both color paths, with `get_overdraw_stats` (O5) as the first reader.
   Verify numerically (Verification below) before anything is drawn.
2. `Overdraw_node`, the rewiring and post-processing bypass, the Viewport
   window menu item, legend and `set_overdraw_visualization` (O4).
3. `render_scene_image` `output: "overdraw"` (O6), after
   `doc/plans/mcp_image_resolution.md` item 2 made `width` / `height`
   required.
4. Desktop Bistro measurement through O6 from the Quest bench camera
   (`config/editor/benchmark_bistro.json`), occlusion culling off and on,
   the result in `doc/plans/quest_performance.md`, next to the Quest
   counters it explains.
5. The other fragment shaders of the frame through `erhe_overdraw.glsl`,
   each with its own variant define: the occlusion test boxes (their fill
   cost near large boxes is an open occlusion-culling item), the sky, the
   grid and the wide-line edges, plus a per-category view (content only /
   everything).
6. Quad overshading: a second counter of the helper invocations around
   each pixel, from subgroup quad operations (each quad's lowest live lane
   adds the quad's helper lane count to its pixel), behind a device flag for
   quad operations in the fragment stage; the ramp gains an "invocations
   including helpers" mode. Small triangles make this large (the Quest
   counters report an 18 pixel average polygon area on Bistro).
7. Multiview: the view index in the buffer index and a per-eye
   visualization, for a later on-headset measurement.

## Built (steps 1 and 2)

Where the build differs from or adds to the design above:

- Which draws count (O2): a pass counts when `counts_overdraw()`
  (`composition_pass.cpp`) accepts it - a `scene_content` pass over the
  content layer with no shader stages or shader debug override - and then
  every mesh it draws counts (content and controller meshes alike; the
  draw-list path draws a list with one variant, so a per-mesh filter could
  not hold on both paths). `bucket_primitives()` clears the axis on
  depth-only and ID keys.
- Two counting modes (`Overdraw_count`, the viewport's Overdraw combo and
  the MCP `mode`): shaded counts the invocations the frame runs (O3);
  rasterized counts every rasterized fragment of the counted draws,
  ignoring the depth test (counted at the top of `standard.frag`'s
  `main()`, before any discard, with no early fragment tests). Rasterized
  shows what occlusion and frustum culling remove; shaded shows the
  fragment shader work that remains.
- The axis is a per pass decision recorded in
  `Composition_pass_prepared::overdraw`, not part of `Color_environment`:
  the draw-list path resolves each mode beside the off resolution of each
  list (`Draw_list_color_resolution::overdraw`), so two views of one scene
  with different modes do not invalidate each other's resolutions every
  pass.
- The count buffer (O1) is `Overdraw_view_state` in
  `erhe::scene_renderer` (`overdraw_counter.{hpp,cpp}`), owned by the
  `Viewport_scene_view`: device memory, a uint per pixel with no header
  (`erhe_overdraw.glsl` indexes by `camera.cameras[0].viewport`), grown on
  demand, released when the mode turns off. Binding 13 of the scene layout
  (Metal's index 13 is the ray query TLAS, which only compute shaders bind).
  The reduction (O5) is `Overdraw_counter` (`res/shaders/overdraw_reduce.comp`,
  per-workgroup shared histograms), one per editor (`App_context::overdraw_counter`).
- The translucent passes use the opaque fill pipelines, so they depth-test
  and write depth like the opaque passes: translucent layers count by draw
  order like opaque ones, not N.
- With MSAA, the pixels a triangle edge splits count once per covering
  triangle (the diagonal of a quad counts 2 per layer on those pixels).
- Occlusion culling lowers the shaded counts only by the fragments of
  hidden entries drawn before their occluders: an entry drawn after its
  occluder already runs no fragment shader (early depth test), so culling it
  saves its vertex, rasterization and depth-test work, which the rasterized
  counts show. Measured: six boxes behind a wall, drawn before it, shaded
  1700907 -> 1579629 invocations with occlusion culling on; drawn after it,
  1579629 both ways. Bistro from the bench camera, occlusion off -> on:
  shaded 6380905 -> 6032753 (-5.5 %), rasterized 16092369 -> 14268985
  (-11 %, mean 10.8 -> 9.6 per covered pixel, max 667; a max count around
  32 keeps the rasterized heat map out of white).
- `Overdraw_node` (`src/editor/rendergraph/overdraw_node.{hpp,cpp}`,
  `res/editor/shaders/overdraw_visualize.frag`) and the rewiring
  (`Scene_views::set_overdraw_mode()`): the toolbar's Overdraw combo (queued
  out of the ImGui iteration) and max count; the MCP tool calls it
  directly (MCP requests run in the prepare phase). The node is created
  when the mode leaves off and dropped when it returns to off. Without post-processing the tool meshes
  draw in the content pass and the node covers them; the ImGui navigation
  gizmo and the legend stay on top.

Verified 2026-10-29 on Vulkan (Intel, headless Release, validation layer
on): `scripts/overdraw_verify.py` (4 full-screen rectangles, bucket path and
draw lists with cpu and gpu classification, occlusion off): shaded - opaque
far first 4, near first 1, coplanar 1, alpha_test 4 in either order;
rasterized - 4 in every case; with occlusion culling on, the distinct-depth
opaque stacks count 1 in both modes; a scene image with nested
alpha-tested squares counts each region's square depth exactly; 80 MCP
toggles with and without post-processing; no new validation message.
Not yet verified: Metal 3 / Metal 4 (the binding from ICB-executed draws),
OpenGL, the mode-off render-thread gates against the previous HEAD, and
occlusion culling's check with the mode on.

## Verification

- Synthetic scene (an MCP-built creation): N camera-facing opaque quads
  covering a known screen rectangle. Drawn back to front, every covered
  pixel counts N; front to back, 1; the same quads with an `alpha_test`
  material count N in either order. `get_overdraw_stats` histograms match
  exactly, on the bucket path and the draw-list path (`use_draw_lists` on,
  culling classification `cpu` and `gpu`).
- Mode off: the render-thread gate images identical to HEAD, and no
  overdraw variant in the variant cache.
- Mode on: `get_composition_passes` shows the same paths (draw lists,
  GPU commands) as with the mode off; occlusion culling still culls (its
  check reports the same culled entries).
- Backends: Vulkan, Metal 3 and Metal 4 (the fragment buffer is written
  from ICB-executed draws on the GPU command path; confirm the binding
  reaches them), OpenGL. A device without `fragment_stores_and_atomics`
  greys the menu item and the MCP tool refuses.
- O6: the synthetic scene through `render_scene_image` `output:
  "overdraw"` gives the same histogram as the viewport at the same camera
  and size; the image has the requested size.
- Toggle soak: switching the mode every few frames for 300 frames with
  frame pipelining on, no validation errors, no stale texture in the
  window.

## Open questions

- Counting the selected-fill and selection-outline passes as content (the
  default above counts what draws the content meshes, selected or not).
