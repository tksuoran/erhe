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
- Every prim the conversion gives a transform to keeps the xformOp stack it
  was authored with, next to the transform Tydra composed for it
  (`doc/usd-compatibility-plan.md` M8, `src/erhe/scene/notes.md` "Authored
  xformOp stacks"). Tydra reports the composed `local_matrix` only, so the
  ops come off the raw prim's `lightusd::Xformable::xformOps` - the composed
  prim the authored-opinion reader below already looks up. Each op keeps its
  type, its suffix and its `!invert!` flag as authored, and its value in
  double precision with the authored value type (`half3` / `float3` /
  `double3`, `half` / `float` / `double`, `quath` / `quatf` / `quatd`,
  `matrix4d`) kept as the op's precision, so the export writes the type name
  the file used. `!resetXformStack!` is the stack's flag rather than an op.
  A prim that authored no ops gets an empty stack, which composes to identity
  and exports as no `xformOp`s at all, so a saved layer says exactly what the
  loaded one did. An op whose value is time-sampled contributes its default
  when it has one and its first sample otherwise; animated transforms are
  future work (`doc/usd-compatibility-plan.md` section 6).
  Three cases keep the composed matrix and no stack, each reported in the
  log: a prim the stage lookup does not answer for or whose class carries no
  `xformOps`, an op of a value type erhe has no counterpart for (one
  warning), and a stack whose composition disagrees with the transform the
  stage evaluates by more than 1e-5 (one warning - the two agreeing is what
  `erhe_usd_tests` asserts for every prim of the fixtures). The stage's
  `upAxis` / `metersPerUnit` correction reaches the top-level prims of a
  non-Y-up or non-metre stage; those prims write a transform that is not what
  their ops say, so they keep the composed matrix too (logged at debug
  level).
- The erhe class of a prim is the class its `typeName` names
  (`doc/usd-compatibility-plan.md` C5, the object-model table of
  `doc/usd_compatibility.md`): a `Mesh` prim becomes an
  `erhe::scene::Mesh`, a `Camera` prim an `erhe::scene::Camera` and a UsdLux
  prim an `erhe::scene::Light`, each carrying its own transform; `Xform`
  becomes an `erhe::scene::Xform`, `Scope` becomes an `erhe::Scope`, and
  every other `typeName`, a typeless `def` included, becomes an
  `erhe::Typed` carrying that token. The `typeName` comes from the composed
  prim: a generic `Model` prim carries the authored token, every typed prim
  is named by its schema class. A prim outside `Xformable` carries no
  transform, so the transform that reached it composes with its children,
  and a transform authored on such a prim is dropped with one warning
  naming the prim.
- A `Material` prim becomes the erhe material prim, parented where the
  stage puts it (`doc/usd-compatibility-plan.md` U4): a stage keeping its
  materials in `/Looks` gives erhe a `Scope` named `Looks` holding them, and
  the prim name is the material's name, so two materials of one name in two
  scopes stay apart by their place. A `material:binding` names a path, so
  the binding follows the prim rather than the name. Every `Scope` is an
  `erhe::Scope`, an empty one included.
- A `Shader`, `NodeGraph` or `GeomSubset` prim whose subtree carries no
  mesh, camera, light, skeleton or volume contributes no erhe prim: the
  shading network is namespace and a subset's facets already ride a
  primitive of its mesh, yet Tydra lists each as a transform node.
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
- A local value is an authored value (`doc/property-system.md` D32). Tydra
  reports a schema fallback the same way it reports an authored opinion, so
  the conversion asks the composed prim instead: LightUSD's typed attribute
  wrappers answer `authored()`, and Tydra's `GetPropertyNames` collects the
  names that answer true plus every custom attribute the prim carries. An
  opinion arriving over a reference or a sublayer is authored on the
  composed prim, so it counts. `Importer::is_authored` gates every field the
  conversion writes: `visibility` and `purpose` (M3, onto the node the prim
  becomes); the `UsdPreviewSurface` inputs `diffuseColor`, `emissiveColor`,
  `metallic`, `roughness`, `opacity`, `opacityThreshold`, `ior` and
  `occlusion`, read from the Shader prim the material's `outputs:surface`
  connects to; the camera's `clippingRange`, `focalLength`, the apertures
  and `exposure`; and the light's `inputs:color`, `inputs:intensity`,
  `inputs:exposure`, `inputs:colorTemperature`, `inputs:shaping:cone:*` and
  `inputs:shadow:enable`. What a prim leaves at its fallback is not written,
  so the erhe property keeps the ERHE default - which differs from the USD
  fallback where the two schemas disagree (`base_color` stays white where
  `diffuseColor` falls back to 0.18); `doc/usd_compatibility.md` states that
  per row.
- `erhe::property::clear_default_valued_local_properties` still runs over the
  converted nodes, meshes, lights, cameras and materials at the end of the
  conversion. It is the safety net for what is still written
  unconditionally: the light type, which comes from the prim's schema type
  rather than from an attribute, and the values an item's own constructor
  seeds.
- A namespaced custom attribute under the `erhe` namespace is an erhe
  property value: `custom float erhe:Light:temperature = 5000`. USD reserves
  `.` for the property separator of a path, so the erhe qualified name
  `Owner.name` (D30) is spelled `erhe:Owner:name`, and `erhe:name` names a
  property of the item's own class. The name is resolved against the prim
  the `typeName` made (a `Material` prim resolves against the material
  alone), both as a holder addresses the name and as that class's own
  property. The USDA
  literal is stripped of brackets, commas and quotes and parsed with the
  property type's `from_string` (D16). A name that resolves to no property,
  and a value that fails to parse or to validate, are skipped with one
  warning each.

### Composition arcs

LightUSD composes nothing on load: `LoadUSDFromFile` reads the root layer and
leaves every `references` / `payload` arc in the prim's metadata (its
`do_composition` option is declared but not implemented). That is what the
importer wants (doc/usd-compatibility-plan.md X1): a prim that authors arcs
arrives as it was authored, and the arcs it names are reported in
`Usd_data::references` as `Usd_prim_references` - the erhe prim the arcs were
authored on, its stage path, and one `Usd_reference` per arc. The caller
instantiates each arc's target under that prim, so the erhe scene keeps the
instance structure instead of a flattened copy; in the editor an arc becomes a
`Prefab_instance` attachment (`src/editor/prefabs/prefab_library.hpp`).

