# glTF and USD feature support matrix

Stability: mostly stable

What erhe reads and writes of glTF 2.0 / 2.1 and of OpenUSD, feature by
feature, and where it falls short. glTF goes through `erhe::gltf` (fastgltf,
`doc/erhe/gltf.md`) and is the editor's native scene format
(`doc/editor/scene_serialization.md`); USD goes through `erhe::usd` (LightUSD,
`doc/erhe/usd.md`) and is a second, independent scene format
(`doc/erhe/usd_compatibility_design.md`). Each row is a one-line verdict; the
document named in its notes owns the behavior, and the work that closes a gap
is a plan linked from "Future work".

Legend: **yes** - read or written as the format means it; **partial** -
carried with the loss the notes name; **no** - not carried (dropped, ignored
or skipped with a log line); **n/a** - the direction does not apply.

## glTF

### Core

| Feature | Import | Export | Notes |
|---|---|---|---|
| `.glb` | yes | yes | the round-trip form |
| `.gltf` + external / data-URI buffers | yes | partial | the text export writes no buffer URI and cannot be re-imported (`doc/plans/gltf.md`, "Text .gltf plus .bin variant") |
| Scenes | partial | partial | the first scene only; `asset.scene` is ignored. One scene per file is the glTF 2.1 direction |
| Node hierarchy, TRS and `matrix` | yes | yes | erhe keeps TRS |
| Meshes, indexed triangles | yes | yes | two erhe meshes of identical content share one glTF mesh on export |
| Non-indexed primitives | no | n/a | skipped with an error |
| Primitive modes other than TRIANGLES | partial | no | the mode is recorded on `Triangle_soup::primitive_type`, but the renderers draw every fill range as a triangle list |
| Attributes `POSITION`, `NORMAL`, `TANGENT` | yes | yes | tangents are generated (MikkTSpace) when a normal texture needs them and none are authored |
| `TEXCOORD_n` | partial | partial | sets 0..2 (`texcoord_2` is the lightmap set); a material reading a higher set falls back to set 0 with a warning |
| `COLOR_n` | yes | yes | |
| `JOINTS_n` / `WEIGHTS_n` | yes | yes | |
| Custom `_ATTRIBUTE`s | no | no | |
| Accessor formats, normalized integers, `KHR_mesh_quantization` | yes | n/a | an unsupported format skips the primitive with an error |
| Sparse accessors | no | no | the base buffer view is loaded and an error is logged, so the data is wrong |
| Morph targets and `weights` | no | no | absent end to end; a `weights` animation channel is skipped with a warning |
| Materials: metallic-roughness, base color, emissive, normal, occlusion | yes | yes | `normalTexture.scale`, `occlusionTexture.strength` carried |
| `alphaMode` / `alphaCutoff` / `doubleSided` | yes | yes | |
| Samplers (filters, wrap) | yes | yes | |
| Images PNG / JPEG | yes | yes | export re-embeds the retained source bytes; a texture whose source cannot be reconstructed loses its slot with a warning (`doc/editor/scene_serialization.md`, "What is not persisted") |
| Cameras, perspective and orthographic | yes | yes | |
| Skins | yes | yes | a skin whose joints are outside the first scene fails |
| Animations on node TRS, STEP / LINEAR / CUBICSPLINE | yes | yes | channels on other properties export nowhere (USD carries them) |
| Several animations playing at once | no | n/a | `Animation_player` plays one |
| Non-destructive playback (rest pose) | no | n/a | a sampled pose is written into the node transform |
| `extras` | partial | partial | the legacy erhe carriers are read; editor state rides the `ERHE_*` extensions instead |
| `extensionsRequired` naming an unsupported extension | partial | n/a | the file loads and the extension is ignored |

### Khronos and vendor extensions

