# Running erhe_graphics_gpu_tests on non-headless OpenGL / Metal

## Status

- The `erhe_graphics_gpu_tests` target builds and runs on non-headless OpenGL
  (`build_vs2026_opengl`: ERHE_GRAPHICS_API=opengl + a real window library).
- Headless Vulkan: **41/41 green** (unchanged; this is the hard gate -- never regress it).
- OpenGL: **37/41 green**. The two original blockers (host-buffer map pattern and
  the silent render-path abort) are RESOLVED; see "Resolved blockers" below. Four
  cases remain, each a distinct pre-existing GL-backend gap exposed by the suite
  (see "Remaining failures").

The test device is built from a default `Graphics_config{}` (it does not read
`config/editor/erhe_graphics.json`), so it uses native GL capabilities and every
fix below is an engine fix, not a config toggle.

## What already works on OpenGL

- Device bring-up; shader compilation (portable `gl_VertexID` / `gl_InstanceID`);
  and all buffer / compute / clear / readback / blend / depth / stencil / MSAA /
  color-format / device-capability cases, plus attribute-less (fullscreen-triangle)
  draws.

## Resolved blockers

### 1. Host/readback buffer coherent map (FIXED)

The fixture creates a host-visible coherent buffer and uses transient
`map_bytes -> memcpy -> unmap`. The GL backend rejected this
(`ERHE_VERIFY(persistent)` for a coherent buffer, then `ERHE_VERIFY(m_map.empty())`
once persistently mapped). Fixed in `gl_buffer.cpp`: a coherent request implies a
persistent mapping at the GL level, and `map_bytes`/`unmap` are persistent-aware
(mirroring the existing `begin_write`/`end_write`).

### 2. Render-path silent abort (FIXED)

Two causes, fixed separately:
- Core-profile GL rejects `glDraw*` with VAO 0; pipelines with no vertex input
  (fullscreen triangles) bound VAO 0. Fixed by binding a device-owned persistent
  empty VAO (`gl_state_tracker.cpp` / `gl_device.cpp`), created lazily on first use.
- The underlying GL error was masked by a `DebugBreak()` in the GL debug callback,
  which silently terminated the debugger-less test process. Fixed by gating it on
  `IsDebuggerPresent()` (`gl_debug.cpp`).

Also resolved: GL format-capability reporting in `gl_device.cpp`
(`probe_image_format_support` now rejects `format_undefined`; `stencil_renderable`
is kept consistent with the abstraction's stencil aspect for
`format_x8_d24_unorm_pack32`).

## Remaining failures (4) -- careful, editor-critical follow-ups

These touch intricate, editor-critical GL code (texture dimension conventions and
render Y-orientation). Each needs a deliberate fix verified against BOTH the editor
and the headless Vulkan suite -- do not guess; a wrong fix regresses the editor.

### A. Render Y-orientation -- `topology_point_list` (FAIL)

Four 1-pixel points render (the lit-texel count is exactly 4) but vertically
flipped: read-back lit pixel `y == (height - 1) - expected_y`. The test emits raw
NDC and negates Y "to match the render encoder's negative-height viewport" (a
Vulkan-specific assumption); erhe's GL backend orients Y via `glClipControl`
instead, so raw-NDC geometry lands Y-flipped relative to Vulkan. Settle this before
fixing: does the abstraction guarantee a consistent raw-NDC -> framebuffer
orientation across backends (then the GL path is wrong), or only at the
projection-matrix level (then the test's raw-NDC Y assumption is backend-specific
and the test should adapt)? No currently-passing render test is Y-asymmetric, so
this is the first case to exercise the question.

### B. Texture layer/depth convention -- `texture_cube_sample_faces`, `texture_2d_array_sample_layers`, `copy_from_texture_sub_rect` (ABORT)

All three abort in `convert_texture_dimensions_to_gl` (`gl_texture.cpp`) on a
GL-vs-Vulkan dimension-convention mismatch:
- Cube creation: the abstraction creates a cube with `array_layer_count = 6`,
  `width/height = 1`, depth unset (Vulkan style: 6 faces as array layers); the GL
  `texture_cube_map` case asserts `depth == 6 && array_layer_count == 0` (a
  GL-centric layout).
- `copy_from_buffer` to a 2D array passes a hardcoded `array_layer_count = 0`
  (`gl_blit_command_encoder.cpp:156`), but the `texture_2d_array` case requires `>= 1`.
- `copy_from_texture` (9-arg) passes a hardcoded `array_layer_count = 1`
  (`gl_blit_command_encoder.cpp:100`), but a plain `texture_2d` requires `0`.

Root cause: `convert_texture_dimensions_to_gl` is designed for texture *creation*
(it folds `array_layer_count` into `depth` and asserts per-target invariants) but
is reused for *copy regions* with a hardcoded layer count. A correct fix must
reconcile the cube/array/3D convention across creation, copy, and
`convert_to_gl_texture_target` (which keys cube-vs-cube-array off
`get_array_layer_count() != 0`) without regressing the editor's working texture
paths or the already-passing texture tests (whole-image copy, 2D
`copy_from_buffer`, `texture_3d`). Determine the editor's canonical cube/array
representation first (grep how the editor creates `texture_cube_map` /
`texture_2d_array`).

## Metal

The gate enables the Metal build, but Metal cannot be built or run on the Windows
dev machine. Validate on macOS (Xcode); the same classes of issue may apply.

## Bisection snapshot (OpenGL, build_vs2026_opengl)

37/41 pass. Remaining: `topology_point_list` (FAIL, render Y-flip);
`copy_from_texture_sub_rect`, `texture_2d_array_sample_layers`,
`texture_cube_sample_faces` (ABORT, texture dimension convention).
