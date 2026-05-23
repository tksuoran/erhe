# Android Support for erhe editor

## Context

The goal is to bring the erhe editor to Android (arm64-v8a, API 21+, NDK r28).
A skeleton `android-project/` (SDL3 Gradle scaffold) has already been added but
no other porting work is done. The chosen graphics backend on Android is
**Vulkan only** - the OpenGL backend is desktop-only and is not used here.

This is delivered in three phases:

1. **Phase 1 - Build-only** (landed). Produce an APK that builds cleanly.
   Runtime correctness is out of scope.
2. **Phase 2 - Minimum viable launch** (landed). Reach a Vulkan clear +
   ImGui frame on device, survive home/resume.
3. **Phase 3 - Full editor.** Touch/keyboard input, soft-keyboard,
   DPI scaling, package id. Sketched at the bottom.

## Key facts established during exploration

- erhe top-level `CMakeLists.txt` at line 81-101 has `if WIN32 / elseif Linux
  / elseif Darwin / else FATAL_ERROR`. Android is not in the chain, so a
  configure with the NDK toolchain currently fails with "OS not supported".
- The same file already has an Android branch for geogram's
  `VORPALINE_PLATFORM` at line 132 - so geogram should compile.
- SDL is fetched via CPM at version `release-3.4.4`, linked as
  `SDL3::SDL3-static` from `src/erhe/window/CMakeLists.txt:70`. SDL3 has solid
  Android support; no version change needed.
- Volk is the Vulkan loader. `VOLK_STATIC_DEFINES` is set per platform (lines
  85, 90, 94 of top-level CMakeLists). Android needs
  `VK_USE_PLATFORM_ANDROID_KHR`.
- `src/editor/main.cpp` is a 7-line file that calls `editor::run_editor()`.
  It does **not** include `<SDL3/SDL_main.h>`. On Android, SDL's Java shim
  invokes `SDL_main`, which the macro magic in `SDL_main.h` redirects from
  `main`.
- The editor target at `src/editor/CMakeLists.txt:4` is declared as
  `add_executable(editor)`. Android requires it to be a shared library named
  `libmain.so` (the SDL Java shim `dlopen`s it).
- Optional dependencies that are not portable to Android arm64 as-is, and are
  disabled for Phase 1:
  - **OpenXR**: needs a vendor-specific Android loader (Meta/Pico) absent
    from the OpenXR-SDK CPM package.
  - **Embree**: x86-only. Use `bvh` (header-only) on Android.
  - **mimalloc**: ports to Android, but adds linker complexity - defer.
  - **Tracy**: ports to Android, but needs network setup - defer.
- `cmake/JoltPhysicsCompatibility.cmake` at lines 57-66 only passes x86 flags
  inside an `if x86_64 OR AMD64` guard, and the `aarch64` branch is empty. So
  Jolt should compile clean on arm64 NDK clang without modification.
- `android-project/app/build.gradle` currently points externalNativeBuild at
  `jni/CMakeLists.txt`, which contains `add_subdirectory(SDL)` plus
  `add_subdirectory(src)` (the YourSourceHere.c stub). This must be redirected
  to erhe's top-level `CMakeLists.txt`.
- The directory `android-project/app/src/main/java/org/libsdl/` exists but is
  empty - SDL3's Java shim sources (SDLActivity.java and friends) have not
  been copied in. They must be present for the APK to build.

## Phase 1 - Build-Only

### 1.1 Top-level CMake: add Android branch

**File**: `CMakeLists.txt`

In the platform detection block (lines 81-101), add a branch before the
`else FATAL_ERROR`:

```cmake
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Android")
    message("Detected Android platform")
    set(ERHE_TARGET_OS_ANDROID TRUE)
    add_definitions(-DERHE_OS_ANDROID)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_ANDROID_KHR)
```

### 1.2 Force-lock options on Android

**File**: `CMakeLists.txt` (immediately after the platform branch)

```cmake
if (ERHE_TARGET_OS_ANDROID)
    set(ERHE_GRAPHICS_API "vulkan"  CACHE STRING "" FORCE)
    set(ERHE_WINDOW_LIBRARY   "sdl"     CACHE STRING "" FORCE)
    set(ERHE_XR_LIBRARY       "none"    CACHE STRING "" FORCE)
    set(ERHE_RAYTRACE_LIBRARY "bvh"     CACHE STRING "" FORCE)
    set(ERHE_PROFILE_LIBRARY  "none"    CACHE STRING "" FORCE)
    set(ERHE_USE_MIMALLOC     OFF       CACHE BOOL   "" FORCE)
    set(ERHE_USE_ASAN         OFF       CACHE BOOL   "" FORCE)
endif()
```

