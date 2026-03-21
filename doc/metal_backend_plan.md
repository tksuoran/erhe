# Metal Backend Plan for erhe::graphics

## 1. Overview

### Goals

Add Apple Metal as a third graphics backend for `erhe::graphics`, alongside the existing OpenGL and work-in-progress Vulkan backends. This will enable erhe and the editor application to run natively on Apple platforms using the Metal API through the `metal-cpp` C++ wrapper.

### Scope

- macOS (desktop) support as the primary target
- iOS/iPadOS as a secondary target (architecture supports it, but editor UI may need adaptation)
- Feature parity with the OpenGL backend for core rendering: buffers, textures, samplers, shaders, render passes, pipeline state, draw calls, compute dispatch, ring buffers, GPU timers
- GLSL-to-MSL shader translation via SPIRV-Cross

### Non-Goals (Out of Scope for Initial Effort)

- VR/XR support on Apple platforms (Apple Vision Pro uses a different SDK)
- Sparse textures (Metal has limited sparse texture support)
- Bindless textures in the OpenGL sense (Metal uses argument buffers instead)
- Feature parity with every OpenGL extension erhe can optionally use


## 2. metal-cpp Integration

### Source Placement

Fork/copy metal-cpp from `https://github.com/bkaradzic/metal-cpp` into the erhe source tree. Two options:

**Option A (Recommended): External dependency via CPM**
```cmake
CPMAddPackage(
    NAME              metal-cpp
    GITHUB_REPOSITORY bkaradzic/metal-cpp
    GIT_TAG           <stable-tag>
    GIT_SHALLOW       TRUE
)
```
This follows the existing pattern used for Vulkan-Headers, VulkanMemoryAllocator, volk, and other dependencies. The `metal-cpp` headers would be available as an interface library target.

**Option B: Vendored copy**
Place header files under `src/erhe/graphics/metal-cpp/` or `external/metal-cpp/`. This avoids network dependency but requires manual updates. Acceptable since metal-cpp is header-only and relatively stable.

Recommendation: **Option A** for consistency with existing CPM-based dependency management. If CPM proves problematic for this dependency (e.g., no stable tags), fall back to Option B.

### Header-Only Nature

metal-cpp is a header-only C++ wrapper around Objective-C Metal framework calls. It uses a single implementation define pattern:
```cpp
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
```

A single `.cpp` file in the Metal backend directory will define these macros and include the implementation headers. All other files include the headers without the macros.

### CMake Setup

Add `metal` as a third option for `ERHE_GRAPHICS_LIBRARY` in the root `CMakeLists.txt`:

```cmake
set_option(ERHE_GRAPHICS_LIBRARY
    "Graphics library to use with erhe. Either opengl, vulkan, or metal"
    "opengl"
    "opengl;vulkan;metal"
)
```

In the graphics library selection block:
```cmake
if (${ERHE_GRAPHICS_LIBRARY} STREQUAL "opengl")
    message(STATUS "Erhe configured to use OpenGL as graphics API.")
    set(ERHE_GRAPHICS_API_OPENGL TRUE)
    add_definitions(-DERHE_GRAPHICS_LIBRARY_OPENGL)
elseif (${ERHE_GRAPHICS_LIBRARY} STREQUAL "vulkan")
    message(STATUS "Erhe configured to use Vulkan as graphics API.")
    set(ERHE_GRAPHICS_API_VULKAN TRUE)
    add_definitions(-DERHE_GRAPHICS_LIBRARY_VULKAN)
    # ... existing Vulkan CPM packages ...
elseif (${ERHE_GRAPHICS_LIBRARY} STREQUAL "metal")
    message(STATUS "Erhe configured to use Metal as graphics API.")
    set(ERHE_GRAPHICS_API_METAL TRUE)
    add_definitions(-DERHE_GRAPHICS_LIBRARY_METAL)
    # Fetch metal-cpp via CPM
    CPMAddPackage(
        NAME              metal-cpp
        GITHUB_REPOSITORY bkaradzic/metal-cpp
        GIT_TAG           <stable-tag>
        GIT_SHALLOW       TRUE
    )
endif ()
```

The define `ERHE_GRAPHICS_LIBRARY_METAL` follows the existing pattern of `ERHE_GRAPHICS_LIBRARY_OPENGL` and `ERHE_GRAPHICS_LIBRARY_VULKAN`. The CMake variable `ERHE_GRAPHICS_API_METAL` follows the same pattern as `ERHE_GRAPHICS_API_OPENGL` and `ERHE_GRAPHICS_API_VULKAN`.

### Platform Guard

Metal is only available on Apple platforms, so the `metal` option should be guarded:
```cmake
if (${ERHE_GRAPHICS_LIBRARY} STREQUAL "metal")
    if (NOT APPLE)
        message(FATAL_ERROR "Metal backend is only available on Apple platforms (macOS, iOS)")
    endif()
    # ...
endif ()
```


