# Graphics tests: outstanding work

Status: proposed

Extends `doc/erhe/graphics_test_coverage.md` and
`doc/erhe/graphics_test_nonheadless_port.md`.

## GPU tests in CI under a software Vulkan

Deferred by decision: it cannot be developed or verified from a Windows dev
machine and needs new infrastructure.

Add a matrix entry (or a step of the Linux Vulkan entry) that configures
`-DERHE_BUILD_TESTS=ON -DERHE_GRAPHICS_API=vulkan -DERHE_WINDOW_LIBRARY=none`,
installs a software Vulkan (Mesa lavapipe on Ubuntu runners; SwiftShader as an
alternative), points the loader at its ICD, verifies with `vulkaninfo`, then
runs `ctest -L gpu` in addition to the label-excluded run.

Software Vulkan does not support every format and feature, so tests must probe
and skip: `probe_image_format_support`, `get_format_properties`,
`get_supported_depth_stencil_formats` / `choose_depth_stencil_format`, and the
`Device_info` flags. The Environment `SetUp` should `GTEST_SKIP()` (not fail)
when no Vulkan device can be created. Keep targets tiny (16x16, N ~ 1000).
Optionally enable `VK_LAYER_KHRONOS_validation` in CI to keep the zero-VUID
guarantee under software.

## Run the suite on Metal

The CMake gate enables the Metal build of `erhe_graphics_gpu_tests`, but the
suite has not been run on it. Validate on macOS (Xcode); the classes of issue
the OpenGL port hit - format-capability reporting, layered copy slice counts,
cube-map addressing, coordinate conventions - are the ones to expect.
