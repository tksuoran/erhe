# SPIR-V cache robustness

Status: proposed

This plan extends `doc/erhe/graphics.md` with two changes to the on-disk
SPIR-V cache (`src/erhe/graphics/erhe_graphics/spirv_cache.cpp`).

## Hash the compile settings into the cache salt

`make_settings_salt()` carries a manual `"vN"` tag that has to be bumped by
hand whenever the `SpvOptions` struct, the `EShMessages` bitmask in
`glsl_to_spirv.cpp`, or the target environment changes. A missed bump serves
stale binaries on the next run.

Hash the file-scope `SpvOptions` value, the `messages` mask and the glslang
version with `erhe::hash::hash`, and feed the resulting `uint64_t
compile_settings_hash` into both `Spirv_cache::get` and `Spirv_cache::put`
(a signature change). `try_load_all_from_cache` and `link_program` both pass
the same value, so the settings themselves hoist to file scope.

## Write cache entries atomically

`Spirv_cache::put` truncates and writes the `.spv` file in place, so a crash
mid-write leaves a partial file that `get()` rejects on the SPIR-V magic check
while it still occupies disk space. Write to
`<hash>.spv.tmp.<per-process-suffix>` and `std::filesystem::rename(tmp, final)`
instead: POSIX-atomic on the same filesystem and NTFS-atomic on Windows.
