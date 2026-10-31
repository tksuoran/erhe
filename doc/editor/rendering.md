# Editor Rendering

Stability: stable

> This document was mostly written by Claude and may contain inaccuracies.

Lit material drawing goes through `standard.{vert,frag}` as an
uber-shader; feature differences (circular brushed metal, anisotropic
BRDF, debug visualization modes, depth-only, ID rendering, brush
preview, rendertarget) are variant axes on the `Shader_key`. See
`doc/erhe/shader_variants.md` for the variant key, `doc/erhe/mesh_memory.md`
for the `Render_bucket` / `bucket_primitives()` flow,
`doc/editor/prewarm.md` for the init-time compile path, and
`doc/editor/post_processing.md` for the full bloom/tonemap pipeline (the
"Post-processing" section below is only a summary).

This document describes how the erhe editor uses erhe libraries to render a frame, covering the rendergraph, the composer, render pipelines, and stencil buffer usage.

## Rendergraph

The editor builds a DAG of `erhe::rendergraph::Rendergraph_node` instances. The rendergraph executes nodes in dependency order each frame, with data flowing through producer-consumer texture connections.

### Node types

| Node | Purpose | Outputs | Inputs |
| :--- | :--- | :--- | :--- |
| `Shadow_render_node` | Renders shadow depth maps for each light | `shadow_maps` (depth texture array) | none |
| `Viewport_scene_view` | Renders scene content into an MSAA render target | `viewport_texture` | `shadow_maps` |
| `Post_processing_node` | Bloom and tonemapping via mipmap pyramid | `viewport_texture` | `viewport_texture` |
| `Window_imgui_host` | Displays viewport texture in an ImGui window | screen | `viewport_texture` |

### Connection chain

Connections are established in `Scene_views::create_viewport_scene_view()`:

```
Shadow_render_node
    |
    | shadow_maps
    v
Viewport_scene_view
    |
    | viewport_texture
    v
Post_processing_node (optional)
    |
    | viewport_texture
    v
Window_imgui_host (Viewport_window)
```

Multiple viewports can exist simultaneously, each with their own shadow node, scene view, and optional post-processing node.

## Viewport_scene_view execution

`Viewport_scene_view` extends `erhe::rendergraph::Texture_rendergraph_node` and manages an MSAA render target. Its `execute_rendergraph_node()` runs these steps:

1. **ID rendering** -- If the viewport is hovered (or a region scan or pick probe is pending), `Id_renderer::render()` draws the id pass the prepare phase prepared for the view (`Id_renderer::prepare`), a unique 32-bit ID per mesh primitive, into a separate FBO. GPU readback with a ring buffer (4 entries for pipeline latency) returns the mesh, primitive index, triangle ID, and depth at the mouse position.

