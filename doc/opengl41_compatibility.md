# OpenGL 4.1 Compatibility Layer

erhe targets OpenGL 4.5 with DSA (Direct State Access) but includes a runtime compatibility layer that allows it to run on OpenGL 4.1 core profile (the maximum version available on macOS).

## Feature Gap and Solutions

| Feature | GL version | Solution |
|---|---|---|
| Direct State Access (DSA) | 4.5 | Bind-to-edit wrappers with RAII guards |
| Shader Storage Buffers (SSBO) | 4.3 | UBOs for data blocks; buffer textures for text renderer |
| Compute shaders | 4.3 | Disabled; CPU/GL_LINES fallback for debug renderer |
| Multi-draw indirect | 4.3 | Loop of individual draw calls |
| glBufferStorage / persistent mapping | 4.4 | glBufferData + CPU shadow buffer + glBufferSubData |
| glBindTextureUnit | 4.5 | glActiveTexture + glBindTexture |
| glBindTextures / glBindSamplers | 4.4 | Loop of individual binds |
| glClipControl | 4.5 | Forward depth with [-1,1] range (no reverse-Z) |
| glTextureView | 4.3 | sampler2DArray + layer index; post-processing disabled |
| glClearTexImage | 4.4 | FBO clear fallback |
| BaseInstance draw calls | 4.2 | draw_elements_instanced_base_vertex (base_instance always 0) |
| glInvalidateBufferSubData | 4.3 | No-op (hint only) |
| glInvalidateFramebuffer | 4.3 | Disabled |
| layout(binding=N) in GLSL | 4.20/4.30 | glUniformBlockBinding / glUniform1i after link |
| std430 layout | 4.30 | std140 (existing fallback) |
| glVertexAttribFormat / glBindVertexBuffer | 4.3 | glVertexAttribPointer combined model |
| glDebugMessageCallback | 4.3 | Disabled (TODO: GL_ARB_debug_output) |

## Runtime Capability Detection

`Device_info` flags control all compatibility paths at runtime. Each flag is detected from GL version and/or ARB extensions during device initialization:

- `use_direct_state_access` — GL >= 4.5 or ARB_direct_state_access
- `use_shader_storage_buffers` — GL >= 4.3
- `use_compute_shader` — GL >= 4.3
- `use_persistent_buffers` — GL >= 4.4 or ARB_buffer_storage
- `use_clip_control` — GL >= 4.5 or ARB_clip_control
- `use_texture_view` — GL >= 4.3 or ARB_texture_view
- `use_clear_texture` — GL >= 4.4 or ARB_clear_texture
- `use_base_instance` — GL >= 4.2 or ARB_base_instance
- `use_debug_output` / `use_debug_groups` — GL >= 4.3
- `use_bindless_texture` — ARB_bindless_texture

When `force_glsl_version` is set below 430, `use_compute_shader` and `use_shader_storage_buffers` are automatically disabled regardless of hardware support.

For testing on capable hardware, `erhe.json` supports force flags: `force_no_direct_state_access`, `force_no_persistent_buffers`, `force_no_clip_control`, `force_emulate_multi_draw_indirect`, `force_gl_version`, `force_glsl_version`.

## DSA Emulation

### Binding State Tracker

`Gl_binding_state` (in `Device_impl`) shadows all GL binding points: buffers (11 targets), textures (32 units x 10 targets), samplers, framebuffers (read/draw), renderbuffer, VAO, program. It provides two APIs:

- `bind_*()` — persistent state change (updates shadow, issues GL call only if changed)
- `push_*()` — temporary binding for DSA emulation, returns RAII guard that restores previous binding

The `std::optional` + `emplace` pattern allows conditional RAII:

