# erhe::graphics GPU and compute test plan (headless Vulkan)

## Context

`erhe::graphics` has no automated GPU coverage. The only graphics test today,
`src/erhe/graphics/test/test_shader_resource_size.cpp`, is deviceless (std140/std430
layout math). Nothing constructs a graphics `Device` or runs render/compute. The
standalone `rendering_test` app is rotten (see CLAUDE.md) and is not a test.

The headless Vulkan configuration added on branch `headless-vulkan` makes real GPU
tests runnable without a display: a surfaceless `VkInstance`/`VkDevice`, a dummy null
window, and a standalone `Emulated_swapchain_impl` (offscreen VMA images instead of a
real `VkSwapchainKHR`). The editor runs clean under `VK_LAYER_KHRONOS_validation`
(0 VUIDs over 5000 frames, no GPU/VMA leak). Headless also sidesteps the real-surface
"physical-device selection fails when the panel is off" flakiness. This is the right
vehicle for golden-image raster tests and compute readback tests.

Goal: add an incremental suite of real-GPU tests for `erhe::graphics` -- bring up a
`Device`, clear/draw into offscreen targets, dispatch compute, read results back and
assert -- starting from the smallest "device up" milestone and growing coverage. This
doc plans milestones 1-4 in detail and sketches 5-6 (deferred detail).

## Scope and conventions

- New, separate GoogleTest target `erhe_graphics_gpu_tests`, gated under
  `ERHE_BUILD_TESTS` AND a headless-capable config. The existing deviceless
  `erhe_graphics_tests` keeps building in all configs.
- C++20, ASCII only, `class` not `struct`, explicit types (not `auto`), sufficient
  parentheses. GoogleTest + `gtest_discover_tests`. No band-aids.
- Validated config: `scripts/configure_vs2026_vulkan_headless.bat` ->
  `build_vs2026_vulkan_headless` (`ERHE_GRAPHICS_API=vulkan`,
  `ERHE_WINDOW_LIBRARY=none`). Build/run the relevant build after each change.

## Load-bearing facts (confirmed against source)

- Headless detection: a `Device` is headless when `context_window == nullptr` OR
  `!context_window->has_vulkan_surface()`. The null `Context_window` returns false
  from `has_vulkan_surface()`.
- `Device` ctor (`device.hpp:271`): `Device(const Surface_create_info&,
  const Graphics_config&, Device_message_callback = {}, const Vulkan_external_creators* = nullptr)`.
- `Graphics_config` is generated (`erhe_graphics/generated/graphics_config.hpp`) and
  default-constructible (`null_device.hpp:27` uses `= {}`). A default-constructed one
  is fine for tests; no JSON load needed.
- The null window is compiled into `erhe::window` only when `ERHE_WINDOW_LIBRARY=none`.
  `erhe::graphics` already links `erhe::window`, so the test target gets
  `Context_window` transitively.
- Minimal offscreen frame sequence (from the editor init path,
  `editor.cpp:757-759` + `1913-1926`):
  `wait_frame()` -> `get_command_buffer(0)` -> `cb.begin()` -> record ->
  `cb.end()` -> `submit_command_buffers({&cb})` -> `wait_idle()` -> `end_frame()`.
  No `wait_for_swapchain`/`begin_swapchain` for offscreen work (the OpenXR path,
  `editor.cpp:290+`, opens a cb with `begin()` and binds no swapchain -- identical).
- Offscreen pipelines need no swapchain/surface: `set_format_from_render_pass`
  (`render_pipeline.cpp`) falls through to `att.texture->get_pixelformat()` when both
  `swapchain` and `surface` are null, so a plain `Texture` color target populates
  pipeline format compatibility.
- `wait_idle()` "blocks until all in-flight device frames complete; flushes pending
  completion handlers" (`device.hpp:319-321`). So a one-shot readback can map a plain
  host-visible buffer immediately after `wait_idle()`; no completion handler required.
- Logging must be initialized before Device work (`erhe::log` sinks +
  `erhe::graphics::initialize_logging()` + `erhe::window::initialize_logging()`); the
  Device backends log through `erhe::graphics` loggers.

## Test harness

### Target: a separate `erhe_graphics_gpu_tests` executable