- `asset_path` empty means an internal reference - a prim of the same layer -
  and `prim_path` empty means the target layer's default prim, which
  `Usd_data::default_prim` names.
- A payload is reported with kind `payload` and is otherwise a reference: erhe
  reads every arc when the file is read and has no deferred loading (plan
  section 5). `references` arcs come before `payload` arcs, the arc order of
  LIVRPS.
- A list-edited op resolves the way USD composes it: an unqualified op
  replaces the list, `prepend` inserts at the front, `append` and the
  deprecated `add` at the back, `delete` removes every entry naming the same
  target, and `order` is ignored. LightUSD keeps its own resolution private (a
  static helper of `composition.cc`), so `usd_import.cpp` repeats the rule.
- Only the arcs a prim itself authors count. An arc authored inside a
  referenced layer is part of that target, and the target's own instantiation
  is what reproduces it.
- The prims the referencing layer authors below a referencing prim - an `over`
  holding sparse opinions over what the reference supplied - are not imported.
  LightUSD does not report which layer an opinion on a composed prim came
  from, so the root layer is re-read once (only when a referencing prim is
  met) and its prim spec below the referencing prim is what the warning names.
  X2 carries these overrides; until then each such prim is reported once with
  a warning naming what was dropped, and X2 is where the per-layer
  distinction is needed.

Not yet imported: skeletons and skinning, blend shapes, animation clips,
`PointInstancer` / instanceable prototypes beyond what Tydra flattens,
volumes, MaterialX / OpenPBR shading networks, texture wrap and filter
state, and `UsdTransform2d` UV transforms.

## Export

`usd_export.cpp` writes one `.usda` layer through LightUSD's `SaveAsUSDA`.
`save_usda(const Usd_save_arguments&) -> Usd_save_result` takes erhe content
rather than a `Usd_data` - the caller hands the writer the scene it holds -
and reports failures as values. `sanitize_usd_identifier(name)` is public
because the same spelling rule decides what an item is called on a stage.

- Prim layout. The root node is not a prim: an erhe item path excludes the
  root's own name (M1), so the root's children are the stage's top-level
  prims. The prim's `typeName` is the one its erhe class names: an
  `erhe::Scope` writes a `Scope` prim, an `erhe::Typed` writes
  `def <token> "name"` - a typeless `def` when the token is empty - with its
  children and none of its attributes, and a transformable prim writes the
  prim its own class names, each with its own `xformOp`s: an
  `erhe::scene::Mesh` writes `Mesh`, an `erhe::scene::Camera` writes
  `Camera`, an `erhe::scene::Light` writes the UsdLux type its `light_type`
  names (`DistantLight` or `SphereLight`), and an `erhe::scene::Xform`
  writes `Xform`. That is what the importer inverts, so a file round-trips
  without gaining a level. A prim of a class that carries no transform gets
  none, and the transform that reached it composes with its children. A mesh
  with one primitive binds its material directly; several primitives become
  one `materialBind` `GeomSubset` each, over the concatenated `points` /
  `faceVertexCounts` / `faceVertexIndices` of every primitive, with the
  primvars written `faceVarying`. `subdivisionScheme` is always `none`: the
  authored polygons are the mesh. A material is written as a `Material` prim
  where the material sits in the scene tree, holding its
  `UsdPreviewSurface` `Shader`, one `UsdUVTexture` shader per bound slot and
  one `UsdPrimvarReader_float2` for their UVs; those shader prims occupy the
  material prim's namespace, so a prim the user parented to a material is not
  written. The stage names one `defaultPrim`: the single top-level prim, or a
  `World` `Xform` gathering them when the scene has several.
