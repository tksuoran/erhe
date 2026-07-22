---
name: erhe-quest-launch
description: Build, install, and launch the erhe editor APK on a Meta Quest 3 attached over adb. Enforces the headset-prompt protocol (install before asking the user to put the headset on, never launch without acknowledged readiness; batch/soak runs confirm once up front) and walks through the canonical script invocations. Also covers the persistent logcat capture (scripts/quest_logcat.sh -- start it BEFORE every launch), filtering captures to erhe-only lines, readiness signals (never blind-sleep), Android SDK/JDK locations, and the mobile (non-Quest) Android sideload security-check dialog.
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

3. **Prompt the user** to put on the headset and pick up the controllers, then **wait for explicit acknowledgement**. Do not skip this. Pure builds and installs (no `am start`) do not need the prompt; only the launch step does. A confirmation applies ONLY to that one launch -- every later one-off launch needs a fresh prompt; never assume the user is still wearing the headset.

   **Batch / soak runs are the exception**: a single headset-readiness confirmation at the START of the batch covers the whole unattended sweep (clean reinstall + launch + watch per iteration) -- do NOT prompt between iterations. If the batch detects the headset went off-head mid-run (repeated INCONCLUSIVE results / no ticks), stop and re-confirm rather than silently burning iterations.

4. **Launch** only after the user acknowledges -- and ALWAYS start the persistent logcat capture first. The in-memory logcat ring rolls over within seconds once the editor enters a per-frame error loop (e.g. an `XR_FRAME_DISCARDED` storm), so only a disk capture keeps the full startup trace:
   ```
   bash scripts/quest_logcat.sh             # prints the new capture path
   $LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe shell am start -n org.libsdl.app.quest/org.libsdl.app.ErheActivity
   ```
   (Optionally `adb logcat -c` before starting the capture for a clean ring.)

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

## Persistent logcat capture: scripts/quest_logcat.sh

Capture files land in `logs/quest_<YYYYMMDD>_<HHMMSS>.log` (gitignored) and are appended live by the streamer -- `tail -F` / `grep` them while the editor runs. To stop a capture explicitly (e.g. when wrapping up a session with no further launches planned): `bash scripts/quest_logcat.sh stop`. If you forget, the next launch's start call cleans it up automatically.

The script enforces these invariants -- understand them before changing it:

- **One extra process, ever.** The single backgrounded `adb logcat` is the only process owned by this workflow. Re-running `quest_logcat.sh` kills the previous streamer before starting a new one; `stop` kills without starting a replacement. The shared `adb` server daemon is not "ours" -- it persists across sessions regardless.
- **No leaks across Claude Code sessions.** The PID is tracked in `logs/.logcat.pid` (a real file in the repo, gitignored), so even a fresh shell finds and stops the prior capture.
- **No PID-recycle hazard.** Before killing, the script confirms the recorded PID still points at an `adb` process via `ps -p PID`; if the PID was recycled to something unrelated, it leaves the new owner alone and just removes the stale PID file. Git Bash's `ps` reports the executable path only (no argv), so it matches on `adb` -- fine because every other adb call in this workflow is short-lived.

## Filtering a capture to erhe-only lines

Raw captures interleave hundreds of system tags (`MRSS`, `[CT]`, `wlan`, ...); a typical 30 s capture is 30k+ lines -- too much to scan. Filter to the editor's `erhe` spdlog tag:

```
bash scripts/quest_logcat.sh filter logs/quest_<YYYYMMDD>_<HHMMSS>.log
# writes logs/quest_<YYYYMMDD>_<HHMMSS>_erhe.log
```

The filter is a single grep on ` erhe +:` (space, literal tag, optional padding, colon) anchored to logcat's threadtime tag column, so it does not match the substring "erhe" inside other tags or message bodies. Always analyze the filtered file; reach for the unfiltered capture only to correlate against OS-level events (`OpenXR`, `VrApi`, `MRSS`, `[CT]` tags from the Horizon runtime, or `WLAN`/battery for thermal-throttle context).

## Readiness signals (NEVER blind-sleep before capturing)

The editor takes ~15-20 s to initialize on Quest and the exact time varies per run. `sleep N` before looking once wastes runs: too short misses the window, too long and the user may have removed the headset or exited. ALWAYS poll/stream for a definite readiness signal and act the instant it appears. Pick the signal that matches the state you need:

- **XR session up / projection depth submitted**: poll the live disk capture (from `scripts/quest_logcat.sh`) for a stable session line, e.g. `OpenXR submitted projection depth` or `OpenXR multiview depth swapchain`. Use the disk file, not the in-memory ring, so the line is not evicted before you grep it.
- **Editor actively rendering AND a specific compositor layer present** (e.g. the HUD quad is open): enable the compositor layer dump and stream it through a watcher that exits the instant the editor's own block contains the layer you need. This is the robust trigger for any compositor-layer / depth-submission diagnostic, and the captured block itself carries the data (per-layer `Depth SwapChain` valid vs `BAD`, and `Depth Info min/max/near/far`):

