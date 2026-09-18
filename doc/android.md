# Android support

Stability: experimental

The editor builds and runs on Android (arm64-v8a, API 21+, NDK r28). The
graphics backend there is **Vulkan only** - the OpenGL backend is desktop-only.
The APK renders the editor's UI, survives backgrounding and resuming, and logs
to logcat. What is still missing for a usable phone build - touch gestures,
the soft keyboard, DPI scaling, the final package id - is in the plan linked
at the end.

Quest builds on the same pipeline through Gradle product flavors (`mobile` and
`quest`); see `doc/quest.md`. The two documents are maintained together: when
the mobile flavor or the shared core changes here, check whether
`doc/quest.md` needs a matching update, and vice versa.

## Updating SDL

SDL is pinned (CPM `GIT_TAG` via `ERHE_SDL_GIT_TAG` in `CMakeLists.txt`) to a
commit on the **`erhe` branch of the tksuoran/SDL fork**. That branch carries
all of erhe's `android-project/` changes as topic commits on top of upstream
SDL: the erhe `app/build.gradle`, the manifests, `ErheActivity.java`, the
quest flavor manifest, the hwasan wrap script, and the removal of the
template `app/jni/` tree. erhe's `android-project/` directory is a pure
mirror of the branch's `android-project/` at exactly the pinned commit; the
mirror state is recorded in `android-project/SDL_SYNC_COMMIT` and enforced at
configure time (mismatch is a `FATAL_ERROR`).

Rules:

- **Never edit `android-project/` directly in the erhe repo** (the stamp file
  is the one exception). Make the change in the fork's `erhe` branch and sync
  it over; `scripts/update_sdl.py` deletes anything the fork does not have.
- **Never edit `ERHE_SDL_GIT_TAG` by hand**; the script rewrites it together
  with the stamp so the pin and the mirror cannot drift. Drift would let the
  Java shim and the SDL3 C library diverge, which surfaces as an "SDL C/Java
  version mismatch" dialog at app launch.
- The SDL Java shim files (`org/libsdl/app/*.java`) stay byte-identical to
  upstream; erhe's only shim customization is the `ErheActivity` subclass.

To update SDL (assuming the fork is checked out as a sibling `../sdl` with
remotes `origin` = tksuoran/SDL and `upstream` = libsdl-org/SDL):

```bash
git -C ../sdl fetch upstream
git -C ../sdl rebase --onto upstream/main <old-base> erhe   # or: checkout erhe && rebase upstream/main
git -C ../sdl tag erhe-snapshot-<date>
git -C ../sdl push -f origin erhe
git -C ../sdl push origin erhe-snapshot-<date>
py -3 scripts/update_sdl.py
```

The snapshot tag is mandatory: rebasing rewrites the branch, and a
force-push would otherwise leave previously pinned commits unreachable on
GitHub (they get garbage-collected, breaking CPM fetches for older erhe
revisions). The script refuses to pin a commit that is not reachable from
`origin/erhe` or a tag.

After the sync: re-run the configure script (the drift guard re-checks the
pin), rebuild the quest APK, and launch on device - the SDL version check
(`SDLActivity` Java constants vs `nativeGetVersion()`) passes only when shim
and C library really came from the same commit.

## Build wiring

### CMake

The top-level `CMakeLists.txt` has an Android branch in its platform
detection block that sets `ERHE_TARGET_OS_ANDROID`, defines `ERHE_OS_ANDROID`
and sets `VOLK_STATIC_DEFINES` to `VK_USE_PLATFORM_ANDROID_KHR`. Immediately
after it, Android force-locks the option set:

```cmake
if (ERHE_TARGET_OS_ANDROID)
    set(ERHE_GRAPHICS_API "vulkan"  CACHE STRING "" FORCE)
    set(ERHE_WINDOW_LIBRARY   "sdl"     CACHE STRING "" FORCE)
    set(ERHE_RAYTRACE_LIBRARY "bvh"     CACHE STRING "" FORCE)
    set(ERHE_PROFILE_LIBRARY  "none"    CACHE STRING "" FORCE)
    set(ERHE_MALLOC_LIBRARY   "none"    CACHE STRING "" FORCE)
    set(ERHE_USE_ASAN         OFF       CACHE BOOL   "" FORCE)
endif()
```