- A prim's transform. A prim that carries the xformOp stack it was imported
  with writes that stack: one attribute per op in the authored value type its
  precision names, the `xformOpOrder` token list carrying the suffixes, the
  `!invert!` flags and a leading `!resetXformStack!`. An empty stack writes no
  `xformOp` at all. Every other prim writes its composed matrix as a single
  `xformOp:transform`, and an identity matrix writes nothing - so a prim erhe
  created is written the way it always was. The stack is used only when it
  composes (within 1e-5) to the matrix being written, which is what keeps it
  out of the two cases where the matrix is not the prim's own: an
  `import_root` container whose transform is pre-multiplied into its
  children, and a stage the importer applied an `upAxis` / `metersPerUnit`
  correction to. erhe keeps every op value in double precision, so a `half`
  op round-trips through the nearest half - the value it was read from.
- Which prims are written. A prim is written when it is scene content
  (`Item_flags::content`), or when it is a resource the file carries or holds
  one below it - a resource prim is shown in the UI and is not content, so
  that widening is what puts a material and the scopes down to it on the
  stage, and it leaves an empty kind scope out of the file. Today the file
  carries materials; the other resource kinds are plan step E4.
- Two passes. The first decides every prim's stage path - the sanitized,
  sibling-unique name under each parent, and the `World` wrapper when the
  scene has several top-level prims - and records where each material
  landed; the second writes the prims, so a mesh binds its material by the
  path the material prim actually got. `Usd_save_arguments::materials` is
  the caller's texture index, not a placement list: a material of that list
  which is not a prim of the tree is not written, and a material a mesh
  binds but the scene does not own has no prim, so its binding is dropped
  with one warning naming it (plan step X1 gives a prefab template's
  materials a prim of their own).
- Values. Only a local value is written (D32). A property the mapping gives a
  USD attribute goes into that attribute (the closed list is
  `is_native_usd_property`, the exact inverse of what the import reads);
  every other serializable local value becomes an `erhe:Owner:name` custom
  attribute whose USD type follows the erhe property type and whose value is
  the D16 `to_string` form. A quaternion travels as a `float4` in erhe's
  `x y z w` order, because that is what the import's parse expects. Bridged
  properties are skipped: the node transform, the item name and the tags have
  a USD form that owns them. `visible` and `purpose` are read from the node,
  which is where the import puts them.
- Name sanitizing. `sanitize_usd_identifier` replaces every character outside
  `[A-Za-z0-9_]` with `_` and prefixes `_` to a name starting with a digit.
  Sanitizing can map two distinct item names onto one spelling, so the writer
  then applies erhe's own sibling-unique suffix rule (M2, `<base>_<n>` from
  1). The prim name is the item name: an item whose name needed sanitizing
  comes back under the sanitized spelling.
- Item tags become `UsdCollectionAPI` collections on the default prim, one
  per tag, whose `includes` names every prim carrying it.
- `Usd_save_arguments::custom_layer_data` is written verbatim as the root
  layer's `customLayerData`, one string entry per pair, and `Usd_data`
  reports the string entries of a loaded layer back. That pair is how the
  editor carries its own scene state in a USD file.
- Textures. `erhe::usd` decodes nothing and creates no GPU object, so the
  caller resolves each bound slot to a file (`Usd_save_texture`); the path is
  written relative to the `.usda`. A slot with a local texture value the
  caller could not resolve - a generated texture - is left out of the
  network with one warning.

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

The Android build passes it too, from the `externalNativeBuild` CMake
argument list of `defaultConfig` in `android-project/app/build.gradle`, so
both the `mobile` and the `quest` flavor carry USD. It is a build argument
there rather than a forced value in the root `CMakeLists.txt` Android block,
which keeps the option selectable the way the Windows wrappers keep it.
LightUSD compiles on the NDK toolchain (clang, arm64-v8a, `c++_static`)
without a change on either side.

What the option costs the Quest Debug APK, measured as two clean native
builds of the `quest` flavor on one machine:

| `ERHE_USD_LIBRARY` | Build | APK | `lib/arm64-v8a/libmain.so` |
|---|---|---|---|
| `none` | 6 m 5 s | 145 279 989 B | 102 339 416 B |
| `lightusd` | 7 m 7 s | 182 340 597 B | 139 405 592 B |

