# Metal Graphics Backend

> This Metal backend was substantially created by Claude (Anthropic),
> with architectural guidance and review from Timo Suoranta.

## Overview

The Metal backend implements the `erhe::graphics` abstraction layer using Apple's Metal API via the `metal-cpp` C++ wrapper. It provides feature parity with the OpenGL backend for core rendering on macOS.

### Build Configuration

Select Metal backend via configure script:
```bash
scripts/configure_xcode_metal.sh
cmake --build build_xcode_metal --target editor --config Debug
```

CMake option: `ERHE_GRAPHICS_LIBRARY=metal`, which defines `ERHE_GRAPHICS_LIBRARY_METAL`. The metal-cpp headers are fetched via CPM from `bkaradzic/metal-cpp`.

## Architecture

### File Structure

All Metal-specific code lives in `src/erhe/graphics/erhe_graphics/metal/`:

| File | Purpose |
|------|---------|
| `metal_device.cpp/hpp` | MTLDevice creation, format properties, device info |
| `metal_buffer.cpp/hpp` | MTLBuffer with StorageModeShared (persistent mapping) |
| `metal_texture.cpp/hpp` | MTLTexture creation, texture views for array slices |
| `metal_sampler.cpp/hpp` | MTLSamplerState with argument buffer support |
| `metal_render_pass.cpp/hpp` | MTLRenderPassDescriptor, encoder lifecycle, static binding map |
| `metal_render_command_encoder.cpp/hpp` | Pipeline state, depth/stencil, draw calls, multi-draw-indirect |
| `metal_compute_command_encoder.cpp/hpp` | Compute dispatch for wide line rendering |
| `metal_blit_command_encoder.cpp/hpp` | Buffer-to-texture copies |
| `metal_command_encoder.cpp/hpp` | UBO/SSBO buffer binding with identity index mapping |
| `metal_shader_stages.hpp` | Binding map types (UBO/SSBO Metal index lookups) |
| `metal_shader_stages_prototype.cpp` | GLSL-to-SPIRV-to-MSL compilation, explicit resource bindings |
| `metal_texture_heap.cpp/hpp` | Argument buffer for texture arrays (sampler heap) |
| `metal_vertex_input_state.cpp/hpp` | Vertex descriptor setup |
| `metal_helpers.cpp/hpp` | Pixel format and enum conversion utilities |
| `metal_debug.cpp` | Scoped debug groups (pushDebugGroup/popDebugGroup) |
| `metal_gpu_timer.cpp/hpp` | GPU timing queries |
| `metal_surface.hpp/mm` | CAMetalLayer integration with SDL |
| `metal_swapchain.cpp/hpp` | Swapchain with app-managed depth texture |
| `metal_implementation.mm` | metal-cpp implementation defines |
| `metal_error_handler.mm` | Objective-C error handler registration |

### Shader Compilation Pipeline

GLSL source -> glslang (GLSL to SPIR-V) -> SPIRV-Cross (SPIR-V to MSL)

Key SPIRV-Cross options:
- `argument_buffers = true`, Tier 2
- `force_active_argument_buffer_resources = true`
- Descriptor set 0: discrete (UBOs/SSBOs as individual `[[buffer(N)]]`)
- Descriptor set 7: argument buffer for sampled images

### Explicit Metal Buffer Index Layout

All Metal buffer indices are explicitly assigned during SPIRV-Cross compilation via `add_msl_resource_binding()`, eliminating runtime remapping:

```
Index 0-7:   UBOs/SSBOs (= GLSL binding number, identity mapping)
Index 8-13:  (available)
Index 14:    Texture argument buffer
Index 15:    Push constant (ERHE_DRAW_ID, vertex/compute only)
Index 16+:   Vertex attributes (vertex_buffer_index_offset)
```

Push constant declaration is only emitted for vertex and compute shaders. Fragment shaders receive draw ID via interpolated varying.

### Deterministic Argument Buffer Layout

Sampled images get deterministic `[[id(N)]]` values within the argument buffer, assigned only for fragment/compute stages. Vertex shaders keep sampled images in discrete set 0 (optimized away since unused).

Formula (sorted by GLSL binding):
```
For each sampler with array_size S:
  texture_id = running_offset
  sampler_id = running_offset + S
  running_offset += 2 * S
```

