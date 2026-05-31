# Running erhe_graphics_gpu_tests on non-headless OpenGL / Metal

## Status

- The `erhe_graphics_gpu_tests` target builds and runs on non-headless OpenGL
  (`build_vs2026_opengl`: ERHE_GRAPHICS_API=opengl + a real window library).
- Headless Vulkan: **41/41 green** (unchanged; this is the hard gate -- never regress it).
- OpenGL: **40 passed + 1 skipped, 0 failures, 0 aborts** -- effectively complete.
  The skip is `snorm_color_render_readback`, a legitimate device-capability skip
  (`format_8_vec4_snorm` is not color-renderable on this GL device; the test
  `GTEST_SKIP`s with that reason).

The test device is built from a default `Graphics_config{}` (it does not read
`config/editor/erhe_graphics.json`), so it uses native GL capabilities and every
fix below is an engine fix, not a config toggle.

## Fixes that brought OpenGL up (all on branch headless-vulkan)

### Buffers
- **Host/readback coherent map** (`gl_buffer.cpp`): a coherent request now implies
  a persistent mapping at the GL level, and `map_bytes`/`unmap` are persistent-aware
  (mirroring the existing `begin_write`/`end_write`). Previously the fixture's
  transient map of a coherent buffer aborted via two `ERHE_VERIFY`s.

### Render path
- **Empty VAO for no-vertex-input pipelines** (`gl_state_tracker.cpp` / `gl_device.cpp`):
  core-profile GL rejects `glDraw*` with VAO 0, so fullscreen-triangle pipelines
  (no vertex input) bound a device-owned persistent empty VAO, created lazily on
  first use (the public `Device::m_impl` is not yet wired during `Device_impl`
  construction).
- **DebugBreak gating** (`gl_debug.cpp`): the GL debug callback's `DebugBreak()` is
  gated on `IsDebuggerPresent()`, so a GL error is logged instead of silently
  terminating the debugger-less test process (this had masked the VAO error).

### Format-capability reporting (`gl_device.cpp`)
- `probe_image_format_support` rejects `format_undefined`.
- `get_format_properties` clears `stencil_renderable` (not just `stencil_size`) for a
  format the abstraction defines with no stencil aspect (e.g.
  `format_x8_d24_unorm_pack32`, which maps to `GL_DEPTH24_STENCIL8`).

### Textures
- **Layered copy slice count** (`gl_blit_command_encoder.cpp`): `copy_from_texture` /
  `copy_from_buffer` derive the GL copy depth from the copied texture
  (`source_size.z` for layered targets, 0 otherwise) instead of a hardcoded layer
  count, fixing sub-rect 2D copies and `copy_from_buffer` into 2D-array layers.
- **Cube map support** (`gl_texture.cpp` + `gl_blit_command_encoder.cpp`): the GL
  backend now honors the abstraction's Vulkan-style cube representation
  (`array_layer_count == 6`): `convert_texture_dimensions_to_gl` folds the layer
  count into depth, `convert_texture_offset_to_gl` selects the face via the z offset,
  and `copy_from_buffer` uses 3D sub-image addressing for cubes (2D storage but
  z = face for uploads).

### Tests
- **Point Y-mapping** (`test_topology.cpp`): `topology_point_list` queries the
  device's coordinate-space conventions (`texture_origin`) for the read-back Y sign
  instead of hardcoding Vulkan's negative-height-viewport assumption -- tests must
  respect device coordinate conventions like all application code does.

## Metal

The CMake gate enables the Metal build, but Metal cannot be built or run on the
Windows dev machine. Validate on macOS (Xcode); the same classes of issue may apply.
The cube-map sub-image path has a macOS (`__APPLE__`) branch that mirrors the same
3D-addressing fix but is unverified there.