Keep GPU tests in a new executable, not folded into `erhe_graphics_tests`, because:
the deviceless target must build everywhere (pure math, CI-friendly), while the GPU
target needs a real backend + null window + a Vulkan loader and must be gated;
`gtest_discover_tests` runs the binary to enumerate, so the GPU binary (which creates
a `VkInstance`) must not be executed on a GPU-less builder (use `DISCOVERY_MODE PRE_TEST`).

### Files (new, under `src/erhe/graphics/test/`)

- `gpu_test_environment.{hpp,cpp}` -- process-wide `Device` + null window
  (`::testing::Environment`), validation-message collection.
- `gpu_test_fixture.{hpp,cpp}` -- `Gpu_test` fixture with helpers.
- `test_m1_device_up.cpp`, `test_m2_clear_color.cpp`, `test_m3_triangle.cpp`,
  `test_m4_compute_ssbo.cpp` -- milestones.
- `main.cpp` (existing) is reused verbatim; the Environment self-registers via a
  static initializer in `gpu_test_environment.cpp`.

### Fixture helpers (illustrative; confirm exact field/enum names against headers)

- `submit_and_wait(record_fn)` -- drives the minimal frame sequence above; after it
  returns, all GPU work is complete and any mappable readback buffer is safe to read.
- `make_color_target(w, h, format = format_8_vec4_unorm)` -- 2D color texture with
  usage `color_attachment | sampled | transfer_src` (mirrors `thumbnails.cpp:42-59`).
- `read_texture_rgba8(texture) -> std::vector<uint8_t>` -- runs its own blit frame
  (`Blit_command_encoder::copy_from_texture` into a host-visible buffer), then maps.
- `read_buffer(buffer, byte_count = 0) -> std::vector<std::byte>` -- `invalidate()` +
  `map_bytes()` + copy; assumes GPU idle.
- `make_readback_buffer(byte_count, label)` -- host-visible mappable buffer, usage
  `transfer_dst | storage`.

The per-test `SetUp` clears the validation-message buffer; `TearDown` calls
`wait_idle()` and, matching the editor's device-message policy, fails the case (via
`ADD_FAILURE`) for validation **errors** (correctness VUIDs) while surfacing
**warnings** (best-practices advisories) non-fatally as `[ vk-warn ]`. The
Device-message callback records rather than `ERHE_FATAL`-ing (unlike the editor) so a
bad case is named precisely and the run continues. `make_color_target` also
pre-transitions the fresh texture to `transfer_src_optimal`, so render passes can
declare `usage_before/after = transfer_src` uniformly (the Id_renderer readback
pattern).

### Device lifetime: one shared device for the whole process

One `VkInstance`/`VkDevice` for the whole executable, owned by the
`::testing::Environment` (created in `SetUp`, destroyed in `TearDown`). This matches
the editor's proven single-device lifetime: the device is created once and destroyed
once, so the suite never exercises an untested device-recreate path or runs two
devices side by side. All milestones, including M1, use this shared device via the
`Gpu_test` fixture. `VkDevice` creation is also the dominant per-test cost, so sharing
keeps the suite fast as it grows.

Teardown concerns to honor: `wait_idle()` before destroying any resource or the
Device; prefer constructing `Render_pipeline`/`Compute_pipeline` directly (not via the
`Render_pipeline_state`/`Compute_pipeline_state` global `s_pipelines` registries) to
avoid cross-test residue; if a test tears down `Shader_stages` whose modules fed a
cached pipeline, call `device.clear_render_pipeline_cache()` (Vulkan recycles module
handles -- see `device.hpp:332`); if Device creation fails at runtime (no Vulkan on
the runner), `GTEST_SKIP()` rather than red-fail.

### CMake gating (inside `src/erhe/graphics/test/CMakeLists.txt`)

Deviceless `erhe_graphics_tests` is declared unconditionally (as today). The GPU
target is added only when the config can bring up a headless Device. Gate on the
cache strings (note: `ERHE_WINDOW_LIBRARY_NONE` is only an `add_definitions`, not a
CMake-scope variable):