The texture heap constructor computes the same layout from `default_uniform_block` sampler declarations, creating the argument encoder at construction time via `MTL::Device::newArgumentEncoder()` with explicit `MTL::ArgumentDescriptor` objects. No per-pipeline re-creation or rebinding needed.

### Texture Heap

The texture heap manages bindless-like texture access through Metal argument buffers.

- Constructor takes `const Shader_resource* default_uniform_block` to derive the argument buffer layout
- Argument buffer uses ring buffer allocation (fresh region per encode, no aliasing across render passes)
- `bind()` encodes if dirty, then calls `setFragmentBuffer` + `useResource(Read | Sample)`
- No pipeline change detection or re-encoding -- layout is fixed at construction
- Reserved slots for individual samplers (e.g., shadow textures)
- Dynamic slots for material texture arrays

### Coordinate Conventions

| Property | Metal | OpenGL (with clip control) | Vulkan |
|----------|-------|---------------------------|--------|
| NDC Y direction | Up | Up | Down |
| NDC depth range | [0, 1] | [0, 1] | [0, 1] |
| Framebuffer origin | Top-left | Bottom-left | Top-left |
| Texture origin | Top-left | Bottom-left | Top-left |
| Viewport Y transform | `(1 - y_ndc) * h/2 + oy` | `(y_ndc + 1) * h/2 + oy` | `(y_ndc + 1) * h/2 + oy` |
| Clip space Y flip needed | No | No | Depends |

Metal's viewport transform uses `window_y = (1 - y_ndc) * height/2 + originY`, which maps positive NDC Y to the top of the screen despite the top-left framebuffer origin. This means **no projection Y-flip is needed** for Metal -- the standard OpenGL-convention projection matrix works directly.

The `erhe::math::Coordinate_conventions` class bundles all convention parameters and is passed through the rendering pipeline as a single value:

```cpp
class Coordinate_conventions
{
public:
    Depth_range        native_depth_range;  // [0,1] or [-1,1]
    Framebuffer_origin framebuffer_origin;  // top-left or bottom-left
    Texture_origin     texture_origin;      // top-left or bottom-left
    Clip_space_y_flip  clip_space_y_flip;   // disabled for OpenGL and Metal
};
```

`Clip_space_y_flip` replaces the previous `Ndc_y_direction` enum. It directly answers the question "does the projection need to negate Y?" rather than requiring the application to derive this from framebuffer origin and NDC direction. For Metal, it is `disabled` because the viewport transform handles the Y mapping.

Convention-dependent adjustments in the codebase:

- **Projection matrix**: negates Y column when `clip_space_y_flip == enabled` (currently no backend needs this)
- **Winding order**: `Rasterization_state::with_winding_flip_if()` compensates for the winding reversal caused by projection Y-flip
- **Screen-space math**: `unproject()` and `project_to_screen_space()` negate the Y scale term for `Framebuffer_origin::top_left` to match the viewport transform inverse
- **Mouse input**: `get_viewport_from_window()` flips Y only for `bottom_left` framebuffer origin
- **Shadow maps**: `texture_from_clip` matrix accounts for framebuffer origin
- **Text renderer**: applies `y_scale` based on framebuffer origin for glyph orientation
- **Wide line compute shaders**: `vp_y_sign` uniform adjusts screen-space coordinates for fragment shader `gl_FragCoord` matching
- **ImGui renderer**: `get_rtt_uv0()`/`get_rtt_uv1()` flip UVs for render-to-texture based on texture origin

### Texture Clear

Metal's `clear_texture` for color textures uses `MTL::Texture::replaceRegion()` (shared/managed storage) or a staging buffer blit via `MTL::BlitCommandEncoder` (private storage). This avoids requiring `MTLTextureUsageRenderTarget` on textures that are only sampled. Depth/stencil clears use a render pass with `MTLLoadActionClear`.

### Stencil Support

Metal DepthStencilDescriptor configures front/back face stencil operations, compare functions, read/write masks. Stencil reference values set on the render command encoder.

### Swapchain

Metal swapchain provides only a color texture (from CAMetalLayer's nextDrawable). The depth-stencil texture is app-managed, created alongside the swapchain and recreated on resize.

### Multi-Draw Indirect

Metal does not support `glMultiDrawElementsIndirect`. The implementation loops over individual `drawIndexedPrimitives` calls, reading draw commands from the indirect buffer on the CPU side.
