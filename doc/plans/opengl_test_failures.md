# OpenGL backend: failures found by the GPU tests

Status: in progress

Defects of the OpenGL backend found while running `erhe_graphics_gpu_tests`
against the OpenGL tree during the 2026-09-30 audit item 10 work. The OpenGL
tests there ran on the NVIDIA proprietary GL driver (the `GL Renderer:` line of
the device startup log names the driver; `erhe.graphics.startup` is off by
default in the GPU tests, so the line appears only when that logger's level is
raised). Each item below is a root-cause fix to make; none is a test to
disable. Run suites serially and one failure at a time (`doc/testing.md`
"Running"). The Linux build trees and the headless run recipe are in
`doc/agents/linux.md`.

## 1. `Texel_fetch_test.texture_3d` reads zeros

`texelFetch` on a `sampler3D` returns `(0, 0, 0, 0)` for every texel; the 2D
and 2D array cases of the same test pass, and Vulkan passes all three. The
failure exists without any of this session's changes.

Established by experiment (all experiments reverted):

- The volume data is correct: the same texture sampled with `texture()` at
  the texel centres passes, and `texture_3d_sample_image` (same creation and
  `copy_from_buffer` upload) passes.
- The shader sees the right texture: `textureSize(s_texture, 0)` is
  (16, 16, 4) and `textureQueryLevels` is 1.
- `texelFetch(s_texture, ivec3(5, 0, 0), 0)` returns the correct texel when
  the same shader also contains a `texture()` call on `s_texture`; with
  `texelFetch` as the only access it returns zeros.
- No effect: binding no sampler object (`glBindSampler(unit, 0)`), setting
  the texture object's `GL_TEXTURE_MIN_FILTER` to `GL_NEAREST`, moving the
  binding from unit 0 to unit 3, omitting
  `#extension GL_ARB_bindless_texture : enable` from the prelude.
- The generated fragment source is correct GLSL (`layout(binding = 0)
  uniform sampler3D s_texture;`, `texelFetch(s_texture, ivec3(texel, layer), 0)`).
- Under Mesa zink (`__GLX_VENDOR_LIBRARY_NAME=mesa`,
  `__EGL_VENDOR_LIBRARY_FILENAMES=.../50_mesa.json`) the 3D case fails too,
  and `texture_2d` and `stencil_two_draw_mask` fail as well.

Next steps: a standalone reproduction outside erhe (raw GL: immutable
`glTextureStorage3D` 16x16x4 RGBA8, `glTextureSubImage3D`, a fragment shader
whose only access is `texelFetch`) decides between an erhe state problem and
a driver defect; compare the GL state at the draw with RenderDoc or apitrace
against the variant that adds `texture()`. Only a driver defect may end in a
documented workaround, and then with the reproduction attached.

## 2. Heap corruption at `Worker_context_gl_test` teardown

Running the `Worker_context_gl_test` suite (with or without the rest of
`erhe_graphics_gpu_tests`) aborts at "Global test environment tear-down"
with glibc `malloc_consolidate(): unaligned fastbin chunk detected`. All 12
cases pass first (`guard_aborts_offscope_worker_creation` asserts by design
and is excluded with `--gtest_filter='Worker_context_gl_test.*-*guard_aborts*'`;
the corruption occurs with and without it). The failure exists without any of
this session's changes.

Diagnose with an AddressSanitizer build: configure an OpenGL tree with
`-DERHE_USE_ASAN=ON -DERHE_BUILD_TESTS=ON` (`doc/building.md` options; on
Linux run cmake directly with the flags of
`scripts/configure_ninja_linux_opengl.sh`, into its own build directory),
build `erhe_graphics_gpu_tests`, run the suite and read the first ASAN
report: it names the write and the allocation and free stacks. Fix the
owner of the memory, not the symptom (`AGENTS.md` "No Band-Aid Fixes").
Likely suspects are the worker context and `Gl_binding_state` scrub queues
torn down after the context or thread that owns them
(`doc/erhe/gl_worker_thread_contexts.md`).