```cmake
set(_gpu_tests_supported OFF)
if (
    (${ERHE_WINDOW_LIBRARY} STREQUAL "none") AND
    (${ERHE_GRAPHICS_API}   STREQUAL "vulkan")
)
    set(_gpu_tests_supported ON)
endif ()

if (_gpu_tests_supported)
    set(_gpu_target "erhe_graphics_gpu_tests")
    add_executable(${_gpu_target}
        main.cpp
        gpu_test_environment.cpp
        gpu_test_fixture.cpp
        test_m1_device_up.cpp
        test_m2_clear_color.cpp
        test_m3_triangle.cpp
        test_m4_compute_ssbo.cpp
    )
    target_link_libraries(${_gpu_target}
        PRIVATE erhe::graphics erhe::window erhe::dataformat erhe::math erhe::verify GTest::gtest
    )
    erhe_target_settings(${_gpu_target} "erhe/tests")
    include(GoogleTest)
    gtest_discover_tests(${_gpu_target} DISCOVERY_MODE PRE_TEST)
else ()
    message(STATUS "erhe_graphics_gpu_tests skipped (needs ERHE_WINDOW_LIBRARY=none + ERHE_GRAPHICS_API=vulkan)")
endif ()
```

The parent `src/erhe/graphics/CMakeLists.txt:417` (`if (ERHE_BUILD_TESTS ...)
add_subdirectory(test)`) is unchanged.

## Milestones

### M1 -- Device up (smallest milestone)

Goal: construct a headless Vulkan `Device` over a null window and destruct it with
zero validation warnings/errors across the full lifecycle.

- File `test_m1_device_up.cpp`, `TEST_F(Gpu_test, ...)` over the shared device.
- The shared device is brought up by `Gpu_test_environment::SetUp` (init logging ->
  null `Context_window{show=false}` -> `Device{Surface_create_info{.context_window},
  Graphics_config{}, message_callback}`). The default `Graphics_config` enables
  validation, and the headless path is taken because the null window reports
  `has_vulkan_surface() == false`.
- Assertions: `Device_info::api_info` is non-empty (a backend came up); and
  `Gpu_test_environment::setup_messages()` -- the validation messages captured during
  device creation -- is empty. Clean destruction is exercised at process end by the
  Environment teardown (`wait_idle()` + destroy).

### M2 -- Clear-color offscreen + pixel readback

Goal: one offscreen render pass that only clears an RGBA8 target to a known color;
read back; assert every pixel matches.

- File `test_m2_clear_color.cpp`, `TEST_F(Gpu_test, ...)`.
- `make_color_target(16, 16)`; default-construct `Render_pass_descriptor` and set
  `color_attachments[0]` fields individually (it has a user-declared ctor, so no
  designated init): `texture`, `clear_value`, `load_action=Clear`,
  `store_action=Store`, and `usage_before/after = transfer_src` +
  `layout_before/after = transfer_src_optimal` (required -- the render-pass layer
  errors on an unset `usage_before/after`). No swapchain/surface. `submit_and_wait`
  records `Render_pass` + `Scoped_render_pass` only (the clear fires on render-pass
  begin -- no draw); then `read_texture_rgba8`.
- Assertion: for clear `{1.0, 0.5, 0.25, 1.0}` expect bytes ~`{255,128,64,255}`
  (`EXPECT_NEAR(..., 1)` for unorm rounding) across all texels, with explicit
  corner/center spot checks.
- Proves: offscreen `Render_pass` (no swapchain, no draw) + image->buffer readback +
  `wait_idle`/map path; exercises Clear/Store -> transfer_src_optimal + blit.

### M3 -- Triangle draw (inline GLSL) + coverage assert

Goal: render one solid-color triangle covering a known region; assert interior is the
triangle color and background is the clear color.

- File `test_m3_triangle.cpp`, `TEST_F(Gpu_test, ...)`.
- Inline GLSL via `Shader_stage(Shader_type, std::string_view)`. Use
  `no_vertex_input = true` and emit 3 vertices from `gl_VertexIndex` (no vertex
  buffer/format). Fragment writes a constant color to one output. Provide a
  `Fragment_outputs` (the rendering_test always does; see R3). Empty `Bind_group_layout`
  with `uses_texture_heap = false` (omits descriptor set 1).
- Build stages: `Shader_stages_prototype` -> `compile_shaders()` -> `link_program()`
  -> `ASSERT_TRUE(is_valid())` -> `Shader_stages`. Build a `Render_pipeline` directly
  (avoids `s_pipelines`): `Input_assembly_state::triangle`,
  `Rasterization_state::cull_mode_none`, depth/stencil disabled,
  `Color_blend_state::color_blend_disabled`, then `set_format_from_render_pass(desc)`.
