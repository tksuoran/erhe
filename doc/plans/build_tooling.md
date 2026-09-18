# Build tooling: make a stale VS build fail loudly

Status: proposed

Extends `doc/msvc_build_issues.md`, which describes how a Visual Studio
incremental build can link mixed-vintage objects into an ODR-violating
executable, and how to diagnose one. Nothing here is implemented; the
diagnosis recipe is all that stands today.

## Disable the IDE fast up-to-date check for heavyweight targets

`set_property(TARGET editor PROPERTY VS_GLOBAL_DisableFastUpToDateCheck true)`,
in `erhe_target_settings()` or for `editor` alone. MSBuild's tlog check still
runs, so builds stay incremental; this removes only the IDE-level shortcut that
provably lies. Highest value, lowest cost of the options here.

## An ODR canary for the highest-fan-out layout

Give `App_message_bus` a `std::size_t m_size_marker{0}` set to
`sizeof(App_message_bus)` in its constructor (compiled in
`app_message_bus.cpp`), and have `Editor` init verify
`ERHE_VERIFY(bus->m_size_marker == sizeof(App_message_bus))` (compiled in
`editor.cpp`). A mixed-layout executable then dies at launch with a clear
message instead of an undebuggable crash. This is the struct both observed
chimera crashes went through.

## A workflow rule in AGENTS.md

State that after changing a widely included editor header, or after git
operations with VS open, the VS IDE incremental build is not to be trusted:
use the cmake CLI build or a rebuild.

## Rejected

- Dropping `/MP`: the build-time cost far exceeds the risk removed.
- Abandoning the VS tree: it is needed for debugging.

## Verify the Vulkan validation layer download

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

### Wanted

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
