---
name: erhe-quest-launch
description: Build, install, and launch the erhe editor APK on a Meta Quest 3 attached over adb. Enforces the headset-prompt protocol (install before asking the user to put the headset on, never launch without acknowledged readiness) and walks through the canonical script invocations.
---

# erhe Quest launch

Build the editor APK, install it on a connected Meta Quest 3, then launch it into immersive VR. The Quest runtime's `RequiresControllersLaunchInterceptor` blocks the immersive app behind a "Controllers Required" dialog if the headset is off the user's head when the launch intent fires, so the order of operations matters.

## Required order

1. **Build** the APK -- no headset interaction needed:
   ```
   scripts\build_android.bat quest
   ```
   APK lands at `android-project\app\build\outputs\apk\quest\debug\app-quest-debug.apk`.

2. **Install** the APK -- still no headset interaction needed. Force-stop first so the install isn't deferred while a previous instance holds the package:
   ```
   $LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe shell am force-stop org.libsdl.app.quest
   scripts\install_android.bat quest
   ```
   Verify by reading `lastUpdateTime` from `adb shell dumpsys package org.libsdl.app.quest`; it should advance with each install.

3. **Prompt the user** to put on the headset and pick up the controllers, then **wait for explicit acknowledgement**. Do not skip this. Pure builds and installs (no `am start`) do not need the prompt; only the launch step does.

4. **Launch** only after the user acknowledges:
   ```
   $LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe logcat -c
   $LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe shell am start -n org.libsdl.app.quest/org.libsdl.app.ErheActivity
   ```
   Clearing logcat first makes post-launch diagnosis cleaner.

## Variants

- One-shot build + install + launch via Gradle's `installQuestDebug`:
  ```
  scripts\run_android.bat quest
  ```
  Same headset-prompt rule applies before invoking it (`run_android.bat` ends with `am start -W -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -c com.oculus.intent.category.VR ...` so the Horizon shell enters immersive composition).

- Force a fresh on-device SPIR-V cache (e.g. after a glslang patch) by uninstalling first instead of `force-stop` + install:
  ```
  $LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe uninstall org.libsdl.app.quest
  scripts\install_android.bat quest
  ```
  Uninstall wipes `/data/data/org.libsdl.app.quest/files/spirv_cache/` so the next launch re-compiles every shader.

- Pass-through gradle properties: build_android.bat forwards extra args verbatim, but its bat-arg parser strips `=`. Pass bare flags like `-Pvulkan_validation_skip` rather than `-Pvulkan_validation_skip=1`. For values with `=`, invoke `gradlew.bat` directly with `JAVA_HOME` and `ANDROID_HOME` set.

## Locating the Android tools

The wrapper scripts probe in this order:
- `ANDROID_HOME` env var, falling back to `%LOCALAPPDATA%\Android\Sdk`. Adb is `<ANDROID_HOME>\platform-tools\adb.exe`.
- `JAVA_HOME` env var (only needed for direct gradle calls), falling back to `C:\Program Files\Android\Android Studio\jbr` (Android Studio's bundled JDK 21; Gradle 8.12 + AGP do not support JDK 25).

In bash on Windows, the canonical adb path is `"$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"`.

## Pulling logs after launch

The editor logs under tag `erhe`. To pull what just happened:

```
$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe logcat -d -v time | grep -E "erhe|FATAL|Abort message|Creating shader module|VALIDATION ERROR|Headset_view: multiview"
```

When chasing a hang, watch for `Headset_view: multiview render path enabled` (init reached the headset path) and the absence/presence of frame-period `am start`-following logs to decide whether the editor is actually rendering.