### 1.3 Editor target: emit `libmain.so` on Android

**File**: `src/editor/CMakeLists.txt`

Replace `add_executable(${_target})` (line 4) with a platform switch:

```cmake
if (ERHE_TARGET_OS_ANDROID)
    add_library(${_target} SHARED)
    set_target_properties(${_target} PROPERTIES OUTPUT_NAME "main")
    target_link_libraries(${_target} PRIVATE log android)
else()
    add_executable(${_target})
endif()
```

`log` and `android` are NDK system libs; SDL3 needs both, and `log` will be
needed by spdlog's android sink in Phase 2.

The companion `net-test` executable at line 748 should be skipped on Android
(it expects a console main):

```cmake
if (NOT ERHE_TARGET_OS_ANDROID)
    set(_target "net-test")
    # ... existing block ...
endif()
```

Other secondary executables in `src/CMakeLists.txt` (`example`,
`hello_swap`, `rendering_test`, `hextiles`) are also gated off on Android
since they expect a console main and are not packaged into the APK:

```cmake
if (NOT ERHE_TARGET_OS_ANDROID)
    add_subdirectory(example)
    add_subdirectory(hello_swap)
    add_subdirectory(rendering_test)
    if (${ERHE_GUI_LIBRARY} STREQUAL "imgui")
        add_subdirectory(hextiles)
    endif ()
endif ()
```

### 1.4 main.cpp: enable SDL3 main hijacking

**File**: `src/editor/main.cpp`

```cpp
#include "editor.hpp"
#include <SDL3/SDL_main.h>

auto main(int, char**) -> int
{
    editor::run_editor();
    return 0;
}
```

`SDL_main.h` is header-only and safe on every desktop platform too.

### 1.5 Wire android-project to erhe's top-level CMake

**File**: `android-project/app/build.gradle`

Change the `externalNativeBuild { cmake { path 'jni/CMakeLists.txt' } }` block
(line 47) to point at erhe root:

```groovy
cmake {
    path '../../CMakeLists.txt'
}
```

Also drop the `ndkBuild { ... }` arm of the `if` (we never use it) and treat
CMake as the only path. The existing `abiFilters 'arm64-v8a'` is correct.

### 1.6 Retire the jni/ scaffold

**File**: `android-project/app/jni/` (entire subtree)

Once build.gradle points at erhe root, the `jni/` subtree is unused. Delete
it: `Android.mk`, `Application.mk`, `CMakeLists.txt`, `src/`. Avoids
confusion later.

### 1.7 Provide SDL3 Java shim

The APK needs `org.libsdl.app.SDLActivity` and friends in its Java classpath.
SDL3 ships these in
`<SDL_SOURCE>/android-project/app/src/main/java/org/libsdl/app/*.java`.
With SDL3 fetched by CPM, that source path is reachable but not auto-injected.

Strategy: Gradle hands CMake a stable output path
(`${project.buildDir}/sdl-java/`) via the `-D` argument
`ERHE_ANDROID_SDL_JAVA_OUTPUT`. CMake's `file(COPY ...)` deposits the SDL3
Java sources there. Gradle's `sourceSets.main.java.srcDirs` then reads from
the same path. Both sides agree on the path, no hashing required. We also
make `compileJavaWithJavac` depend on `externalNativeBuild` so the files
exist before Java compilation runs.

In `CMakeLists.txt`, after SDL3 is fetched by CPM:

```cmake
if (ERHE_TARGET_OS_ANDROID AND sdl_SOURCE_DIR AND DEFINED ERHE_ANDROID_SDL_JAVA_OUTPUT)
    file(COPY "${sdl_SOURCE_DIR}/android-project/app/src/main/java/org/libsdl"
         DESTINATION "${ERHE_ANDROID_SDL_JAVA_OUTPUT}/org")
endif ()
```

In `android-project/app/build.gradle`:

