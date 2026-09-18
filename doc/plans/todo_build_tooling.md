# Build tooling: verify the Vulkan validation layer download

Status: proposed

This plan extends `doc/android.md` (the Android build's validation-layer
packaging) with an integrity check on a downloaded binary.

`android-project/app/build.gradle` `fetchVulkanValidationLayer` downloads the
Khronos `VK_LAYER_KHRONOS_validation` arm64-v8a `.so` from the GitHub release
tagged with the pinned validation layer version and caches the zip under
`build/vulkan-validation-layer-cache/`. The download trusts whatever bytes
arrive over HTTPS: nothing checks integrity before `zipTree(cacheZip)` unpacks
the `.so` into `libs/arm64-v8a/` and AGP bundles it into the APK.

What that leaves open:

- A tampered upstream tag or swapped release asset ships a hostile or broken
  `libVkLayer_khronos_validation.so` to every developer machine on the next
  build.
- A misconfigured proxy or corporate certificate delivers a truncated or
  modified zip and the Gradle task does not notice.
- Once a corrupted zip lands in the cache directory it is trusted forever: the
  only freshness signal is the pinned version property.

## Wanted

- An expected SHA256 pinned alongside the validation layer version constant,
  one hash per pinned version. After the download, and before `zipTree`,
  compute the cached zip's SHA256 and compare; on mismatch throw a
  `GradleException` naming the mismatch.
- The hash is pinned from a one-time verified fetch when the version is bumped,
  the same model as Bazel `http_archive` `sha256 =` or CPM's hash field.
- The cache directory stays: the hash check is self-healing, because a bad
  cached zip fails verification and forces a re-download.

Verification of closure: corrupt one byte of the cached zip, re-run
`assembleDebug`, and require the build to fail with a hash-mismatch error and
to re-download on retry.

This file exists because `doc/plans/build_tooling.md` is being written in
parallel; fold this entry into it.
