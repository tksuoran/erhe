# USD compatibility notes

erhe saves scenes as glTF (2.1 + `ERHE_*` extensions; see
`doc/scene_serialization.md`) and is growing USD into a second,
independent scene format. This document is the erhe <-> OpenUSD
**concept and naming mapping**: for every erhe mechanism the USD concept it
corresponds to, so that a USD importer / exporter / composition step is a
table lookup, not a redesign. The steps that make erhe more USD-compatible,
and the order to take them in, are the subject of
`doc/usd-compatibility-plan.md`; this document holds the mapping only.

erhe's glTF extensions keep erhe / glTF-context naming, not USD vocabulary
(a `faceVarying` primvar term would be confusing inside a Khronos file);
the translation between the two vocabularies lives here.

References: OpenUSD `pxr/usd/<domain>/schema.usda` in an OpenUSD checkout
(`<OpenUSD>`) is the normative attribute list per schema; LightUSD
(`<LightUSD>`, its `doc/api-status.md`) is the in-editor USD library, built
when `ERHE_USD_LIBRARY=lightusd` and reached through `erhe::usd`
(`src/erhe/usd/notes.md`). Per-machine clone locations are recorded in
`memory-bank/local/`.

## Stage-level constants

erhe has no stage metadata of its own; the glTF constants are the values
an exporter writes and an importer converts to.

