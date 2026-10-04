# OpenGL backend: failures found by the GPU tests

Status: in progress

Defects of the OpenGL backend found while running `erhe_graphics_gpu_tests`
against the OpenGL tree during the 2026-09-30 audit item 10 work. Two of the
three findings are resolved: the one-sample multisample target (e80b64ab2)
and the `Texel_fetch_test.texture_3d` zeros, which turned out to be an NVIDIA
GLSL compiler defect with a standalone reproduction and a documented
workaround in the test shader
(`doc/reference/nvidia_texel_fetch_3d_driver_report.md`). The OpenGL
tests there ran on the NVIDIA proprietary GL driver (the `GL Renderer:` line of
the device startup log names the driver; `erhe.graphics.startup` is off by
default in the GPU tests, so the line appears only when that logger's level is
raised). Each item below is a root-cause fix to make; none is a test to
disable. Run suites serially and one failure at a time (`doc/testing.md`
"Running"). The Linux build trees and the headless run recipe are in
`doc/agents/linux.md`.

## 1. Heap corruption at `Worker_context_gl_test` teardown

Running the `Worker_context_gl_test` suite (with or without the rest of
`erhe_graphics_gpu_tests`) aborts at "Global test environment tear-down"
with glibc `malloc_consolidate(): unaligned fastbin chunk detected`. All 12
cases pass first (`guard_aborts_offscope_worker_creation` asserts by design
and is excluded with `--gtest_filter='Worker_context_gl_test.*-*guard_aborts*'`;
the corruption occurs with and without it). The failure exists without any of
this session's changes.

Established (2026-10-04):

- The abort is inside the NVIDIA driver's `glXDestroyContext` for a worker
  `Context_window`, reached from `Device_impl::~Device_impl` destroying
  `m_worker_context_windows` (gl_device.cpp) from
  `Gpu_test_environment::TearDown`. The free that trips the check is in a
  non-main glibc arena (`av=0x7fffd8000030`, a thread arena), so the
  corrupted chunk was allocated by code running on a worker thread: the
  worker context work (erhe worker code or the driver's per-thread state),
  not the main thread. Backtrace recipe:
  `MALLOC_CHECK_=3 gdb -q -batch -ex run -ex 'bt 40' --args <tree>/bin/erhe_graphics_gpu_tests --gtest_filter='Worker_context_gl_test.*-*guard_aborts*'`.
  `MALLOC_CHECK_=3` does not catch the write earlier.
- `ERHE_USE_ASAN` now applies on Linux and macOS (`cmake/GNU.cmake`,
  `cmake/Clang.cmake`; before 2026-10-04 only `cmake/msvc.cmake` honored it).
  `build/Ninja_OpenGL_Asan` is configured with it (`doc/agents/linux.md`
  "AddressSanitizer"); linking needs the compiler's sanitizer runtime, which
  the Ubuntu clang 18 toolchain ships in the separate `libclang-rt-18-dev`
  package (absent on the machine the diagnosis ran on, so no ASAN report yet).

Next steps: install the sanitizer runtime, build `erhe_graphics_gpu_tests` in
the ASAN tree, run the suite and read the first ASAN report: it names the
write and the allocation and free stacks. Fix the owner of the memory, not
the symptom (`AGENTS.md` "No Band-Aid Fixes"). Hypotheses to check first: a
worker context left current on a thread that exits (the driver's thread-exit
teardown and the later `glXDestroyContext` then both release per-thread
state), and the worker context and `Gl_binding_state` scrub queues torn down
after the context or thread that owns them
(`doc/erhe/gl_worker_thread_contexts.md`).
