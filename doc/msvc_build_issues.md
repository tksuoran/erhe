# MSVC stale objects and ODR chimeras

Stability: experimental

Under the Visual Studio generator an incremental build can link object files
compiled from different vintages of a header. When that header defines a struct
layout, the result is an ODR-violating "chimera" executable in which
translation units disagree about member offsets. There is no source bug to
find: the symptoms are type-impossible behavior at runtime, garbage `this` and
locals in the debugger (the PDB records yet another layout vintage), crashes at
innocent `shared_ptr` teardown sites, and different crashes from unrelated user
actions, because each shifted member misfires in its own way.

The worked example: removing one `Message_bus` member from `App_message_bus`
shifts every later bus down by the size of one `Message_bus` instantiation. A
call site compiled against the old layout then computes a member address that
lands on the wrong bus, so `send_message` of one message type is delivered to
another type's subscribers - a delivery the source makes impossible.

A full object purge plus a full recompile and link fixes it. The ninja trees
(`build_ninja_win_*`) use `/showIncludes` depfile tracking and have no IDE
up-to-date layer, so **a bug that reproduces only in the VS tree is a
build-staleness hint**.

## Mechanisms

1. **MSBuild tracker logs and `/MP` batches.** Under the VS generator,
   incremental C++ builds are driven by per-project tracker logs
   (`CL.read.1.tlog` / `CL.write.1.tlog` in
   `<target>.dir/<config>/<target>.tlog/`). A source recompiles only when the
   tlog says a recorded input is newer than its object. erhe compiles with
   `/MP` (global, `cmake/msvc.cmake`), which batches sources per CL invocation
   and writes tlogs per batch. A build cancelled or aborted mid-batch
   (Ctrl+Break, closing VS, a `/WX` error in a parallel project stopping the
   build) can leave tlog state recording sources as built that never compiled;
   incremental builds then skip them silently.
2. **The VS IDE fast up-to-date check.** The IDE decides per project whether to
   invoke MSBuild at all, using a cruder check than the tlog machinery
   (dotnet/msbuild#4664). Observed on this project: with every object and the
   executable deleted, the IDE build still reported "60 up-to-date" and
   produced nothing. CMake does not emit `DisableFastUpToDateCheck` into the
   generated vcxprojs, and building through
   `cmake --build build_vs2026_vulkan --target editor --config Debug` bypasses
   the check and rebuilds correctly.

Conditions that make it more likely, or make it harder to read once it
happens:

- Git operations (commit, pull, branch switch) while VS is open with cached
  project state.
- `target_precompile_headers(REUSE_FROM erhe_pch)` on every target adds the
  "Copying PDB for PCH reuse" custom step to every project's up-to-date graph.
- Debug incremental linking (`/INCREMENTAL`, off only for ASAN) is what turns
  a chimera executable's callstacks into garbage symbols, from a stale ilk
  over mixed-vintage objects.

## Diagnosis recipe

Use it when a crash stack looks type-impossible.

1. Ask whether two translation units could disagree about a struct layout.
   Compare object timestamps against the last change of the suspect header:
   `git log -1 --format=%ai -- <header>` against the object mtimes under
   `build_vs2026_*/src/editor/editor.dir/<config>/`.
2. Delete `*.obj`, `*.pch`, `*.ipch` and `*.tlog` under the project directory,
   then build with
   `cmake --build build_vs2026_vulkan --target editor --config Debug`. Use the
   CLI build, not the IDE build, whose up-to-date cache lies after a manual
   deletion. Deleting only the exe, pdb and ilk and relinking does not help:
   the stale objects survive, and the fresh PDB picks one layout while the
   runtime code uses another, so the debugger's evaluated values contradict
   the program's behavior.
3. At a breakpoint on a member function's first line, `@rcx` is already
   clobbered by the `/JMC` prologue call, but the x64 home slots survive:
   `[ret_slot+8]` holds the spilled `this` and `[ret_slot+16]` the spilled
   first argument. Comparing the spilled `this` against `&object->member`
   addresses evaluated from the PDB exposes a layout skew directly.

After changing a widely included editor header, or after git operations with
VS open, build through the cmake CLI or rebuild rather than trusting the VS
IDE incremental build.

## References

- https://github.com/dotnet/msbuild/issues/4664 -- VS up-to-date check
  bypasses MSBuild for C++ projects ("All outputs are up-to-date" without
  building); open.
- https://learn.microsoft.com/en-us/visualstudio/msbuild/incremental-builds?view=vs-2022
  -- MSBuild incremental build / tlog mechanism.
- https://devblogs.microsoft.com/cppblog/improved-parallelism-in-msbuild/
  -- `/MP` parallelism; tlog rooting-marker race fixed in VS 16.5.
- https://social.msdn.microsoft.com/Forums/vstudio/en-US/185c4b0a-96d4-41ac-bc00-c370666a54b1/visual-studio-clread1tlog-dependencies-incorrect-if-a-header-incorrectly-marked-as-cc?forum=msbuild
  -- CL.read.1.tlog missing header dependencies.
- https://github.com/microsoft/VSLinux/issues/29 -- header dependencies not
  applied to incremental builds.
- https://developercommunity.visualstudio.com/t/visual-studio-caching-outdated-files-after-switchi/1534915
  and
  https://developercommunity.visualstudio.com/content/problem/519457/vs2019-release-can-get-confused-after-switching-gi.html
  -- stale VS caches after git branch operations.
- https://gitlab.kitware.com/cmake/cmake/-/issues/20721 -- CMake PCH
  REUSE_FROM generator quirks.

## Future work

- [plans/build_tooling.md](plans/build_tooling.md) - the measures that would
  make a stale VS build fail loudly instead of producing a chimera.
