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

- `load_usd(Usd_load_arguments) -> Usd_load_result` - load a file and convert
  its composed stage into erhe scene content. `convert_stage(stage, arguments)`
  is the same conversion over a stage the caller already loaded.
- `Usd_data` - what one USD file contributes: `nodes`, `meshes`, `cameras`,
  `lights`, `materials`, `images`, `material_texture_bindings`, plus the
  stage's `up_axis` and `meters_per_unit`. It is deliberately the shape of
  `erhe::gltf::Gltf_data` where the two formats overlap, so the editor's
  import path is the same one glTF import takes.

`erhe_usd/usd_log.hpp` declares `log_usd` (`erhe.usd`) and
`initialize_logging()`, called from the editor's logging init.
`erhe_usd/usd_impl.hpp` is the internal header holding `Stage::Impl`; it
includes LightUSD headers and is included only by this library's own
translation units.

## Import

`usd_import.cpp` converts a composed stage through LightUSD's Tydra
`RenderSceneConverter`. The decisions the conversion rests on:

- The converter runs with triangulation and vertex-index building off. That
  keeps the authored `faceVertexCounts` / `faceVertexIndices` and the
  authored primvar variability, which is what the geometry-normative path
  needs; the element table of `doc/usd_compatibility.md` maps the
  variability onto erhe's element domains.
- A `Mesh` prim whose `subdivisionScheme` is `none` becomes an
  `erhe::geometry::Geometry` built straight from those arrays, processed
  with connect + build edges + smooth vertex normals (the processing the
  editor's glTF finalize pass runs for imported geometry without edges).
  Every other mesh becomes an `erhe::primitive::Triangle_soup` with one
  vertex per polygon corner and polygons fanned into triangles, the carrier
  glTF primitives use.
- A `Scope`, `Material`, `Shader`, `NodeGraph` or `GeomSubset` prim whose
  subtree carries no mesh, camera, light, skeleton or volume contributes no
  erhe node: none of them is Xformable - the shading network is namespace and
  a subset's facets already ride a primitive of its mesh - yet Tydra lists
  each as a transform node.
- Each materialBind `GeomSubset` becomes one primitive of the erhe mesh,
  with the facets no subset claims forming one more - the same shape a glTF
  mesh's primitive list has. A vertex is emitted for a group only if one of
  its facets uses it.
- LightUSD's image loaders are off, so an image arrives as a resolved file
  path (`Usd_image`) and the caller decodes it with erhe's own image
  loading. `erhe::usd` creates no GPU object at all, which is what lets the
  conversion run off the main thread.
- Values are read at the stage's default time code, and `UsdPhysics` prims
  and API schemas are counted and reported in one log line rather than
  imported (`doc/usd-compatibility-plan.md` section 5).
- Tydra reports a schema fallback the same way it reports an authored
  opinion, so the conversion writes every field of every item and the
  conversion ends with
  `erhe::property::clear_default_valued_local_properties` over the
  converted nodes, meshes, lights, cameras and materials: a field the
  layer left at its fallback reports `Value_source::default_value`
  (`doc/property-system.md` D32). Reading USD's own authored / fallback
  distinction instead is `doc/usd-compatibility-plan.md` I2.

Not yet imported: skeletons and skinning, blend shapes, animation clips,
`PointInstancer` / instanceable prototypes beyond what Tydra flattens,
volumes, MaterialX / OpenPBR shading networks, texture wrap and filter
state, and `UsdTransform2d` UV transforms.

## Dependency

LightUSD (Apache 2.0, C++17, dependency-free) through `CPMAddPackage` in the
root `CMakeLists.txt`, linked as `lightusd::lightusd_static`. The pin is a
commit of the erhe fork `tksuoran/LightUSD`, branch `fix-vs2026`, which
carries MSVC / Visual Studio 2026 build fixes upstream does not have yet; the
comment on the pin says when the fork can be dropped. `GIT_SHALLOW` is off
because a shallow clone cannot fetch a raw commit id.

Tydra, the composition cache (`LIGHTUSD_WITH_PCP`) and the OpenVDB reader
(`LIGHTUSD_WITH_USDVOL`) are enabled. Every other optional module is off, for
two reasons: none of them is on the path from a USD file to an erhe scene, and
several vendor copies of libraries erhe already compiles.

`LIGHTUSD_WITH_USDVOL` is on only because Tydra's `RenderSceneConverter`
calls `usdVol::ReadVDBFromMemory` unconditionally (`ConvertVolume` in
`tydra/render-data.cc`), so any link that pulls that translation unit needs
the module even though erhe imports no volumes. Without it the link fails with
one `LNK2019` for that symbol - and only for a target that actually calls the
converter, which is why the option can look unnecessary until the first such
link. Turn it off again when upstream guards the call.

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

## Tests

`src/erhe/usd/test/` builds `erhe_usd_tests` behind `-DERHE_BUILD_TESTS=ON`
(and `-DERHE_USD_LIBRARY=lightusd`). It imports `test/data/cube.usda` - a
cube with a materialBind `GeomSubset`, two `UsdPreviewSurface` materials, a
camera and a distant light - and checks the node names, the geometry-normative
split by subset, the material values, the camera projection and the light.

The editor side of the import - the undoable operation, the texture creation
and the entry points (asset browser, viewport drag-and-drop, MCP `import_usd`)
- lives in `src/editor/parsers/usd.{hpp,cpp}`; see `src/editor/parsers/notes.md`.

## Future work

- No asynchronous load path: `load_usd` runs on the calling thread and the
  editor's import is synchronous, where a glTF import goes through the asset
  manager's `Asset_load_request` and the droppable-payload
  `Import_gltf_operation` (doc/reloadable-asset-loads.md). The conversion
  itself creates no GPU object, so it is ready to move onto a worker when the
  asset manager learns a second format.
- A USD file cannot be opened as a scene or instantiated as a prefab yet, only
  imported as an asset; the prefab library parses glTF only.
- The macOS and Linux configure wrappers still default to `none`; turning the
  option on there is part of the step that first needs USD on those platforms.
- The Quest / Android build with the option on (build, size, launch) is
  verified once at the end of the USD plan, not per step.