```cpp
std::optional<Texture_binding_guard> texture_guard;
if (!use_dsa) {
    constexpr GLuint scratch_unit = Gl_binding_state::s_max_texture_units - 1;
    texture_guard.emplace(
        binding_state.push_texture(scratch_unit, gl_texture_target, gl_name)
    );
}
// DSA: gl::texture_foo(gl_name, ...)
// Pre-DSA: gl::tex_foo(target, ...)
```

### Texture Operations

Scratch texture unit 31 (`s_max_texture_units - 1`) is used for temporary binds. All DSA texture calls (`texture_storage_*`, `texture_sub_image_*`, `texture_parameter_i`, `texture_buffer`, `generate_texture_mipmap`) are emulated by pushing on the scratch unit and calling the non-DSA equivalent (`tex_storage_*`, `tex_sub_image_*`, etc.).

Multi-bind calls (`bind_textures`, `bind_samplers`) are emulated with loops of individual `bind_texture`/`bind_sampler` calls via the binding state tracker.

### Buffer Operations

`copy_write_buffer` target is used for temporary buffer binds (avoids conflict with real targets). All DSA buffer calls (`named_buffer_storage`, `named_buffer_sub_data`, `map_named_buffer_range`, `unmap_named_buffer`) are emulated by binding to `copy_write_buffer` and calling the non-DSA equivalent.

`named_buffer_storage` (GL 4.4) is emulated with `buffer_data`. Persistent mapping flags are not available; the ring buffer system uses a CPU shadow buffer with `glBufferSubData` upload instead.

Buffer usage hint: `stream_draw` for persistent buffer fallback (instead of `static_draw`) to avoid driver performance warnings.

### Framebuffer Operations

`push_framebuffer(draw_framebuffer)` wraps all framebuffer setup. DSA calls (`named_framebuffer_texture`, `named_framebuffer_draw_buffers`, `check_named_framebuffer_status`) become their non-DSA equivalents operating on the currently bound framebuffer.

For blit operations, read and draw framebuffers are bound explicitly.

Clear operations (`clear_named_framebuffer_fv`, etc.) become `clear_buffer_fv` etc. operating on the bound draw framebuffer.

`get_named_framebuffer_parameter_iv` (GL 4.3 diagnostic) is skipped on pre-DSA.

### Vertex Array Operations

GL 4.3's separate format/binding model (`vertex_attrib_format` + `vertex_attrib_binding` + `bind_vertex_buffer`) is not available on GL 4.1. The fallback uses the combined `vertex_attrib_pointer` model:

- `update()` only enables attributes and sets divisors (format deferred)
- `set_vertex_buffer()` calls `vertex_attrib_pointer` combining format + buffer binding
- Offset = buffer_offset (from set_vertex_buffer) + attribute.offset (relative to vertex start)

### Object Creation

All GL object creation goes through `Device_impl` factory methods. DSA path uses `gl::create_*`, pre-DSA path uses `gl::gen_*` + `push_*()` binding (RAII guard restores). Programs, shaders, samplers, and queries don't need DSA emulation for creation.

## SSBO to UBO Fallback

When `use_shader_storage_buffers` is false, all shader storage blocks are created as uniform blocks with fixed-size arrays. The `Shader_resource` type is set to `uniform_block` instead of `shader_storage_block`, and unsized arrays become fixed-size based on `max_uniform_block_size / element_size`.

### std140 Array Alignment

Array elements in std140 layout are rounded up to vec4 (16-byte) alignment:

```cpp
const std::size_t struct_size  = my_struct.get_size_bytes();
const std::size_t element_size = ((struct_size + 15u) / 16u) * 16u;
const std::size_t ubo_max     = max_uniform_block_size / element_size;
```

`Program_interface` writes clamped UBO limits back to config so callers see actual capacity.

### Affected blocks

| Block | File | Notes |
|---|---|---|
| primitive | primitive_buffer.cpp | max_primitive_count clamped |
| material | material_buffer.cpp | max_material_count clamped |
| joint | joint_buffer.cpp | max_joint_count clamped |
| cube instance/control | cube_instance_buffer.cpp | Two blocks |
| draw (ImGui) | imgui_renderer.cpp | |
| vertex_ssbo (text) | text_renderer.cpp | Uses buffer texture instead (see below) |

