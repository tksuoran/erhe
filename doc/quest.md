# Meta Quest 3 Support for erhe editor

## Context

erhe already targets Android via the pipeline described in
`doc/android.md`. That pipeline produces a single APK aimed at a regular
Android phone (Vulkan, SDL3, namespace `org.libsdl.app`, arm64-v8a).
Meta Quest 3 runs Android too, so the same source tree can produce a
second APK that targets the Quest with very little divergence at the
C++ level - the differences are mostly manifest declarations and an
OpenXR runtime that comes later.

This document is the Quest-specific companion to `doc/android.md`. It
is delivered in two phases that pick up the numbering after Android's
phase 3 (sketch):

1. **Phase 4 - Build flavors, no OpenXR (landed).** Introduce a Gradle
   `device` flavor dimension with `mobile` and `quest` flavors. Both
   flavors compile bit-for-bit identical C++; Quest only differs in
   AndroidManifest entries and applicationId so the two APKs can
   coexist on a device. Goal: a Quest APK that installs and launches
   on Quest 3 in the Horizon flat 2D shell.
2. **Phase 5 - OpenXR runtime on Quest (sketched).** Wire OpenXR into
   the `quest` flavor only. Loader comes from the OpenXR-SDK package
   already CPM-fetched for desktop use - no Maven AAR is added.
   `mobile` is unaffected.

`doc/android.md` and this document are maintained together. When the
mobile flavor or the shared core changes there, check whether this
document needs a matching update, and vice versa.

Background documentation:

- https://developers.meta.com/horizon/documentation/native/android/mobile-openxr/
- https://developers.meta.com/horizon/documentation/android-apps/product-flavors

## Phase 4 - Build flavors, no OpenXR

### 4.1 Gradle product flavors

`android-project/app/build.gradle` declares a single flavor dimension
`device` with two flavors:

- `mobile` - inherits the namespace as its applicationId
  (`org.libsdl.app`). Forwards `-DERHE_ANDROID_FLAVOR=mobile` to CMake.
- `quest` - applicationIdSuffix `.quest` so the install-time
  applicationId becomes `org.libsdl.app.quest`. Forwards
  `-DERHE_ANDROID_FLAVOR=quest` to CMake.

The `namespace` (`org.libsdl.app`) controls only the Java/R-class
package and is unchanged across flavors. The applicationId difference
is what lets both APKs sit side-by-side on the same device.

The forwarded `ERHE_ANDROID_FLAVOR` argument is currently unused by
CMake; it is the hook Phase 5 reads to enable OpenXR.

### 4.2 Manifest split

Manifest merging combines `app/src/main/AndroidManifest.xml` with the
flavor-specific manifest (if any) at build time.

`app/src/main/AndroidManifest.xml` holds everything that is valid on
both targets:

- `<application>` attributes (label, icon, theme).
- `<meta-data android:name="android.app.lib_name" android:value="main"/>`.
- Vulkan `<uses-feature>` (required) and the OpenGL ES "not required"
  declaration.
- The `<activity android:name="ErheActivity" .../>` element with the
  LAUNCHER and USB_DEVICE_ATTACHED intent-filters.
- The `VIBRATE` permission and the various non-required gamepad,
  bluetooth, USB host, mouse, and touchscreen `<uses-feature>` lines
  (Quest accepts these as `required="false"`).

`app/src/quest/AndroidManifest.xml` adds Quest-specific entries:

```xml
<uses-feature android:name="android.hardware.vr.headtracking"
              android:version="1"
              android:required="true" />

<application>
    <meta-data android:name="com.oculus.supportedDevices"
               android:value="quest3|quest3s|quest2|questpro" />
</application>
```

`vr.headtracking required="true"` marks the APK as a VR app for the
Horizon Store even before OpenXR is wired up. `com.oculus.supportedDevices`
whitelists the Meta hardware models eligible to install the APK.