```groovy
defaultConfig.externalNativeBuild.cmake.arguments
    "-DERHE_ANDROID_SDL_JAVA_OUTPUT=${project.buildDir}/sdl-java"

sourceSets.main.java.srcDirs += "${project.buildDir}/sdl-java"

applicationVariants.all { variant ->
    tasks.matching { it.name == "compile${variant.name.capitalize()}JavaWithJavac" }
        .configureEach { dependsOn("externalNativeBuild${variant.name.capitalize()}") }
}
```

The CPM package name is `sdl` (top-level CMakeLists.txt:351), so the source
dir variable is `sdl_SOURCE_DIR`, not `SDL3_SOURCE_DIR`.

Alternative considered and rejected: vendor SDL3 Java sources by `git add` to
`android-project/app/src/main/java/org/libsdl/app/`. Rejected because they go
stale relative to the C side of SDL3.

### 1.8 AndroidManifest.xml: declare lib_name and Vulkan feature

**File**: `android-project/app/src/main/AndroidManifest.xml`

Inside `<application>`, add the meta-data SDL3's loader checks:

```xml
<meta-data android:name="android.app.lib_name" android:value="main" />
```

In `<manifest>`, replace the `<uses-feature android:glEsVersion=...>` (line 8)
with Vulkan requirements:

```xml
<uses-feature android:glEsVersion="0x00020000" android:required="false" />
<uses-feature android:name="android.hardware.vulkan.version"
              android:version="0x00400003" android:required="true" />
<uses-feature android:name="android.hardware.vulkan.level"
              android:version="1" android:required="true" />
```

Leave the `<activity android:name="SDLActivity">` element unchanged; the
package stays as the `org.libsdl.app` placeholder for now.

### 1.9 Things to monitor (not preemptive changes)

- **simdjson**: SIMD selection on arm64 (NEON) usually auto-detects; if not,
  may need `-DSIMDJSON_IMPLEMENTATION_ARM64=ON`.
- **httplib**: needs `<sys/socket.h>`; works on NDK without changes.
- **glslang**: builds host tools that may end up cross-compiled - watch the
  build log; might need to mark them as host. Contingency only.
- **`erhe_codegen`**: runs Python at build time on the *host* during
  cross-compile, not affected by target platform.
- **Icon font download** (`src/editor/CMakeLists.txt:698-720`): runs on the
  host at configure time, also unaffected.

If a dep refuses to build, the appropriate response is to guard it off on
Android and add a TODO for Phase 2 - this is a contingency, not a planned
change.

## Critical files to modify (Phase 1 summary)

- `CMakeLists.txt` - platform branch, locked options, SDL3 Java copy.
- `src/editor/CMakeLists.txt` - shared-library variant, skip net-test.
- `src/editor/main.cpp` - add SDL_main include.
- `android-project/app/build.gradle` - redirect cmake.path, add java.srcDirs.
- `android-project/app/src/main/AndroidManifest.xml` - lib_name + Vulkan
  features.
- `android-project/app/jni/` - delete.

## Verification (Phase 1)

Success criterion: a green build, not a working APK. The build was driven
with:

```bash
ANDROID_HOME="$LOCALAPPDATA/Android/Sdk" \
JAVA_HOME="C:/Program Files/Android/Android Studio/jbr" \
android-project/gradlew.bat -p android-project --no-daemon assembleDebug
```

The bundled `gradlew` requires JDK 21 (Gradle 8.12 does not support JDK 25 -
`Unsupported class file major version 69`). Android Studio's `jbr` ships JDK
21 and was used here.

Achieved on 2026-04-29:
1. `BUILD SUCCESSFUL in 29s`.
2. `android-project/app/build/outputs/apk/debug/app-debug.apk` (~24 MB).
3. APK contains `lib/arm64-v8a/libmain.so` (~66 MB unstripped) plus
   `lib/arm64-v8a/libSDL3.so` (~3 MB).
4. `llvm-nm -D libmain.so` shows `T SDL_main` at the expected offset.

Out of scope for Phase 1: launching the app, rendering a frame, asset
access. Crashes on launch are expected and acceptable.

## Phase 1 - Fixes applied during verification

These were not part of the original Phase 1 plan but were required to reach
a green build:

- **Geogram VORPALINE_PLATFORM**: the fork ships `Android-generic` only,
  not `Android-aarch64-gcc-dynamic`. Top-level `CMakeLists.txt` updated.
