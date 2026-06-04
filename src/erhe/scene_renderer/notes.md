# erhe_scene_renderer

## Purpose
Renders `erhe::scene` content (meshes, lights, shadows, skinning) to the GPU. Provides forward rendering, shadow map generation, and GPU buffer management for uploading camera, light, material, primitive, joint, and cube instance data via ring buffers. Defines the standard shader interface (uniform/SSBO block layouts) shared between C++ and GLSL.

## Key Types
- `Forward_renderer` -- Renders meshes with full lighting, materials, and shadows. Takes `Render_parameters` specifying camera, lights, skins, materials, mesh spans, pipeline states, and viewport.
- `Shadow_renderer` -- Generates shadow maps by rendering meshes from each light's perspective into a texture array.
- `Program_interface` -- Defines the shader resource layout (vertex format, camera/light/material/primitive/joint/cube blocks). Creates shader prototypes with all blocks pre-configured.
- `Camera_buffer` -- Ring buffer client uploading camera matrices, viewport, exposure, and grid settings.
- `Light_buffer` -- Ring buffer client uploading light data (position, direction, color, shadow transforms) and shadow map texture handles.
- `Material_buffer` -- Ring buffer client uploading PBR material properties and texture handles.
- `Primitive_buffer` -- Ring buffer client uploading per-primitive world transforms, normal transforms, color, material index, and skinning data. Also manages ID ranges for GPU picking.
- `Joint_buffer` -- Ring buffer client uploading skeletal joint transforms for skinned meshes.
- `Cube_renderer` / `Cube_instance_buffer` / `Cube_control_buffer` -- Instanced voxel cube rendering system with packed 11-11-10 bit positions.
- `Glyph_interface` / `Glyph_buffer` -- Static SSBO holding quadratic bezier glyph curve data (from `erhe::ui::extract_glyph_outlines()`) for GPU curve-based text rendering, e.g. grid axis labels in the editor's grid shader. Fixed slot convention: 0..9 = digits '0'..'9', 10 = '-', 11 = '.'. SSBO-only: when the device lacks shader storage buffers, the block falls back to a dummy uniform block and `ERHE_GRID_LABELS` is not defined for shaders. Bound unconditionally by `Forward_renderer` (binding point 8) so the shared bind group stays complete.
- `Texel_renderer` -- Simplified renderer for texel-space operations.
- `Light_projections` -- Computes and stores shadow projection transforms for all lights in a frame.

## Public API
- Create `Program_interface` with a vertex format and config.
- Create `Forward_renderer` / `Shadow_renderer` with the program interface.
- Each frame, call `forward_renderer.render(params)` or `shadow_renderer.render(params)`.
- Buffer classes (`Camera_buffer`, etc.) have `update()` methods that return `Ring_buffer_range` for binding.

## Dependencies
- erhe::graphics (Device, Ring_buffer_client, Shader_resource, Shader_stages, Texture, Sampler, Render_pass, Buffer, Gpu_timer)
- erhe::renderer (Draw_indirect_buffer)
- erhe::scene (Camera, Light, Mesh, Skin, Node, Mesh_layer)
- erhe::primitive (Material, Primitive_mode)
- erhe::dataformat (Format, Vertex_format)
- erhe::math (Viewport)
- erhe::ui (Glyph_outline_set for GPU glyph curve data)
- glm

## Notes
- Buffer binding points are defined as macros in `buffer_binding_points.hpp` (0-8).
- All GPU buffers use the ring buffer pattern for lock-free multi-frame usage, except `Cube_instance_buffer` and `Glyph_buffer` which are static (uploaded once at init).
- `Primitive_buffer` supports ID-based GPU picking by assigning unique ID offsets to each primitive.
