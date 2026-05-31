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

## Non-DSA OpenGL (opt-in)

The test device builds a default `Graphics_config` and does not read
`erhe_graphics.json`, so the editor's `force_no_direct_state_access` does not affect
the suite. Set `ERHE_TEST_OPENGL_NO_DSA=1` to force the non-DSA OpenGL path (which
also disables persistent buffers, since erhe ties persistent mapping to
`glNamedBufferStorage`); the default (unset) keeps DSA.

Two changes make the suite mode-agnostic:
- The fixture's `make_host_buffer` uses the `Ring_buffer` required/preferred split
  (`host_read|host_write` required; `host_coherent|host_persistent` preferred)
  instead of requiring coherent, which aborted in non-DSA mode ("coherent buffers
  required but not supported").
- The Vulkan `Buffer::unmap` is persistent-aware (mirrors `map_bytes` and the GL
  backend), needed because the fixture now requests a persistent mapping.

Result with `ERHE_TEST_OPENGL_NO_DSA=1`: **39 passed + 1 skipped + 1 failed**. The
failure is `texture_cube_sample_faces`: classic GL must upload cube faces via the
per-face 2D target (`GL_TEXTURE_CUBE_MAP_POSITIVE_X + face`), but the generated gl
wrapper's `gl::Texture_target` does not expose the per-face cube enums (the DSA path
uses `glTextureSubImage3D` with the texture name and works). Exposing those enums
(a gl-binding regeneration) is the remaining work for non-DSA cube support; the
editor does not use cube maps, so this path is otherwise unexercised.

## Metal

The CMake gate enables the Metal build, but Metal cannot be built or run on the
Windows dev machine. Validate on macOS (Xcode); the same classes of issue may apply.
The cube-map sub-image path has a macOS (`__APPLE__`) branch that mirrors the same
3D-addressing fix but is unverified there.