- **Python interpreter**: `find_package(Python3)` on Windows hit the
  Microsoft Store stub. Gradle now resolves the real interpreter via
  `py -3 -c "import sys; print(sys.executable)"` and forwards it as
  `-DPython3_EXECUTABLE=...` to CMake.
- **mango cpu-features**: `src/mango/source/mango/core/cpuinfo.cpp` includes
  `<cpu-features.h>` on Android. `src/mango/CMakeLists.txt` now compiles
  `${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c` and adds the
  matching include path on `if (ANDROID)`.
- **erhe::net POSIX guard**: `net_os.hpp` and the matching
  `src/erhe/net/CMakeLists.txt` now also enable the Linux/macOS POSIX
  socket path on `ERHE_OS_ANDROID` / `ERHE_TARGET_OS_ANDROID`.
- **SDL3 Java shim duplicate-class**: using `java.srcDirs += ...` produced
  duplicate-class errors because the same path was added twice. Switched to
  `java.srcDir "${project.buildDir}/sdl-java"` which replaces the default
  for that source set entry.

## Phase 2 - Minimum Viable Launch

Phase 2 goal: get the editor process to launch on a real Android device,
come up far enough to render an ImGui frame, and survive backgrounding /
resuming. The user's strategy is **try the full editor first** with the
minimum set of changes; fall back to simpler targets only if blockers
appear during testing.

User decisions for Phase 2:

- **Asset strategy**: AAssetManager-aware file layer - route asset reads
  through `SDL_IOFromFile` (relative paths route to AAssetManager on
  Android), no pre-extraction.
- **Writable storage**: logs go to **logcat via spdlog's android sink**
  (no file logger on Android); `spirv_cache/` goes to internal storage;
  scene save / config save can fail gracefully on Android.
- **Touch input**: defer to Phase 3.
- **Lifecycle**: extend the existing `SDL_AddEventWatch` at
  `sdl_window.cpp:519` with the Android background/foreground/device-reset
  cases. Skip render while paused. Recreate swapchain on resume.

### 2.1 Bootstrap: writable cwd + skip the cwd walk

`src/editor/main.cpp` chdirs to `SDL_GetAndroidInternalStoragePath()` on
Android before `editor::run_editor()`. This makes every relative write
(`spirv_cache/...`, generated ini files) land in
`/data/data/<pkg>/files/`. `erhe::file::ensure_working_directory_contains`
becomes a no-op on Android - the parent-directory walk is meaningless when
assets live in the APK.

### 2.2 Asset reads via `SDL_IOFromFile`

`src/erhe/file/erhe_file/file.cpp` `read()` adds an Android branch that
opens via `SDL_IOFromFile(path, "rb")`. SDL3 routes paths that don't start
with `/` to AAssetManager on Android. Existing relative paths
(`config/editor/editor_settings.json`, `res/shaders/standard.vert`, ...)
keep working.

`check_is_existing_non_empty_regular_file` gets a parallel Android branch
that uses `SDL_IOFromFile` + `SDL_GetIOSize` instead of
`std::filesystem::exists`. Avoids auditing every caller.

### 2.3 Logging: spdlog `android_sink_mt`

`src/erhe/log/erhe_log/log.cpp` swaps `basic_file_sink_mt("logs/log.txt")`
for `android_sink_mt("erhe")` on Android. No file I/O for logs, no `logs/`
directory needed, output goes to logcat under tag `erhe`. Field renamed
to `m_sink_main` (typed as `spdlog::sinks::sink_ptr`) so the rest of the
sink wiring stays identical.

`spirv_cache/` is still on disk on Android. Add
`erhe::file::ensure_directory_exists("spirv_cache")` before `Spirv_cache`
construction (useful on every platform; mandatory on Android because
internal storage starts empty).

### 2.4 Lifecycle: extend the existing `sdl_event_filter`

`Context_window` gains two `std::atomic<bool>`s: `m_paused{false}` and
`m_swapchain_dirty{false}`. The atomic flags are primitives, not
lock-free data structures, so they don't violate the project's
"no lock-free / atomic techniques" rule for non-trivial synchronization.

`Context_window::sdl_event_filter` (already attached via
`SDL_AddEventWatch` at sdl_window.cpp:519) is extended with:

- `SDL_EVENT_WILL_ENTER_BACKGROUND` / `SDL_EVENT_DID_ENTER_BACKGROUND`:
  set `m_paused = true`.