### Text Renderer Buffer Texture

UBOs limit the text renderer to ~1024 glyphs per draw (64KB / 16 bytes per uvec4). Buffer textures (`GL_TEXTURE_BUFFER`, GL 3.1+) provide much larger capacity.

When `use_shader_storage_buffers` is false:
- A `Texture` with `texture_buffer` type and `rgba32ui` pixel format is created
- `Texture::set_buffer()` (re)associates the texture with the ring buffer's underlying GL buffer
- `Texture_heap` uses `reserved_slot_count = 2`: slot 0 = font texture (unit 0, `s_texture`), slot 1 = buffer texture (unit 1, `s_vertex_data`)
- Shader reads via `texelFetch(s_vertex_data, index + vertex_data_offset)` with `ERHE_VERTEX_DATA_TEXTURE_BUFFER` define
- Projection UBO written per-draw because `vertex_data_offset` differs per ring buffer range

## Persistent Buffer Fallback

Without `glBufferStorage` (GL 4.4), the ring buffer system uses a CPU shadow buffer:

- `acquire()` returns spans into the CPU buffer
- `close()` / `flush()` upload to GPU via `glBufferSubData`
- `glBufferData` with `stream_draw` usage hint creates the GPU buffer

DSA-disabled path automatically disables persistent buffers.

## Depth and Projection

When `use_clip_control` is false (no `glClipControl`):
- Forward depth with [-1,1] NDC range (default OpenGL)
- Reverse-Z disabled (only effective with [0,1] range)
- `Depth_range::negative_one_to_one` passed to all projection math
- Depth comparison operators use forward direction

When `use_clip_control` is true:
- `glClipControl(lower_left, zero_to_one)` sets [0,1] range
- Reverse-Z with swapped near/far for better precision

`reverse_depth` is clamped at preset application: `preset.reverse_depth && use_clip_control`. All depth state is propagated via `rebuild_depth_state()` methods.

## Texture View Fallback

Without `glTextureView` (GL 4.3):
- Post-processing (bloom) is disabled (requires mip level views)
- Thumbnails use `sampler2DArray` + layer index instead of per-layer `texture_2d` views
- ImGui renderer supports both `sampler2D` and `sampler2DArray` via `array_layer` field in `Erhe_ImTextureID`
- `Texture_heap` reserves slot 0 for the array texture; fragment shader branches on `v_array_layer` sentinel value

## Debug Renderer

Without compute shaders, the debug renderer writes line vertices directly to a vertex buffer and draws as `GL_LINES` (1-pixel wide). The compute shader pipeline (CPU → SSBO → compute expand → triangles) is only created when `use_compute_shader` is true.

## GLSL 4.10 Compatibility

- `layout(binding = N)` is not emitted for GLSL < 4.20 (uniform blocks) or < 4.30 (samplers)
- After program link: `glUniformBlockBinding()` sets block bindings, `glUniform1i()` sets sampler texture units
- Sampler arrays use `glUniform1iv` to initialize all elements (prevents uninitialised elements defaulting to unit 0)
- Qualifier ordering: `flat out` (not `out flat`)

## GL Debug Output

`glDebugMessageCallback`, `glObjectLabel`, `glPushDebugGroup`/`glPopDebugGroup` require GL 4.3. When unavailable:
- `Scoped_debug_group` becomes a no-op
- Object labels are not set
- Debug callback is not installed
- `glGetProgramResourceiv` SSBO introspection is skipped

## DSA Function Reference

### Buffer DSA