## 3. Abstraction Mapping

erhe::graphics already uses a Vulkan-like abstraction with pimpl pattern. Each public type (e.g., `Buffer`, `Texture`, `Sampler`) has a forward-declared `*_impl` class. The OpenGL implementations live in `gl/` and Vulkan implementations in `vulkan/`. The Metal implementations will live in `metal/`.

The following table shows how each erhe abstraction maps to Metal concepts:

### 3.1 Device and Surface

| erhe Type | Metal Equivalent | Notes |
|-----------|-----------------|-------|
| `Device` / `Device_impl` | `MTL::Device` | Central device object. `MTL::CreateSystemDefaultDevice()` on macOS. On iOS, same function. |
| `Device_info` | `MTL::Device` feature queries | Metal uses feature set / GPU family queries rather than extension strings. Map to `supportsFamily()`, `maxBufferLength`, `maxThreadsPerThreadgroup`, etc. |
| `Surface` / `Surface_impl` | `CA::MetalLayer` | Core Animation layer that vends drawable textures. Attached to the SDL/GLFW window's native view. |
| `Swapchain` / `Swapchain_impl` | `CA::MetalLayer` + `MTL::Drawable` | Metal's "swapchain" is the layer. `nextDrawable()` provides the current frame's texture. The `Swapchain_impl` manages the layer, presents drawables, and handles resize. |

### 3.2 Buffers

| erhe Type | Metal Equivalent | Notes |
|-----------|-----------------|-------|
| `Buffer` / `Buffer_impl` | `MTL::Buffer` | Created via `device->newBuffer(length, options)`. Metal buffers are simpler than GL/VK -- no explicit bind targets. |
| `Buffer_create_info` | `MTL::ResourceOptions` | Map `Memory_usage` to `MTL::ResourceStorageModeShared` (CPU+GPU), `MTL::ResourceStorageModePrivate` (GPU-only), `MTL::ResourceStorageModeMemoryless` (iOS tile memory). |
| `Buffer_allocator` | No Metal equivalent | erhe-level bump allocator, works unchanged on top of `Buffer`. |
| `Ring_buffer` / `Ring_buffer_impl` | `MTL::Buffer` + manual fencing | Metal has explicit command buffer completion handlers (`addCompletedHandler`) for fence-like synchronization. Use these instead of GL sync objects or VK fences. |

**Buffer mapping:** Metal buffers with `StorageModeShared` are always mapped (CPU-visible). `buffer->contents()` returns the pointer. No explicit map/unmap needed for shared storage. For `StorageModeManaged` (macOS only), call `didModifyRange()` after CPU writes.

### 3.3 Textures

| erhe Type | Metal Equivalent | Notes |
|-----------|-----------------|-------|
| `Texture` / `Texture_impl` | `MTL::Texture` | Created via `MTL::TextureDescriptor` + `device->newTexture(descriptor)`. |
| `Texture_create_info` | `MTL::TextureDescriptor` | Map `Texture_type` to `MTL::TextureType` (1D, 2D, 3D, Cube, 2DArray, etc.). Map `erhe::dataformat::Format` to `MTL::PixelFormat`. |
| `Texture_heap` / `Texture_heap_impl` | `MTL::ArgumentBuffer` or direct binding | Metal uses argument buffers for descriptor-indexing style access (analogous to bindless textures). Alternatively, bind textures directly to shader stages. |

**Pixel format mapping:** A format conversion table from `erhe::dataformat::Format` to `MTL::PixelFormat` is needed. Most standard formats (RGBA8, RGBA16F, Depth32F, etc.) have direct Metal equivalents. Some GL-specific formats (e.g., certain compressed formats) may differ.

### 3.4 Samplers

| erhe Type | Metal Equivalent | Notes |
|-----------|-----------------|-------|
| `Sampler` / `Sampler_impl` | `MTL::SamplerState` | Created via `MTL::SamplerDescriptor` + `device->newSamplerState(descriptor)`. |
| `Sampler_create_info` | `MTL::SamplerDescriptor` | Direct mapping: `Filter` -> `MTL::SamplerMinMagFilter`, `Sampler_address_mode` -> `MTL::SamplerAddressMode`, `Compare_operation` -> `MTL::CompareFunction`. |

### 3.5 Shaders and Pipeline State

