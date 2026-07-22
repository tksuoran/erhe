# MSVC / Visual Studio solution build issues: stale objects and ODR chimeras

Status: analysis only -- prevention measures listed at the end are NOT yet
implemented. Written 2026-07-12 after the 2026-07-11 incident below.

## Incident summary (2026-07-11)

Two "impossible" editor crashes were reported from the `build_vs2026_vulkan`
tree, both freshly built in the VS IDE:

- Clicking the rotate tool in the hotbar crashed with an access violation in
  `std::_Ref_count_base::_Decref` under `Scene_root::remove_browser_window()`,
  reached from `Editor::on_close_scene()` -- invoked, per the callstack, from
  `Tools::set_priority_tool()` sending a `Tool_select_message`. That delivery
  is type-impossible in the source: `close_scene` is a queue-only bus whose
  handler can only run from the `App_message_bus::update()` pump.
- Duplicating a prefab instance crashed in shared_ptr internals elsewhere.

Live debugging (VS MCP, breakpoints + raw stack reads) established that the
callstack was REAL, not a symbol artifact: `Editor::on_close_scene()` executed
with a `Tool_select_message` reinterpreted as `Close_scene_message&` -- the
`scene_root` field contained ASCII text bytes read as pointer/refcounts.
The decisive datum: `send_message`'s spilled `this` (read from the x64 home
slot `[ret_slot+8]`) did not match the PDB's `&app_message_bus->tool_select`;
the call site had computed the member address with a DIFFERENT struct layout
than the object was constructed with, landing on the wrong bus.

## Root cause

Commit `03aecdcc` (2026-07-11 15:37) removed the `open_scene` member from
`App_message_bus`, shifting every later bus member down by 0x190 bytes
(sizeof of one `Message_bus` instantiation). The VS build then failed to
recompile all affected translation units:

- `editor.obj` -- compiled 15:00, BEFORE the header change (old layout)
- `tools.obj`, `hotbar.obj` -- compiled 17:06, AFTER it (new layout)

The linker happily linked this mix into `editor.exe`: an ODR-violating
"chimera" where different TUs disagree about `App_message_bus` member
offsets. Every symptom follows: type-confused message delivery, garbage
`this`/locals in the debugger (the PDB records yet another layout vintage),
crashes at innocent shared_ptr teardown sites, and DIFFERENT crashes from
unrelated user actions (each shifted member misfires in its own way).

There was no source bug. A full object purge + recompile (329 TUs) + full
link fixed both repros.

## Why the VS build tree can get here (mechanisms)

Two documented mechanisms, both observed or strongly indicated:

1. **MSBuild tracker logs + `/MP` batches.** Under the VS generator,
   incremental C++ builds are driven by per-project tracker logs
   (`CL.read.1.tlog` / `CL.write.1.tlog` in `<target>.dir/<config>/<target>.tlog/`).
   A source recompiles only if the tlog says a recorded input is newer than
   its obj. erhe compiles with `/MP` (global, `cmake/msvc.cmake`), which
   batches sources per CL invocation and writes tlogs per batch. A build that
   is CANCELLED or aborted mid-batch (Ctrl+Break, closing VS, a `/WX` error
   in a parallel project stopping the build) can leave tlog state recording
   sources as built that never compiled -- after which incremental builds
   silently skip them. Microsoft fixed some `/MP` tlog races in VS 16.5
   (rooting-marker race), but the failure class persists in reports.

2. **The VS IDE "fast up-to-date check".** The IDE decides per project
   whether to invoke MSBuild at all, using a cruder check than the tlog
   machinery (dotnet/msbuild#4664, still open). Directly observed during the
   incident on this project: with ALL objs and the exe deleted, the IDE build
   still reported "60 up-to-date" and produced nothing. CMake does not emit
   `DisableFastUpToDateCheck` into the generated vcxprojs. Building via
   `cmake --build build_vs2026_vulkan --target editor --config Debug`
   (CLI MSBuild) bypasses this check and rebuilt everything correctly.

Contributing conditions:

- Git operations (commit/pull/branch switch) while VS is open with cached
  project state -- a well-trodden Developer Community failure area (variants
  "fixed in 16.6", folk remedy: delete `.vs/`).
- `target_precompile_headers(REUSE_FROM erhe_pch)` on every target adds the
  "Copying PDB for PCH reuse" custom step to every project's up-to-date
  graph (REUSE_FROM has its own generator-quirk history, CMake #20721).
  Amplifier, not a cause.
- Debug incremental linking (`/INCREMENTAL`, disabled only for ASAN) is what
  produced the garbage-symbol callstacks from the first chimera exe
  (stale ilk + mixed-vintage objs). Amplifier, not a cause.
- The ninja trees (`build_ninja_win_*`) use `/showIncludes` depfile tracking
  and have no IDE up-to-date layer; they did not exhibit the problem. A bug
  that reproduces only in the VS tree is a build-staleness hint.

## Diagnosis recipe (when a crash stack looks type-impossible)

1. Ask: could two TUs disagree about a struct layout? Check obj timestamps
   against the last change of the suspect header:
   `git log -1 --format=%ai -- <header>` vs obj mtimes under
   `build_vs2026_*/src/editor/editor.dir/<config>/`.
2. Deleting only exe/pdb/ilk and relinking does NOT help -- stale objs
   survive, and the fresh PDB picks one layout while runtime code uses
   another (debugger evaluate results will contradict runtime behavior).
3. Purge `*.obj`, `*.pch`, `*.ipch`, `*.tlog` under the project dir, then
   build with `cmake --build build_vs2026_vulkan --target editor --config Debug`
   (NOT the IDE build -- its up-to-date cache lies after manual deletion).
4. Debugger trick that cracked the incident: at a breakpoint on a member
   function's first line, `@rcx` is already clobbered by the `/JMC` prologue
   call, but the x64 home slots survive: `[ret_slot+8]` = spilled `this`,
   `[ret_slot+16]` = spilled first argument. Comparing spilled `this`
   against `&object->member` addresses evaluated from the PDB exposes a
   layout skew directly.

## Prevention options (not yet implemented)

1. **Disable the IDE fast up-to-date check for heavyweight targets**:
   `set_property(TARGET editor PROPERTY VS_GLOBAL_DisableFastUpToDateCheck true)`
   (in `erhe_target_settings()` or just for `editor`). MSBuild's tlog check
   still runs, so builds stay incremental; this only removes the IDE-level
   shortcut that provably lies. Highest value, lowest cost.
2. **ODR canary for the highest-fan-out layout**: give `App_message_bus` a
   `std::size_t m_size_marker{0}` set to `sizeof(App_message_bus)` in its
   constructor (compiled in `app_message_bus.cpp`), and have `Editor` init
   `ERHE_VERIFY(bus->m_size_marker == sizeof(App_message_bus))` (compiled in
   `editor.cpp`). A mixed-layout exe then dies at launch with a clear
   message instead of an undebuggable crash. Both incident crashes went
   through this struct.
3. **Workflow rule** (candidate for AGENTS.md): after changing widely
   included editor headers, or after git operations with VS open, do not
   trust the VS IDE incremental build -- use the cmake CLI build or rebuild.
4. Rejected: dropping `/MP` (build-time cost far exceeds the risk removed);
   abandoning the VS tree (needed for debugging).

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