| USD stage metadata | erhe value | notes |
|---|---|---|
| `upAxis` | `Y` | glTF is Y-up; the importer rotates a `Z`-up stage by -90 degrees about X, as part of the one transform it applies to the top-level imported nodes. An `X`-up stage is imported unrotated with a warning |
| `metersPerUnit` | `1` | glTF is metres; the importer scales by it in the same top-level transform as `upAxis` |
| `timeCodesPerSecond` | `1` (or the animation's sample rate) | glTF animation time is seconds |
| `defaultPrim` | the scene root | erhe scenes have one root node, and the root itself is not a prim (its name is outside every item path), so the exporter names the single top-level prim; a scene with several gets one `World` `Xform` gathering them, which is then the default prim |

## Identity and addressing

| erhe | USD | notes |
|---|---|---|
| `Item_base` name (`get_reference_path()` returns the name for an item outside a hierarchy) | prim name (one path component) | USD requires sibling-unique valid identifiers; erhe keeps names sibling-unique (`Hierarchy::make_sibling_unique_name`, applied when a child is attached), and the exporter sanitizes a name that is not a valid USD identifier (plan step E1) |
| item path (`Hierarchy::get_path()`, `erhe::find_by_path`; `get_reference_path()` returns it, and stored references, expressions and MCP take it) | prim path (`/Root/Child/Leaf`) | an erhe path is relative to the root, whose own name it excludes (`Child/Leaf`); the same form addresses content-library folders |
| `Item_base::m_gltf_uid` (glTF 2.1 uid) | none; identity is the path | a uid can ride as `customData` |
| `Item_type` bit / `get_type_name()` | prim `typeName` | one erhe class per USD schema, see "Object model" |
| owner type chain (`Owner_type`, D27) | schema inheritance (`Xformable` > `Gprim` > `Mesh`) | |
| content library (per-scene, categories + folders) | `Scope` prims under the stage (`/Materials`, `/Looks`, ...) | folder tree = scope hierarchy |

## Object model

USD has one typed prim per object; erhe has a `Node` with attachments. The
mapping an exporter applies and an importer inverts:

| erhe | USD | notes |
|---|---|---|
| `Node` (transform + children) | `Xform` (`UsdGeomXformable`) | glTF quantizes to one T*R*S; USD allows arbitrary xformOp stacks, imported as composed then decomposed |
| `Node` whose only attachment is one `Mesh` | `Mesh` prim (Xformable itself) | the natural form; an importer creates node + mesh attachment |
| `Node` with several attachments | `Xform` with one typed child prim per attachment | child prims carry no transform of their own |
| `Mesh` attachment + `Mesh_primitive` list | `Mesh` prim + `GeomSubset` per primitive (`familyName = materialBind`) | one material per subset via `MaterialBindingAPI` |
| `Light` attachment | `UsdLux` prim, see "Lights" | |
| `Camera` attachment | `Camera` prim, see "Cameras" | |
| `Node_physics` / `Node_joint` attachments | `UsdPhysics` API schemas / joint prims, see "Physics" | |
| `Skin` | `UsdSkel` (`SkelRoot`, `Skeleton`, `SkelBindingAPI`) | |
| `Layout` / `Layout_item`, `Brush_placement`, `Grid`, `Rendertarget_mesh`, graph meshes / textures | custom (codeless) schemas or namespaced custom attributes (`erhe:...`) | editor domain, no USD counterpart; the attribute form is the qualified-name row of "Property system" |
| prefab instance (`Prefab_instance`, glTF 2.1 externalAssets) | `references` composition arc on an `Xform` | USD references are stronger: any target prim, list-edited |
| item tags (`ERHE_collections`) | `UsdCollectionAPI` (`collection:<name>:includes`) on the default prim, one collection per tag | |
| per-scene settings (`ERHE_scene`) | root-layer `customLayerData` or a custom API schema on the root prim | |
| `EXT_mesh_gpu_instancing` (import expands into child nodes) | `PointInstancer` / `instanceable` | erhe has no render-level instancing; an importer expands the same way |

## Property system

erhe's property system (`doc/property-system.md`) is the part of erhe
that is closest to USD's value-resolution model; the mapping is the basis
for import (USD opinions -> erhe layers) and export (erhe layers -> USD
opinions).

| erhe (`erhe::property`) | USD | notes |
|---|---|---|
| registered property (name, type, owner type, default) | schema attribute (name, type, fallback) | registration order has no USD meaning |
| local value (`Value_source::local`) | authored opinion in the layer | export writes local values only; import writes one exactly for an attribute the composed prim reports as authored (LightUSD `authored()`, which counts an opinion arriving over a reference or a sublayer) |
| default (`Value_source::default`) | schema fallback | never written. An unauthored USD attribute leaves the erhe property at the ERHE default, which is not the USD fallback for every attribute: `base_color` stays white where `diffuseColor` falls back to 0.18, and `visible` / `purpose` keep the value the item derives |
| `inherits` flag + closest-ancestor read (R8, D8) | primvar namespace inheritance; `visibility` and `purpose` inheritance | USD inherits only primvars and a few tokens; erhe inherits any flagged property |
| style layer (D25) and `Style` items (`doc/style-library.md`) | `class` prim + `inherits` arc | style values = the class prim's opinions; the item's local values are stronger, as in LIVRPS |
| folder-held category values (D30, `Material.roughness` on a Materials folder) | opinions on an ancestor `Scope`, read through primvar-style inheritance | no standard USD mechanism inherits material inputs; carry as custom attributes on the scope |
| node-held attachment values (D30, `Light.color` on an empty node) | same as folder-held values | |
| attached property (R7, `Layout.align_y` on a child node) | applied API schema attribute (`layout:alignY`) | |
| secondary / attached qualified name `Owner.name` | namespaced attribute `erhe:Owner:name` | `.` in erhe, `:` in USD, under the `erhe` namespace. This is the form every erhe-only property value takes on a prim: `custom float erhe:Light:temperature = 5000`, `custom color3f erhe:Material:emissive = (1, 0.5, 0)`, `custom bool erhe:Mesh:shadow_cast = true`, `custom token erhe:purpose_hint = "guide"` for a property of the prim's own class. The importer reads the USDA literal, drops brackets, commas and quotes, and parses the result with the property type's `from_string` (D16), so a tuple, an array, a token, a string, a number and a bool all travel. It resolves the name against the attachment the prim's type made and then against the node carrying it, taking `Owner.name` either as a holder addresses it (attached R7, secondary D30) or as `name` on an object of exactly that class. A name that resolves to no property, and a value that fails to parse or to validate, are skipped with one warning each |
| enumeration (D2a) | `token` attribute with `allowedTokens` | labels travel as tokens |
| object reference (D28, material of a primitive, texture of a slot) | relationship (`material:binding`) or connection (`inputs:file`) | |
| bridged property (D18, node TRS) | attribute whose value the schema computes from another representation (`xformOp:*`) | always local, never inherited: same as xformOps |
| computed property (D26, `world_translation`, `Light.flux`) | computed value (`ComputeLocalToWorldTransform`, `extent`) | not authored; a writable computed (D26 `writes`) authors its source |
| expression / binding (D22) | none (closest: `UsdShade` connections) | erhe-only; carried as custom string metadata if exported at all |
| `Property_set` (D17) | a `PrimSpec`'s property dictionary | |
| sealing (D24, `lock_edit`) | none (layer permission / `instanceable` are the nearest) | |
| `Item_base::visible` (bool, `inherits`) and `Item_base::purpose` (`Purpose` enumeration, `inherits`) | `visibility` (`inherited` / `invisible`), `purpose` (`default` / `render` / `proxy` / `guide`) | `visible` <-> `visibility`; `purpose` maps token for token, and the value of an item that authors none is derived from its editor-only flag bits (`tool`, `brush`, `controller`, `rendertarget`, `show_in_ui` off), so editor-only content reads `guide`. Import puts both on the node that holds the prim's place in the scene graph, and only when the prim authors the attribute. The remaining item flags have no USD counterpart |
| animated layer (future, property-system section 6) | time samples (stronger than `default`) | prerequisite for importing time samples without clobbering local values |
| `Value_source` of an effective value | opinion provenance (`PcpPrimIndex` node / LightUSD `ArcOrigin`) | erhe already answers "where does this value come from" |
| text form `to_string` / `from_string` (D16, `1 0.9 0.8`) | USDA literal (`(1, 0.9, 0.8)`) | a converter pair, not a change of erhe's form |

## Geometry attributes

`ERHE_geometry` dumps every geogram attribute with an `element` field.
The names are erhe / geogram's own:

| ERHE_geometry `element` | geogram element | USD primvar interpolation | meaning |
|---|---|---|---|
| `mesh`   | mesh    | `constant`    | one value for the whole mesh |
| `facet`  | facet   | `uniform`     | one value per polygon |
| `vertex` | vertex  | `vertex`      | one value per vertex (glTF vertex i == geogram vertex i) |
| `corner` | corner  | `faceVarying` | one value per polygon-corner, in the flat facet_vertex_indices order of ERHE_geometry's polygon encoding |
| `edge`   | edge    | (none)        | one value per edge; the edge index list is serialized alongside. A USD writer carries these as namespaced `constant` arrays plus the edge index list |

| erhe attribute (`geometry.hpp` constants) | USD | notes |
|---|---|---|
| vertex positions | `points` | |
| `ERHE_geometry` facet_vertex_counts / facet_vertex_indices | `faceVertexCounts` / `faceVertexIndices` | deliberately the same encoding shape |
| `normal` (corner) | `normals` (`faceVarying`) or `primvars:normals` | |
| `texcoord_0..2` | `primvars:st`, `primvars:st1`, `primvars:st2` (`texCoord2f[]`) | `texcoord_2` is the lightmap UV set |
| `color_0..1` | `primvars:displayColor` (+ `displayOpacity`), `primvars:color1` | |
| `tangent`, `bitangent` | `primvars:tangents`, `primvars:bitangents` | USD has no schema slot; primvar by convention |
| `joint_indices_n` / `joint_weights_n` | `primvars:skel:jointIndices` / `primvars:skel:jointWeights` (`elementSize`) | |
| `edge_sharpness` (edge) | `creaseIndices` / `creaseLengths` / `creaseSharpnesses` | the one edge attribute with a USD form |
| (geometry-normative polygon mesh) | `subdivisionScheme = none` | Catmull-Clark is an erhe operation, not a render-time scheme. The importer builds erhe geometry for exactly this value; every other scheme (including USD's `catmullClark` fallback) imports as a triangle soup |

## Materials

| erhe `Material` property | USD `UsdPreviewSurface` input / other | notes |
|---|---|---|
| `base_color`, `base_color_texture` | `diffuseColor` | |
| `opacity`, `alpha_cutoff`, `blending_mode` | `opacity`, `opacityThreshold` | blend vs mask is a threshold in USD |
| `roughness` (x), `metallic`, `emissive`, `ior` | `roughness`, `metallic`, `emissiveColor`, `ior` | erhe's anisotropic `roughness.y` has no PreviewSurface input |
| `normal_texture`, `normal_texture_scale` | `normal` via `UsdUVTexture` | |
| `occlusion_texture`, `occlusion_texture_strength` | `occlusion` | |
| `metallic_roughness_texture` | separate `metallic` / `roughness` reads of one texture (channel outputs) | erhe has one slot for the pair, so the importer takes the image the `roughness` input names and falls back to the `metallic` one |
| `reflectance`, `transmission`, `bxdf_model`, brushed-metal fields, `use_aniso_control` | none in PreviewSurface; `OpenPBRSurface` / MaterialX carry anisotropy and transmission | erhe-only fields ride as `erhe:` custom attributes, in the form the qualified-name row of "Property system" gives |
| `<slot>_texture_uv_*` | `UsdTransform2d` | |
| `<slot>_texture_wrap_*`, filters | `UsdUVTexture` `wrapS` / `wrapT`; no filter inputs | |
| `double_sided` | `doubleSided` on the `Mesh` prim | a mesh flag in USD, a material flag in erhe, and one material can be bound by several meshes, so an authored `doubleSided` does not reach the erhe material; `erhe:Material:double_sided` on the `Material` prim is what carries the erhe flag |

## Lights

| erhe `Light` property | USD (`UsdLux`) | notes |
|---|---|---|
| `light_type` directional / point / spot | `DistantLight` / `SphereLight` (radius 0, `treatAsPoint`) / `SphereLight` + `ShapingAPI` | the presence of `ShapingAPI` on the prim is what makes an imported sphere / point light a spot light, and the exporter applies that schema for exactly a spot light and authors `inputs:radius = 0` for both sphere forms; area lights (`Rect`, `Disk`, `Cylinder`) import as point lights with a log line, and `DomeLight` and the remaining types are skipped |
| `color` | `inputs:color` | |
| `intensity` | `inputs:intensity` (and `inputs:exposure` = 0) | unit conventions differ; a conversion factor per light type. erhe has no exposure on a light, so the importer folds the two into `intensity * 2^exposure` and applies no unit conversion of its own |
| `temperature` | `inputs:colorTemperature` + `inputs:enableColorTemperature` | exact match of the erhe property |
| `range` | none (USD lights have no range cutoff) | erhe-only |
| `inner_spot_angle`, `outer_spot_angle` | `ShapingAPI` `inputs:shaping:cone:angle` + `inputs:shaping:cone:softness` | |
| `cast_shadow` | `ShadowAPI` `inputs:shadow:enable` | |
| `flux`, `blackbody` (computed) | not authored | |
| scene ambient light (`ERHE_scene`) | `DomeLight` with a constant color | |

## Cameras

| erhe `Camera` property | USD `Camera` | notes |
|---|---|---|
| `projection_type` | `projection` (`perspective` / `orthographic`) | erhe's asymmetric frustum and per-edge FOV forms map to `horizontalApertureOffset` / `verticalApertureOffset` |
| `fov_y` / `fov_x` | `focalLength` + `horizontalAperture` / `verticalAperture` | USD is physical-camera-first: three values carry two angles, so the exporter fixes `focalLength` at 50 and puts each angle in its aperture, `aperture = 2 * focalLength * tan(fov / 2)`, which the importer's `2 * atan(0.5 * aperture / focalLength)` reads back exactly |
| `ortho_*` | `horizontalAperture` / `verticalAperture` in orthographic mode | USD apertures are in tenths of a scene unit, so `ortho_width` = `horizontalAperture` / 10 and `ortho_height` = `verticalAperture` / 10 |
| `z_near`, `z_far`, `infinite_z_far` | `clippingRange` | infinite far has no USD form |
| `exposure` | `exposure` | exact match |
| `shadow_range` | none | erhe-only |

## Physics

`UsdPhysics` is the closest semantic match of all the domains: bodies,
colliders, joints, materials, filtering and groups all exist on both
sides.

| erhe | USD (`UsdPhysics`) | notes |
|---|---|---|
| `Node_physics` on a node | `RigidBodyAPI` + `CollisionAPI` applied to the prim | |
| `motion_mode` static / kinematic / dynamic | no `RigidBodyAPI` / `physics:kinematicEnabled` / `physics:rigidBodyEnabled` | |
| `mass`, `center_of_mass_offset` | `MassAPI` `physics:mass`, `physics:centerOfMass` | erhe's density-derived default mass = `physics:mass = 0` |
| `initial_linear_velocity`, `initial_angular_velocity` | `physics:velocity`, `physics:angularVelocity` | |
| `is_trigger` | `PhysicsTriggerAPI` | |
| `gravity_factor` | none in core (`PhysxRigidBodyAPI:disableGravity` is vendor) | erhe-only |
| collision shapes (`KHR_implicit_shapes`) | `Cube` / `Sphere` / `Capsule` / `Cylinder` / `Mesh` with `CollisionAPI`, `MeshCollisionAPI` approximation | |
| `Physics_material` `static_friction`, `dynamic_friction`, `restitution`, `density` | `MaterialAPI` `physics:staticFriction`, `physics:dynamicFriction`, `physics:restitution`, `physics:density` | exact match |
| `friction_combine`, `restitution_combine` | none in core (PhysX vendor schema has them) | |
| `linear_damping`, `angular_damping`, `wind_receptivity` | none in core (PhysX vendor schema has damping) | erhe-only |
| `Collision_filter` (systems, collide-with lists) | `CollisionGroup` prims + `FilteredPairsAPI` | |
| `Node_joint` + `Physics_joint_settings` | `PhysicsJoint` subclasses (`Fixed`, `Revolute`, `Prismatic`, `Spherical`, `Distance`) + `LimitAPI` / `DriveAPI` | the erhe six-dof settings-less joint = `PhysicsJoint` with no limits |
| `enable_physics` (`ERHE_scene`), gravity | `PhysicsScene` prim (`physics:gravityDirection`, `physics:gravityMagnitude`) | |

## Animation

| erhe | USD | notes |
|---|---|---|
| animation samplers / channels targeting node TRS (glTF model) | time samples on `xformOp:*` attributes | glTF is keyframe-sampler-first, USD time-sample-first; cubic tangents re-encode as `Ts` splines |
| `Animation_player` playback writing the transform | time-sampled value resolution (stronger than `default`) | erhe overwrites the local value today; the animated layer (property-system section 6) restores the USD distinction |
| channels on arbitrary properties (future) | time samples on any attribute | |
| skins | `UsdSkel` `SkelAnimation` | |

## Composition

erhe has no composition engine; the entries below name what each erhe
mechanism composes as, and what has no erhe counterpart yet.

| erhe | USD composition | notes |
|---|---|---|
| a scene file | a root layer | one scene = one layer stack of one layer |
| prefab instance (sealed subtree, `doc/gltf-prefabs-plan.md`) | `references` arc (`R` in LIVRPS) | |
| edits inside an instance (not possible today: sealed) | `over` prims with sparse local opinions (`L`) | the property system's local layer is the natural carrier once instances are editable and items have paths |
| `Style` items | `class` prims + `inherits` (`I`) | |
| none | variant sets (`V`) | material variants (KHR_materials_variants) would be the first slice |
| none | payloads (`P`) | deferred loading; erhe's prefab library loads eagerly |
| none | `specializes` (`S`) | |
| none | sublayers, session layer | an undo stack is not a layer |
| `Value_source` | opinion provenance | |