| erhe Type | Metal Equivalent | Notes |
|-----------|-----------------|-------|
| `Shader_stages` / `Shader_stages_impl` | `MTL::Library` + `MTL::Function` | A Metal library contains compiled MSL functions. Vertex and fragment functions are extracted by name. |
| `Shader_stages_prototype` / `Shader_stages_prototype_impl` | SPIRV-Cross compilation step | Compile GLSL -> SPIR-V (via glslang, already supported) -> MSL (via SPIRV-Cross). Alternatively, load pre-compiled `.metallib` files. |
| `Shader_stages_create_info` | Build pipeline: source -> SPIR-V -> MSL -> `MTL::Library` | The existing `Shader_resource` system generates GLSL interface declarations. For Metal, we translate the final GLSL to MSL via the SPIR-V path. |
| `Render_pipeline_state` | `MTL::RenderPipelineState` | Created via `MTL::RenderPipelineDescriptor`. Combines vertex/fragment functions, vertex descriptor, color attachment formats, depth/stencil attachment format, blend states, multisample state. |
| `Compute_pipeline_state` | `MTL::ComputePipelineState` | Created from a compute kernel `MTL::Function`. |

### 3.6 Render Passes

| erhe Type | Metal Equivalent | Notes |
|-----------|-----------------|-------|
| `Render_pass` / `Render_pass_impl` | `MTL::RenderPassDescriptor` | Describes color/depth/stencil attachments with load/store actions. erhe's `Load_action` and `Store_action` enums map directly to `MTL::LoadAction` and `MTL::StoreAction`. |
| `Scoped_render_pass` | `MTL::RenderCommandEncoder` lifetime | Begin render pass creates the encoder; end render pass calls `endEncoding()`. |
| `Render_pass_attachment_descriptor` | `MTL::RenderPassColorAttachmentDescriptor` / `MTL::RenderPassDepthAttachmentDescriptor` | Direct mapping of texture, level, layer, load/store actions, clear values, resolve texture. |

### 3.7 Command Encoders

| erhe Type | Metal Equivalent | Notes |
|-----------|-----------------|-------|
| `Command_encoder` | Base concept for Metal encoders | |
| `Render_command_encoder` / `Render_command_encoder_impl` | `MTL::RenderCommandEncoder` | Set pipeline state, set vertex/fragment buffers, set viewport/scissor, draw primitives. |
| `Compute_command_encoder` / `Compute_command_encoder_impl` | `MTL::ComputeCommandEncoder` | Set compute pipeline, set buffers, dispatch threads. |
| `Blit_command_encoder` / `Blit_command_encoder_impl` | `MTL::BlitCommandEncoder` | Copy buffers, copy textures, generate mipmaps, fill buffers. erhe's `Blit_command_encoder` API was clearly designed with Metal in mind (the commented-out methods in `blit_command_encoder.hpp` reference `MTL::Region` and Metal-specific operations). |

### 3.8 Other Types

| erhe Type | Metal Equivalent | Notes |
|-----------|-----------------|-------|
| `Gpu_timer` / `Gpu_timer_impl` | `MTL::CounterSampleBuffer` or command buffer timestamps | Metal supports GPU timestamp queries via counter sample buffers. Alternatively, use `addCompletedHandler` with `GPUStartTime`/`GPUEndTime` on the command buffer. |
| `Vertex_input_state` / `Vertex_input_state_impl` | `MTL::VertexDescriptor` | Part of the render pipeline descriptor. Maps vertex attributes (format, offset, buffer index) and buffer layouts (stride, step function). |
| `Scoped_debug_group` | `pushDebugGroup()` / `popDebugGroup()` on command encoder | Metal supports debug groups on command encoders and command queues. |
| `Draw_indexed_primitives_indirect_command` | `MTL::DrawIndexedPrimitivesIndirectArguments` | Layout matches (index_count, instance_count, first_index, base_vertex, base_instance). |
| `Shader_monitor` | No Metal equivalent needed | File watching and hot-reload logic is backend-independent. The reload triggers re-compilation via the SPIR-V -> MSL path. |
| `Shader_resource` | No Metal equivalent | This is GLSL source generation code. For Metal, it drives GLSL source that is then cross-compiled to MSL. It works unchanged. |
| `Fragment_outputs` | Color attachment configuration in `MTL::RenderPipelineDescriptor` | The GLSL output declarations are consumed by cross-compilation. |


## 4. Shader Strategy

### The Problem

erhe shaders are written in GLSL. The `Shader_resource` system programmatically generates GLSL interface declarations (uniform blocks, shader storage blocks, vertex attributes, samplers) from C++ code and injects them into shader source strings. Metal requires Metal Shading Language (MSL), not GLSL.

### Recommended Approach: GLSL -> SPIR-V -> MSL via SPIRV-Cross

erhe already has optional SPIR-V support (`ERHE_SPIRV` option) using glslang to compile GLSL to SPIR-V. The Metal backend extends this pipeline:

1. **GLSL source generation** -- unchanged. `Shader_resource` and `Shader_stages_create_info` produce complete GLSL source.
2. **GLSL -> SPIR-V** -- use glslang (already integrated via `Glslang_shader_stages`). Make `ERHE_SPIRV` mandatory when `ERHE_GRAPHICS_LIBRARY=metal`.
3. **SPIR-V -> MSL** -- add SPIRV-Cross as a dependency. It translates SPIR-V bytecode to MSL source with proper resource binding remapping.
4. **MSL -> MTL::Library** -- compile the MSL source at runtime via `device->newLibrary(source, options)`, or ahead-of-time via the `metal` command-line compiler to produce `.metallib` files.