2. **Tool pre-rendering** -- recorded before the node runs, on the main thread in M2 (`Viewport_scene_view::prepare_render`, into the frame slot named by the view's `Viewport_render_state` of the frame): `Debug_renderer::begin_frame()` opens the view's frame slot in the debug renderer's copy of the frame being prepared (one copy per frame in flight, so the render phase of the previous frame keeps drawing its own), `Tools::render_viewport_tools()` submits tool geometry (gizmos, manipulators) using the 6-pass stencil protocol described below, and `Renderable::render()` is called without an encoder for first-pass setup. The node then runs `Debug_renderer::compute()`, which expands the lines recorded for the frame it renders.

3. **Render pass begins** -- A `Render_command_encoder` is created and an MSAA render pass starts, clearing color, depth, and stencil.

4. **Main scene rendering** -- `render_viewport_main()` calls `render_composer()` which executes the Composer's composition passes in order (see below).

5. **Renderables** -- Renderable objects get a second call, now with the active encoder.

6. **Debug overlays** -- `Debug_renderer::render()` draws accumulated debug lines and points. `Text_renderer::render()` draws 2D text labels placed at 3D positions.

7. **Render pass ends** -- MSAA resolve produces the final viewport texture.

## Shadow rendering

`Shadow_render_node` owns a depth texture array (one layer per light) and a `Light_projections` object. With the draw-list gate on, the casters come from the scene's shadow draw lists; with it off (the bucket path), `Shadow_render_node::prepare_casters()` buckets the content layer's casters in the last M2 step into the node's slot for the frame (`Shadow_renderer::prepare`, a `Render_bucket_snapshot` without the pass variant bits). Both paths take the frustum fit bounds from the scene's `Draw_list_scene` slot (`get_shadow_bounds`), so the render phase reads no mesh or node. Its pre-execute hook (before any node of the frame executes) fits the light projections and adds the draw-list passes' culling views (`Shadow_renderer::fit()`, doc/plans/culling.md phase 6a); its execute calls `erhe::scene_renderer::Shadow_renderer::draw()`, which:

- Renders each light into its own array layer via a separate render pass
- Uses a depth-only shader with `Color_writes_disabled`
- Culls front faces: only back faces write shadow depth, which reduces peter-panning
  for closed meshes. Caveat: open / single-sided meshes do not cast shadows from their
  front side. Mirrored (negative-determinant) casters draw with the front-face-flipped
  pipeline variant per bucket (see "Mirrored (negative-determinant) geometry" below)
- Supports reverse depth (clear to 0.0) and forward depth (clear to 1.0)
- Shadow resolution, light count, and depth bits are configurable at runtime: the main thread requests a configuration (`request_configuration()`, the shadow technique from the render settings in `prepare()`), and the render phase applies it with `reconfigure()` at the start of the node's execute

The main scene rendering accesses shadow maps through the rendergraph connection and passes `Light_projections` to the forward renderer.

## Composer and composition passes

The `Composer` holds an ordered list of `Composition_pass` instances. `render_composer()` iterates them in order. Each pass runs in two halves:

- `Composition_pass::prepare()` (main thread, the last M2 step; `doc/editor/editor.md` "Frame phases and the render thread") evaluates the pass's gating for one scene view - `enabled`, the `is_enabled` predicate, the view's render style, whether edge lines go to `Content_wide_line_renderer`, whether the pass is draw-list eligible, its primitive settings - and, for a pass on the bucket path, has `Forward_renderer::prepare()` bucket its meshes into a `Render_bucket_snapshot` (complete primitive records and draw commands). `Composer::prepare()` fills one `Composition_pass_prepared` per pass into the view's `Composer_frame_slots` slot for the frame.
- `Composition_pass::render(context, prepared)` (render phase) draws what was prepared: the fullscreen draw, the wide-line draw, the draw-list draw or the snapshot (`Forward_renderer::render(Draw_parameters, Render_bucket_snapshot)`), and records the outcome for `get_composition_passes`. A draw-list pass that drew GPU-generated commands (`Cull_classification::gpu` on a device with draw indirect count) records the entry and culled counts of the retired frame's GPU count readback for the same pass and scene view (frames in flight late; `Composition_pass_stats::draw_list_counts_frame_index` names that frame, MCP `last_draw_list_counts_frame_index`), so they equal the `cpu` counts only on a static frame; the pass's selection is registered with its culling view in `Composer::add_cull_views()` (`doc/erhe/draw_list_renderer.md` Q6 "GPU command generation").

The potentially visible set rule (`doc/plans/pvs.md` "Runtime consumption", `doc/erhe/draw_list_renderer.md` Q6 "PVS") reaches the viewports through their culling views: `Composer::prepare()` (the prepare phase) computes the PVS query box of a viewport (`Render_context::viewport_scene_view`, content `scene_and_editor_aids`, one view with a perspective camera), the camera position plus and minus the editor setting `pvs_query_margin` (default 0.05 m) per axis, keeps it in the view's `Composer_prepared` and registers it with the scene's draw lists (`Draw_list_scene::register_pvs_view()`, keyed by the scene view, `get_pvs_view_key()`): the change site of the PVS page streaming (`doc/plans/pvs.md` "Residency and streaming"). `Composer::add_cull_views()` (the render phase) gives the view's color culling view that box, and every draw-list pass of the view and its occlusion test pass share that view. The streaming settings `pvs_resident_pages` (default 64) and `pvs_stream_radius_pages` (default 1) are pushed to every scene's draw lists from their change sites (Settings window "PVS Resident Pages" / "PVS Stream Radius" when the edit is done, MCP `set_pvs_streaming`; `App_scenes::set_pvs_streaming_settings()`) and to a scene's at its registration (`App_scenes::register_scene_root()`). An open releases the database's in-memory bits once the scene file holds them (`Pvs_database::release_bits()` in `open_scene_gltf()`): the pages stream from the file; a save onto that file reads them back into memory first (`make_bits_resident()` in `save_scene_gltf()`). Scene image captures, orthographic cameras, previews, thumbnails, the id pass, the shadow passes and the headset register no box. The editor setting `pvs_culling` (default on; Settings window "PVS Culling", MCP `set_pvs_culling`) is carried by the render settings snapshot to `Scene_root::publish_draw_lists()`; the draw lists apply it only while the scene's database is ready. `Scene_root::set_pvs_database()` hands the database to the scene's `Draw_list_scene` (at its next flush), and the draw lists' staleness callback calls `Scene_root::mark_pvs_stale()`; a scene close detaches the database from the draw lists first (`Scene_root::detach_pvs_consumer()` at the start of `Editor::on_close_scene()`), so the teardown's unregistrations mark nothing stale. A draw-list pass records the rule's tested and culled counts of its view in `Composition_pass_stats` (`pvs_tested_count`, `pvs_culled_count`; MCP `get_composition_passes` `last_pvs_*`, and per viewport in `get_pvs_status` `views`).

The edge-line passes that `Content_wide_line_renderer` draws are fed in M2 too: `Viewport_scene_view::prepare_wide_line_feed` puts the records of their meshes into the same slot (`Composer_prepared::wide_line_feed`), and the render phase queues them (`add_feed`) before the compute pre-pass. Viewports and scene image captures prepare in M2 and pass the slot as `Render_context::composer_prepared`; the previews and the headset call `Composition_pass::render_immediate()`, which prepares where it renders.

A `Composition_pass` specifies:

- **Mesh layers** to render (content, brush, controller, rendertarget)
- **Primitive mode** (polygon_fill, edge_lines, corner_points, polygon_centroids)
- **Item filter** selecting meshes by flag combinations (visible, opaque, selected, translucent, etc.)
- **Render pipeline states** (one or more, executed in sequence)
- **Render style** callback providing colors, roughness, metallic overrides
- **Non-mesh vertex count** for fullscreen passes (sky, grid)

### Composition pass order

The editor creates these passes, in this order, in
`App_rendering::App_rendering()` (names as they appear in code; several are
conditional no-ops per frame, noted below):

| # | Pass | Primitive mode | Notes |
| :--- | :--- | :--- | :--- |
| 1 | Content fill opaque not selected | polygon_fill | content + controller layers, `Blending_class::opaque` primitives |
| 2 | Content fill selected | polygon_fill | selected/hovered; also writes selection stencil bit 7 |
| 3 | Content solid wireframe not selected | solid_wireframe | `SOLID_WIREFRAME` shader-variant overlay over the opaque fill (same depth, no z-fight, no depth write); disabled when `Device_info::use_solid_wireframe` is false; no-op unless the render style enables solid wireframe |
| 4 | Content solid wireframe selected | solid_wireframe | as above, selected/hovered |
| 5 | Selection stencil mask (fill disabled) | polygon_fill | depth + stencil bit 7 only, color writes disabled (`VARIANT_DEPTH_ONLY`); active only when the selection polygon fill is OFF -- exactly one of this pass and pass 2 runs per frame |
| 6 | Selection stencil mask (proxy hidden) | polygon_fill | depth + stencil bit 7 for selected/hovered `proxy_hidden` sources (their render proxies draw the fill) |
| 7 | Content corner points not selected | corner_points | `VARIANT_POINTS` variant; no-op unless the render style enables corner points |
| 8 | Content corner points selected | corner_points | as above, selected |
| 9 | Content polygon centroids not selected | polygon_centroids | `VARIANT_POINTS` variant; no-op unless the render style enables polygon centroids |
| 10 | Content polygon centroids selected | polygon_centroids | as above, selected |
| 11 | Content edge lines not selected | edge_lines | wide-line edges via `Content_wide_line_renderer`; suppressed when solid wireframe is enabled |
| 12 | Content edge lines opaque selected | edge_lines | as above, selected |
| 13 | Ghost edge lines | edge_lines | geometry graph ghost meshes (`render_wireframe` flag), always on |
| 14 | Content fill alpha tested not selected | polygon_fill | `Blending_class::alpha_tested` primitives (alpha_test, screen_door) of pass 1; blending off, depth write on |
| 15 | Content fill alpha tested selected | polygon_fill | as above, of pass 2; writes selection stencil bit 7 |
| 16 | Occlusion test | - | `Composition_pass_kind::occlusion_test`: the raster occlusion test of the view (`Draw_list_scene::draw_occlusion()`, doc/plans/occlusion_culling.md); viewports only (`Render_context::render_state`), with `occlusion_culling` on, the draw lists and the `gpu` culling classification; draws the camera-facing faces of every tested entry's box, depth test without depth or color writes; the next frame's classification culls the entries it found hidden |
| 17 | Content outline opaque selected | edge_lines | selection/hover outline (outline pipeline, constant colors; draws where stencil bit 7 is clear) |
| 18 | Sky | polygon_fill | fullscreen (3 vertices), depth==far, stencil==0; `Sky_composition_pass` subclass switches between the gradient/checker shader and the physically-based atmosphere (`doc/erhe/procedural_sky.md`) |
| 19 | Grid | polygon_fill | fullscreen infinite plane (12 vertices), depth clamp, blended overlay that leaves the depth buffer to the content |
| 20 | Content fill translucent not selected | polygon_fill | `Blending_class::translucent` primitives of pass 1, premultiplied blending |
| 21 | Content fill translucent selected | polygon_fill | as above, of pass 2 |
| 22 | Brush | polygon_fill | brush layer, `VARIANT_BRUSH_PREVIEW` variant, two pipelines in sequence (cull back then cull front), premultiplied alpha |
| 23 | Bone solid (N.V) | polygon_fill | bone proxies, six stencil-assisted pipelines; after every content pass because passes 1-2 of it leave stencil tags the edge-line pipelines test |
| 24 | Rendertarget | polygon_fill | rendertarget-mesh UI (hotbar quad); overlay pass that ignores camera exposure and runs after post-processing (issue #230) |
| 25 | Rendertarget view anchored | polygon_fill | as above, for the view-anchored hotbar of the hovered view only |

The order serves the GPU's early depth test (on Apple GPUs, hidden surface
removal; doc/erhe/primitive.md "Blending classes"): a draw that writes depth
and may discard turns it off for the draws that follow it. Every pass that
writes depth without discarding - the opaque fills, the selection stencil
masks, the corner points and centroids - comes before the alpha-tested
fills (14-15), then the occlusion test (16), which writes nothing and so
tests against the view's final opaque depth, and the sky follows them, so it
skips the pixels the alpha-tested content covers. The editor overlays keep
their places relative to the content they had before the alpha-tested passes existed,
where moving them would change what they show:

- The corner points and centroids (7-10) write depth without discarding,
  so they come before the edge lines, whose wide-line shader discards.
  Which of a point and a line wins a pixel does not depend on their order
  (the point's `less` test after the line equals the line's
  `less_or_equal` test after the point), and a point's depth bias
  (`0.0005 * |N.V|` in clip z, `standard.vert`) is at least the line's
  (`0.0005 * (N.V)^2`, or the ulp-sized tent lift), so a point stays on top
  of the lines meeting at it. Where a line fragment is nearer than the
  point's flat sprite depth, the line blends over the point rather than over
  the surface (a handful of pixels on the default scene with 30 px points).
- The edge lines (11-12, 13) stay ahead of the alpha-tested fills: content
  drawn after an edge line covers its pixels, as the translucent fills do;
  the wide-line shader discards, so these are discarding draws themselves.
- The outline (17) follows the alpha-tested fills: it draws regardless of
  depth where stencil bit 7 is clear, so the alpha-tested selected pass has
  to write its bit 7 first, and an alpha-tested surface in front of the
  outline's pixels gets the outline over it like an opaque one does.
- The grid (19) depth-tests against the alpha-tested content like against
  the opaque content; the translucent fills (20-21) depth-test against both
  and blend over them, whatever their registration order.

Open: "Selection stencil mask (fill disabled)" (5) overrides the blending
mode to opaque and draws `VARIANT_DEPTH_ONLY`, which has no alpha test, so
for a selected or hovered alpha-tested mesh it writes depth and stencil
bit 7 over the whole quad, the discarded texels included, while the
selection fill is off. The outline then follows the quad, not the cut-out.

Solid wireframe (passes 3-4) is a shader-variant axis
(`Shader_bool::SOLID_WIREFRAME`): the expanded fill geometry is redrawn with
real polygon edges blended over the lit fill, sharing the fill's exact depth.
It replaces the wide-line edge path when enabled; the two never both draw. On
devices where the variant cannot link, the passes are disabled and the
wide-line edge passes draw instead. (The macOS OpenGL 4.1 case that originally
motivated this fallback is gone -- OpenGL 4.5 + DSA is now the hard minimum.)

### Mirrored (negative-determinant) geometry

Mirrored (negative-scale) world transforms reverse apparent triangle winding. This is
handled per bucket, not per pass: `bucket_primitives()` partitions `Render_bucket`s by
the mesh's `Item_flags::negative_determinant` flag (maintained by
`Mesh::handle_node_transform_update()`), and each renderer (forward, shadow, ID) asks
`Base_render_pipeline::get_pipeline_for()` for the front-face-flipped pipeline variant
when drawing a negative-determinant bucket. The variant is cached alongside the
non-flipped one because the pipeline variant hash covers the rasterization state. The
normal-matrix flip for mirrored meshes is applied in `Primitive_buffer::update()`.