- `SDL_EVENT_WILL_ENTER_FOREGROUND` / `SDL_EVENT_DID_ENTER_FOREGROUND`:
  clear `m_paused`, set `m_swapchain_dirty`.
- `SDL_EVENT_RENDER_DEVICE_RESET`: set `m_swapchain_dirty`.

`Context_window::is_paused()` and `consume_swapchain_dirty()` are exposed.
`Editor::run()` (editor.cpp:1827) early-outs while paused
(`SDL_Delay(50); continue;`).

`SDL_HINT_ANDROID_BLOCK_ON_PAUSE` defaults to `"1"` in SDL3 - no hint
change needed.

### 2.5 Swapchain recreate on resume / device reset

#### Relevant SDL3 lifecycle events

Confirmed against `SDL3/SDL_events.h` in the pinned SDL CPM source:

- `SDL_EVENT_TERMINATING` - OS is killing the app; last chance to save
  state. Must be observed via `SDL_AddEventWatch` (the main poll loop
  may not run again).
- `SDL_EVENT_LOW_MEMORY` - free caches if possible. Watch-thread.
- `SDL_EVENT_WILL_ENTER_BACKGROUND` - the activity is about to pause.
  Fires synchronously on the SDL event thread; the ANativeWindow is
  about to become invalid. Watch-thread only - the OS may suspend
  the process before this reaches the main poll loop.
- `SDL_EVENT_DID_ENTER_BACKGROUND` - paused; surface is now invalid.
- `SDL_EVENT_WILL_ENTER_FOREGROUND` - about to resume; CPU will be
  available, but the new ANativeWindow may not exist yet.
- `SDL_EVENT_DID_ENTER_FOREGROUND` - interactive again; a fresh
  ANativeWindow is bound and `SDL_Vulkan_CreateSurface` will succeed.
- `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` / `SDL_EVENT_WINDOW_RESIZED` -
  fires on rotation; trigger the standard swapchain rebuild path.
- `SDL_EVENT_RENDER_DEVICE_RESET` - Vulkan device reset (rare on
  Android).
- `SDL_EVENT_RENDER_DEVICE_LOST` - unrecoverable.

`SDL_HINT_ANDROID_BLOCK_ON_PAUSE` defaults to `"1"`. While paused the
SDL Java side blocks `SDLActivity.onPause` so events drain in order
before the OS suspends the process; the watch-thread handler is
responsible for putting native state into a frozen-but-recoverable
shape.

#### Status: surface recreate landed and hot path enabled (2026-04-30)

`Surface_impl::recreate_for_new_window()` is implemented end to end
and wired into `Editor::run`'s `consume_swapchain_dirty()` path on
Android:

1. `Device_impl::wait_idle()` so the GPU is no longer touching the
   old swapchain images.
2. `Swapchain_impl::reset_for_new_surface()` -- the C++ Swapchain
   (and Swapchain_impl) instance stays alive; only its Vulkan-side
   state is dropped. The method recycles per-slot fences and
   semaphores (including any `present_semaphore` left over from a
   SURFACE_LOST early return), drains present-history fences, frees
   old swapchain garbage, and finally calls `vkDestroySwapchainKHR`.
3. `vkDestroySurfaceKHR` (synchronous, since per Vulkan spec all
   VkSwapchainKHRs derived from a VkSurfaceKHR must be destroyed
   first).
4. `SDL_Vulkan_CreateSurface` over the freshly-bound ANativeWindow.
5. `use_physical_device` re-queries surface formats, present modes,
   and capabilities against the new VkSurfaceKHR.
6. The Swapchain object stays the same instance; the next
   `Swapchain_impl::wait_frame` / `init_swapchain` pass creates the
   actual VkSwapchainKHR against the new VkSurfaceKHR.

Hooked through the public `Device::recreate_surface_for_new_window()`
API (Vulkan does the work, GL / Metal / Null backends return false).

The Swapchain identity stability above is what makes the rebuild
safe without a separate cache-invalidation pass. Render_pass objects
created against a swapchain (most importantly
`Window_imgui_host::m_render_pass`, but also any other
swapchain-targeted Render_pass cached anywhere) hold a raw
`Swapchain*` inside `Render_pass_impl::m_swapchain` that they
dereference every frame to acquire a framebuffer. Destroying and
re-creating the unique_ptr<Swapchain> would leave those raw
pointers dangling and the driver would dereference stale state on
the next indirect draw -- the SIGSEGV inside
`vkCmdDrawIndexedIndirect` we observed in the earlier
"hot path disabled" iteration. Keeping the Swapchain (and
Swapchain_impl) C++ identity stable across recreate sidesteps that
class of bug. The `Base_render_pipeline` cache and the bindless
texture heap descriptor set are keyed on format / hold no
swapchain-derived handles, so they survive the rebuild without any
explicit invalidation.

