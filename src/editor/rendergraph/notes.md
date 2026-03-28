# rendergraph/

## Purpose

Editor-specific render graph nodes that extend `erhe::rendergraph` for shadow mapping, scene rendering, and post-processing.

## Key Types

- **`Shadow_render_node`** -- Rendergraph node that renders shadow maps for a `Scene_view`. Owns a depth texture and `Light_projections`. Called during rendergraph execution to render shadow passes via `erhe::scene_renderer::Shadow_renderer`. Can be reconfigured at runtime (resolution, light count, depth bits).

- **`Post_processing`** -- Manages the post-processing pipeline (bloom with downsample/upsample passes and tonemapping). Creates shader programs and render pipeline states. Factory method `create_node()` creates `Post_processing_node` instances.

- **`Post_processing_node`** -- A `Rendergraph_node` that applies bloom and tonemapping to a rendered scene texture. Manages downsample/upsample texture pyramids with configurable mip levels. Connected between `Viewport_scene_view` (producer) and `Viewport_window` (consumer).

## Public API / Integration Points

- `Shadow_render_node::get_light_projections()` -- access light projection matrices
- `Shadow_render_node::reconfigure()` -- change shadow map settings at runtime
- `Post_processing::create_node()` -- factory for post-processing nodes
- `Post_processing_node::viewport_toolbar()` -- per-viewport bloom/tonemap settings

## Dependencies

- erhe::rendergraph, erhe::graphics, erhe::scene_renderer
- editor: App_context, Programs