So USD adds about 37 MB to the stripped native library and the same to the
APK, and about a minute to a from-scratch build. Compare APK sizes only
against a freshly packaged APK: AGP packages incrementally and leaves the
previous `libmain.so` bytes orphaned in the file, which inflates the size on
disk well past the sum of the archive's entries.

## Tests

`src/erhe/usd/test/` builds `erhe_usd_tests` behind `-DERHE_BUILD_TESTS=ON`
(and `-DERHE_USD_LIBRARY=lightusd`). It imports `test/data/cube.usda` - a
cube with a materialBind `GeomSubset`, two `UsdPreviewSurface` materials, a
camera and a distant light - and checks the node names, the geometry-normative
split by subset, the material values, the camera projection and the light.
`test/data/authored.usda` covers the authored / fallback rule: an authored
`visibility` and `purpose`, a material with only `diffuseColor` authored, and
a light with an `erhe:Light:temperature` custom attribute next to a bogus
`erhe:Light:nope`, asserted through `get_value_source`.

`test/data/looks.usda` covers where materials sit: two `Scope`s below one
`Xform` holding three materials, two of them named alike, each bound by a
mesh of its own. `test_usd_materials.cpp` asserts that the material prims
land at their stage paths, that the binding follows the path rather than the
name, that a save writes each `Material` prim back where it sits (and invents
no `Materials` scope) and that the second save is byte-identical.

`test/data/xform_ops.usda` is the authored-xformOp-stack case, written in the
writer's own output spelling so a load and save has to reproduce it line for
line: a three-op `translate` / `rotateXYZ` / `scale` stack in mixed
`double3` / `float3` precision, a pivot stack (`translate:pivot`,
`!invert!translate:pivot`, `!resetXformStack!`), a `matrix4d` `transform` op
and a prim with no ops. `test_usd_xform_ops.cpp` asserts the round trip is
byte-identical and is a fixed point (a second load and save reproduces it),
that every imported prim's stack composes to the transform the stage
evaluates, and that a `set_parent_from_node` with a changed translation lands
in the `translate` op while the other ops come back unchanged.

`test/data/references.usda` and `test/data/reftarget.usda` are the composition
arc case: a reference to another file's default prim, a reference naming a prim
path in it, an internal reference to a prim of the same layer, a list-edited
`prepend` / `append` pair and a payload. `test_usd_references.cpp` asserts the
arcs are reported in the order they resolve to, with the right targets and
kinds, that the carrier prims are imported with their own transforms, and that
the prims the arcs name are not.

`test/data/textured.usda` binds an image file through a `UsdUVTexture`
network; it is the round-trip script's texture case rather than a unit-test
input.

`test_usd_export.cpp` round-trips both data files through `save_usda` and
`load_usd` and asserts that the node names, the mesh topology, the subset
material bindings, the material local sets, the camera and light values,
`visibility` / `purpose` and an `erhe:`-carried value all survive, plus the
identifier sanitizing and the suffix rule for two names that collapse onto
one spelling.

Live verification of the whole USD path - open a file as a scene, edit it
through MCP, save, reload, diff, save again and compare the two files, plus
`usdchecker` when an OpenUSD build is available - is the `usd-roundtrip`
section of `scripts/scene_roundtrip_verify.py`
(doc/scene_serialization.md, "Verifying round-trips").

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
- Of the editor state `ERHE_scene` and the asset-root extensions hold in
  glTF, the scene-level block travels as the `erhe:scene` string of
  `customLayerData` (doc/scene_serialization.md, USD-backed scenes) and the
  materials travel as the prims they are, so the scopes that hold them
  travel with them. The brush library, the geometry and texture node graphs,
  the style library and the remaining resource kinds have no USD form yet
  and no custom prims of their own (C1); a save logs one line per kind the
  scene holds. The writer also emits no `.usdc` or `.usdz`, no
  MaterialX, and none of the composition structure of the file it loaded -
  the first version flattens what it read (plan steps X1 and X2).
- A node-held secondary value (D30, `Light.color` on a plain Xform) is written
  as `erhe:Light:color` but the import resolves neither the qualified nor the
  bare name against a node, so such a value does not come back.
- An erhe material with both a texture and a factor in one slot writes the
  connection alone: `UsdPreviewSurface` has no multiplier, and the factor
  would have to ride on the texture's `scale`.
- A camera's `infinite_z_far` has no USD form; the finite `clippingRange` is
  written and one warning says so.
- The macOS and Linux configure wrappers still default to `none`; turning the
  option on there is part of the step that first needs USD on those platforms.
- The Quest launch with the option on - the editor coming up on the headset
  and answering `describe_usd_file` over the forwarded MCP port - is still
  pending; only the Android build and its size and build-time cost are
  measured (see "Configurations").
