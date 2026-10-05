# Testing

Stability: stable

Unit tests, the editor MCP test suite, the test build trees, and how CI runs
them. Editor-level behavior is verified live through the in-editor MCP server
instead (`doc/agents/editor_runs.md`).

## Suites

Several `erhe::*` libraries have gtest suites under `src/erhe/<name>/test/`
(circular_ring_buffer, codegen, dataformat, geometry, gltf, graphics, item, math,
physics, primitive, raytrace, renderer, usd), plus `mcp_server_tests` for the editor's
MCP server. `erhe_usd_tests` additionally needs `-DERHE_USD_LIBRARY=lightusd`,
and `erhe_physics_tests` needs a backend that simulates
(`-DERHE_PHYSICS_LIBRARY=jolt`, the default, or `box3d`); a `box3d` tree adds
that backend's own tests to the suite. Each builds an `erhe_<name>_tests`
executable, gated behind `-DERHE_BUILD_TESTS=ON` (default OFF). The root
`CMakeLists.txt` fetches googletest once for all of them; a new test directory
links `GTest::gtest`, calls `include(GoogleTest)` and
`add_dependencies(erhe_tests <target>)`. `erhe_smoke` (`doc/erhe/smoke.md`)
runs under ctest as a short randomized hierarchy stress run.

- A test `main()` that exercises code which logs must bootstrap logging the way
  `src/erhe/graph/test/main.cpp` does: `erhe::log::initialize_log_sinks()`, then
  `erhe::file::log_file` by hand, then the library's `initialize_logging()`. The
  `log_*` globals are null `shared_ptr`s until then, so the first log call from
  a library under test is an access violation, not a silent no-op.
- `gtest_discover_tests` makes every case its own ctest process, and `ctest -j`
  runs them concurrently, so a case that writes files never uses a fixed path
  shared with other cases: `erhe_usd_tests` writes under
  `erhe_usd_test::process_temporary_directory()`
  (`src/erhe/usd/test/test_temporary_directory.hpp`), a directory each process
  claims for itself and removes at exit.
- When adding pure-logic code to an `erhe::*` library that already has a
  `test/` directory (geometry operations, math, item/graph logic, dataformat),
  add or extend a test for it. Editor-level behavior is instead verified live
  through the in-editor MCP server.

## `mcp_server_tests`

`mcp_server_tests` is a pure HTTP client of the editor's MCP server, so under
`ctest` editors are CTest fixtures: `mcp_editor` (MCP port 3773) is one editor
shared by every `Mcp_test.*` case, and `mcp_editor_auth` (port 3774) is a
dedicated editor started with a configure-time test token
(`ERHE_MCP_TOKEN_FILE`) for `Mcp_auth_test.*`.

- Each fixture's start test runs `mcp_server_tests --start-editor`, which
  spawns `editor` detached from the repo root with its standard streams on the
  null device (a fixture setup test must exit before its dependents run, CMake
  itself cannot start a process without waiting for it, and a child that
  inherits ctest's output pipe makes ctest wait for it, so the editor's console
  output is not captured - read `logs/log.txt`), refuses when something already
  answers on the port, and exits once `GET /health` answers 200 (it answers 503
  until the main loop serves requests); the cases wait for that 200 too and run
  one at a time.
- The stop test runs `mcp_server_tests --request-editor-exit`, which waits for
  a still-starting editor, calls that editor's `request_exit` MCP tool and
  waits for it to go away.
- The editors run with the fixed-dt editor clock (`ERHE_FIXED_DT_MS=16.667`,
  set by `src/editor/mcp/test/editor_launcher.cpp` unless the environment
  already sets it; set it empty to test against the wall clock), so a case
  means the same at any frame rate (`doc/editor/time.md`). Cases let frames
  pass with the `advance_frames` MCP tool and wait out editor time, not wall
  time, between gestures.
- Every case begins with the `reset_editor_state` MCP tool (selection, mesh
  component selection, clipboard, undo/redo stacks, queued operations,
  shader-debug stack and thumbnail slots cleared, window visibility back to the
  startup state, every scene closed - the call returns once the scene list is
  empty) and then prepares its own scene over MCP (create_scene + textured glTF
  import + a material); the last case's scene is reset away at exit.
- Run `ctest -C Debug -R "Mcp_"` from the build directory; the windowed editor
  needs a live display (`doc/agents/editor_runs.md`).
