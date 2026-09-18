# Meta Quest 3 support

Stability: mostly stable

erhe targets Android through the pipeline described in `doc/android.md`. Quest
runs Android too, so the same source tree produces a second APK for the
headset with very little divergence at the C++ level: a Gradle product flavor,
manifest declarations, and OpenXR enabled for that flavor only. The editor
renders immersively on Quest 3 - `xrCreateSession` succeeds, eye swapchains
are allocated, and the per-frame `xrLocateViews` / `xrEndFrame` loop drives
the headset display.

`doc/android.md` and this document are maintained together: when the mobile
flavor or the shared core changes there, check whether this document needs a
matching update, and vice versa.

Background: Meta's
[mobile OpenXR](https://developers.meta.com/horizon/documentation/native/android/mobile-openxr/)
and
[product flavors](https://developers.meta.com/horizon/documentation/android-apps/product-flavors)
documentation.

## Gradle product flavors

`android-project/app/build.gradle` declares a single flavor dimension `device`
with two flavors:

- `mobile` - inherits the namespace as its applicationId (`org.libsdl.app`).
  Forwards `-DERHE_ANDROID_FLAVOR=mobile` to CMake.
- `quest` - `applicationIdSuffix .quest`, so the install-time applicationId is
  `org.libsdl.app.quest`. Forwards `-DERHE_ANDROID_FLAVOR=quest` to CMake.

The `namespace` (`org.libsdl.app`) controls only the Java / R-class package
and is the same for both flavors. The applicationId difference is what lets
both APKs sit side by side on one device.

Both flavors compile the same C++ except for the OpenXR gate below.

## Manifest split

Manifest merging combines `app/src/main/AndroidManifest.xml` with the
flavor-specific manifest at build time.

`app/src/main/AndroidManifest.xml` holds everything valid on both targets:

- `<application>` attributes (label, icon, theme).
- `<meta-data android:name="android.app.lib_name" android:value="main"/>`.
- Vulkan `<uses-feature>` (required) and the OpenGL ES "not required"
  declaration.
- The `<activity android:name="ErheActivity" .../>` element with the LAUNCHER
  and USB_DEVICE_ATTACHED intent-filters.
- The `VIBRATE` permission and the non-required gamepad, bluetooth, USB host,
  mouse and touchscreen `<uses-feature>` lines (Quest accepts these as
  `required="false"`).

`app/src/quest/AndroidManifest.xml` adds the Quest-specific entries:

```xml
<uses-feature android:name="android.hardware.vr.headtracking"
              android:version="1"
              android:required="true" />

<application>
    <meta-data android:name="com.oculus.supportedDevices"
               android:value="quest3|quest3s|quest2|questpro" />
</application>
```

`vr.headtracking required="true"` marks the APK as a VR app for the Horizon
Store. `com.oculus.supportedDevices` whitelists the Meta hardware models
eligible to install it.

There is no `app/src/mobile/AndroidManifest.xml` overlay: mobile inherits
everything from main. Add one when a mobile-only declaration is actually
needed.

**The immersive intent-filter trap.** Horizon only treats an app as
immersive-VR when `android.intent.action.MAIN`,
`android.intent.category.LAUNCHER`, `com.oculus.intent.category.VR` and
(optionally, for forward compatibility)
`org.khronos.openxr.intent.category.IMMERSIVE_HMD` all appear on the *same*
`<intent-filter>` element. Putting the VR / IMMERSIVE_HMD categories on a
separate intent-filter from the LAUNCHER one is silently a no-op: the activity
launches as a 2D panel, the OpenXR session still reaches VISIBLE, VrApi even
reports frame submissions, and the user sees a flat black window in the
Horizon shell instead of an immersive scene. The quest flavor manifest
therefore replaces the activity element wholesale via `tools:node="replace"`,
so LAUNCHER and VR end up on one filter.

## Build, install and launch

Three wrapper scripts cover the common flows. All probe `ANDROID_HOME`
(falling back to `%LOCALAPPDATA%\Android\Sdk`) and `JAVA_HOME` (falling back
to Android Studio's bundled JBR JDK 21), so no environment setup is needed
when Android Studio is installed in the default location.

### Build only (`scripts\build_android.bat`)

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

`build_android.bat` requires the flavor as its first argument and forwards
every remaining argument to gradlew verbatim. Its argument parser drops `=` in
flag values, so pass bare properties like `-Pvulkan_validation_skip` rather
than `-Pvulkan_validation_skip=1`. For a property value that needs an `=`,
invoke `gradlew.bat` directly (after exporting `JAVA_HOME` and
`ANDROID_HOME`).

### Install and launch (`scripts\install_android.bat`)

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
pre-built APK; when the APK does not exist it prints which
`build_android.bat` invocation to run.

### Build, install and launch in one step (`scripts\run_android.bat`)

```bat
:: Drives Gradle's install<Flavor><BuildType> task (compile + package
:: + adb install) then launches via MAIN+LAUNCHER+VR so Horizon enters
:: immersive composition.
scripts\run_android.bat quest
scripts\run_android.bat quest release
scripts\run_android.bat quest release -s <quest-adb-serial>
```

### APK output paths

- `android-project/app/build/outputs/apk/mobile/debug/app-mobile-debug.apk`
- `android-project/app/build/outputs/apk/quest/debug/app-quest-debug.apk`
- `android-project/app/build/outputs/apk/mobile/release/app-mobile-release.apk`
- `android-project/app/build/outputs/apk/quest/release/app-quest-release.apk`

Release APKs are signed with the auto-generated debug keystore so they can be
`adb install`-ed locally for performance testing. That is declared in
`android-project/app/build.gradle` and is **not** suitable for store
submission - replace it with a real `signingConfig` before publishing.

When both a phone and a Quest are attached, pass `-s <serial>` so adb knows
which device to talk to.

## Vulkan validation layer

GPU-side errors on Quest are silent without validation layers loaded: the
editor hangs on the first bad command (descriptor mismatch, image layout
violation, multiview misuse) with no log line, and the device-error abort hook
(the `device_message` lambda in `editor.cpp`; search for
`Message_severity::error`) never fires.

**Bundling (build time).** The Khronos `VK_LAYER_KHRONOS_validation` .so is
fetched from the Vulkan SDK release pinned in
`android-project/app/build.gradle` (`validationLayerVersion`) and staged under
`android-project/app/libs/arm64-v8a/` by the Gradle task
`fetchVulkanValidationLayer`; Gradle's normal `jniLibs` packaging puts it in
the APK. **That task runs by default for every flavor and build type**, and
the download is cached under `build/vulkan-validation-layer-cache/` after the
first run. The .so adds about 10 MB to the APK; pass
`-Pvulkan_validation_skip` to drop it:

```bat
scripts\build_android.bat quest assembleQuestDebug -Pvulkan_validation_skip
```

**Enabling (runtime).** Bundling alone does not slow rendering - the loader
loads the layer only when device init explicitly asks for it. Set the JSON
config knob to ask:

```jsonc
// config/editor/erhe_graphics.json
{
    "vulkan": {
        "vulkan_validation_layers": true,
        ...
    }
}
```

`vulkan_device_init.cpp` enables `VK_LAYER_KHRONOS_validation` only when both
the config knob is true AND the layer is loadable (that is, the .so is in the
APK). When enabled, expect noticeable per-frame CPU overhead from the layer.

**Diagnostic flow.** Validation messages route through the existing
`Device::device_message` callback, which copies the message to the clipboard
and calls `ERHE_FATAL` on `Message_severity::error`. So the first invalid
Vulkan command becomes a loud abort with the offending message in
`adb logcat`.

Leave the config knob off for normal runs. Flip it on when chasing hangs or
visual corruption.

## OpenXR on the quest flavor

### Loader source

The OpenXR loader comes from the OpenXR-SDK already pinned by CPM in
`CMakeLists.txt`; no Maven AAR dependency is added. Its
`src/loader/CMakeLists.txt` builds a fully functional Android loader - the
Android branches link `${ANDROID_LOG_LIBRARY}` and `${ANDROID_LIBRARY}` and
apply `-Wl,-z,max-page-size=16384`, and the JNI plumbing (jnipp and
android-jni-wrappers) is wired into the same `openxr_loader` target on
Android. The CPM options keep the build lean: `BUILD_TESTS OFF`,
`BUILD_API_LAYERS OFF`, `BUILD_CONFORMANCE_TESTS OFF`, `DYNAMIC_LOADER ON`.

### CMake flavor gate

The root `CMakeLists.txt` reads `ERHE_ANDROID_FLAVOR` to decide whether
OpenXR is built:

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

### erhe::xr Android adaptations

`src/erhe/xr/CMakeLists.txt` has an Android branch that adds
`XR_USE_PLATFORM_ANDROID=1` and `XR_USE_GRAPHICS_API_VULKAN=1` to the target's
compile definitions. `target_link_libraries(${_target} PRIVATE openxr_loader)`
works on Android unchanged - the CPM-built target name is the same on every
platform - and `libopenxr_loader.so` is co-bundled into the APK under
`lib/arm64-v8a/`, like `libSDL3.so`.

### Loader init from JNI

OpenXR on Android requires `xrInitializeLoaderKHR` to be called BEFORE any
other OpenXR call, with a populated `XrLoaderInitInfoAndroidKHR` (applicationVM
plus applicationContext). SDL3 exposes `SDL_GetAndroidJNIEnv()` and
`SDL_GetAndroidActivity()`, which provide both; `XrInstanceCreateInfoAndroidKHR`
then chains the same handles into the instance create info. This lives in
`Xr_instance::create` (`src/erhe/xr/erhe_xr/xr_instance.cpp`), guarded by
`#if defined(XR_USE_PLATFORM_ANDROID)`.

### Quest manifest entries for OpenXR

Because the prebuilt Khronos AAR (which would auto-merge a manifest) is not
consumed, the `<queries>` block, the OpenXR permissions and the immersive
intent-filter category are written by hand in
`app/src/quest/AndroidManifest.xml`. The reference text is the AAR's own merge
manifest, `src/loader/AndroidManifest.xml.in` in the OpenXR-SDK source:

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

## Verifying a change on device

Install the APK first, then prompt the user to put the headset on before every
launch; see the `erhe-quest-launch` skill for the full protocol. The rungs to
check, in order:

1. `xrCreateInstance` succeeds on Quest 3.
2. `xrCreateSession` succeeds through the existing
   `Graphics_device::get_native_handles()` Vulkan path, which is already
   platform-clean and does not reach into native window handles.
3. The editor enters an immersive session and renders one frame to each eye
   swapchain through the existing rendergraph.
4. Home and resume cycle cleanly, with a stable process PID across cycles.

A Quest manifest or config change needs a clean reinstall - uninstall first,
not `adb install -r` - because the config is APK-bundled and
`migrate_android_assets_to_writable()` never overwrites an existing config.

## Future work

- [XR](plans/xr.md) - controller render models, prewarm, multiview.
- [Android: full editor on a phone](plans/android.md) - the shared mobile
  flavor's remaining work.
