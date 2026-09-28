# rendergraph/

Stability: stable

## Purpose

Editor-specific render graph nodes that extend `erhe::rendergraph` for shadow mapping, scene rendering, and post-processing.

## Key Types

- **`Shadow_render_node`** -- Rendergraph node that renders shadow maps for a `Scene_view`. Owns a depth texture and `Light_projections`. Called during rendergraph execution to render shadow passes via `erhe::scene_renderer::Shadow_renderer`. Can be reconfigured at runtime (resolution, light count, depth bits). Resolves the scene's light set before the shadow passes; when the scene's light layer is empty and `Graphics_settings::headlight_when_unlit` is on it resolves its own one-light set instead (`resolve_headlight`): one white directional light along this view's camera axis, casting no shadow, the way usdview lights a stage that authors no light. The headlight is owned by the render node and is no scene item - it is in no hierarchy, no save and no undo - and because it lives per node, one scene shown in two viewports gets one headlight per viewport. Its only per-frame cost is writing the camera transform onto it, and only while the scene stays unlit.

- **`Post_processing`** -- Manages the post-processing pipeline (bloom with downsample/upsample passes and tonemapping). Creates shader programs and render pipeline states. Factory method `create_node()` creates `Post_processing_node` instances.

- **`Post_processing_node`** -- A `Rendergraph_node` that applies bloom and tonemapping to a rendered scene texture. Manages downsample/upsample texture pyramids with configurable mip levels. Connected between `Viewport_scene_view` (producer) and `Viewport_window` (consumer). The level 0 upsample texture (the post-processed image, `get_producer_output_texture()`) also carries transfer-source usage, so it can be read back.

- **`Scene_image_capture`**, **`Scene_image_view`**, **`Scene_image_readback_node`** (`src/editor/scene/scene_image_capture.hpp`) -- the offscreen scene render behind the MCP tool `render_scene_image`; see "Scene image capture" below.

## Scene image capture

`render_scene_image` renders a scene through an explicit camera into render
targets owned by the request, so the image does not depend on any
`Viewport_window` or ImGui window existing, being visible, focused, hovered or
sized. Agents verify scene content with it and keep `capture_screenshot` for
the editor UI.

- Chain: `Scene_image_capture` builds `Shadow_render_node` ->
  `Scene_image_view` -> `Post_processing_node` -> `Scene_image_readback_node`
  in the rendergraph, the same nodes and the same wiring `Scene_views`
  gives a viewport, minus the overlay node and the ImGui host consumer.
  Under `--no-post-processing` the post-processing node is left out and the
  png is the HDR scene color, as viewports then show it.
- `Scene_image_view` is a window-less sibling of `Viewport_scene_view`
  (`Scene_view` + `Texture_rendergraph_node`, HDR `format_16_vec4_float`
  color, `d32_sfloat_s8_uint` depth, the requested MSAA sample count). Its
  `execute_rendergraph_node()` ensures the atmosphere LUTs and calls
  `App_rendering::render_viewport_main(context, false)` with
  `Render_context::content = Render_content::scene_only`: no ID pass, no tools,
  no renderables, no debug renderer or text, no overlay passes, and
  `Composer::render()` skips the composition passes marked
  `Composition_pass_kind::editor_aid` (grid, selection outline, ghost edge
  lines, brush preview, solid bones). Its viewport config is the default one
  a new viewport starts from (`Scene_views::get_viewport_config_data()`)
  with edge lines, solid wireframe, centroids and corner points off and the
  selected style equal to the unselected one. Nothing about the view persists
  (no settings store). The shadow fit gets its aspect ratio from
  `Scene_view::get_camera_viewport()`, which it overrides.
- Explicit camera (`camera`): a standalone `erhe::scene::Camera` (perspective,
  vertical fov) owned by the view, placed with `erhe::math::create_look_at`,
  with the given exposure and shadow range. `camera_node` uses a scene camera
  as is.
- Lifetime: the `Mcp_server` holds the one `Scene_image_capture` of the
  pending request. The first MCP pass builds the chain and defers the
  request; the next `Rendergraph::execute()` runs the chain, and
  `Scene_image_readback_node` copies the chosen texture (the post-processed
  image for png, the view's resolved HDR color for linear) into a host-visible
  buffer, records the frame index and disables all four nodes; later passes
  poll `Device::is_frame_completed()`, convert the half floats, write the file
  and destroy the capture, which unregisters the nodes and hands the shadow
  node back through `App_rendering::destroy_shadow_node()`. Nothing is cached
  between requests, so the tool costs nothing per frame when unused. When the
  server drops the request (expired), it clears the capture's request and
  `Mcp_server::release_abandoned_scene_image_capture()` destroys the capture
  right away, or, while its copy is still in flight, on the first MCP pass
  after that frame retires. `batch` refuses the tool before any sub-call runs,
  so no chain is built for a call that cannot defer. A second request waits
  (defers) while another request's capture is pending.
- Files: png is the post-processed image sRGB-encoded on the CPU (IEC
  61966-2-1, what the sRGB swapchain does; measured within 1 level of the
  viewport's own pixels). linear is a portable float map (`.pfm`: `PF`
  header, width height, scale `-1.0` = little-endian, RGB float32, rows
  bottom to top) of the scene color before post-processing, camera exposure
  applied; the reply adds min / max / mean Rec. 709 luminance.
- Debug modes: `shader_debug` selects a `Shader_debug` variant for the
  view's content passes. A debug override replaces the lit color after the
  exposure and output-range clamp, so the linear file holds its values
  unscaled, at the fp16 precision of the color target (a relative step of
  2^-11). `world_position` (36) writes the fragment world position, the
  receiver position of each pixel; MSAA resolve averages positions across
  silhouettes, so `msaa_samples: 0` gives exact per-pixel receivers.
  `shadow_visibility` (30) shows the light named by `shadow_debug_light`
  (light name or id): `Scene_image_view::execute_rendergraph_node()` resolves
  it to its light slot through the light set its own shadow pass just built
  (`Light_projections::get_light_projection_transforms_for_light()`), passes
  the slot as `Render_context::shadow_debug_light_index`, and the reply
  reports it as `shadow_debug_light.light_index` (null when the light got no
  slot). Without it the slot is 0, as for viewports.
- Determinism: with a static field (ambient) repeated renders are pixel
  identical; with DDGI the field's per-update random rays make them differ by
  at most one 8-bit level while it is converged.

## Public API / Integration Points

- `Shadow_render_node::get_light_projections()` -- access light projection matrices. The consumer
  (`Composition_pass` -> `Light_buffer::update`) reads whatever the node last left there, and the
  slot entries name their lights by raw pointer, so `execute_rendergraph_node` clears the set before
  anything can return: a frame in which the node bails (no scene root, no camera, no content mesh, no
  shadow map texture) must hand the forward pass an empty set, never the previous frame's pointers.
  Closing a scene empties its light layer and frees its lights while those early exits are being
  taken, and a retained set is then a use-after-free in `Light_buffer::update`.
- `Shadow_render_node::reconfigure()` -- change shadow map settings at runtime
- `Post_processing::create_node()` -- factory for post-processing nodes
- `Post_processing_node::viewport_toolbar()` -- per-viewport bloom/tonemap settings

## Dependencies

- erhe::rendergraph, erhe::graphics, erhe::scene_renderer
- editor: App_context, Programs
