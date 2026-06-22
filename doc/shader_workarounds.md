# Shader workarounds and capability defines

## Policy

Shaders must NOT branch on the graphics API (Vulkan / Metal / OpenGL). We do not
emit an `ERHE_TARGET_VULKAN` / `ERHE_TARGET_METAL` / `ERHE_TARGET_OPENGL` style
macro, and shader code must never assume a backend.

Instead, shaders branch on the concrete *capability* or *driver workaround* they
need. There are two kinds of shader-visible defines:

- **Capability / feature defines** - emitted when the device supports (or
  requires) a particular feature. Existing examples emitted by
  `Shader_stages_create_info::final_source()`: `ERHE_HAS_CLIP_DISTANCE`,
  `ERHE_MULTIVIEW`, `ERHE_GLSL_VERSION`, the `ERHE_TEXTURE_HEAP_*` family.

- **Workaround defines** - emitted when a specific driver mishandles a
  spec-valid construct, so the shader must take an alternate path. These are
  named `WORKAROUND_<description-of-the-limitation>`. The `WORKAROUND_` prefix
  means "the named thing is broken on this device; take the workaround path."

The rationale: the same backend (e.g. Vulkan) runs on many drivers with
different bugs. Keying shader behavior on the *driver-detected limitation* rather
than the API keeps the correct path on conformant drivers and isolates the
workaround to the devices that actually need it.

## Mechanism

1. **Detect** the limitation at device init and store a flag on `Device_info`
   (`src/erhe/graphics/erhe_graphics/device.hpp`). Detection is usually by driver
   ID (see the `is_moltenvk` / `is_kosmickrisp` checks in
   `vulkan_device_init.cpp`), the same way `use_solid_wireframe` is cleared on
   macOS OpenGL 4.1. The flag defaults to `false`, so backends and drivers that
   do not need the workaround are unaffected.

2. **Emit** the define into every shader's prelude from that flag, in
   `Shader_stages_create_info::final_source()`
   (`src/erhe/graphics/erhe_graphics/shader_stages_create_info.cpp`):

   ```cpp
   if (graphics_device.get_info().workaround_no_compute_storage_image_read) {
       sb << "#define WORKAROUND_NO_COMPUTE_STORAGE_IMAGE_READ 1\n";
   }
   ```

   Because the define is part of the generated source, it is automatically part
   of the shader variant (and of the SPIR-V cache key derived from that source).

3. **Branch** in the shader on `#if defined(WORKAROUND_...)`.

4. **Match** any C++ resource setup that must agree with the shader path off the
   *same* `Device_info` flag - never re-detect it independently. For example a
   bind group layout whose binding type differs between the normal and workaround
   paths must read the flag so the descriptor types match what the compiled
   shader expects.

## Naming

`WORKAROUND_<UPPER_SNAKE_CASE_LIMITATION>`. Name the *limitation*, not the
driver, so a second driver with the same bug can reuse the define. Document the
driver(s) currently known to need it in the `Device_info` field comment and at
the detection site.

## Current workarounds

### `WORKAROUND_NO_COMPUTE_STORAGE_IMAGE_READ`

- **Limitation**: Mesa KosmicKrisp (Vulkan-on-Metal, `VK_DRIVER_ID_MESA_KOSMICKRISP`)
  rejects `OpImageRead` from a storage image (`imageLoad` on a `uniform image2D`)
  at `vkCreateComputePipelines` with `VK_ERROR_INVALID_SHADER_NV`, even though the
  SPIR-V is spec-valid (`spirv-val` passes) and the same driver accepts
  `OpImageWrite`.
- **Device flag**: `Device_info::workaround_no_compute_storage_image_read`,
  set in `vulkan_device_init.cpp`.
- **Shader path**: read the source image as a `sampler2D` via `texelFetch`
  (`OpImageFetch`, which the driver accepts) instead of `imageLoad`. `texelFetch`
  is an unfiltered single-texel read, so the values are identical.
- **C++ side**: the consuming bind group binds the source as a
  `combined_image_sampler` (and the image is transitioned to
  `shader_read_only_optimal` before the dispatch) instead of a `storage_image`.
- **User**: `res/editor/shaders/sky_multiscatter_lut.comp` reading the sky
  transmittance LUT (`src/editor/renderers/sky_renderer.cpp`). The compute
  encoder gained `set_sampled_image()` for this (real on Vulkan; no-op on the
  other backends, which keep the storage-image path).
- **Remove when**: KosmicKrisp accepts storage-image reads in compute; then the
  detection in `vulkan_device_init.cpp` can be dropped and the `#else` /
  storage-image path becomes unconditional again.