`ERHE_XR_LIBRARY` is decided per Gradle flavor (see `doc/quest.md`). Embree is
x86-only, so `bvh` is the raytrace backend; mimalloc and Tracy both port to
Android but add linker and network setup and are left off.

### The editor target is a shared library

`src/editor/CMakeLists.txt` emits `libmain.so` on Android (the SDL Java shim
`dlopen`s it) and links the NDK system libraries `log` and `android`:

```cmake
if (ERHE_TARGET_OS_ANDROID)
    add_library(${_target} SHARED)
    set_target_properties(${_target} PROPERTIES OUTPUT_NAME "main")
    target_link_libraries(${_target} PRIVATE log android)
else()
    add_executable(${_target})
endif()
```

`src/editor/main.cpp` includes `<SDL3/SDL_main.h>` so SDL's macro magic
redirects `main` to `SDL_main`, which the Java shim invokes. The header is
header-only and safe on every desktop platform too.

The secondary executables (`net-test`, `example`, `hello_swap`, `hextiles`)
expect a console `main` and are not packaged into the APK, so
`src/CMakeLists.txt` gates them off on Android.

### Gradle to CMake

`android-project/app/build.gradle` points `externalNativeBuild.cmake.path` at
erhe's top-level `CMakeLists.txt` (there is no `jni/` scaffold), and keeps
`abiFilters 'arm64-v8a'`.

The APK needs `org.libsdl.app.SDLActivity` and friends in its Java classpath,
and SDL3 ships them inside the CPM-fetched source. Rather than vendoring them
(which would go stale relative to the C side), Gradle hands CMake a stable
output path through `-DERHE_ANDROID_SDL_JAVA_OUTPUT=${project.buildDir}/sdl-java`
and CMake copies the sources there:

```cmake
if (ERHE_TARGET_OS_ANDROID AND sdl_SOURCE_DIR AND DEFINED ERHE_ANDROID_SDL_JAVA_OUTPUT)
    file(COPY "${sdl_SOURCE_DIR}/android-project/app/src/main/java/org/libsdl"
         DESTINATION "${ERHE_ANDROID_SDL_JAVA_OUTPUT}/org")
endif ()
```

Gradle reads the same path with `java.srcDir` - **`srcDir`, not
`srcDirs +=`**, which would add the path twice and fail with duplicate-class
errors - and makes `compileJavaWithJavac` depend on `externalNativeBuild` so
the files exist before Java compilation runs. Note the CPM package name is
`sdl`, so the source directory variable is `sdl_SOURCE_DIR`, not
`SDL3_SOURCE_DIR`.

### Dependencies that need an Android branch

- **geogram**: the fork ships `VORPALINE_PLATFORM=Android-generic` only, not
  an aarch64-specific variant.
- **Python interpreter**: `find_package(Python3)` on Windows finds the
  Microsoft Store stub, so Gradle resolves the real interpreter with
  `py -3 -c "import sys; print(sys.executable)"` and forwards it as
  `-DPython3_EXECUTABLE=...`.
- **mango**: `source/mango/core/cpuinfo.cpp` includes `<cpu-features.h>` on
  Android, so `src/mango/CMakeLists.txt` compiles
  `${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c` and adds the
  matching include path under `if (ANDROID)`.
- **erhe::net**: `net_os.hpp` and `src/erhe/net/CMakeLists.txt` enable the
  Linux / macOS POSIX socket path on `ERHE_OS_ANDROID` /
  `ERHE_TARGET_OS_ANDROID` as well.

Things that are fine unchanged, and are worth knowing when a build breaks:
simdjson auto-detects NEON on arm64, httplib needs only `<sys/socket.h>`,
`erhe_codegen` runs Python on the *host* during a cross-compile, and the icon
font download runs on the host at configure time.

### Manifest

`android-project/app/src/main/AndroidManifest.xml` declares the library name
SDL3's loader checks and the Vulkan requirements:

```xml
<meta-data android:name="android.app.lib_name" android:value="main" />

<uses-feature android:glEsVersion="0x00020000" android:required="false" />
<uses-feature android:name="android.hardware.vulkan.version"
              android:version="0x00400003" android:required="true" />
<uses-feature android:name="android.hardware.vulkan.level"
              android:version="1" android:required="true" />
```

