# rendergraph/

## Purpose

Editor-specific render graph nodes that extend `erhe::rendergraph` for shadow mapping, scene rendering, and post-processing.

## Key Types

- **`Shadow_render_node`** -- Rendergraph node that renders shadow maps for a `Scene_view`. Owns a depth texture and `Light_projections`. Called during rendergraph execution to render shadow passes via `erhe::scene_renderer::Shadow_renderer`. Can be reconfigured at runtime (resolution, light count, depth bits). Resolves the scene's light set before the shadow passes; when the scene's light layer is empty and `Graphics_settings::headlight_when_unlit` is on it resolves its own one-light set instead (`resolve_headlight`): one white directional light along this view's camera axis, casting no shadow, the way usdview lights a stage that authors no light. The headlight is owned by the render node and is no scene item - it is in no hierarchy, no save and no undo - and because it lives per node, one scene shown in two viewports gets one headlight per viewport. Its only per-frame cost is writing the camera transform onto it, and only while the scene stays unlit.

- **`Post_processing`** -- Manages the post-processing pipeline (bloom with downsample/upsample passes and tonemapping). Creates shader programs and render pipeline states. Factory method `create_node()` creates `Post_processing_node` instances.

- **`Post_processing_node`** -- A `Rendergraph_node` that applies bloom and tonemapping to a rendered scene texture. Manages downsample/upsample texture pyramids with configurable mip levels. Connected between `Viewport_scene_view` (producer) and `Viewport_window` (consumer).

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