- Record: `Render_command_encoder` -> `set_viewport_rect`/`set_scissor_rect` ->
  `set_bind_group_layout(empty)` -> `set_render_pipeline(pipeline)` ->
  `draw_primitives(Primitive_type::triangle, 0, 3)`.
- Assertions: an interior texel == triangle color (+/-1); an exterior texel == clear
  color; green-texel count within an expected band (rejects empty and full-screen
  draws).

### M4 -- Compute dispatch writes a known pattern to an SSBO + exact readback

Goal: dispatch a compute shader that writes `data[i] = i` into a storage buffer; read
back; assert exact integer equality for every element.

- File `test_m4_compute_ssbo.cpp`, `TEST_F(Gpu_test, ...)`.
- Guard: `if (!device().get_info().use_compute_shader) GTEST_SKIP();` (and storage
  buffers).
- `make_readback_buffer(N * sizeof(uint32_t), ...)` already has `storage |
  transfer_dst` + host-visible memory, so the shader writes it and the host reads it
  directly (small N).
- Inline compute GLSL: `local_size_x=64`, `std430 binding=0 writeonly buffer { uint
  data[]; }`, `if (i < N) data[i] = i;`. `Bind_group_layout` with one storage binding,
  `uses_texture_heap = false`. (If erhe reflection requires it, declare the SSBO via a
  `Shader_resource` block + `interface_blocks` rather than raw inline -- see R4.)
- `Compute_pipeline{device, Compute_pipeline_data{ .shader_stages, .bind_group_layout }}`;
  record `Compute_command_encoder` -> `set_bind_group_layout` -> `set_compute_pipeline`
  -> bind buffer (`set_buffer(Buffer_target::storage, ...)`) -> `dispatch_compute(groups,1,1)`.
- Assertion: `for i in [0,N): EXPECT_EQ(values[i], i)` -- exact, no tolerance. Choose N
  not a multiple of 64 (e.g. 1000) to exercise the bounds branch.
- No compute->graphics barrier needed (only consumer is a host read gated by
  `wait_idle()`); note that feeding the SSBO into a later draw would require a
  `Memory_barrier`.

### M5 -- Grow coverage (sketch; detail deferred)

Parametrized `TEST_F`s, each `submit_and_wait` + readback + assert, guarded on the
relevant `Device_info` capability with `GTEST_SKIP()` when unsupported. Reference cells
live in `src/rendering_test/`:

- Blend (`Color_blend_state::color_blend_alpha`), depth (add depth attachment; nearer
  color wins; use `get_reverse_depth()`), stencil (write-then-equal-test), multiple
  color attachments (two `Fragment_outputs`), samplers/textures (upload a known
  texture, sample via `set_sampled_image`), buffer up/download round-trip
  (`copy_from_buffer`), MSAA resolve (multisampled target + resolve texture; guard on
  `msaa_sample_counts`).

### M6 -- CI ctest job (sketch; detail deferred)

`.github/workflows/build.yml` currently only builds the editor and runs no ctest.
Add a job that configures `-DERHE_BUILD_TESTS=ON -DERHE_GRAPHICS_API=vulkan
-DERHE_WINDOW_LIBRARY=none`, installs a software Vulkan (Mesa lavapipe on Ubuntu
runners; SwiftShader as alternative), points the loader at its ICD, verifies with
`vulkaninfo`, then runs `ctest -R erhe_graphics_gpu_tests`. Keep a separate
`ctest -R erhe_graphics_tests` (deviceless) job that needs no Vulkan.

Software Vulkan does not support every format/feature; tests must probe and skip:
`probe_image_format_support`, `get_format_properties`,
`get_supported_depth_stencil_formats`/`choose_depth_stencil_format`, and `Device_info`
flags. The Environment `SetUp` should `GTEST_SKIP()` (not fail) if no Vulkan device can
be created. Keep targets tiny (16x16, N~1000). Optionally enable
`VK_LAYER_KHRONOS_validation` in CI to keep the 0-VUID guarantee under software.

## Verification

