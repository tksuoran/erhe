# Quest / Android sessions

Stability: stable

What an agent session working on the Quest (or `mobile`-flavor Android) build
needs beyond `AGENTS.md`. Building, packaging and the flavors are described in
`doc/quest.md` and `doc/android.md`.

## Skills

- **`erhe-quest-launch`** for any Quest build / install / launch /
  log-capture work: canonical script invocations, the persistent logcat
  capture (`scripts/quest_logcat.sh`, started BEFORE every launch), erhe-only
  log filtering, readiness signals, Android SDK/JDK probing, and the mobile
  sideload-dialog workaround.
- **`erhe-quest-validation`** makes silent GPU hangs abort loudly.
- **`erhe-quest-shader-failure`** triages vkCreateShaderModule / spirv-val
  aborts at startup.
- RenderDoc captures on Quest: `doc/agents/quest_renderdoc_capture.md`.

## Launch protocol

Install the APK FIRST, then prompt the user to put the headset on and
activate the controllers, and wait for explicit confirmation before EVERY
one-off `adb shell am start`; a confirmation applies only to that one launch.
Batch / soak runs are the exception: one confirmation at the start covers the
whole unattended sweep -- do not prompt between iterations, but stop and
re-confirm if results suggest the headset went off-head. Pure builds and
installs need no prompt. Poll for a readiness signal (recipes in the skill)
instead of sleeping.

## Vulkan validation on Quest

Without `VK_LAYER_KHRONOS_validation`, GPU-side errors are silent hangs. The
layer .so is bundled in every APK by default; the runtime knob is
`"vulkan_validation_layers"` in `config/editor/erhe_graphics.json`
(device-level errors then abort via `ERHE_FATAL`). Leave the knob off for
normal runs (per-frame overhead); the bundled .so alone costs nothing -- see
`erhe-quest-validation` for the full flow including the SPIR-V cache wipe.

## Reaching the MCP server on the device

On device the in-editor MCP server binds the same loopback address
(`127.0.0.1:3743`), unreachable from the host directly -- forward it over adb
(`adb forward tcp:3743 tcp:3743`, re-run after each device reconnect) and then
drive it exactly as on desktop (`doc/agents/editor_runs.md`). The APK declares
the `INTERNET` permission (`android-project/app/src/main/AndroidManifest.xml`):
Android gates all socket creation -- even loopback -- behind it, so without it
`bind()` returns `EACCES` on every port and the fallback loop logs
`failed to bind any port in [3743, 3763)` (a blanket denial, not a port
clash). Confirm the grant with
`adb shell dumpsys package org.libsdl.app.quest | grep INTERNET`. See
`doc/agents/mcp_server_usage.md` for the full recipe.
