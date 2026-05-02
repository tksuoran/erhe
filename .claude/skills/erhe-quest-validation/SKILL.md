---
name: erhe-quest-validation
description: Enable VK_LAYER_KHRONOS_validation on the erhe Quest APK so silent GPU hangs become loud aborts. Covers the bundle (default on), the runtime config knob, the on-device SPIR-V cache wipe via uninstall, and the device-error abort hook in editor.cpp. Use when a Quest build runs but renders nothing in VR, hangs at the Horizon launch panel, or otherwise misbehaves silently.
---

# Quest Vulkan validation layer

Quest's Vulkan loader has no validation layer of its own. Without it, GPU-side errors (descriptor mismatches, image-layout violations, multiview misuse, sync hazards, command-buffer lifecycle bugs) silently hang the editor on the first bad frame. With validation on, the same errors hit the device-error callback registered in `editor.cpp:702` (`Message_severity::error` -> `ERHE_FATAL`), so the process aborts with the offending VUID copied to the clipboard and dumped to logcat.

## Bundling (build-time)

The Khronos validation layer .so is bundled in every Quest / Android APK by default. Gradle task `fetchVulkanValidationLayer` downloads `libVkLayer_khronos_validation.so` for `arm64-v8a` from the SDK release pinned in `android-project/app/build.gradle` (`validationLayerVersion`) and stages it under `app/libs/arm64-v8a/`. The .so adds ~10 MB to the APK; opt out with `-Pvulkan_validation_skip` (bare; bat-arg parser strips `=`).

Verify the .so is in the APK:
```
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
& "$env:JAVA_HOME\bin\jar.exe" tf <apk-path> | Select-String "VkLayer|libVk"
```

## Enabling at runtime

1. Edit `config/editor/erhe_graphics.json` so the vulkan block reads:
   ```json
   "vulkan": { "vulkan_validation_layers": true, ... }
   ```

2. Wipe the on-device SPIR-V cache so all shaders re-compile through the validator on next launch (otherwise stale cached SPIR-V from earlier runs is loaded straight to `vkCreateShaderModule` and validation never sees the source). Easiest: uninstall + install.
   ```
   $LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe uninstall org.libsdl.app.quest
   scripts\install_android.bat quest
   ```

3. Launch via the standard headset-prompt protocol (see `erhe-quest-launch`). Validation messages route through:
   ```
   debug_utils_messenger_callback (vulkan_device_debug.cpp)
     -> Device::device_message
     -> editor.cpp:698 lambda
     -> ERHE_FATAL on Message_severity::error
   ```

## Confirming the layer actually loaded

In logcat, expect:
```
D vulkan: added global layer 'VK_LAYER_KHRONOS_validation' from library '...libVkLayer_khronos_validation.so'
I erhe: Vulkan Instance Layer: VK_LAYER_KHRONOS_validation spec_version 0x00404148 implementation_version 0x00000001 LunarG validation Layer
I erhe: Enabling VK_LAYER_KHRONOS_validation
I vulkan: Loaded layer VK_LAYER_KHRONOS_validation
```

`spec_version` decodes as VK_VERSION: `0x00404148` = `1.4.328` -- compare against `validationLayerVersion` in `android-project/app/build.gradle`.

The linked glslang version is also logged once per process (used by the SPIR-V cache-key salt; cache entries auto-invalidate when glslang is bumped):
```
I erhe: glslang version: 16.3.0
```

## Disabling

Flip `vulkan_validation_layers` back to `false` in `config/editor/erhe_graphics.json`. The .so stays in the APK but the loader does not load it -- no per-frame overhead. The runtime cost only kicks in when both the bundle and the config knob are on.

## Common VUIDs encountered + status

| VUID | Cause | Status |
|---|---|---|
| `VUID-VkDeviceCreateInfo-pNext-02829` | `VkPhysicalDeviceVulkan11Features` chained alongside `VkPhysicalDevice16BitStorageFeatures` / `VkPhysicalDeviceMultiviewFeatures` (subsumed by 1.1 features struct) | Fixed in commit `524888f8` |
| `VUID-VkShaderModuleCreateInfo-pCode-08737` (`DebugGlobalVariable: expected operand Variable must be ...`) | glslang emits `DebugGlobalVariable` with `Variable` operand referencing `OpConstantComposite` for global const composite arrays + the synthesised `gl_WorkGroupSize` | Patched locally via CPM PATCHES in commit `f354e22a`. See `doc/glslang_bug_report_debugglobalvariable.md` and the `erhe-quest-shader-failure` skill |
| `SYNC-HAZARD-READ-AFTER-WRITE` (vertex stage reads SSBO after compute dispatch) | `Memory_barrier_mask::vertex_attrib_array_barrier_bit` alone is insufficient when the consuming vertex shader reads via SSBO instead of the input assembler | Use `vertex_attrib_array_barrier_bit \| shader_storage_barrier_bit` between the compute encoder and the render encoder |
| `VUID-vkFreeCommandBuffers-pCommandBuffers-00047` (cb in pending state when freed) | Editor frees a command buffer the GPU has not finished with -- a frame-fence/lifecycle bug surfaced now that validation is on | Open as of 2026-05-02 |

## When NOT to enable

- Performance work (validation adds noticeable per-frame CPU overhead).
- Any user-facing test (some VUIDs above abort the editor and a user-built APK should not).