- Configure + build: `scripts\configure_vs2026_vulkan_headless.bat` with
  `ERHE_BUILD_TESTS=ON`, then build target `erhe_graphics_gpu_tests` in
  `build_vs2026_vulkan_headless`.
- Run: `ctest --test-dir build_vs2026_vulkan_headless -C Debug -R Gpu_test
  --output-on-failure` (gtest names the cases `Gpu_test.<name>`, so filter on the
  `Gpu_test` suite, not the target name). Or run the executable directly:
  `build_vs2026_vulkan_headless/src/erhe/graphics/test/Debug/erhe_graphics_gpu_tests.exe`.
  Each milestone is a separate test case, so milestones can be run/added one at a time.
- Validation is on by default (the default `Graphics_config` enables
  `VK_LAYER_KHRONOS_validation` when loadable). Per milestone: assertions pass AND no
  validation **errors** (the fixture fails on errors; best-practices **warnings** are
  printed as `[ vk-warn ]` but do not fail, matching the editor).
- Keep `ctest -R erhe_graphics_tests` (deviceless) green in all configs.

## Risks / unknowns to verify during implementation

- R1 (resolved in M2): the render-pass layer errors if `usage_before/after` is left
  unset, so both must be provided; `make_color_target` pre-transitions the fresh
  texture to `transfer_src_optimal` and the pass declares `transfer_src` before/after.
  Transitions to `transfer_src_optimal` emit a non-fatal best-practices warning
  (`TRANSFER_WRITE` vs `TRANSFER_READ`) intrinsic to erhe's readback path (the
  Id_renderer does the same); the fixture surfaces it without failing.
- R2: `wait_idle()` makes a plain mappable buffer readable without a completion handler
  (strongly indicated by `device.hpp:319-321`); fall back to the `id_renderer`
  completion-handler pattern if not.
- R3: whether a trivial single-output fragment shader requires `Fragment_outputs`
  (plan supplies one to be safe).
- R4: compute SSBO declaration -- inline `buffer{}` vs a `Shader_resource` block +
  `interface_blocks` (the working reference uses the block form).
- R5: a single-storage-binding `Bind_group_layout` with `uses_texture_heap=false`
  binds correctly via `set_buffer(Buffer_target::storage, ...)` (vs needing
  `Ring_buffer_client::bind`).
- R6: `gtest_discover_tests` default `POST_BUILD` runs the binary at build time; the
  GPU target uses `DISCOVERY_MODE PRE_TEST` so a `VkInstance` is created only at ctest
  time. Confirm toolchain CMake supports `PRE_TEST`.
- R7: with a shared Device, destroying per-test `Shader_stages`/pipelines leaves no
  stale backend-cache entry for the next test (direct `Render_pipeline` ctor;
  `clear_render_pipeline_cache()` if needed).
- R8: default-constructed `Graphics_config` leaves nothing unset that breaks
  inline-GLSL compilation under Vulkan/glslang.
- R9: unorm rounding -- use `EXPECT_NEAR(..., 1)` for colors; exact only for M4's
  integer SSBO.
- R10: no working-directory/file dependency -- plan uses inline GLSL (no `res/`
  shaders) and default `Graphics_config` (no JSON load). Confirm no erhe::graphics
  startup path reads a file unconditionally.

## Reference files

- `src/erhe/graphics/test/CMakeLists.txt` (target pattern + gating site)
- `src/erhe/graphics/test/test_shader_resource_size.cpp`, `main.cpp` (existing pattern)
- `src/erhe/graphics/erhe_graphics/device.hpp` (ctor, frame lifecycle, `wait_idle`,
  `Device_info`, encoder factories)
- `src/erhe/graphics/erhe_graphics/render_pipeline.cpp` (`set_format_from_render_pass`
  offscreen fall-through -- load-bearing for M3)
- `src/editor/editor.cpp:757-759, 1913-1926` (minimal frame sequence)
- `src/editor/graphics/thumbnails.cpp:42-59` (offscreen color target)
- `src/erhe/graphics/erhe_graphics/{render_pass,render_command_encoder,
  blit_command_encoder,compute_command_encoder,buffer,texture,shader_stages}.hpp`
- `src/rendering_test/{rendering_test.cpp, rendering_test_setup.cpp,
  cell_minimal_compute_triangle.cpp}` (rotten app, but the GPU call shapes for M2-M5)
