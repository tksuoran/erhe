# TODO

## Positive crash signal for harness-run apps

When Claude Code runs an erhe executable (editor, example, rendering_test)
for smoke testing, currently the only crash signal is "exit code != 0 + maybe
empty output". That misses crashes whose exit happens to look like a clean
timeout, and gives false positives when a user-quit returns nonzero.

We want a *positive* signal: a per-process artifact that says "I reached steady
state without crashing" or "I crashed, here is where". Sketch:

- App writes a `logs/<exe>_run.marker` line ("startup_complete" / "shutdown_clean")
  at well-defined points. The smoke-test wrapper greps for the expected marker.
  Absence + nonzero exit code = crash.
- Pair with a postmortem hook: install a top-level `std::terminate_handler` /
  `SetUnhandledExceptionFilter` that flushes spdlog, writes a "crashed at: ..."
  marker, and (on Windows) writes a minidump next to the marker. The wrapper
  reads the marker for the failure mode and the dump for postmortem.
- Wire the wrapper into a `scripts/smoke_test.sh` (or `.bat`) so future Claude
  Code sessions have one canonical invocation that returns a structured result
  instead of "did stdout look ok?".

Motivation: during the section-E dead-code commit the editor smoke-test
passed, but the example.exe smoke-test reported "fine" purely because output
was empty within the timeout window -- it had actually crashed. Detecting that
required the user to notice. See conversation 2026-05-15.

## SHA256-verify the Vulkan validation layer download

`android-project/app/build.gradle` `fetchVulkanValidationLayer` downloads the
Khronos `VK_LAYER_KHRONOS_validation` arm64-v8a .so from the GitHub release
tagged `vulkan-sdk-${validationLayerVersion}` and caches the zip under
`build/vulkan-validation-layer-cache/`. The download trusts whatever bytes
arrive over HTTPS -- there is no integrity check before `zipTree(cacheZip)`
unpacks the .so into `libs/arm64-v8a/` and AGP bundles it into the APK.

Failure modes this leaves open:
- Supply-chain compromise (tampered tag or release asset swap upstream) ships
  a hostile or broken `libVkLayer_khronos_validation.so` to every dev machine
  on the next build.
- Network corruption / MITM via a misconfigured proxy or corp cert delivers
  a truncated or modified zip; nothing in the Gradle task notices.
- Cache poisoning: once a corrupted zip lands in the cache dir it is trusted
  forever (the only freshness signal is `inputs.property("validationLayerVersion")`
  -- byte-level integrity is not checked).

Fix sketch:
- Bake an expected SHA256 alongside the existing `validationLayerVersion`
  constant -- one hash per pinned version. After `ant.get` (or before
  `zipTree`), compute the cached zip's SHA256 and compare. On mismatch,
  `throw new GradleException("Vulkan validation layer SHA256 mismatch: ...")`.
- Hash is pinned from a one-time verified-good fetch when the version is
  bumped; same model as Bazel `http_archive` `sha256 =` or CPM's hash field.
- Cache directory can stay -- the hash check is self-healing because a bad
  cached zip fails verification and forces a re-download.

Verification of closure: corrupt one byte of the cached zip and re-run
`assembleDebug`; the build must fail with a hash-mismatch error and
re-download on retry.

(The companion sub-fixes -- `inputs.property("validationLayerVersion")`
on the Gradle task, and the `-Pvulkan_validation_skip` branch deleting
any staged .so so flipping to skip mode actually drops the layer --
have already landed in `android-project/app/build.gradle`.)

## Id_renderer is untested and possibly broken

`src/editor/renderers/id_renderer.{hpp,cpp}` (and its pipeline setup
in `App_rendering`, per-viewport call in
`Viewport_scene_view::execute_rendergraph_node`) has not been
exercised since the may26 shader-variant / multi-vertex-format push.
Likely candidates: mouse picking returns no hit / wrong hit / random
IDs; or the editor aborts on the first picking attempt.

Acceptance: left-click on a mesh in the 3D viewport selects it; the
readback ring (4 entries) cycles without stalling; primitive index
and triangle ID match the picked surface. See `doc/editor_rendering.md`
"ID renderer".

## SPIR-V cache robustness

Two improvements were drafted during the multiview bring-up but
reverted to keep that commit focused. Both worth landing later:

- **Auto-hash `SpvOptions` + `EShMessages` into the cache salt.**
  Today `make_settings_salt()` in `src/erhe/graphics/erhe_graphics/spirv_cache.cpp`
  carries a manual `"vN"` tag that must be bumped by hand whenever
  the `SpvOptions` struct, the `EShMessages` bitmask in
  `glsl_to_spirv.cpp`, or the target environment changes. Easy to
  forget; a missed bump silently serves stale binaries on the next
  run. Fix: hash the file-scope `SpvOptions` value + `messages` mask
  + glslang version with `erhe::hash::hash` and feed the resulting
  `uint64_t compile_settings_hash` into both `Spirv_cache::get` /
  `put` (signature change). `try_load_all_from_cache` and
  `link_program` both need to pass the same value, so the settings
  themselves should hoist to file scope.

- **Atomic writes in `Spirv_cache::put`.** Today the .spv file is
  truncated and written in place; a crash mid-write leaves a partial
  file that `get()` rejects via the SPIR-V magic check but still
  takes disk space. Fix: write to `<hash>.spv.tmp.<per-process-suffix>`
  then `std::filesystem::rename(tmp, final)` -- POSIX-atomic on the
  same filesystem and NTFS-atomic on Windows.

## Enable only strictly-required Vulkan device features

`src/erhe/graphics/erhe_graphics/vulkan/vulkan_device_init.cpp` builds the
`VkDeviceCreateInfo` feature chain by copying each feature from the value the
physical device reported (`set_*.feature = query_*.feature`) instead of
requesting only the features the engine actually uses. The
`VkPhysicalDeviceDescriptorIndexingFeatures` block is the clearest case: all 20
fields are copied from the query even though the backend depends on only three
(`runtimeDescriptorArray`, `descriptorBindingPartiallyBound`,
`descriptorBindingVariableDescriptorCount`). `dynamicRendering` is enabled the
same way and never used at all -- the backend renders through `VkRenderPass` /
`vkCmdBeginRenderPass2` and never calls `vkCmdBeginRendering`.

Why this is not ideal:
- It enables features the backend does not use, widening the apparent dependency
  surface and making it hard to tell what is actually required.
- It can mask bugs: a shader that accidentally relies on a SPIR-V capability
  compiles and runs on the dev machine because the matching feature happened to
  be available and got enabled, then fails `vkCreateShaderModule` on hardware
  where it is not.
- "Copy whatever the device has" is the opposite of an explicit contract; the
  required-vs-optional split is invisible at the call site.

Fix sketch:
- Define the minimal required feature set explicitly and abort (as a handful of
  features already do) when one is missing.
- For genuinely optional features, enable them only when a concrete code path
  uses them, gated on the corresponding `Device_info` flag.
- Stop copying entire feature structs wholesale; set only the named fields the
  engine consumes. Drop `dynamicRendering` from the enabled set unless/until the
  backend migrates to dynamic rendering.

See `doc/vulkan_backend.md` ("Features enabled but not used by the backend").