The activity element names `ErheActivity`, erhe's `SDLActivity` subclass.

### Toolchain

The bundled `gradlew` requires JDK 21 - Gradle 8.12 rejects JDK 25 with
`Unsupported class file major version 69`. Android Studio's bundled `jbr`
ships JDK 21. The wrapper scripts in `scripts/` probe for both `ANDROID_HOME`
and `JAVA_HOME`; see `doc/quest.md` for their invocations.

## Runtime adaptations

### Bootstrap: a writable working directory

`src/editor/main.cpp` chdirs to `SDL_GetAndroidInternalStoragePath()` on
Android before `editor::run_editor()`, so every relative write
(`spirv_cache/...`, generated ini files) lands in `/data/data/<pkg>/files/`.
`erhe::file::ensure_working_directory_contains` is a no-op on Android: the
parent-directory walk is meaningless when assets live in the APK.

`erhe::file::ensure_directory_exists("spirv_cache")` runs before `Spirv_cache`
construction - useful on every platform, mandatory on Android because internal
storage starts empty.

### Asset reads through `SDL_IOFromFile`

`erhe::file::read()` has an Android branch that opens through
`SDL_IOFromFile(path, "rb")`. SDL3 routes paths that do not start with `/` to
AAssetManager, so existing relative paths
(`config/editor/editor_settings.json`, `res/shaders/standard.vert`, ...) keep
working with no pre-extraction step.
`check_is_existing_non_empty_regular_file` has a parallel Android branch using
`SDL_IOFromFile` + `SDL_GetIOSize` instead of `std::filesystem::exists`, which
avoids auditing every caller.

### Logging to logcat

`erhe::log` uses spdlog's `android_sink_mt("erhe")` instead of
`basic_file_sink_mt("logs/log.txt")` on Android: no file I/O for logs, no
`logs/` directory, and output under the logcat tag `erhe`. The rest of the
sink wiring is identical.

### Lifecycle

`Context_window` carries `m_paused` and `m_swapchain_dirty` (plain
`std::atomic<bool>` flags, not a lock-free data structure), and
`Context_window::sdl_event_filter` - attached with `SDL_AddEventWatch` -
handles:

- `SDL_EVENT_WILL_ENTER_BACKGROUND` / `SDL_EVENT_DID_ENTER_BACKGROUND`: set
  `m_paused`.
- `SDL_EVENT_WILL_ENTER_FOREGROUND` / `SDL_EVENT_DID_ENTER_FOREGROUND`: clear
  `m_paused`, set `m_swapchain_dirty`.
- `SDL_EVENT_RENDER_DEVICE_RESET`: set `m_swapchain_dirty`.

`Editor::run()` early-outs while `is_paused()` (`SDL_Delay(50); continue;`)
and calls `Device::recreate_surface_for_new_window()` on
`consume_swapchain_dirty()`.

The handler must live on the event watch, not in the main poll loop: the OS
may suspend the process before a background event reaches the loop.
`SDL_HINT_ANDROID_BLOCK_ON_PAUSE` defaults to `"1"`, so the SDL Java side
blocks `SDLActivity.onPause` and events drain in order before the process is
suspended. The relevant events, and what each one means for the
`ANativeWindow`:

| event | state |
| --- | --- |
| `SDL_EVENT_TERMINATING` | the OS is killing the app; last chance to save state. Watch-thread only. |
| `SDL_EVENT_LOW_MEMORY` | free caches if possible. |
| `SDL_EVENT_WILL_ENTER_BACKGROUND` | about to pause; the `ANativeWindow` is about to become invalid. |
| `SDL_EVENT_DID_ENTER_BACKGROUND` | paused; the surface is invalid. |
| `SDL_EVENT_WILL_ENTER_FOREGROUND` | about to resume; CPU is available, but a new `ANativeWindow` may not exist yet. |
| `SDL_EVENT_DID_ENTER_FOREGROUND` | interactive; a fresh `ANativeWindow` is bound and `SDL_Vulkan_CreateSurface` will succeed. |
| `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` / `SDL_EVENT_WINDOW_RESIZED` | rotation; triggers the standard swapchain rebuild. |
| `SDL_EVENT_RENDER_DEVICE_RESET` | Vulkan device reset (rare on Android). |
| `SDL_EVENT_RENDER_DEVICE_LOST` | unrecoverable. |