### SPIRV-Cross Integration

Add SPIRV-Cross as a CPM dependency:
```cmake
if (${ERHE_GRAPHICS_LIBRARY} STREQUAL "metal")
    CPMAddPackage(
        NAME              SPIRV-Cross
        GITHUB_REPOSITORY KhronosGroup/SPIRV-Cross
        GIT_TAG           <stable-tag>
        GIT_SHALLOW       TRUE
    )
endif ()
```

The cross-compilation step runs in `Shader_stages_prototype_impl` (Metal variant) after glslang produces SPIR-V:
```
GLSL source (from Shader_resource)
    |
    v
glslang -> SPIR-V binary
    |
    v
SPIRV-Cross -> MSL source (with resource binding remapping)
    |
    v
MTL::Device::newLibrary() -> MTL::Library
    |
    v
MTL::Library::newFunction("vertex_main") -> MTL::Function
MTL::Library::newFunction("fragment_main") -> MTL::Function
```

### Resource Binding Remapping

SPIRV-Cross can remap GLSL binding points to Metal buffer/texture/sampler indices. The Metal backend's `Shader_stages_prototype_impl` must:
- Query SPIRV-Cross for the remapped resource bindings
- Store the mapping so that `Render_command_encoder_impl` can bind erhe buffers to the correct Metal buffer indices

### Shader Hot-Reload

`Shader_monitor` watches GLSL source files and triggers reload. For Metal, reload means re-running the full pipeline: GLSL -> SPIR-V -> MSL -> MTL::Library. This is slower than GL's shader recompile but acceptable for development.

### Alternative Approaches (Not Recommended)

- **Hand-written MSL shaders**: Would require maintaining two shader sets (GLSL + MSL) and would not work with `Shader_resource`'s GLSL generation.
- **Runtime MoltenVK translation**: Would bring in Vulkan overhead without benefit. Better to target Metal directly.
- **Pre-compiled .metallib files**: Could be used for release builds as an optimization, but runtime compilation is needed for development and hot-reload.


## 5. Implementation Order

### Phase 0: Prerequisites and Build Infrastructure
- Add `metal` option to `ERHE_GRAPHICS_LIBRARY` in root `CMakeLists.txt`
- Add `ERHE_GRAPHICS_LIBRARY_METAL` preprocessor define
- Set `ERHE_GRAPHICS_API_METAL` CMake variable
- Add metal-cpp and SPIRV-Cross as CPM dependencies
- Force `ERHE_SPIRV=ON` when Metal is selected
- Add `metal/` source directory to `src/erhe/graphics/CMakeLists.txt` (following the pattern of `gl/` and `vulkan/` blocks)
- Add Apple framework linkage (`-framework Metal -framework QuartzCore -framework Foundation`)
- Create stub `metal_device.hpp`/`.cpp` that compiles but does nothing
- Verify the build succeeds on macOS with `ERHE_GRAPHICS_LIBRARY=metal`
- Add CMake preset `Metal_Debug` and `Metal_Release`

### Phase 1: Device, Surface, and Swapchain
- Implement `Device_impl` (Metal): create `MTL::Device`, `MTL::CommandQueue`
- Implement `Surface_impl` (Metal): create `CA::MetalLayer`, attach to SDL window's native view
- Implement `Swapchain_impl` (Metal): `nextDrawable()`, `presentDrawable()`, resize handling
- Implement `Device_info` population from Metal device queries
- Implement `Scoped_debug_group` via Metal debug groups
- Goal: window opens, clears to a solid color, presents

### Phase 2: Buffers
- Implement `Buffer_impl` (Metal): create `MTL::Buffer`, map/unmap (trivial for shared storage)
- Implement buffer flush (`didModifyRange` for managed storage on macOS)
- Implement `Ring_buffer_impl` (Metal): fence-based sync via command buffer completion handlers
- Goal: buffers can be created, mapped, written to, and fenced

### Phase 3: Textures and Samplers
- Create `erhe::dataformat::Format` -> `MTL::PixelFormat` conversion table
- Implement `Texture_impl` (Metal): create textures from `MTL::TextureDescriptor`
- Implement `Sampler_impl` (Metal): create sampler states from `MTL::SamplerDescriptor`
- Implement texture upload via `replaceRegion` or blit encoder
- Implement `Texture_heap_impl` (Metal): argument buffer based or direct binding
- Goal: textures and samplers can be created and configured

