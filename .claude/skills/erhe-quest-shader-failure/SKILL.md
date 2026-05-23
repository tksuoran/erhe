---
name: erhe-quest-shader-failure
description: Triage erhe editor aborts on Quest where vkCreateShaderModule fails with spirv-val (VUID-VkShaderModuleCreateInfo-pCode-08737), or any Shader_stages compile/link error. Identifies which shader / stage failed by reading the immediately-preceding "Creating shader module" log line, then maps known VUIDs to known fixes. Use when the editor aborts on Quest at startup with a SPIR-V or shader-link validation message.
---

# Quest shader failure triage

When the validation layer rejects a SPIR-V module inside `vkCreateShaderModule`, the editor's device-error callback aborts via `ERHE_FATAL` (see `editor.cpp:702`). Identifying the failing shader is the first step before deciding whether the cause is in our shader source, in the SPIR-V cache, or in glslang itself.

## Step 1: pull logs and identify the failing shader

```
$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe logcat -d -v time \
  | grep -E "Creating shader module|VALIDATION ERROR|Abort message|spirv-val produced"
```

`erhe::graphics::Shader_stages_impl::create_shader_module` logs `Creating shader module: <name> <stage>` at info level immediately before each `vkCreateShaderModule` call (see `vulkan_shader_stages.cpp`). The **last** such log line preceding the `VALIDATION ERROR` is the shader+stage that tripped the validator. Example:

```
[I] Creating shader module: visualize_depth fragment
[E] VALIDATION ERROR [a5625282 VUID-VkShaderModuleCreateInfo-pCode-08737] vkCreateShaderModule(): pCreateInfo->pCode (spirv-val produced an error):
[E] NonSemantic.Shader.DebugInfo.100 DebugGlobalVariable: expected operand Variable must be a result id of OpVariable or OpConstant or DebugInfoNone
[E]   %680 = OpExtInst %void %1 DebugGlobalVariable %681 %644 %36 %uint_133 %uint_0 %34 %681 %679 %uint_8
```

Failing shader: `visualize_depth fragment`. Failing instruction: `DebugGlobalVariable` whose Variable operand (`%679`) is not in the spec's allowed set.

## Step 2: map the VUID

### `VUID-VkShaderModuleCreateInfo-pCode-08737` with `DebugGlobalVariable`

Cause: glslang 16.x emits `DebugGlobalVariable` with `Variable` = `OpConstantComposite` / `OpSpecConstantComposite` for global const composites and the synthesised `gl_WorkGroupSize`. The CPM PATCHES file `cmake/patches/glslang-debugglobalvariable-noop-for-composites.patch` (wired in `CMakeLists.txt`'s `glslang` `CPMAddPackage`) substitutes `DebugInfoNone` and resolves this for every shader.

If a fresh shader you just added still fails:

1. **Check it's not a stale cache**. The cache key includes `GLSLANG_VERSION_*`, but if you bypassed the version bump and only changed `SpvOptions`, bump the trailing `vN` tag in `Spirv_cache::make_settings_salt` and uninstall + install on device to wipe `/data/data/<pkg>/files/spirv_cache/`.
2. **Check the patch is in the active build**. Inspect `.cpm_cache/glslang/<hash>/SPIRV/SpvBuilder.cpp` for `variableOperand`; if missing, delete the cache + the corresponding `_deps/glslang-*` dirs and rebuild so CPM re-clones and re-applies the patch.
3. **Disassemble the SPIR-V** to identify `%679` (or whatever the validator points at):
   ```
   "C:/VulkanSDK/<sdk>/Bin/spirv-dis.exe" <cached.spv> | grep -B2 "%679 ="
   ```
   If it's an `OpConstantComposite`, the patch should have caught it -- check (2). If it's something else (e.g. `OpSpecConstantOp`), this is a different glslang bug; see `doc/glslang_bug_report_debugglobalvariable.md` for the related glslang #4186 family.

**NEVER disable `NonSemantic.Shader.DebugInfo.100`** in `glsl_to_spirv.cpp` SpvOptions to silence this. The runtime debug info is wanted, the validation layer is wanted, and per-shader / per-bug fixes are the agreed approach. (See `feedback_keep_nonsemantic_debug_info.md` in memory.)

### `VUID-VkShaderModuleCreateInfo-pCode-08737` without `DebugGlobalVariable`

Different VUID body, same enclosing VUID number. Read the actual `spirv-val produced an error: <body>` line carefully. Common variants:
- `DebugTypeArray: Component Count must be ...` -- glslang #4186 (open upstream), affects spec-constant-sized arrays. Workaround: avoid the spec-constant arithmetic in array dimensions, or omit debug info for that one shader (last resort).

### `VUID-VkDeviceCreateInfo-pNext-02829`

Aliased feature struct in pNext chain (e.g. `VkPhysicalDeviceVulkan11Features` + `VkPhysicalDevice16BitStorageFeatures`). Fixed in commit `524888f8`; if it returns, search `vulkan_device_init.cpp` for newly-added pNext entries that the 1.1 / 1.2 / 1.3 Features struct subsumes.

### Generic `Shader compilation/linking failed`

Editor aborts at `editor.cpp:718` with the GLSL error log + source copied to clipboard. Read the clipboard or grep logcat for the GLSL `ERROR:` lines. Most often a typo, a missing extension declaration, or a layout/binding mismatch with the C++ side's bind_group_layout.

## Step 3: rebuild + re-test

After applying a source-level fix:

1. `scripts\build_android.bat quest`
2. Force-stop + install (or uninstall + install if the salt didn't change and you suspect the cache is stale).
3. Launch via the headset-prompt protocol (see `erhe-quest-launch`).
4. Re-grep logcat to confirm the offending shader name no longer appears in the `Abort message`.

## Cross-references

- `doc/glslang_bug_report_debugglobalvariable.md` -- root-cause writeup of the recurring `DebugGlobalVariable` family of issues.
- `cmake/patches/glslang-debugglobalvariable-noop-for-composites.patch` -- the local workaround.
- `doc/quest.md` Section 4.3.1 -- end-to-end guide for enabling the validation layer.
- `erhe-quest-validation` skill -- enable validation if it isn't on yet.
- `erhe-quest-launch` skill -- canonical build/install/launch sequence + headset prompt rule.