## Forward renderer

`erhe::scene_renderer::Forward_renderer` is the core drawing system invoked by each composition pass. For each call it:

1. Updates GPU ring buffers: camera, material (with textures from texture heap), joint (skinning), light (with shadow map handles), primitive (per-mesh data)
2. Builds draw indirect commands
3. For each render pipeline state, sets the pipeline and calls `multi_draw_indexed_primitives_indirect()` to draw all matching primitives

Debug visualization modes (depth, normals, tangents, etc.) ride on the `SHADER_DEBUG` integer axis of `Shader_key`; each value selects a compile-time override block at the end of `standard.frag`. The renderer falls back to an error shader when a variant fails to compile.

## Render pipelines

`Pipeline_renderpasses` pre-creates all `erhe::graphics::Render_pipeline_state` objects. Each encapsulates the full fixed-function GL state: shader stages, vertex input, input assembly, rasterization, depth/stencil, color blend, multisample, viewport depth range.

Key pipelines and their depth/stencil/blend configuration:

### Opaque fill (not selected)

- Shader: `standard` variant (per-material `Shader_key`)
- Depth: test enabled, write enabled
- Stencil: disabled
- Blend: disabled
- Rasterization: cull back CCW; negative-determinant buckets draw with the front-face-flipped variant (see above)

