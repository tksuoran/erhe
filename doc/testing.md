# Testing

Stability: stable

Unit tests, the editor MCP test suite, the test build trees, and how CI runs
them. Editor-level behavior is verified live through the in-editor MCP server
instead (`doc/agents/editor_runs.md`).

## Suites

Several `erhe::*` libraries have gtest suites under `src/erhe/<name>/test/`
(circular_ring_buffer, codegen, dataformat, geometry, graphics, item, math,
physics, primitive, raytrace, usd), plus `mcp_server_tests` for the editor's
MCP server. `erhe_usd_tests` additionally needs `-DERHE_USD_LIBRARY=lightusd`,
and `erhe_physics_tests` needs a backend that simulates
(`-DERHE_PHYSICS_LIBRARY=jolt`, the default, or `box3d`); a `box3d` tree adds
that backend's own tests to the suite. Each builds an `erhe_<name>_tests`
executable, gated behind `-DERHE_BUILD_TESTS=ON` (default OFF).

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
graphics `Device`: `erhe_graphics_gpu_tests`, `erhe_scene_renderer_gpu_tests`)
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
`ctest --label-exclude "gpu|editor" --output-junit`; the runners have no GPU.
That ctest step does not fail its job, so the build badge stays a build
verdict: the JUnit files are uploaded as `test-results-*` artifacts and
`.github/workflows/tests.yml` (triggered when a build run completes)
summarizes them with `scripts/ci_test_summary.py` and gives the tests badge in
README.md. A build run that did not succeed fails the tests workflow too.
Running the `gpu` tests in CI under a software Vulkan is described in
`doc/erhe/graphics_test_coverage.md`.

## Running

Run via `ctest` from the build directory, or invoke the
`.../bin/<config>/erhe_<name>_tests.exe` binary directly. Run suites serially
and fix one failure at a time -- an abort hides the rest of the run.

## Future work

- [plans/graphics_tests.md](plans/graphics_tests.md)
