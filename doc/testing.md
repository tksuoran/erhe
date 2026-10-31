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
- The deviceless suites register through `gtest_discover_tests`, which makes
  every case its own ctest process: such a process is cheap, and CI's JUnit
  results report case by case because of it. `ctest -j` runs these processes
  concurrently, so a case that writes files never uses a fixed path shared
  with other cases: `erhe_usd_tests` writes under
  `erhe_usd_test::process_temporary_directory()`
  (`src/erhe/usd/test/test_temporary_directory.hpp`), a directory each process
  claims for itself and removes at exit.
- The four `gpu`-labelled binaries register through `erhe_add_gpu_test()`
  (`cmake/functions.cmake`) as one ctest test each, so all cases of a binary
  run in one process against one `Device`: every process pays the window and
  `Device` bring-up (about 1 s on Metal, 2.5 s on MoltenVK), which per case
  made `erhe_graphics_gpu_tests` take about 200 s under ctest instead of 7 s.
  `-DERHE_GPU_TESTS_PER_PROCESS=ON` registers every case of them as its own
  ctest test instead (one process per case), for a run whose results must
  survive a crash or an abort in one case. The fixture keeps the cases of one
  process apart ("GPU test isolation").
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
- Every case begins with the `reset_editor_state` MCP tool (selection, mesh
  component selection, clipboard, undo/redo stacks, queued operations,
  shader-debug stack and thumbnail slots cleared, window visibility back to the
  startup state, every scene closed - the call returns once the scene list is
  empty) and then prepares its own scene over MCP (create_scene + textured glTF
  import + a material); the last case's scene is reset away at exit.
- Run `ctest -C Debug -R "Mcp_"` from the build directory; the windowed editor
  needs a live display (`doc/agents/editor_runs.md`).
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
graphics `Device`: `erhe_graphics_gpu_tests`, `erhe_gltf_gpu_tests`, `erhe_renderer_gpu_tests`,
`erhe_rendergraph_gpu_tests`, `erhe_scene_renderer_gpu_tests`)
and `editor` (they drive a running editor: `mcp_server_tests` and its
fixtures). A machine without a GPU runs `ctest --label-exclude "gpu|editor"`;
a new test that needs either must carry the label. A new binary that
brings up a `Device` registers with `erhe_add_gpu_test(<target>)`, which
applies the `gpu` label in both registration modes; `ctest -L gpu` then runs
four processes, or one per case in a `-DERHE_GPU_TESTS_PER_PROCESS=ON` tree.

## Discovery

Test cases are discovered when `ctest` runs, not as a post-build step: the
top-level `CMakeLists.txt` sets
`CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST`. Keep it that way --
MSBuild's `Exec` task turns any stderr output of a post-build discovery run
into a build error even when the run exits 0, so a test binary that prints
anything at startup breaks the Visual Studio build. Precedent: Tracy's
`SymInitialize FAILED with code 0xc0000004` warning failed the Windows CI test
builds while discovery ran post-build.

## GPU test isolation

The GPU tests derive their fixtures from `erhe::graphics::test::Gpu_test`
(`src/erhe/graphics/test/gpu_test_fixture.hpp`; the renderer, scene renderer
and rendergraph fixtures derive from it too). A derived fixture calls
`Gpu_test::SetUp` first; one that holds device objects as members releases
them in its own `TearDown` and then calls `Gpu_test::TearDown`, so their
deferred releases are retired and counted inside the case. Since the cases of
one binary share one `Device`, the fixture makes each case start
from the same device state, whatever ran before it:

- **Settling.** `SetUp` and `TearDown` wait the device idle and then run
  empty frames (`wait_frame()` + `end_frame()` + `wait_idle()`, at most four)
  until no deferred release is left: completion handlers and ring buffer
  ranges queued while no frame is open are keyed to the next frame, which the
  Metal backends retire only once that frame has ended (Vulkan's `wait_idle()`
  drains them itself).
- **Open frames.** `TearDown` ends every frame a failed case left open
  between `wait_frame()` and `end_frame()`, oldest first, and fails the case
  naming the frame ids, so the next case starts with no frame open.
- **Caches.** `TearDown` drops the render pipeline cache
  (`Device::clear_render_pipeline_cache()`; the Vulkan pipeline map is keyed
  on raw handles a driver may recycle) and settles again for the pipelines'
  deferred destruction. No other device-level cache needs a reset: bind state
  lives in the command encoders on Vulkan, Metal and Metal 4 (Metal's
  `reset_shader_stages_state_tracker()` is a no-op), the OpenGL state tracker
  mirrors the context's own state, and the remaining caches (compatible
  render passes, the blit-framebuffer pipelines per format, the SPIR-V and
  shader source caches) are keyed on their full content.