| DSA function | Non-DSA equivalent | Bind target |
|---|---|---|
| `named_buffer_storage` | `buffer_data` | `copy_write_buffer` |
| `named_buffer_sub_data` | `buffer_sub_data` | `copy_write_buffer` |
| `map_named_buffer_range` | `map_buffer_range` | `copy_write_buffer` |
| `unmap_named_buffer` | `unmap_buffer` | `copy_write_buffer` |
| `get_named_buffer_parameter_iv` | `get_buffer_parameter_iv` | `copy_write_buffer` |
| `flush_mapped_named_buffer_range` | `flush_mapped_buffer_range` | `copy_write_buffer` |
| `copy_named_buffer_sub_data` | `copy_buffer_sub_data` | `copy_read` + `copy_write` |
| `invalidate_buffer_sub_data` | No-op | — |

### Texture DSA

| DSA function | Non-DSA equivalent | Bind via |
|---|---|---|
| `texture_storage_1d/2d/3d` | `tex_storage_1d/2d/3d` | scratch unit |
| `texture_storage_2d_multisample` | `tex_storage_2d_multisample` | scratch unit |
| `texture_sub_image_1d/2d/3d` | `tex_sub_image_1d/2d/3d` | scratch unit |
| `texture_parameter_i` | `tex_parameter_i` | scratch unit |
| `texture_buffer` | `tex_buffer` | scratch unit |
| `generate_texture_mipmap` | `generate_mipmap` | scratch unit |
| `bind_texture_unit` | `active_texture` + `bind_texture` | — |
| `bind_textures` | Loop of individual binds | — |
| `bind_samplers` | Loop of individual binds | — |

### Framebuffer DSA

| DSA function | Non-DSA equivalent | Bind via |
|---|---|---|
| `named_framebuffer_texture` | `framebuffer_texture` | push draw FB |
| `named_framebuffer_texture_layer` | `framebuffer_texture_layer` | push draw FB |
| `named_framebuffer_draw_buffers` | `draw_buffers` | push draw FB |
| `named_framebuffer_read_buffer` | `read_buffer` | push read FB |
| `check_named_framebuffer_status` | `check_framebuffer_status` | push FB |
| `blit_named_framebuffer` | `blit_framebuffer` | bind read + draw |
| `clear_named_framebuffer_*` | `clear_buffer_*` | bind draw FB |

### Vertex Array DSA

| DSA function | Non-DSA equivalent |
|---|---|
| `vertex_array_attrib_format` | deferred to `vertex_attrib_pointer` |
| `vertex_array_attrib_i_format` | deferred to `vertex_attrib_i_pointer` |
| `vertex_array_attrib_binding` | implicit in `vertex_attrib_pointer` |
| `vertex_array_binding_divisor` | `vertex_attrib_divisor` |
| `vertex_array_element_buffer` | `bind_buffer(element_array_buffer)` |
| `vertex_array_vertex_buffer` | `bind_buffer(array_buffer)` + `vertex_attrib_pointer` |

## Key Files

- `gl_device.cpp` — capability detection, factory methods, forced version re-checks
- `gl_binding_state.cpp/hpp` — binding state shadow tracker with push/pop RAII guards
- `gl_buffer.cpp` — buffer DSA emulation, persistent buffer fallback
- `gl_texture.cpp` — texture DSA emulation, buffer textures
- `gl_render_pass.cpp` — framebuffer DSA emulation
- `gl_vertex_input_state.cpp` — vertex array DSA emulation
- `gl_state_tracker.cpp` — VAO buffer binding via vertex_attrib_pointer
- `gl_texture_heap.cpp` — texture/sampler binding, reserved slots
- `gl_blit_command_encoder.cpp` — texture upload, framebuffer blit
- `gl_render_command_encoder.cpp` — draw call fallbacks
- `gl_shader_stages_prototype.cpp` — post-link binding, sampler array init
- `shader_resource.cpp` — SSBO/UBO block generation
- `text_renderer.cpp` — buffer texture fallback
- `imgui_renderer.cpp` — sampler2DArray support