### Opaque fill (selected)

- Same as above, plus:
- Stencil: enabled, function=always, op=replace, reference=0x80, write_mask=0x80
- Marks selected geometry in stencil bit 7 for later use by outline, sky, and grid passes

The alpha-tested and translucent fill passes use these same two pipelines;
the blend state comes from the blending class (disabled for opaque and
alpha tested, premultiplied for translucent).

### Edge lines

- Shader: `wide_lines_draw_color`
- Depth: test less_or_equal, write enabled
- Stencil: function=equal 0, test_mask=0x7F, write_mask=0x7F, z_pass_op=incr
- Blend: premultiplied alpha
- Only draws where lower 7 stencil bits are 0; increments on z_pass to tag drawn pixels

### Hidden lines

- Shader: `wide_lines_draw_color`
- Depth: test greater (behind geometry), write disabled
- Stencil: function=equal 0, test_mask=0xFF, write_mask=0x7F, z_pass_op=incr
- Blend: constant alpha 0.2 (ghost lines)
- Renders lines occluded by geometry with transparency

### Selection outline

- Shader: `wide_lines_draw_color` (line topology + geometry-shader expansion when the GS path is enabled, otherwise compute-shader-fed triangle output via `Content_wide_line_renderer`)
- Depth: disabled
- Stencil: function=not_equal 0x80, test_mask=0x80, write_mask=0x80, z_pass_op=replace
- Blend: premultiplied alpha, alpha-to-coverage
- Draws outline only where bit 7 is NOT set (outside the selected mesh silhouette), then sets bit 7 to prevent double-drawing