- **Counter check.** `SetUp` fails the case when a frame is open (no case
  left it open, so something outside the cases did), then snapshots
  `Device::get_debug_state()` (open frames, ended frames not yet retired, uncompleted command buffers, queued
  completion handlers, busy ring buffers and their bytes in use) after
  settling; `TearDown` fails the case, with both snapshots in the message,
  when a frame is still open or a counter grew past the snapshot. The ring
  buffer count itself may grow (the pool keeps one warm buffer per usage
  class) and is only reported.
- **Validation.** `SetUp` clears the runtime validation-message list;
  `TearDown` fails the case for every validation error emitted during it and
  prints the warnings.

An order dependency between cases shows up when the cases run in a random
order: `--gtest_shuffle --gtest_random_seed=<N>` on any GPU binary, or
`SHUFFLE=1 scripts/run_graphics_tests_metal.sh` for `erhe_graphics_gpu_tests`
(the seed is printed; `SEED=<N>` repeats one). A failure that appears only in
some order is fixed in the test or in the state it leaves behind, not by
pinning the order.

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
argument). Every entry compiles erhe's targets with warnings as errors
(`ERHE_WARNINGS_AS_ERRORS`, default on). Each test is bounded to
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
Running the `gpu` tests in CI under a software Vulkan is described in
`doc/erhe/graphics_test_coverage.md`.

## Running

Run via `ctest` from the build directory, or invoke the
`.../bin/<config>/erhe_<name>_tests.exe` binary directly. Run suites serially
and fix one failure at a time -- an abort hides the rest of the run. A GPU
binary is one ctest test by default, so an abort in it also hides its later
cases; rerun the binary with `--gtest_filter` past the aborting case, or use a
`-DERHE_GPU_TESTS_PER_PROCESS=ON` tree (`GPU_MODE=ctest` in
`scripts/run_graphics_tests_metal.sh` configures one) to get every case's
result.

Pick the set by what the change touches. The full suite is for a change to a
shared library that the deviceless suites exercise (`erhe::geometry`,
`erhe::scene`, `erhe::item`, `erhe::math`, ...). A change confined to a
graphics backend (`erhe_graphics/vulkan/`, `metal/`, `metal4/`, `gl/`) or to
`erhe::graphics` itself can only break the suites that bring up a `Device`,
which are exactly the two labels: `ctest -L "gpu|editor" -j1` (the `gpu` label
is four processes, about 30 s on an M4 Pro Debug tree, most of it the scene
renderer suite's shader builds; `mcp_server_tests` about 1 minute). The
deviceless suites cannot see a backend change; the build is their check. For a
change inside one backend subsystem, run that subsystem's cases first
(`erhe_graphics_gpu_tests --gtest_filter='Draw_indirect_test.*'`,
`'Gpu_timer*'`, `'Material_set_gpu_test.*'`, the `Gpu_test.*` image cases) and
the labelled set before the commit. A backend change still needs the image
gate of its backend (byte-identical renders against the reference backend,
`doc/editor/render_thread.md` "Metal") in addition to the tests, since the
goldens cover the test scenes only.

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

## Potentially visible set verification

What each gate checks and the current results are in
[`editor/pvs.md`](editor/pvs.md) "Verification"; the commands:

- **Unit tests** (deviceless): `erhe_scene_tests --gtest_filter=Pvs_database.*`,
  `erhe_gltf_tests --gtest_filter=Gltf_pvs.*` and
  `erhe_scene_renderer_tests --gtest_filter=Pvs_*:Cull_lanes.pvs_*`.
- **Library GPU tests** (label `gpu`): `erhe_scene_renderer_gpu_tests
  --gtest_filter=Pvs_disocclusion_gpu_test.*`.
- **Gates** (headless editor; usage in each docstring):
  `scripts/visible_entries_check.py --editor <editor>` and
  `scripts/pvs_verify.py --editor <editor> --sections
  sampled,wall,heavy,bistro,streaming` (each section can run alone, `wall`
  after `sampled` in the same run; `--trace` buckets the misses with the
  bake trace). They launch their own editor and restore the editor config
  files a run may write.

## Future work

- [plans/graphics_tests.md](plans/graphics_tests.md)