| Extension | Import | Export | Notes |
|---|---|---|---|
| `KHR_lights_punctual` | yes | yes | an absent `range` becomes 1000 rather than infinite |
| `KHR_texture_transform` | yes | yes | including the `texCoord` override |
| `KHR_texture_basisu` (KTX2) | yes | partial | transcoded to BC7, ASTC 4x4 or RGBA8 by device. Export, for this and the two rows below: the retained image bytes are written as the texture's core `source` with no extension object, which erhe reads back and a conforming viewer does not |
| `EXT_texture_webp` | yes | partial | |
| `MSFT_texture_dds` | yes | partial | BC formats |
| `KHR_mesh_quantization` | yes | no | export writes float attributes |
| `EXT_meshopt_compression` | yes | no | decoded by fastgltf |
| `KHR_draco_mesh_compression` | no | no | disabled in both extension masks |
| `EXT_mesh_gpu_instancing` | partial | no | expanded into one child node per instance; ignored on a skinned mesh |
| `KHR_materials_emissive_strength` | yes | no | folded into `emissive`; export writes the product as `emissiveFactor`, so a value above 1 leaves the core range |
| `KHR_materials_unlit` | yes | yes | `Bxdf_model::unlit` |
| `KHR_materials_ior` | yes | yes | |
| `KHR_materials_transmission` | partial | partial | factor only; `transmissionTexture` is not read |
| `KHR_materials_variants` | yes | yes | per instantiated mesh (`doc/erhe/gltf.md`) |
| `KHR_materials_clearcoat`, `_sheen`, `_specular`, `_iridescence`, `_volume` | no | no | parsed by fastgltf, not mapped to erhe material fields |
| `KHR_materials_pbrSpecularGlossiness` | no | no | parsed, not mapped |
| `KHR_materials_anisotropy` | no | no | erhe has anisotropic roughness, carried in `ERHE_material` only |
| `KHR_materials_dispersion`, `_diffuse_transmission` | no | no | |
| `KHR_animation_pointer` | no | no | |
| `KHR_xmp_json_ld` | no | no | |
| `KHR_implicit_shapes` + `KHR_physics_rigid_bodies` | yes | partial | plane shapes, inertia overrides, world-attached joints and static-body mass are not written (`doc/erhe/khr_physics_rigid_bodies_support.md`, "Known limitations") |
| `EXT_mesh_polygon` (draft) | no | no | polygon rings ride `ERHE_geometry` until it ratifies (`doc/plans/gltf.md`) |

### glTF 2.1 proposals