### Sky

- Shader: `sky`
- Depth: test equal at far plane (viewport_depth_range forced to 0.0 for reverse-Z)
- Stencil: function=equal 0, test_mask=0xFF, write_mask=0x00
- Blend: disabled
- Only renders where the entire stencil is 0 (no geometry, no selection, no edge lines)

### Grid

- Shader: `grid`
- Depth: test less_or_equal, write DISABLED, depth clamp enabled
- Stencil: function=not_equal 0x80, test_mask=0x80, write_mask=0x80, z_pass_op=keep
- Blend: premultiplied alpha
- Renders where bit 7 is not set (skips pixels covered by selected geometry outline)

The grid is an overlay guide, not a surface: it composites its line coverage
over the content phase and leaves the depth buffer to the content. Depth writes
stay off for two independent reasons. A partially covered, blended fragment has
no single depth to claim; and the grid's depth does not agree with the
content's -- the infinite plane is rendered through `clip_from_world_for_grid`
and passes the depth test over opaque content at every pixel below the horizon,
which is why grid lines cross objects that stand in front of the plane. Writing
depth therefore stamped a depth the content disagrees with across the whole
plane, and every pass ordered after the grid -- the translucent content
fill first of all -- was rejected by it, so no alpha-blended or
alpha-tested primitive reached the frame while the grid was visible. The
disagreement itself is still open: the grid should not draw over content that
stands in front of the plane.

