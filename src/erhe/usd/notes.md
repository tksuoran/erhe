# erhe::usd

## Purpose

`erhe::usd` is the only erhe library that includes LightUSD headers. It wraps
the USD library behind erhe types so that the rest of erhe - and the editor -
never names a LightUSD type. It exists so that USD support can be built or
left out at configure time (`ERHE_USD_LIBRARY`) without any other subsystem
noticing.

The library is built only when `ERHE_USD_LIBRARY=lightusd`; with `none` the
directory is not added at all (`src/erhe/CMakeLists.txt`), so a `none` build
contains no USD code, no LightUSD sources and no `erhe_usd` target. Editor
code that uses the library is compiled under
`#if defined(ERHE_USD_LIBRARY_LIGHTUSD)`.

## Public API (`erhe_usd/usd.hpp`)

- `class Stage` - one composed USD stage, a pimpl over the LightUSD stage.
  Non-copyable and non-movable; `get_source_path()` returns the file it came
  from.
- `load_stage(const std::filesystem::path&) -> Load_stage_result` - loads and
  composes a `.usd` / `.usda` / `.usdc` / `.usdz` file (format detected from
  content). `Load_stage_result::stage` is null exactly when `error` is
  non-empty; `warning` can be set either way. Failures are values, not
  exceptions.
- `describe_stage(const Stage&) -> Stage_description` - prim count, per-schema
  -type prim counts sorted by type name (`Prim_type_count`), the layers the
  stage names (`Layer_reference`: `root` for the loaded file, `sublayer` for a
  root-layer `subLayers` entry, `reference` and `payload` for the arcs a prim
  authored), plus up axis, default prim and metersPerUnit.

`erhe_usd/usd_log.hpp` declares `log_usd` (`erhe.usd`) and
`initialize_logging()`, called from the editor's logging init.

## Dependency

LightUSD (Apache 2.0, C++17, dependency-free) through `CPMAddPackage` in the
root `CMakeLists.txt`, linked as `lightusd::lightusd_static`. The pin is a
commit of the erhe fork `tksuoran/LightUSD`, branch `fix-vs2026`, which
carries MSVC / Visual Studio 2026 build fixes upstream does not have yet; the
comment on the pin says when the fork can be dropped. `GIT_SHALLOW` is off
because a shallow clone cannot fetch a raw commit id.

Only Tydra and the composition cache (`LIGHTUSD_WITH_PCP`) are enabled. Every
other optional module is off, for two reasons: none of them is on the path
from a USD file to an erhe scene, and several vendor copies of libraries erhe
already compiles.

### Duplicate symbols

A static link of both libraries would define the same symbols twice where
LightUSD vendors what erhe already builds. Three options remove that before
any rename or fork is needed, and no other duplicate was found - the editor
links with zero `LNK2005` / `LNK1169` in every configuration listed below:

- `LIGHTUSD_WITH_BUILTIN_IMAGE_LOADER OFF` drops LightUSD's `fpng.cpp`; erhe
  compiles fpng into `erhe::graphics`.
- `LIGHTUSD_WITH_MESHOPT OFF` drops LightUSD's vendored meshoptimizer; erhe
  fetches meshoptimizer itself.
- `LIGHTUSD_WITH_ZSTD_COMPRESSION OFF` drops LightUSD's amalgamated `zstd.c`;
  erhe compiles the copy bundled with basis_universal into `erhe::graphics`.
  USDC crate payloads use LZ4, which LightUSD vendors under its own names.

LightUSD's `miniz.c` and `lz4.c` have no counterpart in an erhe build (`spng`
is present in the tree but not built), so they stay.

### Warnings and the C++ standard

erhe compiles with `/W4 /WX` and LightUSD does not; `erhe_target_settings`
applies those flags to erhe targets only, so LightUSD keeps its own. Its
headers are included as a SYSTEM include directory by `erhe_usd` (LightUSD
keeps that directory PRIVATE to its own targets, so it is named explicitly).
`LIGHTUSD_NO_WERROR ON` keeps its self-selected `-Weverything` (any clang,
clang-cl included) from being fatal, and `LIGHTUSD_CXX_RTTI ON` avoids a
`/GR-` that MSVC reports as `D9025` once per translation unit.

Added through `add_subdirectory`, LightUSD sets no C++ standard of its own and
documents that the parent project names one. The erhe root scope names none,
so the root `CMakeLists.txt` sets C++20 around the `CPMAddPackage` and
restores the previous (usually unset) value afterwards. Without it clang-cl
compiles LightUSD at its C++14 default and `core/property.hh` fails on
`std::variant`; naming C++20 on both sides also keeps the vendored `nonstd::`
types, which pick their implementation from `__cplusplus`, at one definition
across `erhe::usd` and LightUSD.

## MCP

The editor exposes `describe_usd_file(path)` (`src/editor/mcp/mcp_server_file_io.cpp`).
The tool is listed in `config/editor/mcp_tools.json` in every build; in a
`none` build it answers with the error `USD support not built
(ERHE_USD_LIBRARY=none)` so a script gets a clear message rather than an
unknown-tool reply.

## Configurations

`scripts\configure_ninja_win_vulkan.bat`, `scripts\configure_ninja_win_clang.bat`
and `scripts\configure_vs2026_vulkan_headless.bat` pass
`-DERHE_USD_LIBRARY=lightusd`.

## Future work

- I1 of `doc/usd-compatibility-plan.md`: import a USD file as an asset,
  through LightUSD Tydra `RenderScene`. That is what grows this library past
  `load_stage` / `describe_stage`.
- The macOS and Linux configure wrappers still default to `none`; turning the
  option on there is part of the step that first needs USD on those platforms.
- The Quest / Android build with the option on (build, size, launch) is
  verified once at the end of the USD plan, not per step.