Verified on Galaxy S23 (Snapdragon 8 Gen 2 / Adreno 740): home
button takes the activity to the background
(`vkQueuePresentKHR` returns `VK_ERROR_SURFACE_LOST_KHR`, the
swapchain marks itself invalid, the editor loop logs
"Could not obtain swapchain image, skipping frame" while paused);
returning to foreground triggers
`Surface_impl::recreate_for_new_window()` which logs OK, the next
`wait_frame` builds a fresh VkSwapchainKHR against the new
VkSurfaceKHR, and rendering continues. The cycle was repeated
multiple times in a row with no SIGSEGV, no fatal log, and process
PID stable throughout.

### 2.6 Critical files

- `src/editor/main.cpp` - chdir on Android.
- `src/erhe/file/erhe_file/file.cpp` and `file.hpp` - Android `read()`
  branch; no-op `ensure_working_directory_contains`; AAssetManager-aware
  existence check.
- `src/erhe/log/erhe_log/log.cpp` - `android_sink_mt` on Android.
- `src/editor/editor.cpp` - ensure `spirv_cache/` exists; main-loop
  pause check; on `consume_swapchain_dirty()`, calls
  `Device::recreate_surface_for_new_window()` and sets
  `m_request_resize_pending`.
- `src/erhe/window/erhe_window/sdl_window.cpp` and `.hpp` - extend
  `sdl_event_filter`; add `m_paused`, `m_swapchain_dirty`, getters.
- `src/erhe/graphics/erhe_graphics/vulkan/vulkan_surface.hpp` /
  `.cpp` - `Surface_impl::recreate_for_new_window()`; reuses the
  existing `Swapchain` instance, destroys and re-creates the
  `VkSurfaceKHR`, re-runs `use_physical_device`.
- `src/erhe/graphics/erhe_graphics/vulkan/vulkan_swapchain.hpp` /
  `.cpp` - `Swapchain_impl::reset_for_new_surface()` and the
  shared `release_resources()` helper that the destructor and the
  reset path both call.

### 2.7 Verification ladder

End-to-end on a real device:

```bat
scripts\build_android.bat
adb install -r android-project\app\build\outputs\apk\debug\app-debug.apk
adb logcat -c
adb shell am start -n org.libsdl.app/org.libsdl.app.SDLActivity
adb logcat -v time --pid=$(adb shell pidof org.libsdl.app)
```

Pass criteria, top to bottom (Phase 2 currently passes the top rung
on Galaxy S23 / Adreno 740):

1. **Top**: editor's startup UI renders, ImGui windows visible; home
   button to background and resume to foreground cycles cleanly with
   no SIGSEGV / fatal log; logcat shows
   `Surface_impl::recreate_for_new_window() OK` on each resume and
   stable process PID across cycles.
2. **Mid**: process survives initial launch and reaches a rendered
   ImGui frame; SURFACE_LOST during background is non-fatal.
3. **Floor**: process survives until first frame call without
   crashing; logcat shows clean chdir and `SDL_Vulkan_CreateSurface`
   success.

Lock the target at the highest rung that passes; document the next
blocker as the Phase 3 starting point.

## Phase 3 - sketch only

Touch input mapping (SDL3 already provides synthetic mouse events via
`SDL_HINT_TOUCH_MOUSE_EVENTS=1`; the rendergraph and physics paths are
backend-agnostic and need no changes). Soft-keyboard support. UI
density / DPI scaling. Resolve any shader compilation issues specific to
Android Vulkan drivers. Decide on the final package id (currently the
`org.libsdl.app` placeholder).

## See also: Meta Quest 3 support

Quest 3 support builds on top of this Android pipeline using Gradle
product flavors (`mobile` and `quest`). See `doc/quest.md` for the
full plan, manifest overlays, build/install commands, and OpenXR
roadmap. The two documents are maintained together: when the `mobile`
flavor or the shared core changes here, verify whether `doc/quest.md`
needs a matching update, and vice versa.