- On Linux in a cloud container (4 cores, no GPU, no display) the headless
  Vulkan tree runs the suite on lavapipe (`doc/agents/linux.md` for the driver
  variables): `ctest -R "Mcp_"` passes 101 of 101 (97 cases plus the four
  fixture steps) in about 35 minutes, at a few hundred milliseconds per
  editor frame; `scripts/mesh_modeling_verify.py` (465 checks),
  `scripts/geometry_nodes_smoke_test.py` (136) and
  `scripts/scene_roundtrip_verify.py` (162, the USD leg skipped without
  `ERHE_USD_LIBRARY`) pass against the same headless editor with
  `ERHE_FIXED_DT_MS=16.667`.
- Visual Studio's Test Explorer runs the gtest binary directly (no ctest, no
  fixtures): there the binary launches the editors from their compiled-in path
  when nothing answers on the port and stops them at exit
  (`src/editor/mcp/test/editor_launcher.hpp`).
- Running the binary by hand against an editor you started yourself: point it
  at that editor with `ERHE_MCP_TEST_PORT` (default 3743); each case waits
  `ERHE_MCP_TEST_TIMEOUT_S` seconds (default 30) before launching one.

## Build trees with tests

- The macOS `configure_xcode_*.sh` scripts and `configure_vs2026_vulkan.bat`
  enable tests by default. On Windows, `scripts\configure_tests_asan.bat`
  produces a dedicated test configuration (`build_tests_asan/`, OpenGL + ASAN
  + tests ON); the other configure wrappers leave tests off -- every wrapper
  passes extra arguments through to cmake (`%*` / `"$@"`), so pass
  `-DERHE_BUILD_TESTS=ON` to get tests in a regular build tree.
- For performance measurement, `scripts\configure_tests.bat` produces
  `build_tests/` (no ASAN, profiler none; VS generator, so `--config Release`
  and `--config Debug` build from the same tree) -- used by the geometry
  timing harness, see `doc/erhe/catmull_clark.md`.
- With tests enabled, the `erhe_tests` target builds every test executable
  the configuration defines (each test `CMakeLists.txt` registers its target
  with `add_dependencies(erhe_tests ...)`; a new test target must do the
  same).

## Labels

Two ctest labels partition the tests by what they need: `gpu` (they bring up a
graphics `Device`: `erhe_graphics_gpu_tests`, `erhe_renderer_gpu_tests`,
`erhe_scene_renderer_gpu_tests`)
and `editor` (they drive a running editor: `mcp_server_tests` and its
fixtures). A machine without a GPU runs `ctest --label-exclude "gpu|editor"`;
a new test that needs either must carry the label
(`gtest_discover_tests(... PROPERTIES LABELS "gpu")`).

## Discovery

Test cases are discovered when `ctest` runs, not as a post-build step: the
top-level `CMakeLists.txt` sets
`CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST`. Keep it that way --
MSBuild's `Exec` task turns any stderr output of a post-build discovery run
into a build error even when the run exits 0, so a test binary that prints
anything at startup breaks the Visual Studio build. Precedent: Tracy's
`SymInitialize FAILED with code 0xc0000004` warning failed the Windows CI test
builds while discovery ran post-build.

## CI

CI (`.github/workflows/build.yml`) configures every matrix entry with tests
on, builds `editor` and `erhe_tests`, and runs
`ctest --label-exclude "gpu|editor" --output-junit` through
`scripts/ci_run_tests.py`; the runners have no GPU. One Linux entry builds
with Clang under AddressSanitizer and UndefinedBehaviorSanitizer
(`ERHE_USE_ASAN` / `ERHE_USE_UBSAN`, `doc/building.md`) and runs the same
deviceless set with `UBSAN_OPTIONS=halt_on_error=1`, so an undefined
behavior report fails the test it happens in, and `ASAN_OPTIONS=detect_leaks=0`
(leak reports are a separate verdict, not yet enabled); it shares the Vulkan
wrapper's build directory on its own runner and names its results
`build_ninja_linux_vulkan_sanitizers` (the script's optional fourth
argument). The Linux entries report erhe's compiler warnings without
failing the build (`ERHE_WARNINGS_AS_ERRORS` applies to MSVC only). Each test is bounded to
120 s and the ctest step to 30 minutes, so a hung test is reported as a
Timeout and a runaway step ends while the job is still alive. The script
tees ctest's output line by line to a `.log` next to the JUnit file (ctest's
own `--output-log` is written only at exit); both are uploaded, and the log
names the test that was running when a step was cut off. That ctest step
does not fail its job, so the build badge stays a build verdict: the JUnit
files are uploaded as `test-results-*` artifacts and
`.github/workflows/tests.yml` (triggered when a build run completes)
summarizes them with `scripts/ci_test_summary.py` and gives the tests badge in
README.md. A build run that did not succeed fails the tests workflow too.

