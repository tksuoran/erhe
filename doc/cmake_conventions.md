# CMake conventions and dependencies

Stability: stable

Rules for editing `CMakeLists.txt` files and for adding, bumping or patching
dependencies. The configure options themselves are tabulated in
`doc/building.md`.

## CMake

- Follow modern CMake best practices; use no deprecated CMake features.
- List every source and definition file explicitly in `CMakeLists.txt`. Never
  use `file(GLOB)` or `file(GLOB_RECURSE)` to collect source files - globbing
  does not detect added/removed files without a reconfigure.
- The `erhe_target_sources_grouped()` helper macro (in `cmake/functions.cmake`)
  organizes sources into IDE source groups.
- Each component has its own `CMakeLists.txt`.
- Configure through the `scripts/` wrappers (`doc/building.md`); plans,
  scripts and docs name the wrappers, never `cmake --preset`.
- After a change that affects compile commands (editing a `CMakeLists.txt`,
  adding/removing/renaming source files, adding an `erhe::*` library or
  include directory), refresh the clangd database on Windows
  (`doc/agents/windows.md` "clangd diagnostics vs the real build").
- Many subsystems have swappable backends selected at configure time via
  `#ifdef ERHE_<SUBSYSTEM>_LIBRARY_<VALUE>` guards (physics, raytrace, window,
  XR).
- A new test target registers with the `erhe_tests` aggregate target
  (`doc/testing.md`).

## Dependencies

- External dependencies are fetched at configure time via CPM
  (`cmake/CPM.cmake`).
- **Dependency version pins are not sacred.** A `GIT_TAG` / `VERSION` pin
  exists only to prevent uncontrolled upstream changes from breaking the build
  while nothing in erhe changes - it is not a statement that the pinned
  version is required. If the pinned version has a bug (build break, runtime
  defect, spec non-compliance), bumping to the newest release that fixes it is
  the preferred fix over carrying a downstream patch or fork. Carry a
  downstream change only when no released upstream version fixes it yet.
- **Downstream changes live in a fork, never in a patch file.** CPM's
  `PATCHES` keyword relies on a host `patch` program, which is not portable
  (GitHub Windows CI runners have a broken Strawberry-Perl `patch.exe` 2.5.9
  that asserts on valid diffs). When a dependency genuinely needs a downstream
  change, ask the user to provide a fork repo we control (e.g.
  `tksuoran/<dep>`), apply the change in a dedicated branch of that fork, and
  point `CPMAddPackage(... GITHUB_REPOSITORY tksuoran/<dep> GIT_TAG <commit>)`
  at the patched commit. Record in a comment why the fork exists and which
  upstream version would obsolete it. Precedent: `tksuoran/fastgltf` branch
  `khr_physics_rigid_bodies`.
- **Fork work happens in the dedicated clone, never in `.cpm_cache/`.** The
  CPM source cache is a fetch artifact: commits made there are invisible to
  the fork's real clone, easily lost to a re-fetch, and leave builds silently
  depending on unpushed state. Work in the clone the user has prepared
  (per-machine clone locations are recorded in `memory-bank/local/`; ask the
  user to prepare one when none exists), commit there, then prompt the user to
  arrange branches and perform pushes - never push yourself. Bump the
  `CPMAddPackage` `GIT_TAG` only after the commit is on GitHub.
- Reading third-party sources: CPM fetches every dependency's sources into
  `.cpm_cache/` at configure time; read them there.

## Code generation

- `src/erhe/gl/generate_sources.py` generates the OpenGL wrapper from
  `gl.xml`; run it when updating the GL API bindings (`doc/erhe/gl.md`).
- `src/erhe/codegen/` generates C++ structs with versioned JSON serialization
  from Python definitions (`doc/erhe/codegen.md`), e.g.
  `py -3 src/erhe/codegen/generate.py <definitions_dir> <output_dir>`.
  After a codegen definition change, run the build twice: the first build
  regenerates the structs.
