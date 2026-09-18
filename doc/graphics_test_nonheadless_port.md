# Running erhe_graphics_gpu_tests on non-headless OpenGL / Metal

Stability: stable

`erhe_graphics_gpu_tests` runs on more than the headless Vulkan device it was
written against. The coverage matrix itself is
[`graphics_test_coverage.md`](graphics_test_coverage.md); this document is
about what the non-headless backends need from the engine.

## Where the suite runs

- **Headless Vulkan / lavapipe: 41/41 green.** This is the hard gate - never
  regress it.
- **Non-headless OpenGL** (`build_vs2026_opengl`: `ERHE_GRAPHICS_API=opengl`
  plus a real window library): 40 passed, 1 skipped, 0 failures, 0 aborts. The
  skip is `snorm_color_render_readback`, a legitimate device-capability skip -
  `format_8_vec4_snorm` is not color-renderable on this GL device, and the
  test `GTEST_SKIP`s with that reason. OpenGL 4.5 + DSA is a hard requirement,
  so there is one OpenGL path and no mode matrix.
- **Metal** builds but has not been run; see Future work.

The test device is built from a default `Graphics_config{}` and does **not**
read `config/editor/erhe_graphics.json`, so it uses native device capabilities
and any behavior it needs is an engine property, not a config toggle.

## What the OpenGL backend has to get right for the suite

Each of these is a place where the GL backend has to match what the
abstraction promises, and each was a suite failure before it did.

### Buffers

- **A coherent map implies a persistent mapping at the GL level**
  (`gl_buffer.cpp`), and `map_bytes` / `unmap` are persistent-aware, mirroring
  `begin_write` / `end_write`. The fixture maps a coherent buffer
  transiently, which otherwise trips two `ERHE_VERIFY`s.

### Render path

- **An empty VAO for no-vertex-input pipelines** (`gl_state_tracker.cpp` /
  `gl_device.cpp`): core-profile GL rejects `glDraw*` with VAO 0, so
  fullscreen-triangle pipelines (no vertex input) bind a device-owned
  persistent empty VAO, created lazily on first use - the public
  `Device::m_impl` is not yet wired during `Device_impl` construction.
- **The GL debug callback's `DebugBreak()` is gated on `IsDebuggerPresent()`**
  (`gl_debug.cpp`), so a GL error is logged instead of silently terminating a
  debugger-less test process. Without the gate, the VAO error above was
  invisible.

### Format-capability reporting (`gl_device.cpp`)

- `probe_image_format_support` rejects `format_undefined`.
- `get_format_properties` clears `stencil_renderable` - not just
  `stencil_size` - for a format the abstraction defines with no stencil aspect
  (for example `format_x8_d24_unorm_pack32`, which maps to
  `GL_DEPTH24_STENCIL8`).

### Textures

- **Layered copy slice count** (`gl_blit_command_encoder.cpp`):
  `copy_from_texture` / `copy_from_buffer` derive the GL copy depth from the
  copied texture (`source_size.z` for layered targets, 0 otherwise) rather
  than from a hardcoded layer count. Sub-rect 2D copies and `copy_from_buffer`
  into 2D-array layers depend on it.
- **Cube maps** (`gl_texture.cpp` + `gl_blit_command_encoder.cpp`): the GL
  backend honors the abstraction's Vulkan-style cube representation
  (`array_layer_count == 6`). `convert_texture_dimensions_to_gl` folds the
  layer count into depth, `convert_texture_offset_to_gl` selects the face via
  the z offset, and `copy_from_buffer` uses 3D sub-image addressing for cubes
  (2D storage, z = face for uploads).

### Tests

- **Point Y-mapping** (`test_topology.cpp`): `topology_point_list` queries the
  device's coordinate-space conventions (`texture_origin`) for the read-back Y
  sign instead of hardcoding Vulkan's negative-height-viewport assumption.
  Tests respect device coordinate conventions like all application code does.

## Future work

- [Graphics tests](plans/graphics_tests.md) - running the suite on Metal, and
  GPU tests in CI under a software Vulkan.
