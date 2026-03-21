# renderers/

## Purpose

Low-level rendering infrastructure for the editor: shader programs, GPU memory management, ID-based picking, render pass composition, and viewport configuration.

## Key Types

- **`Programs`** -- Loads and manages all shader programs (standard, debug visualizations, tools, sky, grid, etc.). Provides `get_variant_shader_stages()` for selecting debug visualization modes. Uses `Shader_stages_builder` for deferred shader compilation.

- **`Mesh_memory`** -- Allocates and manages shared GPU buffers for vertex and index data. Provides three vertex buffer streams (position, non-position attributes, custom attributes) and a single index buffer. Includes a `Buffer_transfer_queue` for staging uploads. All editor meshes share this memory pool.

- **`Id_renderer`** -- GPU-based object picking. Renders mesh IDs and triangle IDs to an offscreen framebuffer, then reads back a small region around the cursor. Uses a ring buffer for async readback across frames. Returns `Id_query_result` with mesh, primitive index, triangle ID, and depth.

- **`Composer`** -- Composites multiple render passes (content, selection, tools, etc.) for final viewport output.

- **`Composition_pass`** -- Configurable render pass with fill mode (polygon, wireframe), blend mode, and selection mode.

- **`Render_context`** -- Data passed to rendering functions: scene root, camera, viewport, shadow node, etc.

- **`Viewport_config`** -- Per-viewport rendering options (grid, shadows, edge lines, selection outline, etc.).

## Public API / Integration Points

- `Programs::get_variant_shader_stages()` -- get shader for a debug visualization mode
- `Mesh_memory::buffer_info` -- used by `erhe::primitive` to build GPU meshes
- `Id_renderer::render()` / `get()` -- render and query object IDs
- `Render_context` -- passed to all `tool_render()` and `render_viewport_*()` calls

## Dependencies

- erhe::graphics, erhe::scene_renderer, erhe::renderer, erhe::primitive
- editor: Mesh_memory buffers are shared by all mesh-creating subsystems
