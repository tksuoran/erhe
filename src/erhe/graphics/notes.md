# erhe_graphics

## Purpose
Vulkan-style abstraction over OpenGL (with work-in-progress Vulkan backend). Provides
GPU resource management, render pipeline state, shader compilation, command encoding,
ring buffers, texture/sampler management, render passes, and a device abstraction that
hides the underlying graphics API behind a pimpl pattern.

## Key Types
- `Device` -- Central graphics device. Creates command encoders, manages ring buffers, queries capabilities, handles frame lifecycle (`wait_frame`/`begin_frame`/`end_frame`).
- `Device_info` -- GPU capability queries: GLSL version, limits, feature flags (bindless textures, sparse textures, persistent buffers, compute shaders, multi-draw indirect).
- `Buffer` -- GPU buffer (vertex, index, uniform, storage, etc.) with mapping and flush operations. Uses pimpl for backend.
- `Buffer_allocator` -- Bump allocator over a `Buffer` for sub-allocation.
- `Texture` -- GPU texture (1D/2D/3D/cube) with mipmap, MSAA, sparse, and array layer support. Extends `erhe::Item`.
- `Sampler` -- Texture sampler with filtering, addressing, LOD, and anisotropy settings.
- `Shader_stages` -- Compiled and linked shader program. Created from `Shader_stages_prototype`.
- `Shader_stages_create_info` -- Describes shader sources, defines, interface blocks, vertex format, fragment outputs.
- `Shader_resource` -- Describes GLSL resources (uniform/storage blocks, struct types, samplers). Generates GLSL source strings for injection into shaders, eliminating need for reflection.
- `Render_pipeline_state` -- Complete render pipeline: shader, vertex input, rasterization, depth/stencil, color blend, multisample, viewport, scissor states.
- `Render_pass` -- Framebuffer configuration with color/depth/stencil attachments and load/store actions.
- `Render_command_encoder` -- Records draw commands: set pipeline, bind buffers, draw primitives (including multi-draw indirect).
- `Ring_buffer` -- Circular GPU buffer for streaming per-frame data with fence-based synchronization.
- `Shader_monitor` -- Watches shader source files and hot-reloads programs when files change.
- `Fragment_outputs` -- Describes fragment shader output declarations.
- `Surface` / `Swapchain` -- Window surface and swapchain management.

## Public API
- `Device device(surface_create_info)` -- Create the graphics device.
- `device.make_render_command_encoder()` -- Get a command encoder for drawing.
- `device.allocate_ring_buffer_entry(target, usage, size)` -- Allocate from ring buffer.
- `Buffer(device, create_info)` / `Texture(device, create_info)` -- Create GPU resources.
- `Shader_stages(device, prototype)` -- Create shader program from compiled prototype.
- `Scoped_render_pass(render_pass)` -- RAII render pass begin/end.
- `Render_command_encoder::set_render_pipeline_state()` / `draw_indexed_primitives()` -- Issue draw calls.

## Dependencies
- **erhe libraries:** `erhe::dataformat` (public), `erhe::item` (public), `erhe::utility` (public), `erhe::gl` (for OpenGL backend), `erhe::log`, `erhe::verify`, `erhe::profile`
- **External:** glm, OpenGL or Vulkan (selected at CMake time)

## Notes
- All major types use the pimpl pattern (`*_impl` classes) to isolate backend-specific code. The OpenGL implementations live in `gl/` and Vulkan in `vulkan/`.
- `Shader_resource` is used to programmatically build GLSL interface declarations from C++, keeping shader sources and C++ code in sync without reflection.
- `Reloadable_shader_stages` combines `Shader_stages_create_info` with a live `Shader_stages` for hot-reload via `Shader_monitor`.
- Enums in `enums.hpp` mirror Vulkan concepts (Buffer_target, Texture_type, Memory_usage, etc.) to keep the API backend-neutral.
- The `Graphics_config` type is generated (see `generated/graphics_config.hpp`).
