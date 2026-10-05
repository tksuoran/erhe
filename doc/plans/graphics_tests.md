# Graphics tests: outstanding work

Status: proposed

Extends `doc/erhe/graphics_test_coverage.md` and
`doc/erhe/graphics_test_nonheadless_port.md`. The golden-image machinery and
the tests ported from the agfx suite are planned separately in
[`graphics_tests_agfx_port.md`](graphics_tests_agfx_port.md).

## GPU tests in CI under a software Vulkan

Measured on 2026-10-05 in a Linux container (Ubuntu 24.04, 4 cores, Clang
18, Mesa lavapipe with LLVM 20, Vulkan 1.4): the `gpu` label runs there,
and two engine defects block it.

What was run:

- Software Vulkan: `apt-get install mesa-vulkan-drivers vulkan-tools`;
  `vulkaninfo --summary` lists `llvmpipe` with apiVersion 1.4.318.
- Headless tree: `scripts/configure_ninja_linux_vulkan_headless.sh
  -DERHE_BUILD_TESTS=ON`, building only `erhe_graphics_gpu_tests`,
  `erhe_renderer_gpu_tests` and `erhe_scene_renderer_gpu_tests`; then
  `ctest -L gpu -j4` with no display. 229 tests.
- The windowed tree (`ERHE_WINDOW_LIBRARY=sdl`) needs a display: without one
  every GPU test aborts in `Context_window` ("Failed to initialize SDL").
  Under `xvfb-run` the device comes up; the headless tree needs neither.
- Validation: `VK_LAYER_KHRONOS_validation` built from
  KhronosGroup/Vulkan-ValidationLayers at `vulkan-sdk-1.4.363.0` (the
  latest SDK tag; the LunarG download hosts were not reachable from the
  container, CI runners can download the SDK itself). The layer manifest
  names its library without a path, so the run sets `VK_LAYER_PATH` to the
  manifest directory and `LD_LIBRARY_PATH` to the library directory, which
  is what the SDK's `setup-env.sh` does.

Results, with the first defect below patched locally:

| Run | Passed | Failed | Skipped | Wall time |
|---|---|---|---|---|
| no validation | 229 | 0 | 0 | 85 s |
| validation 1.4.363 | 228 | 1 | 0 | 46 s |

The goldens hold under lavapipe: no test needed a skip or a per-driver
golden.

Defects to fix before the entry can be green:

1. First-frame crash on drivers exposing only `VK_EXT_calibrated_timestamps`
   (lavapipe). `Device_impl::update_gpu_calibration` (vulkan_device.cpp)
   picks `vkGetCalibratedTimestampsKHR` whenever the pointer is non-null,
   and through the loader it always is, so the call reaches a null driver
   entry. `select_calibrated_host_time_domain` (same file) and
   vulkan_swapchain.cpp do the same for the KHR/EXT pairs. Choose by the
   enabled extension (`m_device_extensions.m_VK_KHR_calibrated_timestamps`)
   instead of the pointer; that one change let every test run.
2. `Gpu_test.msaa_stencil_only_resolve_tracks_target_layout` fails on
   VUID-VkSubpassDescriptionDepthStencilResolve-pDepthStencilResolveAttachment-03185:
   a depth-stencil format resolved with depthResolveMode NONE and
   stencilResolveMode SAMPLE_ZERO on a device whose
   `independentResolve` and `independentResolveNone` are both false.
   Hardware drivers that support independent resolve do not report it.

CI recipe (not yet run on a GitHub runner):

- A Linux headless entry: the configure line above, build the three
  targets, install `mesa-vulkan-drivers`, install the latest LunarG Vulkan
  SDK for the validation layer, then `ctest -L gpu` with JUnit output next
  to the label-excluded run. The plain Linux Vulkan entry could run the
  same label under `xvfb-run` instead, saving a configure and build, at the
  cost of an X server per job.
- The SDK download and its environment setup are the unverified part: the
  container could not reach the LunarG hosts.
