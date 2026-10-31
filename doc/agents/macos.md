# macOS sessions

Stability: stable

What an agent session on macOS needs beyond `AGENTS.md`.

## Build trees

The OpenGL backend is not supported on Apple platforms; use Metal or Vulkan.
Configure through the `scripts/` wrappers (they enable tests by default):

```bash
scripts/configure_xcode_metal.sh           # Metal backend  -> build_xcode_metal/
scripts/configure_xcode_metal4.sh          # Metal 4 backend -> build_xcode_metal4/
scripts/configure_xcode_vulkan.sh          # Vulkan backend -> build_xcode_vulkan/
scripts/configure_xcode_metal_headless.sh  # headless Metal -> build_xcode_metal_headless/
```

Then build with:

```bash
cmake --build build_xcode_metal  --target editor --config Debug
cmake --build build_xcode_metal4 --target editor --config Debug
cmake --build build_xcode_vulkan --target editor --config Debug
```

Executables land in `<build>/bin/<Config>/`. The headless Metal build is
described in `doc/erhe/metal_headless.md`; the Metal 4 backend (in progress,
built beside Metal 3) in `doc/erhe/metal4_backend.md`.

## Android and Quest builds

The Quest and phone APKs build on macOS through Gradle, not through the
`scripts/` wrappers (those are Windows `.bat` files). The SDK packages a
fresh Android Studio install lacks and the macOS install recipe are in
`doc/android.md` "Toolchain"; the build, install and launch commands in
`doc/quest.md` "macOS and Linux"; device work in `doc/agents/quest.md`.

## Vulkan drivers

The Vulkan build runs on MoltenVK or on Mesa's KosmicKrisp, whichever ICD the
loader picks; select one explicitly for `ctest` or the editor with
`VK_DRIVER_FILES=<ICD directory>/MoltenVK_icd.json` or
`VK_DRIVER_FILES=<ICD directory>/libkosmickrisp_icd.json`. Run both drivers
when verifying Vulkan changes on macOS: they differ in limits
(`maxPerSetDescriptors`), in `VK_KHR_portability_subset` (MoltenVK only) and in
timestamp behavior (`doc/erhe/vulkan_backend.md` "Texture heap" and
"Portability subset", `doc/erhe/graphics.md` `Gpu_timer`).

Run tests and the editor without sourcing the Vulkan SDK's `setup-env.sh`
(the configure script sources it for itself): with that environment, SDL's
Cocoa Vulkan surface creation calls MoltenVK's `vkGetInstanceProcAddr`
directly and every windowed process segfaults at surface creation.

## Building and diagnostics through Xcode

Invoke the **`erhe-macos-xcode`** skill for building and reading diagnostics
through Apple's Xcode MCP server (`mcp__xcode__*`; always call
`XcodeListWindows` first for the `tabIdentifier`; `BuildProject` +
`GetBuildLog` for structured errors; Apple-docs search). It is not a debugger,
cannot select schemes or run destinations, and cannot launch or drive the
editor. When no Xcode window is open, build with the wrappers above.

## ThreadSanitizer

`ERHE_USE_TSAN` works with AppleClang (`doc/building.md`). Configure a
separate tree the way `scripts/configure_xcode_metal.sh` does, with `-B
build_xcode_metal_tsan`, `-DERHE_BUILD_TESTS=OFF` and `-DERHE_USE_TSAN=ON`,
build the `editor` target and run it with the overlap and the pipelining on:

```bash
TSAN_OPTIONS="suppressions=$PWD/scripts/tsan_suppressions.txt log_path=$PWD/logs/tsan history_size=4" \
ERHE_AI_DRIVER=1 ERHE_RENDER_THREAD=1 ERHE_IMGUI_OVERLAP=1 ERHE_FRAME_PIPELINING=1 \
build_xcode_metal_tsan/bin/Debug/editor
```

macOS symbolizes the reports itself, so function-name suppressions
(`race:`) match; Metal's command buffer completion blocks are suppressed
that way (the ordering through `commit` and completion is inside the
uninstrumented framework). Reports land in `logs/tsan.<pid>`; with any
report pending TSan aborts the process at exit (SIGABRT from
`__tsan::finalize`), which is not an editor crash. MCP requests that span
frames (`capture_screenshot` in the windowed build) can exceed the MCP
server's request timeout under TSan's slowdown and reply "Request timed
out" although they complete (the log says "processed").

## Tracy and frame-time measurement

The configure wrappers pass `-DERHE_PROFILE_LIBRARY=none` after `"$@"`, so
a Tracy tree is configured with the wrapper's cmake line run by hand, the
way the ThreadSanitizer tree is: `-B build_xcode_<api>_tracy`,
`-DERHE_PROFILE_LIBRARY=tracy -DERHE_TRACY_CALLSTACK_DEPTH=0
-DERHE_BUILD_TESTS=OFF`, then `cmake --build <tree> --config Release
--target editor`. Build `tracy-capture` and `tracy-csvexport` from a Tracy
checkout at the version `CMakeLists.txt` pins (`capture/` and `csvexport/`,
plain CMake); `scripts/render_thread_bench.py` and
`scripts/tracy_zone_summary.py` find them in `$HOME/git/tracy`.

Unpaced frame times need the headless Metal tree
(`scripts/configure_xcode_metal_headless.sh`): with `force_disable_vsync`
the windowed builds are not display-paced, but every frame waits 1.4-1.8 ms
for a `CAMetalLayer` drawable, in the `swapchain` term on Metal and inside
the presenting submit's `vkQueueSubmit2` on MoltenVK
(`doc/reference/render_thread_measurements_2026_10.md` section 4). Pass
`--config Release` (or `Debug`) to the bench for the Xcode trees' `bin/<config>/`.
GPU cost on the Apple GPU comes from a Metal System Trace (`xcrun xctrace
record --template 'Metal System Trace' --launch -- <editor> ...`, then
`xcrun xctrace export --xpath` of the `metal-gpu-intervals` table) or, per
`Scoped_gpu_zone`, from the Metal backend's Tracy GPU zones (encoder
granularity, `doc/erhe/metal_backend.md` "GPU timers"; `tracy-csvexport -g`
lists them). MoltenVK's timestamp queries do not time render passes on this
GPU.

## Debugging

Invoke the **`erhe-cpp-debugging`** skill: it is the lldb run-book (Python SB
API emitting JSON preferred; crash capture, all-thread hang snapshots,
post-mortem cores). The windowed editor needs a live, awake display
(attaching to an already-running editor is unaffected). Building and launching
the editor to verify a change is self-serve.

For GPU bugs seen on the Metal build, reproduce them on `build_xcode_vulkan`
and use the **`erhe-renderdoc-gpu-debug`** skill (RenderDoc cannot capture
Metal).