```bash
ADB="$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"
"$ADB" shell setprop debug.oculus.logLayers 1   # enable per-frame layer dump (CompositorClient tag)
# Stream the dump; exit the moment OUR app's block contains a QUAD (HUD open).
# Replace "Type QUAD" with the condition you need; raise timeout for slow init.
timeout 150 "$ADB" logcat -s CompositorClient | awk '
  /LogLayers: Client/ {
    if (cap && hit) { printf "%s", buf; found=1; exit }
    cap = (index($0, "org.libsdl.app.quest") > 0); buf=""; hit=0
  }
  cap { buf = buf $0 "\n"; if (index($0,"Type QUAD")>0) hit=1 }
  END { if (!found) print "[watcher] target block not seen (editor not rendering / HUD not open / headset off-head)" }
'
"$ADB" shell setprop debug.oculus.logLayers 0   # ALWAYS turn the flood back off when done
```

Two gotchas, both learned the hard way:

- **`logLayers` floods logcat** at compositor framerate, so the editor's `erhe`-tagged startup lines are evicted from the in-memory ring within seconds. While `logLayers` is on, do NOT use `adb logcat -d -s erhe` to detect readiness -- use the streaming watcher above (event-driven) or the persistent disk capture.
- **An off-head headset pauses the OpenXR session**: the app stops submitting and only `com.oculus.vrshell` (`FocusPlaceholderActivity`) appears in the dump / as the resumed activity. So "only vrshell in the dump" means "headset off-head or app backgrounded," NOT "editor broken." The editor being `topResumedActivity` is necessary but not sufficient -- it must also be submitting frames (its block present in the dump).

## Mobile (non-Quest) Android: sideload security-check dialog

When running the `mobile` flavor on a regular Android phone (e.g. soaking with `scripts/soak_quest.py --flavor mobile`, or any sideloaded APK), Play Protect / Samsung Auto Blocker pops a "Send app for a security check?" dialog on each `adb install`. It blocks the install until dismissed, which stalls and can crash an unattended clean-reinstall soak. **Always disable it before a mobile Android run**, and restore it afterward (these are global settings):

```bash
ADB="$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"
# disable for the soak:
"$ADB" -s <serial> shell settings put global verifier_verify_adb_installs 0
"$ADB" -s <serial> shell settings put global upload_apk_enable 0
"$ADB" -s <serial> shell settings put global package_verifier_user_consent -1
# restore afterward:
"$ADB" -s <serial> shell settings put global verifier_verify_adb_installs 1
"$ADB" -s <serial> shell settings put global upload_apk_enable 1
"$ADB" -s <serial> shell settings put global package_verifier_user_consent 1
```

If the dialog still appears, it is Samsung Auto Blocker rather than Play Protect: toggle it off in Settings > Security and privacy > Auto Blocker. (The Quest never shows this dialog, but the `mobile` editor cannot run on the Quest anyway -- the Quest sleeps flat 2D activities and the editor then SIGSEGVs at Vulkan surface creation; soak the `mobile` editor on a phone instead. Alternatively, `scripts/soak_quest.py batch --no-reinstall` relaunches the already-installed app each iteration, dodging the install dialog entirely.)

## Locating the Android tools

The wrapper scripts probe in this order:
- `ANDROID_HOME` env var, falling back to `%LOCALAPPDATA%\Android\Sdk`. Adb is `<ANDROID_HOME>\platform-tools\adb.exe`.
- `JAVA_HOME` env var (only needed for direct gradle calls), falling back to `C:\Program Files\Android\Android Studio\jbr` (Android Studio's bundled JDK 21; Gradle 8.12 + AGP do not support JDK 25).

In bash on Windows, the canonical adb path is `"$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"`.

Prefer the wrapper scripts (`build_android.bat`, `install_android.bat`, `run_android.bat`) when possible -- they handle both probes plus device-state checks. Drop to direct adb/gradle/java invocations only when a script does not cover the case (e.g. a `-P` gradle property the bat-arg parser mangles; see Variants above).

## Pulling logs after launch

The editor logs under tag `erhe`. To pull what just happened:

```
$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe logcat -d -v time | grep -E "erhe|FATAL|Abort message|Creating shader module|VALIDATION ERROR|Headset_view: multiview"
```

When chasing a hang, watch for `Headset_view: multiview render path enabled` (init reached the headset path) and the absence/presence of frame-period `am start`-following logs to decide whether the editor is actually rendering.