### Phase 4: Shaders and Pipeline State
- Implement SPIR-V -> MSL translation using SPIRV-Cross in `Shader_stages_prototype_impl`
- Implement `Shader_stages_impl` (Metal): compile MSL -> `MTL::Library` -> `MTL::Function`
- Store SPIRV-Cross resource binding remapping data
- Implement `Vertex_input_state_impl` (Metal): build `MTL::VertexDescriptor`
- Implement `Render_pipeline_state` creation: build `MTL::RenderPipelineDescriptor` combining vertex function, fragment function, vertex descriptor, color/depth/stencil formats, blend state, multisample state
- Goal: shader programs compile and pipeline states are created

### Phase 5: Render Passes and Draw Calls
- Implement `Render_pass_impl` (Metal): build `MTL::RenderPassDescriptor` from erhe descriptors
- Implement `Render_command_encoder_impl` (Metal):
  - Create `MTL::RenderCommandEncoder` from command buffer + render pass descriptor
  - `set_render_pipeline_state` -> `setRenderPipelineState`
  - `set_buffer` -> `setVertexBuffer` / `setFragmentBuffer` (using SPIRV-Cross remapped indices)
  - `set_viewport_rect` -> `setViewport`
  - `set_scissor_rect` -> `setScissorRect`
  - `draw_primitives` -> `drawPrimitives`
  - `draw_indexed_primitives` -> `drawIndexedPrimitives`
  - `multi_draw_indexed_primitives_indirect` -> `drawIndexedPrimitives:indirectBuffer:`
  - Depth/stencil state -> `MTL::DepthStencilState` (created from `MTL::DepthStencilDescriptor`)
- Implement `Blit_command_encoder_impl` (Metal):
  - `copy_from_buffer` / `copy_from_texture` -> `MTL::BlitCommandEncoder` operations
  - `generate_mipmaps` -> `generateMipmaps`
  - `fill_buffer` -> `fillBuffer`
- Goal: triangles render to screen. Editor displays basic geometry.

### Phase 6: Compute
- Implement `Compute_command_encoder_impl` (Metal):
  - `set_compute_pipeline_state` -> `setComputePipelineState`
  - `set_buffer` -> `setBuffer`
  - `dispatch_compute` -> `dispatchThreadgroups`
- Goal: compute shaders work (used for line rendering in the editor)

### Phase 7: GPU Timer
- Implement `Gpu_timer_impl` (Metal) using `sampleCounters` on command buffer or `GPUStartTime`/`GPUEndTime`
- Goal: performance measurement works