| Feature | Import | Export | Notes |
|---|---|---|---|
| Unified `files` array, `externalAssets`, `minVersion` (KhronosGroup/glTF#2585) | yes | yes | carried by the `tksuoran/fastgltf` fork; the editor's prefab layer resolves the references (`doc/plans/gltf_prefabs.md`) |
| Object `uid` (KhronosGroup/glTF#2597) | yes | yes | stable across re-saves; lights and item-less objects carry none |

### erhe vendor extensions

The `ERHE_*` extensions (`doc/gltf_extensions/README.md`) carry the erhe state
glTF has no form for: node, camera, light and material properties, polygon
geometry, scene settings, collections, brushes, node graphs, physics joints,
asset references. They are read and written by erhe only; another viewer
renders the core content and ignores them. The `ERHE_` prefix is not yet
registered with Khronos (`doc/plans/gltf.md`).

## USD

### Files, platforms and composition

| Feature | Read | Write | Notes |
|---|---|---|---|
| `.usda` | yes | yes | |
| `.usdc`, `.usdz` | yes | no | a stage opened from either saves as `.usda`; `.usdz`-packed textures are read out of the archive |
| Build platforms | partial | partial | `ERHE_USD_LIBRARY=lightusd` on the Windows wrappers and Android; macOS and Linux build without USD |
| Asynchronous load | no | no | load and save run on the calling thread |
| `upAxis`, `metersPerUnit` | yes | yes | applied as one transform on the top-level prims; an `X`-up stage is not rotated |
| `defaultPrim` | yes | yes | |
| `subLayers` | partial | no | composed at load; a save writes one flattened layer. A sublayer inside a `.usdz` is not composed |
| `references`, internal references | yes | yes | one arc record per arc; a `.mtlx` target is refused |
| `payload` | yes | yes | loaded eagerly, no load policy |
| `inherits` from a `class` prim | yes | yes | the class prim becomes a `Style` item; a second target or a non-class target is warned about and composes nothing |
| `specializes` | no | no | |
| Variant sets | partial | yes | the material-binding, `def`-child and arc opinions of the selected variant are applied; other opinions are counted and reported |
| `over` prims, `active`, specifier | yes | yes | |
| Session layer, live re-composition, layer-by-layer editing | no | no | `doc/erhe/usd_compatibility_design.md`, "Out of scope" |
| Value clips | no | no | |

### Geometry and scene objects

| Feature | Read | Write | Notes |
|---|---|---|---|
| `Xform` with its authored xformOp stack | yes | yes | written back as authored |
| `Scope` | yes | yes | |
| `Mesh`, `subdivisionScheme = none` | yes | yes | becomes normative polygon geometry |
| `Mesh`, other subdivision schemes | partial | n/a | imported as a triangle soup; erhe does not subdivide at render time |
| `holeIndices` | yes | no | hole faces are not drawn |
| `GeomSubset` (`materialBind`) | yes | yes | one erhe primitive per subset |
| `Cube`, `Sphere`, `Cone`, `Cylinder`, `Capsule` (+ `_1`) | partial | partial | tessellated into a `Mesh` on import; a save writes `def Mesh` |
| `normals`, `primvars:st`, `displayColor` / `displayOpacity` | yes | yes | |
| Secondary UV sets `st1`, `st2` | no | no | |
| `primvars:tangents` / `bitangents` | yes | no | |
| `creaseIndices` / `creaseSharpnesses` | no | no | erhe's `edge_sharpness` has the mapping (`doc/erhe/usd_compatibility.md`) and no reader or writer |
| `doubleSided`, `visibility`, `purpose` | yes | yes | |
| `PointInstancer` | partial | yes | expanded into one prim per instance, no GPU instancing |
| `instanceable` | no | no | |
| `Points`, `BasisCurves`, `NurbsCurves`, `NurbsPatch`, `Volume`, `UsdRender`, `UsdMedia` | partial | partial | the prim, its name and children survive as a `Typed` item; its schema attributes and any drawing do not |
| `UsdGeomModelAPI` draw modes | yes | yes | `origin`, `bounds`, `cards` drawn by the editor; an empty card face is drawn flat rather than borrowing the opposite image |
| `UsdCollectionAPI` | yes | yes | as item tags |
| erhe-only values | yes | yes | `erhe:Owner:name` custom attributes; a node-held value of another class does not read back (`doc/plans/usd_compatibility.md`) |

### Shading

| Feature | Read | Write | Notes |
|---|---|---|---|
| `UsdPreviewSurface` + `UsdUVTexture` + `UsdTransform2d` | yes | yes | including channel outputs and texture `scale` / `bias` |
| `UsdPrimvarReader` of `displayColor` / `displayOpacity` | yes | yes | any other primvar is warned about |
| Inline OpenPBR / Standard Surface network | yes | partial | written only for anisotropic or transmissive materials |
| MaterialX `.mtlx` documents | no | no | `LIGHTUSD_WITH_USDMTLX` is off; `colorSpace` metadata on a shader attribute fails the parse |
| Texture wrap | yes | yes | `clamp` and `black` both become clamp-to-edge |
| Texture filters | no | partial | written as `erhe:` attributes only |
| PNG / JPEG / BMP / TGA images | yes | yes | |
| `.hdr` / `.exr` images | no | no | |
| erhe texture and geometry node graphs | yes | yes | as marked `NodeGraph` prims (`doc/erhe/usd_node_graphs.md`) |
| A graph-fed material slot with its own factor | partial | partial | the factor is lost |

### Lights and cameras

| Feature | Read | Write | Notes |
|---|---|---|---|
| `DistantLight`, `SphereLight`, `SphereLight` + `ShapingAPI` | yes | yes | directional, point, spot |
| `RectLight`, `DiskLight`, `CylinderLight` | partial | no | imported as point lights |
| `DomeLight` | partial | yes | constant ambient only; `texture:file` is not sampled (no environment map) |
| Other UsdLux types | no | no | |
| `ShadowAPI`, color temperature, `exposure` | yes | yes | |
| `Camera`, perspective and orthographic | yes | yes | an infinite far plane is written finite with a warning |

### Skinning, animation and physics

| Feature | Read | Write | Notes |
|---|---|---|---|
| `SkelRoot`, `Skeleton`, `SkelBindingAPI` | yes | yes | |
| `SkelAnimation` | yes | yes | |
| Blend shapes | no | no | |
| Time samples on xformOps | yes | yes | one animation per file |
| Time samples on light, material and `visibility` attributes | yes | yes | the attributes listed in `doc/erhe/usd_compatibility.md`, "Animation"; any other attribute is warned about |
| `Ts` splines | partial | partial | on `intensity`, `roughness`, `metallic`, `opacity` |
| `UsdPhysics` bodies, colliders, mass, materials, collision groups, scene | yes | yes | |
| `UsdPhysics` joints | yes | yes | revolute, prismatic, fixed, spherical and distance read into six-DOF limits; written as a generic `PhysicsJoint` |
| PhysX vendor schemas | no | no | gravity factor and trigger ride `erhe:` attributes |

## Across the two formats

A glTF scene saves as glTF and a USD scene as USD. Converting a scene from one
format to the other, and a prefab of one format inside a scene of the other,
are out of scope (`doc/erhe/usd_compatibility_design.md`, "Out of scope").

## Future work

- [plans/gltf.md](../plans/gltf.md) - glTF persistence gaps.
- [plans/gltf_prefabs.md](../plans/gltf_prefabs.md) - glTF 2.1 prefab phases.
- [plans/usd_compatibility.md](../plans/usd_compatibility.md) - USD gaps, ranked.