### Surface recreate on resume

`Surface_impl::recreate_for_new_window()` (Vulkan; the GL, Metal and null
backends return false from the public
`Device::recreate_surface_for_new_window()`) does:

1. `Device_impl::wait_idle()`, so the GPU no longer touches the old swapchain
   images.
2. `Swapchain_impl::reset_for_new_surface()` - the C++ `Swapchain` (and
   `Swapchain_impl`) instance **stays alive**; only its Vulkan-side state is
   dropped. It recycles per-slot fences and semaphores (including a
   `present_semaphore` left over from a SURFACE_LOST early return), drains
   present-history fences, frees old swapchain garbage, and finally calls
   `vkDestroySwapchainKHR`.
3. `vkDestroySurfaceKHR` - synchronous, because per the Vulkan spec every
   `VkSwapchainKHR` derived from a `VkSurfaceKHR` must be destroyed first.
4. `SDL_Vulkan_CreateSurface` over the freshly bound `ANativeWindow`.
5. `use_physical_device` re-queries surface formats, present modes and
   capabilities against the new `VkSurfaceKHR`.
6. The next `Swapchain_impl::wait_frame` / `init_swapchain` pass creates the
   actual `VkSwapchainKHR`.

**Keeping the `Swapchain` C++ identity stable across the rebuild is what makes
it safe without a separate cache-invalidation pass.** `Render_pass` objects
created against a swapchain - most importantly
`Window_imgui_host::m_render_pass`, but any other swapchain-targeted
`Render_pass` cached anywhere - hold a raw `Swapchain*` inside
`Render_pass_impl::m_swapchain` and dereference it every frame to acquire a
framebuffer. Destroying and re-creating the `unique_ptr<Swapchain>` leaves
those raw pointers dangling, and the driver then dereferences stale state on
the next indirect draw. The `Base_render_pipeline` cache and the bindless
texture heap descriptor set are keyed on format and hold no swapchain-derived
handles, so they survive the rebuild with no explicit invalidation.

During background, `vkQueuePresentKHR` returns `VK_ERROR_SURFACE_LOST_KHR`,
the swapchain marks itself invalid, and the loop logs "Could not obtain
swapchain image, skipping frame" while paused; the resume path then logs
`Surface_impl::recreate_for_new_window() OK`.

### The files this touches

- `src/editor/main.cpp` - chdir on Android.
- `src/erhe/file/erhe_file/file.{hpp,cpp}` - Android `read()` branch, the
  no-op `ensure_working_directory_contains`, the AAssetManager-aware existence
  check.
- `src/erhe/log/erhe_log/log.cpp` - `android_sink_mt`.
- `src/editor/editor.cpp` - `spirv_cache/` creation, the main-loop pause
  check, the `consume_swapchain_dirty()` -> recreate + resize path.
- `src/erhe/window/erhe_window/sdl_window.{hpp,cpp}` - the event filter, the
  two flags and their accessors.
- `src/erhe/graphics/erhe_graphics/vulkan/vulkan_surface.{hpp,cpp}` -
  `Surface_impl::recreate_for_new_window()`.
- `src/erhe/graphics/erhe_graphics/vulkan/vulkan_swapchain.{hpp,cpp}` -
  `Swapchain_impl::reset_for_new_surface()` and the shared
  `release_resources()` helper the destructor and the reset path share.

## Verifying a change on device

```bat
scripts\build_android.bat mobile
adb install -r android-project\app\build\outputs\apk\mobile\debug\app-mobile-debug.apk
adb logcat -c
adb shell am start -n org.libsdl.app/org.libsdl.app.ErheActivity
adb logcat -v time --pid=$(adb shell pidof org.libsdl.app)
```

What to check, top to bottom:

1. The editor's startup UI renders and ImGui windows are visible.
2. Home to background and resume to foreground cycles cleanly: no SIGSEGV, no
   fatal log, `Surface_impl::recreate_for_new_window() OK` on each resume, and
   a stable process PID across cycles.
3. SURFACE_LOST while backgrounded is non-fatal.

## Future work

- [Android: full editor on a phone](plans/android.md) - touch input, the soft
  keyboard, DPI scaling, driver shader issues, the package id.