The `gpu` tests run in a job of their own, "Linux (Vulkan headless /
lavapipe GPU tests)": the headless Vulkan tree
(`scripts/configure_ninja_linux_vulkan_headless.sh`, `ERHE_WINDOW_LIBRARY=none`,
so no display) builds only `erhe_graphics_gpu_tests`, `erhe_renderer_gpu_tests`
and `erhe_scene_renderer_gpu_tests`, and runs them on Mesa lavapipe
(`mesa-vulkan-drivers`, `VK_DRIVER_FILES` pointing at its ICD) with
`scripts/ci_run_tests.py --label gpu`. The Vulkan loader and
`VK_LAYER_KHRONOS_validation` come from the latest LunarG Vulkan SDK
(downloaded by the version `vulkan.lunarg.com/sdk/latest/linux.txt` names and
cached per version), with the SDK's library and layer manifest directories on
`LD_LIBRARY_PATH` and `VK_LAYER_PATH`; a check step fails the job unless
`vulkaninfo` lists both lavapipe and the layer, because the GPU test fixture
enables validation only when the layer loads. Every validation error fails
the test it occurs in. Its JUnit file, `build_ninja_linux_vulkan_headless_gpu`,
joins the others in the tests workflow. Tests that need a capability lavapipe
lacks skip, as on any device (`doc/erhe/graphics_test_coverage.md`).

## Running

Run via `ctest` from the build directory, or invoke the
`.../bin/<config>/erhe_<name>_tests.exe` binary directly. Run suites serially
and fix one failure at a time -- an abort hides the rest of the run.

`erhe_graphics_gpu_tests` also asserts golden buffers and images. Each run
writes `gpu_test_results/results.json` plus per-test artifacts under the
working directory (or `ERHE_GPU_TEST_RESULTS_DIR`);
`py -3 scripts/gpu_test_report.py gpu_test_results` renders them into a
self-contained `report.html`, and `ERHE_GPU_TEST_UPDATE_GOLDENS=1` rewrites
the goldens instead of comparing. The details are in
[`erhe/graphics_test_coverage.md`](erhe/graphics_test_coverage.md) "Golden
assertions".

## Shadow verification

What is measured (requirements, stations, gates G1 to G7, matrices, current
results) is defined in [`erhe/shadows.md`](erhe/shadows.md) "Shadow
verification"; the commands:

- **Library GPU tests** (label `gpu`): `erhe_scene_renderer_gpu_tests
  --gtest_filter=*Shadow*` checks the directional and spot sampling paths,
  both techniques, without the editor (`erhe/shadows.md` "Shadow sampling GPU
  tests"). Run it on Vulkan (`build_vs2026_vulkan_headless`) and on OpenGL
  (the non-ASAN `build_tests` tree); the device's depth convention is logged
  to `logs/log.txt` of the working directory.
- **MCP regression** (label `editor`): `Mcp_test.shadow_head_on_receivers_have_no_acne`
  asserts G1 on `shadow_head_on_floor.glb` and `gi_cornell.glb` with Medium's
  shadow fields and no rasterizer bias; its control
  `..._with_constant_depth_bias` runs the same measurement with rasterizer
  constant bias -4.
- **Gates**: `py -3 scripts/shadow_verify.py` (usage in its docstring) launches
  its own headless Vulkan editor(s) (`build_vs2026_vulkan_headless`, Debug),
  backs up `config/` and restores it byte-exactly, and stops only the editors
  it launched. `--matrix core` (the default, about 40 min on the development
  iGPU), `--matrix pairwise` (about 45 min), the final gate
  `--config "Low,Medium,High,Medium/shadow_technique=distance,Medium/depth_clamp=false"`
  (about 11 min); `--list-configs` prints a matrix, `--station` / `--light` /
  `--config` narrow it, `--root-offset` and `--extra-light` run the placement
  variants, `--save-images failing` keeps the worst image of each failing
  cell, `--enforce` exits non-zero on a FAIL. Results go to
  `logs/shadow_verify/<timestamp>.json`.
- **Cost (G7)**: `py -3 scripts/shadow_verify.py --g7 9` times the
  `render_scene_image` forward pass (MCP `get_gpu_timers`) on the `cornell`
  and `contact_blocks` views with the active preset. For a before / after
  comparison, extract the baseline commit with `git archive <commit> | tar -x
  -C <dir>` (no checkout, no branch), configure and build its headless
  editor there in its own build directory (the `get_gpu_timers` tool and the
  timed forward pass have to be present in that tree), and run
  `--g7 9 --editor-root <dir> --editor <dir>/<build>/bin/Debug/editor.exe`
  alternately with the current editor; compare the medians of the same
  view and light.

## Future work

- [plans/graphics_tests.md](plans/graphics_tests.md)
- [plans/deterministic_editor_clock.md](plans/deterministic_editor_clock.md): remaining scripts onto `advance_frames` and the fixed-dt clock