### Brush preview

- Shader: `brush`
- Two passes: back faces first (cull front), then front faces (cull back)
- Depth: test enabled, write enabled
- Stencil: disabled
- Blend: premultiplied alpha (translucent preview)

## Stencil buffer protocol

The stencil buffer is cleared to 0 at the start of each render pass. The 8-bit stencil value is divided into two regions:

```
Bit 7 (0x80): Selection/outline bit
Bits 0-6 (0x7F): Per-feature stencil tags (tools, edge lines, debug lines)
```

### Bit 7 -- selection bit

Written during selected geometry fill passes (reference=0x80, write_mask=0x80, op=replace on all outcomes). This marks pixels covered by selected meshes.

Consumers:
- **Outline pass**: draws where bit 7 is NOT set, then sets bit 7 -- produces an outline ring around the selection silhouette without overdrawing the mesh itself
- **Sky pass**: requires stencil==0 (all bits including bit 7) -- sky does not render over outlined areas
- **Grid pass**: draws where bit 7 is NOT set -- grid does not render over the outline

### Bits 0-6 -- per-feature stencil tags

Different rendering features write distinct values into the lower 7 bits to prevent mutual overdraw. Edge line and hidden line passes write via incr from 0, using write_mask=0x7F to preserve the selection bit. Tool rendering writes specific tag values to classify fragments as visible or hidden.

### Stencil values

| Value | Name | Used by |
| :--- | :--- | :--- |
| 1 | `s_stencil_edge_lines` | Edge line passes (written by incr from 0) |
| 2 | `s_stencil_tool_mesh_hidden` | Tool fragments behind scene geometry |
| 3 | `s_stencil_tool_mesh_visible` | Tool fragments in front of scene geometry |
| 8 | `s_stencil_line_renderer_grid_minor` | Grid minor lines |
| 9 | `s_stencil_line_renderer_grid_major` | Grid major lines |
| 10 | `s_stencil_line_renderer_selection` | Selection highlight lines |
| 11 | `s_stencil_line_renderer_tools` | Tool debug lines |

## Tool rendering -- 6-pass stencil protocol

Tools (transform gizmos, manipulators) need to render with x-ray visibility: visible parts are drawn opaque, occluded parts are drawn with transparency. This requires a 6-pass protocol using the scene depth buffer and stencil tagging.

All 6 passes render the same tool mesh geometry. They happen before the main render pass starts (in `Tools::render_viewport_tools()`), operating against the scene depth buffer from the previous frame or ID render.

### Pass 1: Tag hidden parts (`tool1_hidden_stencil`)

- Depth test: greater (fragments behind scene geometry pass)
- Depth write: disabled
- Stencil: function=always, z_pass_op=replace with value 2
- Color: writes disabled
- Effect: fragments of the tool mesh that are behind scene geometry get stencil value 2

### Pass 2: Tag visible parts (`tool2_visible_stencil`)

- Depth test: less_or_equal (fragments in front of or at scene geometry pass)
- Depth write: disabled
- Stencil: function=always, z_pass_op=replace with value 3
- Color: writes disabled
- Effect: visible fragments overwrite with stencil value 3 (overrides value 2 where the tool is in front)

### Pass 3: Depth clear (`tool3_depth_clear`)