### Phase 8: Polish and Integration
- Test with the editor application end-to-end
- Handle ImGui rendering (the `Imgui_renderer` uses erhe::graphics, so it should work once the backend is complete)
- Implement format property queries (`get_format_properties`, `get_supported_depth_stencil_formats`)
- Handle reverse depth (Metal clip space is [0, 1] like Vulkan, unlike OpenGL's [-1, 1])
- Wire up `Shader_monitor` for hot-reload through the SPIR-V -> MSL path
- Profile and optimize


## 6. Platform Considerations

### macOS Windowing

SDL already supports Metal via `SDL_WINDOW_METAL` and `SDL_Metal_GetLayer()`. The Metal `Surface_impl` should:
1. Create the SDL window with `SDL_WINDOW_METAL` flag
2. Retrieve the `CAMetalLayer` via `SDL_Metal_GetLayer()`
3. Configure the layer (pixel format, frame buffer only, drawable size)
4. Use the layer for swapchain operations

The `erhe::window` library already abstracts SDL windowing. The Metal backend in `erhe::graphics` should get the layer from the `Context_window` handle. A small addition to `erhe::window` may be needed to expose `SDL_Metal_GetLayer()` or the native view.

### iOS Specifics

- `StorageModeMemoryless` is available for transient render targets (depth/stencil that are only needed within a render pass)
- `StorageModeManaged` is NOT available on iOS -- only `Shared` and `Private`
- Maximum texture sizes differ (check `maxTextureSize` on the device)
- The editor UI may need adaptation for touch input (separate concern from the graphics backend)

### Coordinate System Differences

| Aspect | OpenGL | Vulkan | Metal |
|--------|--------|--------|-------|
| NDC Y direction | Up (+Y) | Down (-Y) | Up (+Y) |
| NDC depth range | [-1, 1] | [0, 1] | [0, 1] |
| Texture origin | Bottom-left | Top-left | Top-left |
| Front face (CCW/CW) | CCW by default | Configurable | Configurable |

Key actions:
- **Depth range**: erhe already supports reverse depth. Metal's [0, 1] depth range matches Vulkan. The depth clear value and compare function logic in `get_depth_clear_value_pointer()` and `get_depth_function()` should work correctly for Metal.
- **Texture origin**: Metal textures have origin at top-left (like Vulkan). Texture upload and UV coordinate conventions may need adjustment if erhe assumes bottom-left origin.
- **NDC Y**: Metal NDC Y points up (same as OpenGL), so no Y-flip is needed in the projection matrix (unlike Vulkan).

### Feature Parity Concerns

Features that exist in OpenGL/erhe but need special handling in Metal:

| Feature | OpenGL | Metal Equivalent |
|---------|--------|-----------------|
| Bindless textures | `GL_ARB_bindless_texture` | Argument buffers / `useResource` |
| Persistent mapped buffers | `GL_ARB_buffer_storage` + `GL_MAP_PERSISTENT_BIT` | `StorageModeShared` (always mapped) |
| Multi-draw indirect | `glMultiDrawElementsIndirect` | `drawIndexedPrimitives:indirectBuffer:` in a loop, or indirect command buffers (`MTL::IndirectCommandBuffer`) |
| Texture buffer | `GL_TEXTURE_BUFFER` | `MTL::Buffer` + `MTL::Texture` backed by buffer, or buffer in shader directly |
| Geometry shaders | `GL_GEOMETRY_SHADER` | Not supported in Metal. Must use compute shaders or mesh shaders (Apple Silicon GPU family 7+) |
| Integer polygon IDs | Used for GPU picking | Works fine with Metal render targets using integer formats |
| Sparse textures | `GL_ARB_sparse_texture` | Limited support via `MTL::SparseTexture` (macOS 11+, Apple Silicon) |
| Clear texture | `glClearTexImage` | Render pass with clear load action, or blit encoder fill |
| Compute shaders | `GL_COMPUTE_SHADER` | Fully supported via `MTL::ComputeCommandEncoder` |

### Geometry Shader Concern

erhe's editor uses geometry shaders for "fat triangle" rendering. Metal does not support geometry shaders. Options:
1. Move the geometry shader logic to a compute shader pre-pass
2. Use mesh shaders on Apple Silicon (GPU family 7+) where available
3. Disable fat triangle rendering on Metal and use an alternative approach
4. Expand triangles on the CPU

This is a known limitation that should be addressed case by case. Most editor rendering does not use geometry shaders.


## 7. File Structure

### New Files to Create

All Metal backend implementation files go in `src/erhe/graphics/erhe_graphics/metal/`:

```
src/erhe/graphics/erhe_graphics/metal/
    metal_implementation.cpp          -- #define NS/MTL/CA_PRIVATE_IMPLEMENTATION
    metal_device.hpp                  -- Device_impl (Metal)
    metal_device.cpp
    metal_surface.hpp                 -- Surface_impl (Metal)
    metal_surface.cpp
    metal_swapchain.hpp               -- Swapchain_impl (Metal)
    metal_swapchain.cpp
    metal_buffer.hpp                  -- Buffer_impl (Metal)
    metal_buffer.cpp
    metal_texture.hpp                 -- Texture_impl (Metal)
    metal_texture.cpp
    metal_texture_heap.hpp            -- Texture_heap_impl (Metal)
    metal_texture_heap.cpp
    metal_sampler.hpp                 -- Sampler_impl (Metal)
    metal_sampler.cpp
    metal_shader_stages.hpp           -- Shader_stages_impl, Shader_stages_prototype_impl (Metal)
    metal_shader_stages.cpp
    metal_shader_stages_prototype.cpp -- SPIR-V -> MSL compilation
    metal_render_pass.hpp             -- Render_pass_impl (Metal)
    metal_render_pass.cpp
    metal_command_encoder.hpp         -- Command_encoder_impl base (Metal)
    metal_command_encoder.cpp
    metal_render_command_encoder.hpp  -- Render_command_encoder_impl (Metal)
    metal_render_command_encoder.cpp
    metal_compute_command_encoder.hpp -- Compute_command_encoder_impl (Metal)
    metal_compute_command_encoder.cpp
    metal_blit_command_encoder.hpp    -- Blit_command_encoder_impl (Metal)
    metal_blit_command_encoder.cpp
    metal_vertex_input_state.hpp      -- Vertex_input_state_impl (Metal)
    metal_vertex_input_state.cpp
    metal_ring_buffer.hpp             -- Ring_buffer_impl (Metal)
    metal_ring_buffer.cpp
    metal_gpu_timer.hpp               -- Gpu_timer_impl (Metal)
    metal_gpu_timer.cpp
    metal_helpers.hpp                 -- Format conversion, enum mapping utilities
    metal_helpers.cpp
    metal_debug.hpp                   -- Debug group helpers
    metal_debug.cpp
    metal_spirv_cross.hpp             -- SPIR-V -> MSL translation wrapper
    metal_spirv_cross.cpp
```

### Naming Convention

Following the existing pattern:
- OpenGL: `gl/gl_*.hpp`, `gl/gl_*.cpp`
- Vulkan: `vulkan/vulkan_*.hpp`, `vulkan/vulkan_*.cpp`
- Metal: `metal/metal_*.hpp`, `metal/metal_*.cpp`


## 8. Build System Changes

### Root `CMakeLists.txt`

1. Add `"metal"` to the `ERHE_GRAPHICS_LIBRARY` option's valid values
2. Add the `elseif (metal)` block with:
   - `ERHE_GRAPHICS_API_METAL` variable
   - `ERHE_GRAPHICS_LIBRARY_METAL` preprocessor define
   - CPM packages: metal-cpp, SPIRV-Cross
   - Force `ERHE_SPIRV=ON`
   - Platform guard (APPLE only)

### `src/erhe/graphics/CMakeLists.txt`

Add a third conditional block after the Vulkan block:

```cmake
if (ERHE_GRAPHICS_API_METAL)
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_graphics/metal/metal_implementation.cpp
        erhe_graphics/metal/metal_device.cpp
        erhe_graphics/metal/metal_device.hpp
        erhe_graphics/metal/metal_surface.cpp
        erhe_graphics/metal/metal_surface.hpp
        erhe_graphics/metal/metal_swapchain.cpp
        erhe_graphics/metal/metal_swapchain.hpp
        erhe_graphics/metal/metal_buffer.cpp
        erhe_graphics/metal/metal_buffer.hpp
        erhe_graphics/metal/metal_texture.cpp
        erhe_graphics/metal/metal_texture.hpp
        erhe_graphics/metal/metal_texture_heap.cpp
        erhe_graphics/metal/metal_texture_heap.hpp
        erhe_graphics/metal/metal_sampler.cpp
        erhe_graphics/metal/metal_sampler.hpp
        erhe_graphics/metal/metal_shader_stages.cpp
        erhe_graphics/metal/metal_shader_stages.hpp
        erhe_graphics/metal/metal_shader_stages_prototype.cpp
        erhe_graphics/metal/metal_render_pass.cpp
        erhe_graphics/metal/metal_render_pass.hpp
        erhe_graphics/metal/metal_command_encoder.cpp
        erhe_graphics/metal/metal_command_encoder.hpp
        erhe_graphics/metal/metal_render_command_encoder.cpp
        erhe_graphics/metal/metal_render_command_encoder.hpp
        erhe_graphics/metal/metal_compute_command_encoder.cpp
        erhe_graphics/metal/metal_compute_command_encoder.hpp
        erhe_graphics/metal/metal_blit_command_encoder.cpp
        erhe_graphics/metal/metal_blit_command_encoder.hpp
        erhe_graphics/metal/metal_vertex_input_state.cpp
        erhe_graphics/metal/metal_vertex_input_state.hpp
        erhe_graphics/metal/metal_ring_buffer.cpp
        erhe_graphics/metal/metal_ring_buffer.hpp
        erhe_graphics/metal/metal_gpu_timer.cpp
        erhe_graphics/metal/metal_gpu_timer.hpp
        erhe_graphics/metal/metal_helpers.cpp
        erhe_graphics/metal/metal_helpers.hpp
        erhe_graphics/metal/metal_debug.cpp
        erhe_graphics/metal/metal_debug.hpp
        erhe_graphics/metal/metal_spirv_cross.cpp
        erhe_graphics/metal/metal_spirv_cross.hpp
    )

    target_link_libraries(${_target}
        PRIVATE
            metal-cpp
            spirv-cross-msl
            "-framework Metal"
            "-framework QuartzCore"
            "-framework Foundation"
    )
endif ()
```

Note: All source files are explicitly listed (no GLOB), following the project's CMake convention.

### `src/erhe/window/` Changes

The window library may need a minor addition to expose the Metal layer from SDL:
- A method on `Context_window` like `get_metal_layer()` or `get_native_surface()` that returns a `void*` to the `CAMetalLayer`
- SDL provides this via `SDL_Metal_GetLayer()` (SDL3) or `SDL_Metal_GetLayer(SDL_MetalView)` (SDL2)
- When `ERHE_GRAPHICS_LIBRARY_METAL` is defined, the SDL backend creates the window with `SDL_WINDOW_METAL`

### CMake Presets

Add Metal presets to `CMakePresets.json` (if it exists) or to the configure scripts:
```json
{
    "name": "Metal_Debug",
    "inherits": "base",
    "cacheVariables": {
        "ERHE_GRAPHICS_LIBRARY": "metal",
        "CMAKE_BUILD_TYPE": "Debug"
    },
    "condition": {
        "type": "equals",
        "lhs": "${hostSystemName}",
        "rhs": "Darwin"
    }
}
```

### Shared Code

Some existing files have `#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)` and `#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)` guards. These need to be extended with `#if defined(ERHE_GRAPHICS_LIBRARY_METAL)` in the following files:

- `erhe_graphics/device.hpp` -- `Device_info` has `#if defined()` blocks for GL/Vulkan version fields. Add Metal family/version fields.
- `erhe_graphics/shader_stages_create_info.cpp` -- The `final_source()` method generates GLSL with `#version` and GL-specific extensions. These are fine for Metal since we cross-compile from GLSL, but any OpenGL-specific code paths need Metal alternatives.
- Any file that uses `#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL) ... #elif defined(ERHE_GRAPHICS_LIBRARY_VULKAN)` needs a third `#elif defined(ERHE_GRAPHICS_LIBRARY_METAL)` branch.


## 9. Risks and Open Questions

### Risks

1. **Geometry shader dependency**: Any editor feature relying on geometry shaders (e.g., fat triangle visualization) will not work directly on Metal. Need to identify all geometry shader uses and plan alternatives (compute pre-pass or CPU expansion).

2. **SPIRV-Cross maturity for MSL**: SPIRV-Cross's MSL output is mature but may have edge cases with erhe's GLSL patterns (e.g., bindless texture handles via `uint64_t`, shader storage buffer layouts). Thorough testing of all shader permutations is needed.

3. **Multi-draw indirect**: Metal does not have a direct equivalent of `glMultiDrawElementsIndirect`. erhe uses this heavily for batch rendering. Options:
   - Loop over individual `drawIndexedPrimitives:indirectBuffer:` calls (simpler, potentially slower)
   - Use `MTL::IndirectCommandBuffer` (ICB) for GPU-driven rendering (more complex, potentially faster)
   - Emit draw calls from a compute shader into an ICB

4. **Texture buffer objects**: Metal does not have a direct `GL_TEXTURE_BUFFER` equivalent. Metal shaders can read from buffers directly using `device const` pointers, or a texture can be created backed by a buffer. The approach depends on how erhe uses texture buffers.

5. **Performance of runtime MSL compilation**: Compiling MSL from source at startup or during hot-reload is slower than loading pre-compiled `.metallib` files. For development this is acceptable; for release builds, consider ahead-of-time compilation.

6. **macOS deployment target**: metal-cpp requires macOS 11+ (Big Sur) minimum. Some Metal features (mesh shaders, ray tracing) require macOS 13+ and Apple Silicon.

7. **Objective-C interop**: metal-cpp wraps Objective-C APIs in C++. Some edge cases (memory management via `NS::AutoreleasePool`, `NS::String` conversions) need careful handling. The implementation file must be compiled as Objective-C++ (`.mm`) or use metal-cpp's C++ wrappers exclusively.

### Open Questions

1. **metal-cpp version/fork**: Should we use the bkaradzic fork specifically, or Apple's official metal-cpp (available at `https://developer.apple.com/metal/cpp/`)? Apple's official version may be more up to date but is distributed as a download, not a Git repository.

2. **Objective-C++ files**: Should Metal implementation files use `.mm` extension (Objective-C++) to allow mixing in native Cocoa/UIKit calls where needed? Or should everything go through metal-cpp's C++ wrappers? Using `.mm` would require CMake adjustments (`LANGUAGE OBJCXX` property).

3. **Release shader compilation**: Should we support ahead-of-time MSL compilation and `.metallib` loading for release builds? This would improve startup time but adds build complexity.

4. **iOS build system**: iOS builds require Xcode, code signing, and potentially different CMake toolchain files. Is iOS support in scope for initial implementation, or should it be deferred?

5. **GLSL version for Metal path**: What GLSL version should the Metal path target for glslang input? GLSL 450 is typical for Vulkan SPIR-V generation and should work well.

6. **Buffer binding model**: Metal requires explicit buffer indices in shader signatures. SPIRV-Cross remaps GLSL bindings to Metal indices, but the `Render_command_encoder_impl` must know these mappings to bind the right erhe `Buffer` to the right Metal buffer index. The mapping needs to be stored per-pipeline and looked up at draw time. What is the most efficient representation?

7. **Argument buffers vs. direct binding**: For the `Texture_heap` (used for bindless-like texture access), should Metal use argument buffers (closer to Vulkan descriptor sets) or a simpler direct binding approach for initial implementation?

8. **Existing `erhe::gl` dependency**: The OpenGL backend links against `erhe::gl` (the generated OpenGL wrapper). The Metal backend obviously does not. Ensure no shared (non-`#ifdef`-guarded) code in `erhe::graphics` references `erhe::gl` types.

9. **Command buffer lifecycle**: Metal requires explicit command buffer creation, encoding, commit, and waiting. erhe's current model (render command encoder obtained from Device) needs to map to Metal's command buffer lifecycle. Should the Device own a command queue and create command buffers per frame? Or per render pass?

10. **Depth bias / polygon offset**: Metal supports depth bias via `setDepthBias:slopeScale:clamp:` on the render command encoder. The `Rasterization_state` does not currently include depth bias fields. If needed, these can be added as a separate concern.
