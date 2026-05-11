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

CMake option: `ERHE_GRAPHICS_API=metal`, which defines `ERHE_GRAPHICS_API_METAL`. The metal-cpp headers are fetched via CPM from `bkaradzic/metal-cpp`.

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

Compiled SPIR-V binaries are cached on disk under `<exe>/spirv_cache/`,
keyed by a hash of the final GLSL source (after preamble injection and
define expansion). Subsequent runs read from the cache instead of
re-invoking glslang.

Key SPIRV-Cross options:
- MSL version 3.0 (Metal 3 / `GPUFamilyMetal3` is required at runtime)
- `argument_buffers = true`, Tier 2
- `force_active_argument_buffer_resources = true`
- Descriptor set 0: discrete (UBOs/SSBOs as individual `[[buffer(N)]]`,
  plus dedicated samplers as `[[texture(N)]]`/`[[sampler(N)]]`)
- Descriptor set 7: argument buffer for texture-heap sampled images

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

### Two Sampler Binding Paths: Texture Heap vs. Dedicated Bindings

Sampled images take one of two completely different routes through the
Metal backend, distinguished by the `is_texture_heap` flag on each
`Bind_group_layout_binding` entry of type `combined_image_sampler`. The
caller sets the flag when declaring the binding; `Bind_group_layout_impl`
mirrors every binding into a private `Shader_resource` of type `samplers`
(the synthesized "default uniform block"), and the Metal shader-stages
prototype reads that block to classify each sampled image it finds in the
SPIR-V binary.

**Path A -- Texture-heap samplers** (`is_texture_heap = true`).

These are bindless / argument-buffer samplers like `s_textures[64]` for
material texture arrays, `s_texture_array` for the imgui sampler2DArray, or
the implicit `s_texture` array that `gl_bind_group_layout.cpp` /
`metal_bind_group_layout.cpp` append when a caller does not declare one
explicitly. They live in descriptor set 7 (an argument buffer at
`[[buffer(14)]]`) with deterministic `[[id(N)]]` values, and are populated
via `Texture_heap::allocate(texture, sampler)` + `Texture_heap::bind()`.

Layout formula (the texture heap and the shader emitter compute the same
result from the matching sampler declarations in the layout's default
uniform block, sorted by GLSL binding):

```
For each is_texture_heap sampler with array_size S:
  texture_id = running_offset
  sampler_id = running_offset + S
  running_offset += 2 * S
```

The classification is **explicit**, not derived from `array_size > 1` --
that historical heuristic mis-classified scalar dedicated samplers and
arrays of size 1.

**Path B -- Dedicated samplers** (`is_texture_heap = false`).

These are named samplers like `s_shadow_compare`, `s_input`, `s_depth`, or
`s_tex0/1/2`. They stay in discrete set 0 with explicit
`[[texture(N)]]/[[sampler(N)]]` indices. The host side binds them via:

```cpp
encoder.set_sampled_image(/*binding_point=*/0, *texture, *sampler);
```

The Metal implementation translates `binding_point` to the per-stage Metal
slot via `binding_point + bind_group_layout->get_sampler_binding_offset()`
(which mirrors how SPIRV-Cross numbers them after the buffer bindings) and
calls `setFragmentTexture` / `setFragmentSamplerState`.

`set_sampled_image` reads the `Sampler_aspect` (`color` / `depth` /
`stencil`) from the matching `combined_image_sampler` entry in the active
`Bind_group_layout`. The aspect is not strictly needed on Metal (the
texture object encodes the format), but it has to match what the bind
group layout declared so the cross-backend path matches Vulkan.

Renderers using the dedicated-sampler path: `Light_buffer` (shadow maps),
`post_processing` (`s_input`/`s_downsample`/`s_upsample`), `text_renderer`
(`s_texture`, optional `s_vertex_data`), `imgui_renderer` (`s_texture_array`),
the rendering_test depth_visualize cell (`s_depth`), the rendering_test
sep_tex cell (`s_tex0/1/2`).

### Texture Heap

The texture heap manages Path A samplers through Metal argument buffers.

- Constructor takes a `Bind_group_layout*` and reads
  `bind_group_layout->get_default_uniform_block()` to discover the
  texture-heap samplers. Iterates only the members with
  `get_is_texture_heap() == true` when computing argument buffer slot
  layout. Dedicated samplers are skipped -- they are bound via
  `set_sampled_image` instead.
- Argument buffer uses ring buffer allocation (fresh region per encode, no
  aliasing across render passes).
- `allocate(texture, sampler)` returns a material index into the
  texture-heap sampler's array; the shader reads this index from the
  material UBO and samples via `s_textures[index]` / equivalent.
- `bind()` encodes if dirty, then calls `setFragmentBuffer` +
  `useResource(Read | Sample)`.
- No pipeline change detection or re-encoding -- layout is fixed at
  construction.
- Material textures occupy `[0, used_slot_count)` in the argument buffer.
  There are no "reserved slots" for named samplers anymore -- dedicated
  samplers (shadow, post-process inputs, etc.) live in discrete set 0 and
  never consume argument-buffer slots.

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

Metal `DepthStencilDescriptor` configures front/back face stencil operations,
compare functions, read/write masks. Stencil reference values set on the
render command encoder via `setStencilFrontReferenceValue` /
`setStencilBackReferenceValue`.

`Device::choose_depth_stencil_format()` honours
`format_flag_require_stencil` and returns `format_d32_sfloat_s8_uint` when
the caller asks for a packed depth+stencil format. The
`Render_pipeline_create_info::set_format_from_render_pass()` swapchain
branch picks up the stencil format from `desc.stencil_attachment` when set
(this used to silently leave it `Invalid`, which Metal pipeline validation
flagged as a stencil format mismatch).

### MSAA Depth/Stencil Resolve

Metal supports resolving the depth and stencil aspects of an MSAA texture
into a single-sample texture via `MTLMultisampleDepthResolveFilter` /
`MTLMultisampleStencilResolveFilter`. The `erhe::graphics::Resolve_mode`
enum (`sample_zero` / `average` / `min` / `max`) maps to these filters.
The rendering_test MSAA depth-resolve cell exercises the path on both
Vulkan and Metal.

### Swapchain

Metal swapchain provides only a color texture (from CAMetalLayer's nextDrawable). The depth-stencil texture is app-managed, created alongside the swapchain and recreated on resize.

### Multi-Draw Indirect

Metal does not support `glMultiDrawElementsIndirect`. The implementation loops over individual `drawIndexedPrimitives` calls, reading draw commands from the indirect buffer on the CPU side. Push constant index 15 carries the draw ID for vertex/compute shaders; fragment shaders receive draw ID via an interpolated varying (Metal does not support push constants in fragment).

### Deferred GPU Resource Destruction

Metal resources (buffers, textures, samplers, render pipelines, depth-stencil
states, argument encoders, etc.) may still be referenced by command buffers
that have not finished executing on the GPU. The Metal backend uses the
same `add_completion_handler()` pattern as the Vulkan backend: destructors
capture handles by value into a lambda and defer release until the device
shuts down. This eliminated a class of intermittent crashes on shutdown
when destroying resources still referenced by in-flight command buffers.