There is no `app/src/mobile/AndroidManifest.xml` overlay yet - mobile
inherits everything from main. Add one when a mobile-only declaration
is actually needed.

### 4.3 Build / install commands

The three wrapper scripts cover the common flows. All probe
`ANDROID_HOME` (falling back to `%LOCALAPPDATA%\Android\Sdk`) and
`JAVA_HOME` (falling back to Android Studio's bundled JBR JDK 21);
no environment setup is needed if Android Studio is installed in the
default location.

#### Build only (`scripts\build_android.bat`)

```bat
:: Debug builds (default)
scripts\build_android.bat mobile
scripts\build_android.bat quest

:: Release builds (debug-keystore signed; for local performance testing)
scripts\build_android.bat mobile assembleMobileRelease
scripts\build_android.bat quest  assembleQuestRelease

:: Clean rebuild
scripts\build_android.bat quest clean assembleQuestDebug

:: Pass-through gradle properties
scripts\build_android.bat quest assembleQuestDebug -Pvulkan_validation_skip
```

`build_android.bat` requires the flavor as its first argument and
forwards every remaining argument to gradlew verbatim. Note that the
bat script's argument parser drops `=` in flag values; pass bare
properties like `-Pvulkan_validation_skip` rather than
`-Pvulkan_validation_skip=1`. For property values that need an `=`,
invoke `gradlew.bat` directly (after exporting `JAVA_HOME` and
`ANDROID_HOME`).

#### Install + launch (`scripts\install_android.bat`)

```bat
:: Install onto a connected device (passes -r -g -d to adb install)
scripts\install_android.bat mobile debug
scripts\install_android.bat quest  debug
scripts\install_android.bat quest  release

:: Install + launch
scripts\install_android.bat quest debug run

:: Target a specific device when multiple are attached
scripts\install_android.bat quest debug run -s <quest-adb-serial>
```

Build type defaults to `debug`. `install_android.bat` only consumes a
pre-built APK; if the APK does not exist it prints which
`build_android.bat` invocation to run.

#### Build + install + launch in one step (`scripts\run_android.bat`)

```bat
:: Drives Gradle's install<Flavor><BuildType> task (compile + package
:: + adb install) then launches via MAIN+LAUNCHER+VR so Horizon enters
:: immersive composition.
scripts\run_android.bat quest
scripts\run_android.bat quest release
scripts\run_android.bat quest release -s <quest-adb-serial>
```

#### APK output paths

- `android-project/app/build/outputs/apk/mobile/debug/app-mobile-debug.apk`
- `android-project/app/build/outputs/apk/quest/debug/app-quest-debug.apk`
- `android-project/app/build/outputs/apk/mobile/release/app-mobile-release.apk`
- `android-project/app/build/outputs/apk/quest/release/app-quest-release.apk`

Release APKs are signed with the auto-generated debug keystore so they
can be `adb install`-ed locally for performance testing. This is
declared in `android-project/app/build.gradle` and is **not** suitable
for store submission - replace with a real `signingConfig` before
publishing.

When both a phone and a Quest are attached at the same time, pass
`-s <serial>` so adb knows which device to talk to.

### 4.3.1 Vulkan validation layer

GPU-side errors on Quest are silent without validation layers loaded,
so the editor hangs on the first bad command (descriptor mismatch,
image layout violation, multiview misuse) with no log line and the
device-error abort hook (the `device_message` lambda in `editor.cpp`;
search for `Message_severity::error`) never fires. To make
those failures loud:

**Bundling (build-time).** The Khronos `VK_LAYER_KHRONOS_validation`
.so is fetched from the Vulkan SDK release pinned in
`android-project/app/build.gradle` (`validationLayerVersion`) and
staged under `android-project/app/libs/arm64-v8a/` by the Gradle task
`fetchVulkanValidationLayer`. From there Gradle's normal `jniLibs`
packaging puts it in the APK. **This task runs by default for every
flavor and build type**; the download is cached under
`build/vulkan-validation-layer-cache/` after the first run. The .so
adds ~10 MB to the APK; pass `-Pvulkan_validation_skip` to drop it:

```bat
scripts\build_android.bat quest assembleQuestDebug -Pvulkan_validation_skip
```

**Enabling (runtime).** Bundling alone does not slow rendering -
the loader only loads the layer when the C++ device init explicitly
asks for it. Set the JSON config knob to ask for it:

```jsonc
// config/editor/erhe_graphics.json
{
    "vulkan": {
        "vulkan_validation_layers": true,
        ...
    }
}
```

`vulkan_device_init.cpp` enables `VK_LAYER_KHRONOS_validation` only
when both the config knob is true AND the layer is loadable (i.e.
the .so is in the APK). When enabled, expect noticeable per-frame
CPU overhead from the layer.

**Diagnostic flow.** Validation messages route through the existing
`Device::device_message` callback (the lambda registered in
`editor.cpp`; search for `Message_severity::error`), which copies the message to the clipboard and
calls `ERHE_FATAL` on `Message_severity::error`. So the first
invalid Vulkan command becomes a loud abort with the offending
message in `adb logcat` and on the host clipboard.

Leave the config knob off for normal runs. Flip it on when chasing
hangs or visual corruption.

### 4.4 Phase 4 pass criterion

Quest APK installs on a Quest 3 with developer mode + USB debugging
enabled, the activity reaches its first ImGui frame inside the
Horizon flat 2D panel, and the activity stays alive across home /
resume. Rendering may look squashed because there is no immersive mode
yet - that is fine. Phase 5 brings the immersive path.

### 4.5 What deliberately does NOT change in Phase 4

- `CMakeLists.txt` still force-locks `ERHE_XR_LIBRARY=none` on
  Android. Phase 5 changes that conditional.
- `src/erhe/xr/CMakeLists.txt` is untouched. No `XR_USE_PLATFORM_ANDROID`
  define yet.
- `src/editor/main.cpp` has no flavor-aware behavior.
- The SDL Java shim under `app/src/main/java/org/libsdl/app/` is
  shared by both flavors.

## Phase 5 - OpenXR runtime on Quest (landed)

Status: editor renders immersively on Quest 3.

The activity launches into Horizon's immersive composition path,
`xrCreateSession` succeeds, eye swapchains are allocated, and the
editor's per-frame xrLocateViews / xrEndFrame loop drives the headset
display.

Subtle gotcha worth recording: Horizon only treats an app as
immersive-VR when `android.intent.action.MAIN`,
`android.intent.category.LAUNCHER`,
`com.oculus.intent.category.VR`, and (optionally for forward
compatibility) `org.khronos.openxr.intent.category.IMMERSIVE_HMD` all
appear on the *same* `<intent-filter>` element. Adding the VR /
IMMERSIVE_HMD categories to a separate intent-filter from the
LAUNCHER one is silently a no-op -- the activity launches as a 2D
panel, OpenXR session still reaches VISIBLE, and `VrApi` even reports
~35 fps frame submissions, but the user sees a flat black window in
the Horizon shell rather than an immersive scene. The quest flavor
manifest replaces the activity element wholesale via
`tools:node="replace"` so that LAUNCHER and VR end up on one filter.

### 5.1 Loader source

We reuse the OpenXR-SDK already pinned by CPM in `CMakeLists.txt`
(KhronosGroup/OpenXR-SDK-Source @ release-1.1.58). Its
`src/loader/CMakeLists.txt` builds a fully-functional Android loader -
the Android branches link `${ANDROID_LOG_LIBRARY}` and
`${ANDROID_LIBRARY}` and apply `-Wl,-z,max-page-size=16384`, and the
JNI plumbing (jnipp + android-jni-wrappers) is wired into the same
`openxr_loader` target on Android. No Maven AAR dependency is added.

Optional CPM options to keep build size lean:

```cmake
"BUILD_TESTS OFF"
"BUILD_API_LAYERS OFF"
"BUILD_CONFORMANCE_TESTS OFF"
"DYNAMIC_LOADER ON"
```

### 5.2 CMake flavor gate

`CMakeLists.txt` used to force `ERHE_XR_LIBRARY=none` on Android
unconditionally. Phase 5 turned this into a per-flavor conditional
(now landed; see the `ERHE_ANDROID_FLAVOR` block in the root
`CMakeLists.txt`):

```cmake
if (ERHE_TARGET_OS_ANDROID)
    set(ERHE_GRAPHICS_API "vulkan" CACHE STRING "" FORCE)
    set(ERHE_WINDOW_LIBRARY   "sdl"    CACHE STRING "" FORCE)
    if (ERHE_ANDROID_FLAVOR STREQUAL "quest")
        set(ERHE_XR_LIBRARY "openxr" CACHE STRING "" FORCE)
    else()
        set(ERHE_XR_LIBRARY "none"   CACHE STRING "" FORCE)
    endif()
    # ... other forces unchanged ...
endif()
```

### 5.3 erhe::xr Android adaptations

`src/erhe/xr/CMakeLists.txt` gains an Android branch that adds
`XR_USE_PLATFORM_ANDROID=1` and `XR_USE_GRAPHICS_API_VULKAN=1` to the
target's compile definitions.

The existing `target_link_libraries(${_target} PRIVATE openxr_loader)`
line works on Android unchanged - the CPM-built target name is the
same on every platform. `libopenxr_loader.so` ends up co-bundled into
the APK under `lib/arm64-v8a/`, just like `libSDL3.so` is today.

### 5.4 Loader init from JNI

OpenXR on Android requires `xrInitializeLoaderKHR` to be called BEFORE
any other OpenXR call, with a populated `XrLoaderInitInfoAndroidKHR`
struct (applicationVM + applicationContext). SDL3 exposes
`SDL_GetAndroidJNIEnv()` and `SDL_GetAndroidActivity()` which provide
both. Then `XrInstanceCreateInfoAndroidKHR` chains the same handles
into the instance create info.

This goes into `Xr_instance::create` in
`src/erhe/xr/erhe_xr/xr_instance.cpp`, guarded by
`#if defined(XR_USE_PLATFORM_ANDROID)`.

### 5.5 Quest manifest entries for OpenXR

Because we are not consuming the prebuilt Khronos AAR (which would
auto-merge a manifest), the `<queries>` block, the OpenXR permissions,
and the immersive intent-filter category have to be added to
`app/src/quest/AndroidManifest.xml` by hand. The reference text is
the AAR's own merge manifest at
`<openxr-sdk-cpm>/src/loader/AndroidManifest.xml.in`:

```xml
<uses-permission android:name="org.khronos.openxr.permission.OPENXR" />
<uses-permission android:name="org.khronos.openxr.permission.OPENXR_SYSTEM" />

<queries>
    <provider android:authorities="org.khronos.openxr.runtime_broker;org.khronos.openxr.system_runtime_broker" />
    <intent>
        <action android:name="org.khronos.openxr.OpenXRRuntimeService" />
    </intent>
    <intent>
        <action android:name="org.khronos.openxr.OpenXRApiLayerService" />
    </intent>
</queries>

<application>
    <activity android:name="ErheActivity" tools:node="merge">
        <intent-filter>
            <category android:name="org.khronos.openxr.intent.category.IMMERSIVE_HMD" />
        </intent-filter>
    </activity>
</application>
```

### 5.6 Phase 5 verification ladder

1. `xrCreateInstance` succeeds on Quest 3.
2. `xrCreateSession` succeeds via the existing
   `Graphics_device::get_native_handles()` Vulkan path (already
   platform-clean - Phase 5 does not need to reach into native window
   handles itself).
3. The editor enters an immersive session and renders one frame to
   each eye swapchain via the existing rendergraph.