- Depth test: always
- Depth write: enabled
- Viewport depth range: fixed at far plane value (0.0 for reverse-Z, 1.0 for forward)
- Color: writes disabled
- Effect: resets depth to far plane everywhere the tool mesh covers, so the next pass can write proper tool depth

### Pass 4: Depth setup (`tool4_depth`)

- Depth test: less (standard)
- Depth write: enabled
- Color: writes disabled
- Effect: writes the tool mesh's actual depth values (against the cleared-to-far background)

### Pass 5: Render visible parts (`tool5_visible_color`)

- Depth test: less_or_equal
- Stencil: function=equal 3 (only where tagged as visible)
- Color: writes enabled, blend disabled (opaque)
- Effect: renders the tool mesh opaque where it was visible against scene geometry

### Pass 6: Render hidden parts (`tool6_hidden_color`)

- Depth test: less_or_equal
- Stencil: function=equal 2 (only where tagged as hidden)
- Color: writes enabled, blend with constant alpha 0.6
- Effect: renders the tool mesh with 60% transparency where it was behind scene geometry

This protocol avoids z-fighting between tool and scene geometry by clearing and re-establishing depth, while the stencil tags ensure each fragment is drawn with the correct opacity.

## Post-processing

When enabled, `Post_processing_node` sits between `Viewport_scene_view` and `Window_imgui_host` in the rendergraph. It implements bloom and tonemapping via a mipmap pyramid:

### Downsample phase

Starting from the full-resolution input texture, each level is downsampled to the next smaller mip level of `downsample_texture`:

- Level 0 to 1: `downsample_with_lowpass_input` -- reads from input texture, applies a custom 36-texel filter (13 bilinear fetches) with lowpass for bloom extraction
- Levels 1 to N: `downsample_with_lowpass` or `downsample` -- reads from `downsample_texture` at `source_lod`

Each level renders into a render pass targeting the specific mip level via `texture_level` in the FBO attachment.

### Upsample phase

Starting from the smallest mip level, each level is upsampled back to the next larger mip level of `upsample_texture`:

- First (smallest): `upsample_first` -- reads from `downsample_texture`
- Middle levels: `upsample` -- reads from `upsample_texture` at `source_lod`, blends with downsample data
- Last (level 0): `upsample_last` -- applies final tonemapping

Configurable parameters: `upsample_radius`, `tonemap_luminance_max`, `tonemap_alpha`, `lowpass_count`.

The output is the level 0 of the upsample texture (returned as a texture view if available, or the full mipmapped texture otherwise).

## ID renderer

`Id_renderer` renders a unique 32-bit ID per mesh primitive for GPU-based mouse picking. It uses a separate FBO with a 32-bit uint color attachment and a depth texture. The pipeline uses the `id` shader with `depth_test_always` to write IDs for all visible geometry.

GPU readback uses a ring buffer of 4 transfer entries to handle pipeline latency. The result provides: mesh reference, primitive index within mesh, triangle ID, and depth value at the queried pixel.

## Key source files

| File | Contents |
| :--- | :--- |
| `src/editor/app_rendering.hpp` | `Pipeline_renderpasses`, `App_rendering`, stencil constants |
| `src/editor/app_rendering.cpp` | Pipeline creation, composition pass setup, render orchestration |
| `src/editor/scene/viewport_scene_view.cpp` | `Viewport_scene_view::execute_rendergraph_node()` |
| `src/editor/scene/viewport_scene_views.cpp` | Rendergraph node creation and connection |
| `src/editor/rendergraph/shadow_render_node.cpp` | Shadow map node |
| `src/editor/rendergraph/post_processing.cpp` | Post-processing node and bloom/tonemap passes |
| `src/editor/renderers/composer.cpp` | Composer iteration |
| `src/editor/renderers/composition_pass.cpp` | Single composition pass execution |
| `src/editor/renderers/id_renderer.cpp` | ID buffer rendering and readback |
| `src/editor/tools/tools.cpp` | Tool 6-pass stencil pipeline setup |
| `src/erhe/scene_renderer/erhe_scene_renderer/forward_renderer.cpp` | Core forward rendering |
| `src/erhe/scene_renderer/erhe_scene_renderer/shadow_renderer.cpp` | Shadow depth rendering |
| `src/erhe/graphics/erhe_graphics/state/depth_stencil_state.hpp` | Stencil state types |
