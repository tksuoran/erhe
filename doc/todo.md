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

Moved from `doc/quest_fixes.md` E1. The other two E1 sub-fixes
(`inputs.property("validationLayerVersion")` and the
`-Pvulkan_validation_skip` branch deleting any staged .so) already landed
and stay recorded under that ticket's Closed Issues entry.
