# erhe_renderer

## Purpose
GPU rendering utilities for debug visualization and text overlay in 3D viewports. Provides a debug line/shape renderer that converts lines to triangles via compute shaders, a 2D text renderer for in-viewport labels, a texture fullscreen renderer, and draw indirect buffer management for batched mesh rendering.

## Key Types
- `Debug_renderer` -- Central coordinator for debug line/shape rendering. Manages a stack of views, dispatches compute shaders to expand lines into triangles, and renders the results.
- `Primitive_renderer` -- Immediate-mode draw API obtained from `Debug_renderer::get()`. Provides `add_lines()`, `add_cube()`, `add_sphere()`, `add_cone()`, `add_torus()`, `add_bone()` etc.
- `Debug_renderer_bucket` -- Groups draw calls by pipeline config (primitive type, stencil, visibility). Internally manages GPU ring buffers.
- `Debug_renderer_config` -- Selects primitive type, stencil reference, and visible/hidden draw flags for a bucket.
- `Text_renderer` -- Renders 2D text at 3D positions using a font atlas texture. Uses `erhe::ui::Font` for glyph layout.
- `Texture_renderer` -- Simple fullscreen texture blit.
- `Draw_indirect_buffer` -- Builds GPU draw-indirect command buffers from a span of meshes filtered by `Item_filter`.
- `Jolt_debug_renderer` -- Adapter implementing Jolt's `JPH::DebugRenderer` interface, forwarding draw calls to `Debug_renderer`.
- `View` -- Camera view data (clip_from_world matrix, viewport rect, FOV sides).

## Public API
- `Debug_renderer::get(config)` returns a `Primitive_renderer` for a given config.
- Call `begin_frame()`, draw with `Primitive_renderer`, then `compute()` and `render()`, finally `end_frame()`.
- `Text_renderer::print(position, color, text)` queues text; `render(encoder, viewport)` draws it.
- `Draw_indirect_buffer::update(meshes, mode, filter)` fills indirect draw commands.

## Dependencies
- erhe::graphics (Device, Ring_buffer_client, Shader_stages, Pipeline, Texture)
- erhe::scene (Camera, Transform -- for sphere/cone rendering)
- erhe::primitive (Primitive_mode for draw indirect)
- erhe::ui (Font for text rendering)
- erhe::math (Viewport)
- erhe::dataformat (Vertex_format)
- erhe::verify
- Jolt Physics (optional, for Jolt_debug_renderer behind `JPH_DEBUG_RENDERER`)
- glm, etl

## Notes
- Debug lines are stored as per-vertex SSBO data and expanded to triangles by a compute shader before rasterization.
- Buckets use `etl::vector` (fixed capacity) so that element addresses remain stable.
- `Primitive_renderer` is move-only; obtain one per frame per config.
