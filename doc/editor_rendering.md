# Editor Rendering

> This document was mostly written by Claude and may contain inaccuracies.

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

1. **ID rendering** -- If the viewport is hovered and ID picking is enabled, `Id_renderer::render()` renders a unique 32-bit ID per mesh primitive into a separate FBO. GPU readback with a ring buffer (4 entries for pipeline latency) returns the mesh, primitive index, triangle ID, and depth at the mouse position.

2. **Tool pre-rendering** -- `Debug_renderer::begin_frame()` starts a new frame of debug line/point data. `Tools::render_viewport_tools()` submits tool geometry (gizmos, manipulators) using the 6-pass stencil protocol described below. `Renderable::render()` is called without an encoder for first-pass setup.

3. **Render pass begins** -- A `Render_command_encoder` is created and an MSAA render pass starts, clearing color, depth, and stencil.

4. **Main scene rendering** -- `render_viewport_main()` calls `render_composer()` which executes the Composer's composition passes in order (see below).

5. **Renderables** -- Renderable objects get a second call, now with the active encoder.

6. **Debug overlays** -- `Debug_renderer::render()` draws accumulated debug lines and points. `Text_renderer::render()` draws 2D text labels placed at 3D positions.

7. **Render pass ends** -- MSAA resolve produces the final viewport texture.

## Shadow rendering

`Shadow_render_node` owns a depth texture array (one layer per light) and a `Light_projections` object. During execution it calls `erhe::scene_renderer::Shadow_renderer::render()` which:

- Renders each light into its own array layer via a separate render pass
- Uses a depth-only shader with `Color_writes_disabled`
- Uses `cull_mode_none` to catch all shadow casters
- Supports reverse depth (clear to 0.0) and forward depth (clear to 1.0)
- Shadow resolution, light count, and depth bits are configurable at runtime via `reconfigure()`

The main scene rendering accesses shadow maps through the rendergraph connection and passes `Light_projections` to the forward renderer.

## Composer and composition passes

The `Composer` holds an ordered list of `Composition_pass` instances. `render_composer()` iterates them in order; each pass calls `Forward_renderer::render()` with its specific configuration.

A `Composition_pass` specifies:

- **Mesh layers** to render (content, brush, controller, rendertarget)
- **Primitive mode** (polygon_fill, edge_lines, corner_points, polygon_centroids)
- **Item filter** selecting meshes by flag combinations (visible, opaque, selected, translucent, etc.)
- **Render pipeline states** (one or more, executed in sequence)
- **Render style** callback providing colors, roughness, metallic overrides
- **Non-mesh vertex count** for fullscreen passes (sky, grid)

### Composition pass order

The editor creates these passes in `App_rendering::App_rendering()`:

| # | Pass | Primitive mode | Filter | Pipeline |
| :--- | :--- | :--- | :--- | :--- |
| 1 | Opaque fill not selected (positive det.) | polygon_fill | visible, opaque, not selected | Standard opaque, cull back CCW |
| 2 | Opaque fill not selected (negative det.) | polygon_fill | visible, opaque, not selected, negative det. | Standard opaque, cull back CW |
| 3 | Opaque fill selected (positive det.) | polygon_fill | visible, opaque, selected | Stencil-marking (bit 7) |
| 4 | Opaque fill selected (negative det.) | polygon_fill | visible, opaque, selected, negative det. | Stencil-marking (bit 7) |
| 5 | Edge lines not selected | edge_lines | visible, opaque, not selected | Edge line pipeline |
| 6 | Edge lines selected | edge_lines | visible, opaque, selected | Edge line pipeline |
| 7 | Selection/hover outline | polygon_fill | visible, opaque, selected or hovered | Outline (fat_triangle shader) |
| 8 | Sky | polygon_fill | (none) | Fullscreen, depth==far, stencil==0 |
| 9 | Grid | polygon_fill | (none) | Fullscreen, depth clamp |
| 10 | Translucent fill | polygon_fill | visible, translucent | Premultiplied alpha |
| 11 | Translucent edge lines | edge_lines | visible, translucent | Premultiplied alpha |
| 12 | Brush (back faces) | polygon_fill | visible, brush | Cull front, premultiplied alpha |
| 13 | Brush (front faces) | polygon_fill | visible, brush | Cull back, premultiplied alpha |
| 14 | Rendertarget meshes | polygon_fill | visible, rendertarget | Textured, premultiplied alpha |

Positive/negative determinant variants handle mirrored (negative-scale) geometry by flipping the cull direction.

## Forward renderer

`erhe::scene_renderer::Forward_renderer` is the core drawing system invoked by each composition pass. For each call it:

1. Updates GPU ring buffers: camera, material (with textures from texture heap), joint (skinning), light (with shadow map handles), primitive (per-mesh data)
2. Builds draw indirect commands
3. For each render pipeline state, sets the pipeline and calls `multi_draw_indexed_primitives_indirect()` to draw all matching primitives

The forward renderer supports shader stages override for debug visualization modes (depth, normals, tangents, etc.) and falls back to an error shader when the main shader is invalid.

## Render pipelines

`Pipeline_renderpasses` pre-creates all `erhe::graphics::Render_pipeline_state` objects. Each encapsulates the full fixed-function GL state: shader stages, vertex input, input assembly, rasterization, depth/stencil, color blend, multisample, viewport depth range.

Key pipelines and their depth/stencil/blend configuration:

### Opaque fill (not selected)

- Shader: `circular_brushed_metal`
- Depth: test enabled, write enabled
- Stencil: disabled
- Blend: disabled
- Positive determinant variant: cull back CCW; negative determinant: cull back CW

### Opaque fill (selected)

- Same as above, plus:
- Stencil: enabled, function=always, op=replace, reference=0x80, write_mask=0x80
- Marks selected geometry in stencil bit 7 for later use by outline, sky, and grid passes

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

- Shader: `fat_triangle` (geometry shader expands triangles into thick outlines)
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
- Depth: test less_or_equal, write enabled, depth clamp enabled
- Stencil: function=not_equal 0x80, test_mask=0x80, write_mask=0x80, z_pass_op=keep
- Blend: disabled
- Renders where bit 7 is not set (skips pixels covered by selected geometry outline)

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

The output is the level 0 of the upsample texture (returned as a texture view if available, or the full mipmapped texture on GL 4.1).

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
