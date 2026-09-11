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

Both are the ROOT layer's: USD composes a reference or payload target without
re-applying the target file's own `upAxis` / `metersPerUnit`, and the composing
stage's correction reaches the target's content through the carrier prim. A
load of such a target passes `erhe::usd::Stage_metrics::referenced` and applies
the identity, while `Usd_data::up_axis` / `meters_per_unit` still report the
file's own values for the caller's log and UI.
| `timeCodesPerSecond` | `1` (or the animation's sample rate) | glTF animation time is seconds |
| `defaultPrim` | the scene root | erhe scenes have one root node, and the root itself is not a prim (its name is outside every item path), so the exporter names the single top-level prim; a scene with several gets one `World` `Xform` gathering them, which is then the default prim |

## Identity and addressing

| erhe | USD | notes |
|---|---|---|
| `Item_base` name (`get_reference_path()` returns the name for an item outside a hierarchy) | prim name (one path component) | USD requires sibling-unique valid identifiers; erhe keeps names sibling-unique (`Hierarchy::make_sibling_unique_name`, applied when a child is attached), and the exporter sanitizes a name that is not a valid USD identifier (plan step E1) |
| item path (`Hierarchy::get_path()`, `erhe::find_by_path`; `get_reference_path()` returns it, and stored references, expressions and MCP take it) | prim path (`/Root/Child/Leaf`) | an erhe path is relative to the root, whose own name it excludes (`Child/Leaf`); one form addresses every prim, content-library resources and their scopes included |
| `Item_base::m_gltf_uid` (glTF 2.1 uid) | none; identity is the path | a uid can ride as `customData` |
| `Item_type` bit / `get_type_name()` | prim `typeName` | one erhe class per USD schema, see "Object model" |
| owner type chain (`Owner_type`, D27) | schema inheritance (`Xformable` > `Gprim` > `Mesh`) | |
| content library (per-scene index of the scene's resources) | the prim tree itself | a resource is a prim where it sits; the library only indexes them (`src/editor/content_library/notes.md`). A folder is a `Scope`, and each kind has one `Scope` below the scene root (`/Materials`, `/Brushes`, ...) - where a resource goes when nothing else places it. glTF carries the position in `ERHE_scene` `library_folders` (`doc/gltf_extensions/ERHE_scene.md`), where a path names any prim of the tree; in USD the tree is the file, so the prim's place in the layer is the position, and the `Material` prim row of "Object model" says which kinds a USD file carries so far |

## Object model

USD has one typed prim per object; erhe is moving to the same shape
(`doc/usd-compatibility-plan.md` C5) and still carries some object kinds as
node attachments. The mapping an exporter applies and an importer inverts:

| erhe | USD | notes |
|---|---|---|
| `Xform` (transform + children) | `Xform` (`UsdGeomXformable`) | an `Xformable` holds the xformOp stack it was authored with (`erhe::scene::Xform_op_stack`) next to the single T\*R\*S the stack composes to, so an imported stack - op types, suffixes, `!invert!` flags, authored value precisions and `!resetXformStack!` - is written back as authored; an edit lands in the op the stack designates (`src/erhe/scene/notes.md`, authored xformOp stacks). A prim erhe created carries no stack and writes one `xformOp:transform`. glTF carries the composed T\*R\*S alone |
| `Scope` (children only, no transform) | `Scope` | a transform composes through it to the nearest transformable ancestor. A content-library folder is one, and so is each kind's scope (`/Materials`, `/Brushes`, ...); every `Scope` a USD file authors round-trips as itself, the scope holding a stage's `Material` prims included. Every `Scope` of the tree is written where it sits, whatever it holds, so an empty folder and an empty kind scope are prims of the saved layer and the folder tree survives the save (`doc/usd-compatibility-plan.md` E4d). A kind scope is recognized on reload by its name, in the identifier spelling the stage carries |
| `Typed` (`typeName` token, children) | every other `typeName`, and a typeless `def` | the class a prim gets when erhe has none for its `typeName` (`Points`, `SkelRoot`): its name, its place in the tree and its children round-trip, its schema attributes do not. A transform authored on such a prim is dropped with one warning |
| `Mesh` prim (`erhe::scene::Mesh`, an `Xformable`) | `Mesh` prim, and the primitive schemas `Cube`, `Sphere`, `Cone`, `Cylinder`, `Capsule`, `Cylinder_1`, `Capsule_1` | one to one: the erhe mesh carries its own transform, name and children, and a parent holds any number of `Mesh` children. A primitive schema imports as the mesh its schema attributes describe, tessellated by the erhe generator of that shape and with the `axis` baked into the geometry (`src/erhe/usd/notes.md`, "Import"); the item is a `Mesh`, so a save writes `def Mesh` with points and the `Cube` spelling is not kept |
| `Point_instancer` prim (`erhe::scene::Point_instancer`, a `Boundable`) | `PointInstancer` prim (`UsdGeomPointInstancer`) | the instancer is expanded into prims: each prototype stays where the file put it and is held abstract (no `Item_flags::content`), and each instance is a child `Xform` holding an internal reference to its prototype. The instance prims ARE `positions` / `orientations` / `scales` / `protoIndices`, all four recomputed on save - the first three from their transforms, the fourth from the prototype each one references - and `rel prototypes` is the abstract children in tree order (`src/erhe/usd/notes.md`, "Point instancers"). erhe draws one prim per instance - there is no GPU instancing |
| `erhe::primitive::Material` (a `Typed` prim) | `Material` prim (`UsdShadeMaterial`) with its `UsdPreviewSurface` / `UsdUVTexture` shader network as child prims | a material is a prim of the erhe tree wherever the user puts it, and both formats carry that: the writer writes the `Material` prim where the material sits and the reader parents it where the stage puts it, so a stage keeping its materials in `/Looks` reads and writes back as a `Scope` named `Looks` holding them. A `material:binding` is resolved by the prim's path, so two materials of one name in two scopes stay apart. The reader creates a `Materials` kind scope only for a material the file gave no place; the shader network below a `Material` prim is namespace, so a prim parented to an erhe material is not written. The other resource kinds are `doc/usd-compatibility-plan.md` step E4 |
| `Xform` with several applied-API-schema attachments (`Node_physics`, `Node_joint`, `Layout`, `Brush_placement`, `Prefab_instance`, `Frame_controller`, `Grid`) | `Xform` with those schemas applied to it | what USD applies to a prim as an API schema is what erhe attaches to a node |
| `Mesh` prim + `Mesh_primitive` list | `Mesh` prim + `GeomSubset` per primitive (`familyName = materialBind`) | one material per subset via `MaterialBindingAPI` |
| `Light` prim (`erhe::scene::Light`, an `Xformable`) | `UsdLux` prim, see "Lights" | one to one: the erhe light carries its own transform, name and children; `light_type` picks the UsdLux schema |
| `Camera` prim (`erhe::scene::Camera`, an `Xformable`) | `Camera` prim, see "Cameras" | one to one: the erhe camera carries its own transform, name and children |
| `Node_physics` / `Node_joint` attachments | `UsdPhysics` API schemas / joint prims, see "Physics" | |
| `Skin` | `UsdSkel` (`SkelRoot`, `Skeleton`, `SkelBindingAPI`) | the skin's pivot is the `Skeleton` prim and its joints are `Xform` prims below it, so a joint animates the way any prim does; `inverse_bind_j` is `inverse(bind_j) * geomBindTransform` and a save splits it back with an identity `geomBindTransform` for the skeleton's first skin (`src/erhe/usd/notes.md`, "Skinning") |
| `editor::Brush` | `Brush` (custom `typeName`, no USD schema) | a brush is a prim where it sits: `erhe:Brush:density` and `erhe:Brush:normal_style` custom attributes, `material:binding` for the material a placed instance gets, and the geometry as a child `def Mesh "geometry"` with `subdivisionScheme = none` (`src/erhe/usd/notes.md`, "Brush prims"). `purpose` is derived from the brush flag (M3), so it is not authored |
| `editor::Graph_texture` | `NodeGraph` marked with `erhe:graph:format` | a texture graph is a prim where it sits, holding one generic `Shader` child per node; see "Texture node graphs" |
| `editor::Graph_mesh` | `NodeGraph` marked with `erhe:graph:format` | a geometry graph is the same prim form, with its nodes' `info:id` under `erhe:geometry:` and its evaluated geometry as a child `def Mesh "result"`; see "Geometry node graphs" |
| `Layout` / `Layout_item`, `Brush_placement`, `Grid`, `Rendertarget_mesh` | custom (codeless) schemas or namespaced custom attributes (`erhe:...`) | editor domain, no USD counterpart; the attribute form is the qualified-name row of "Property system" |
| prefab instance (`Prefab_instance`, glTF 2.1 externalAssets) | `references` (or `payload`) composition arc | one attachment per arc, in the authored order; the arc's target file, prim path and form are what the attachment records, and a save writes them back |
| item tags (`ERHE_collections`) | `UsdCollectionAPI` (`collection:<name>:includes`) on the default prim, one collection per tag | |
| per-scene settings (`ERHE_scene`) | root-layer `customLayerData` or a custom API schema on the root prim | |
| `EXT_mesh_gpu_instancing` (import expands into child nodes) | `PointInstancer` / `instanceable` | erhe has no render-level instancing; both expand into prims (`erhe::scene::Point_instancer` for the USD side) |

## Property system

erhe's property system (`doc/property-system.md`) is the part of erhe
that is closest to USD's value-resolution model; the mapping is the basis
for import (USD opinions -> erhe layers) and export (erhe layers -> USD
opinions).

| erhe (`erhe::property`) | USD | notes |
|---|---|---|
| registered property (name, type, owner type, default) | schema attribute (name, type, fallback) | registration order has no USD meaning |
| local value (`Value_source::local`) | authored opinion in the layer | export writes local values only; import writes one exactly for an attribute the composed prim reports as authored (LightUSD `authored()`, which counts an opinion arriving over a reference or a sublayer) |
| default (`Value_source::default`) | schema fallback | never written, where the two fallbacks agree. Where they differ the importer writes the USD fallback as a local value so that the composed result is USD's: `diffuseColor` falls back to 0.18 against erhe's white `base_color`, which is the only surface input erhe carries that differs (see "Materials"). `visible` / `purpose` keep the value the item derives |
| `inherits` flag + closest-ancestor read (R8, D8) | primvar namespace inheritance; `visibility` and `purpose` inheritance | USD inherits only primvars and a few tokens; erhe inherits any flagged property |
| reference layer (D33): the value an instance item's template counterpart supplies itself | the referenced prim's own opinions under a `references` / `payload` arc, weaker than the referencing layer's | a local value on the instance item is the `over` opinion; the counterpart's inherited values are not carried, the instance's own tree inherits (composition table below) |
| style layer (D25) and `Style` items (`doc/style-library.md`) | `class` prim + `inherits` arc | a class prim is a `Style` item at the place the class prim has, its opinions the style's local values; the item's local values are stronger, as in LIVRPS. Every value of a class prim travels as an `erhe:Owner:name` custom attribute, a class prim having no schema, while `visibility`, `purpose` and `active` keep their native forms. USD composes every `inherits` target, an erhe style has one source (M7): the first target that names a class prim becomes the item's style, and a second target, a target that is not a class and a target that names no prim are each one warning. A `def` descendant of a class prim is a prototype: a prim of the tree held abstract, imported under the `Style` item with `Item_flags::content` clear, and written back as the `def` it is; a reference that names it clones it as content |
| folder-held category values (D30, `Material.roughness` on a Materials folder) | opinions on an ancestor `Scope`, read through primvar-style inheritance | no standard USD mechanism inherits material inputs; carry as custom attributes on the scope |
| node-held attachment values (D30, `Light.color` on an empty node) | same as folder-held values | |
| attached property (R7, `Layout.align_y` on a child node) | applied API schema attribute (`layout:alignY`) | |
| secondary / attached qualified name `Owner.name` | namespaced attribute `erhe:Owner:name` | `.` in erhe, `:` in USD, under the `erhe` namespace. This is the form every erhe-only property value takes on a prim: `custom float erhe:Light:temperature = 5000`, `custom color3f erhe:Material:emissive = (1, 0.5, 0)`, `custom bool erhe:Mesh:shadow_cast = true`, `custom token erhe:purpose_hint = "guide"` for a property of the prim's own class. The importer reads the USDA literal, drops brackets, commas and quotes, and parses the result with the property type's `from_string` (D16), so a tuple, an array, a token, a string, a number and a bool all travel. It resolves the name against the prim the `typeName` made, taking `Owner.name` either as a holder addresses it (attached R7, secondary D30) or as `name` on an object of exactly that class. A name that resolves to no property, and a value that fails to parse or to validate, are skipped with one warning each |
| `double` value type (`Property_type::double_floating`) | `double` attribute | the carrier for USD `double` transforms and time codes; a one-component numeric value wherever `float` is one |
| `glm::mat4` value type (`Property_type::mat4`) | `matrix4d` attribute | glm is column-major with column vectors, USD row-major with row vectors, so one transform is the same 16 numbers in the same order and the conversion is an element-for-element copy |
| asset path value type (`Property_type::asset_path`) | `asset` attribute (`@path@`) | one `std::string path`, kept apart from a plain string; its text form is the path verbatim, so the importer takes the text between the `@` (or `@@@`) delimiters as it stands |
| `float[]` value type (`Property_type::float_array`) | `float[]` attribute | space-separated components in erhe text, `[1, 2, 3]` in USDA; the property's own type decides how the numbers are read back |
| `int[]` value type (`Property_type::int_array`) | `int[]` attribute | same as `float[]`, with integer components |
| enumeration (D2a) | `token` attribute with `allowedTokens` | labels travel as tokens |
| object reference (D28, material of a primitive, texture of a slot) | relationship (`material:binding`) or connection (`inputs:file`) | |
| bridged property (D18, node TRS) | attribute whose value the schema computes from another representation (`xformOp:*`) | always local, never inherited: same as xformOps. A write through one of these goes on into the prim's authored stack the same way a matrix write does (the `Xform` row above) |
| computed property (D26, `world_translation`, `Light.flux`) | computed value (`ComputeLocalToWorldTransform`, `extent`) | not authored; a writable computed (D26 `writes`) authors its source |
| expression / binding (D22) | none (closest: `UsdShade` connections) | erhe-only; carried as custom string metadata if exported at all |
| `Property_set` (D17) | a `PrimSpec`'s property dictionary | |
| sealing (D24, `lock_edit`) | none (layer permission / `instanceable` are the nearest) | USD seals no property: a USD-backed prefab instance never seals; `lock_edit` on an instance subtree is a glTF prefab option only (plan X2) |
| `Item_base::active` (bool, default true, not `inherits`) | `active` prim metadata | the item's own opinion; the subtree effect USD gives it - everything below an inactive prim is out, whatever it says of itself - is the derived `Item_flags::active` bit, recomputed for the subtree on a value or parent change and never written to a file. A false value takes the item and its subtree out of rendering, picking, raytracing, shadow casting and the physics world, and dims the row in the item tree. Written only when the value is local; import reads the metadatum, and the Tydra conversion the importer walks keeps inactive prims, so the opinion round-trips. The way a prim inside a reference is taken out; an `over` carries it |
| `Item_base::defined` (bool, default true, not `inherits`) | the prim's composed specifier (`def` / `over`) | the item's own opinion of whether the prim is defined. A prim no layer defines composes as `over`, and USD's default traversal predicate reaches neither it nor anything below it, so `defined = false` joins `active` in the derived `Item_flags::active` bit and takes the item and its whole subtree out of rendering, picking, raytracing, shadow casting and the physics world. The prim itself stays on the stage and is a valid reference target, which is why the item is kept. The specifier is its own carrier: import reads `Prim::specifier()`, the writer lowers a `def` to `over` for a false value, and no metadata or `erhe:` attribute is written. A `class` prim is a `Style` item and keeps its specifier |
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
| `texcoord_0..2` | `primvars:st`, `primvars:st1`, `primvars:st2` (`texCoord2f[]`) | `texcoord_2` is the lightmap UV set. The two conventions put the image origin in opposite corners - USD's `st` at the bottom-left, erhe's (glTF's) at the top-left - so every texcoord is read and written through `v' = 1 - v`; the formula and what it does to a `UsdTransform2d` are stated once in `src/erhe/usd/notes.md`, "Texture coordinates" |
| `color_0..1` | `primvars:displayColor` (+ `displayOpacity`), `primvars:color1` | |
| `tangent`, `bitangent` | `primvars:tangents`, `primvars:bitangents` | USD has no schema slot; primvar by convention |
| `joint_indices_n` / `joint_weights_n` | `primvars:skel:jointIndices` / `primvars:skel:jointWeights` (`elementSize`) | |
| `edge_sharpness` (edge) | `creaseIndices` / `creaseLengths` / `creaseSharpnesses` | the one edge attribute with a USD form |
| `Gprim.double_sided` (a prim property, not an attribute of the geometry) | `doubleSided` on the geometry prim | the prim's own opinion, read as a local value when authored and written back when local. It is one half of the rule `erhe::scene::is_double_sided()` spells: a primitive is drawn from both sides when the prim asks for it or when its material does (the `double_sided` row of "Materials"). Both directions carry the value in the schema attribute, so it never also rides as an `erhe:` custom attribute |
| (geometry-normative polygon mesh) | `subdivisionScheme = none` | Catmull-Clark is an erhe operation, not a render-time scheme. The importer builds erhe geometry for exactly this value; every other scheme (including USD's `catmullClark` fallback) imports as a triangle soup |

## Materials

| erhe `Material` property | USD `UsdPreviewSurface` input / other | notes |
|---|---|---|
| `base_color`, `base_color_texture` | `diffuseColor` | a connected input takes its value from the texture, so the erhe factor - which the shader multiplies the texel with - is the texture's `inputs:scale` on the channels that input reads, in both directions. That is what makes a textured `emissiveColor` visible: erhe's emissive factor is zero by default. `diffuseColor` is the one input whose schema fallback (0.18 grey) is not the erhe default, so an unauthored one imports as a local 0.18 and a save authors the effective value whenever it differs from 0.18 - an erhe default white included |
| `opacity`, `alpha_cutoff`, `blending_mode` | `opacity`, `opacityThreshold` | blend vs mask is a threshold in USD |
| `roughness` (x), `metallic`, `emissive`, `ior` | `roughness`, `metallic`, `emissiveColor`, `ior` | erhe's anisotropic `roughness.y` has no PreviewSurface input |
| `normal_texture`, `normal_texture_scale` | `normal` via `UsdUVTexture` | `normal_texture_scale` is the bumpiness multiplier glTF's `normalTexture.scale` is, and has no USD carrier of its own |
| `normal_texture_decode_scale`, `normal_texture_decode_bias` | the normal `UsdUVTexture`'s `inputs:scale` / `inputs:bias` | the texel decode, `texel * scale + bias`; the erhe defaults are USD's fallbacks for a normal map, and both are written for every bound normal texture because USD's own attribute fallbacks are not erhe's |
| `occlusion_texture`, `occlusion_texture_strength` | `occlusion` | |
| `metallic_roughness_texture` | separate `metallic` / `roughness` reads of one texture (channel outputs) | erhe has one slot for the pair, so the importer takes the image the `roughness` input names and falls back to the `metallic` one |
| `metallic_channel`, `roughness_channel`, `occlusion_channel`, `opacity_channel` | the `outputs:r` / `g` / `b` / `a` a scalar input connects to | which channel of the slot's texture each scalar input reads. The erhe defaults are glTF's fixed packing (metallic B, roughness G, occlusion R, opacity A), which is what a glTF-derived file authors; the shader indexes the sampled texel with the value, and a save spells the output name from it. An input the file gives a plain value while the other input of the shared metallic-roughness slot is textured reads `Texture_channel::none`: no channel of that image, its own factor alone, and no connection on save. `opacity` reads the base color texture, erhe having no opacity slot of its own, and is connected only where the material names a channel other than alpha; an `inputs:opacity` reading an image of its own is one warning |
| `reflectance`, `transmission`, `bxdf_model`, brushed-metal fields, `use_aniso_control` | none in PreviewSurface; `OpenPBRSurface` / MaterialX carry anisotropy and transmission | erhe-only fields ride as `erhe:` custom attributes, in the form the qualified-name row of "Property system" gives |
| `<slot>_texture_uv_*` | `UsdTransform2d` | USD composes `in * scale`, then the rotation, then the translation, which is the order the erhe slot transform applies. The erhe transform acts on the flipped texcoord, so the two are converted through `v' = 1 - v` in both directions (`src/erhe/usd/notes.md`, "Texture coordinates"). A save authors a `UsdTransform2d` prim between the material's primvar reader and the slot's `UsdUVTexture` for exactly the slots whose transform is not the identity |
| `<slot>_texture_wrap_*`, filters | `UsdUVTexture` `wrapS` / `wrapT`; no filter inputs | `repeat` and `mirror` map onto the erhe address mode of the same name; `clamp`, `black` and the `useMetadata` default all become clamp-to-edge, because erhe has no border color. Both are written for every bound texture; a wrap value on a slot with no texture has no `UsdUVTexture` to ride on. The filters ride as `erhe:` custom attributes |
| a texture image | `UsdUVTexture` `inputs:file` | a file beside the layer, or an entry of the `.usdz` the stage was loaded from: the reader hands the archive entry's bytes over (`Usd_image::bytes`) and the caller decodes those. Radiance `.hdr` is not decoded by either erhe or LightUSD |
| `double_sided` | `erhe:Material:double_sided` on the `Material` prim | the erhe material's own flag (glTF `material.doubleSided`), which USD has no material input for: `doubleSided` in USD is a property of the geometry prim, and one material can be bound by several prims. The prim's own opinion travels in the geometry `doubleSided` attribute instead (the `Gprim.double_sided` row of "Geometry attributes"), and the renderers take either one as asking for both faces |


## Texture node graphs

An erhe texture graph (`editor::Graph_texture`, `doc/texture-graph-plan.md`)
rides a USD file as the `UsdShade` network it is
([`usd-texture-graphs-plan.md`](usd-texture-graphs-plan.md)); the design
document owns the rules, this table owns the mapping rows.

| erhe | USD | notes |
|---|---|---|
| a `Graph_texture` asset | `NodeGraph` prim carrying `custom token erhe:graph:format = "erhe_texture_graph"` | the marker is what makes the network erhe's; a `NodeGraph` without it is a foreign shading network, left to the material conversion. The token is the graph kind, spelled the way the glTF `ERHE_node_graphs` payload spells its graph arrays |
| one graph node | a `Shader` child, `uniform token info:id = "erhe:texture:<factory type>"` | the type name is the one `make_texture_graph_node` takes; a `Shader` whose `info:id` lacks the prefix, or names a type the factory does not make, is one warning and no node |
| the node's editor position | `custom float2 erhe:ui:position` | authored exactly when the node has a canvas position |
| a node parameter | `inputs:<name>` carrying a value | typed from the parameter's own kind: `float`, `int`, `bool`, `float2` (a size), `color3f` / `color4f` (a color), `string` for a name. A value with no USD form - a gradient, a curve - travels as its JSON text in a `string`, with a single quote in place of the double quote, one rule for both |
| a node pin | `inputs:<pin>` / `outputs:<pin>` typed from the pin's `erhe::texgen::Value_type` (`float`, `color3f`, `color4f`) | an input pin with a link is the attribute with a `.connect` to the source node's output; an unlinked pin is the typed attribute alone, so the pin is still in the file |
| the graph's result | the `NodeGraph`'s own `outputs:<pin>` connection | taken from the linked input of the graph's `output` sink node: the value a material slot can name |
| a material slot sampling the graph | the slot's `UsdPreviewSurface` input connected to that interface output | in place of a `UsdUVTexture`, so no image is written for the slot. The baked image is not written at all: a graph loads born dirty and the first evaluation re-bakes it |

## Geometry node graphs

An erhe geometry graph (`editor::Graph_mesh`) rides a USD file as the prim
form a texture graph takes
([`usd-texture-graphs-plan.md`](usd-texture-graphs-plan.md) section 4); the
design document owns the rules, this table owns the mapping rows. Every row
of "Texture node graphs" holds unchanged except the four below.

| erhe | USD | notes |
|---|---|---|
| a `Graph_mesh` asset | `NodeGraph` prim carrying `custom token erhe:graph:format = "erhe_geometry_graph"` | the marker attribute is the same one a texture graph carries and the token is what tells the two kinds apart, so which `info:id` prefix the graph's nodes are under follows from the format |
| one graph node | a `Shader` child, `uniform token info:id = "erhe:geometry:<factory type>"` | a `Shader` whose `info:id` is under another prefix - a texture node inside a geometry graph - is one warning and no node, and the links into it go with it |
| a node pin | `inputs:<pin>` / `outputs:<pin>` typed from the pin's payload kind | `float` for a float, `int` for an int, `bool` for a bool, `float3` for a vec3, `float4` for a vec4 and `matrix4d` for a mat4; the opaque payload pins - geometry, points, instances, sdf, material - are `token`, since an erhe payload has no USD value and the connection is what matters |
| the graph's evaluated geometry | a child `def Mesh "result"` with `subdivisionScheme = none` | written the way a brush writes its geometry, so a viewer without erhe sees what the graph makes. A reload rebuilds the graph from the nodes and re-evaluates it, and reads the child mesh only when the graph carries no node; a graph that has evaluated nothing writes no child |
| a pin name, a node parameter name | the `inputs:` / `outputs:` attribute name in the USD identifier spelling (`sanitize_usd_identifier`) | a geometry node names these for the reader ("x size") and a USD property name is an identifier; the reload matches a recorded name against the spelling the rebuilt node's pin or parameter key takes, so the name itself is unchanged on the erhe side |
| a `Geometry_graph_mesh` attachment (the prim whose mesh the graph makes) and the graph's display / ghost node designation | one `graph_meshes` entry of the `erhe:scene` `customLayerData` block: `graph` (the graph's path), `bound_prims` (the bound prims' paths), `display_node` / `ghost_node` (node names) | the binding is an object reference, which a custom attribute cannot name (see "Property system"), and the designations are erhe's own Houdini-style flags with no USD counterpart. A path is the one the writer plans for that prim (`erhe::usd::plan_usd_prim_paths`) in the item-path spelling, which is the path the item has on reload, so the entry matches exactly; the same rule holds for every scene-block entry that names a prim, `variant_selections` included |

## Lights

| erhe `Light` property | USD (`UsdLux`) | notes |
|---|---|---|
| `light_type` directional / point / spot | `DistantLight` / `SphereLight` (radius 0, `treatAsPoint`) / `SphereLight` + `ShapingAPI` | the presence of `ShapingAPI` on the prim is what makes an imported sphere / point light a spot light, and the exporter applies that schema for exactly a spot light and authors `inputs:radius = 0` for both sphere forms; area lights (`Rect`, `Disk`, `Cylinder`) import as point lights with a log line, `DomeLight` is the scene's ambient light (see the last row) and the remaining types are skipped |
| `color` | `inputs:color` | |
| `intensity` | `inputs:intensity` (and `inputs:exposure` = 0) | unit conventions differ; a conversion factor per light type. erhe has no exposure on a light, so the importer folds the two into `intensity * 2^exposure` and applies no unit conversion of its own |
| `temperature` | `inputs:colorTemperature` + `inputs:enableColorTemperature` | exact match of the erhe property |
| `range` | none (USD lights have no range cutoff) | erhe-only |
| `inner_spot_angle`, `outer_spot_angle` | `ShapingAPI` `inputs:shaping:cone:angle` + `inputs:shaping:cone:softness` | |
| `cast_shadow` | `ShadowAPI` `inputs:shadow:enable` | |
| `flux`, `blackbody` (computed) | not authored | |
| scene ambient light (`ERHE_scene`) | `DomeLight` | the dome's constant radiance is `inputs:color * inputs:intensity * 2^inputs:exposure`, and that is the scene's ambient light; a dome is no prim of the erhe tree and no `erhe::scene::Light`. erhe has no environment map, so `inputs:texture:file` is named in a warning and not sampled. The first dome of a file sets the ambient light, a second one is a warning. A save writes back the domes the load read, as the `DomeLight` prims they were, at the stage root; a scene that read none writes none and carries its ambient light in the `customLayerData` scene block |

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
| animation samplers / channels targeting node TRS (glTF model) | time samples on `xformOp:*` attributes | carried: the samples travel on `erhe::scene::Xform_op` and play as one Animation per file, keyed in seconds; a stack outside `[translate, rotate, scale]` keeps its samples and drives nothing; cubic tangents re-encode as `Ts` splines |
| `Animation_player` playback writing the transform | time-sampled value resolution (stronger than `default`) | erhe overwrites the local value today; the animated layer (property-system section 6) restores the USD distinction |
| channels on arbitrary properties (future) | time samples on any attribute | |
| skins | `UsdSkel` `SkelAnimation` | |

## Composition

erhe has no composition engine; the entries below name what each erhe
mechanism composes as, and what has no erhe counterpart yet.

| erhe | USD composition | notes |
|---|---|---|
| a scene file | a root layer | one scene = one layer stack of one layer |
| prefab instance (`doc/gltf-prefabs-plan.md`) | `references` arc (`R` in LIVRPS) | any layer + prim path target, internal references, one carrier attachment per arc, read and written; a glTF instance seals its subtree, a USD-backed one does not. The carrier is transformable: a typeless or `Scope` prim that authors an arc is read as an `Xform` (USD gives a typeless referencing prim the composed target's type) and written back as `def Xform`; a carrier of a type that carries no transform keeps its type and its arcs are dropped with one warning. The arc's TARGET is any prim: an `Xformable`, a `Scope`, the `Typed` prim a typeless `def` or an unrecognized `typeName` becomes, a root-level `over` with `def` descendants, or a prototype under a `class` prim. A target that carries no transform composes what reaches it through to its children. A `.mtlx` target is a MaterialX document rather than a USD layer: the arc is not instantiated and the reason is named once |
| values a template supplies to an instance | the referenced prims' opinions, weaker than the referencing layer | the reference layer between style and inherited (`doc/property-system.md` D33), read live from the template counterpart |
| a local value inside an instance | an `over` prim with sparse local opinions (`L`) | what an override is is stated once, in `src/erhe/scene/erhe_scene/instance_override.hpp`; an `over` is typeless and so carries every value as an `erhe:Owner:name` custom attribute, a schema-named one (a `Material`'s `roughness`) included; an item's path below the arc's target clone is the `over`'s path below the carrier prim, and the target clone itself is the carrier prim (one level more than USD composes, so a value both author is the carrier's and a transform the target clone overrides is not writable). glTF carries the same list on the carrier node as `ERHE_node.overrides` |
| a material bound inside an instance | a `material:binding` relationship on an `over`, with `MaterialBindingAPI` applied | read and written as the third kind of override (X2). The relationship names the material by an absolute stage path, which can name a prim another arc of the same file supplies (a body asset takes its geometry from one arc and its materials from another), so every carrier's overrides are applied once every arc of the file is instantiated. A binding on one group of facets is the `over` of the group's GeomSubset prim, which erhe resolves to one primitive of the mesh; glTF carries the path as `ERHE_node.overrides[].material` |
| `Style` items | `class` prims + `inherits` (`I`) | read and written as the arc form it is; the style chain (M7) is a class prim inheriting a class prim |
| a variant set on a prim | variant sets (`V`) | material-binding variant sets are read, the selected variant is applied to the meshes (LightUSD composes no variant), and the whole table is written back; a variant that authors anything else has that counted and reported once for the set. The per-scene selection the user switches is the editor half of X4 |
| prefab instance from a `payload` arc | payloads (`P`) | read and written as the arc form it is; erhe's prefab library loads eagerly, so a payload is never deferred |
| none | `specializes` (`S`) | |
| a scene's whole content | a root layer's `subLayers` (`L`) | composed at load, strongest first: the root layer's opinions beat every sublayer, an earlier `subLayers` entry beats a later one, a prim absent from the stronger layers is added whole, and stage metadata the root leaves unauthored comes from the strongest sublayer that authors it. A save writes ONE layer holding the composed content and authors no `subLayers` - erhe edits the flattened stage and has no layer to write an edit back to (`src/erhe/usd/notes.md`, "Sublayers"); a stack the editor could edit layer by layer is future work |
| none | session layer | an undo stack is not a layer |
| `Value_source` | opinion provenance | erhe resolves every arc itself, so the erhe value source IS the composition provenance; the table below restates each source as the USD origin the Properties window and MCP report (`doc/usd-compatibility-plan.md` X5) |

### Where a value comes from

`editor::describe_property_origin` derives this on demand from the item, its
scene's file, the `Prefab_instance` carrier above it and the `Style` it uses;
nothing is stored and no layer is kept alive. `layer` is the file the value is
authored in ("session" for a scene with no file yet), `prim path` the path in
that layer that authors it, `arc` the arc that brings it to the item's own
prim, and `authored as` the attribute the writer spells it as
(`erhe::usd::get_usd_authored_as` owns that naming rule).

| `Value_source` | layer | prim path | arc | authored as |
|---|---|---|---|---|
| `local` | the scene's root layer | the item's own prim path; inside an instance, the `over`'s collapsed path below the carrier | `root layer`, or the carrier's `reference` / `payload` for an override | the native schema attribute, or `erhe:Owner:name` |
| `expression` | the scene's root layer | as `local` | as `local` | not saved: a formula is session state (D14) |
| `reference` | the arc's target file | the counterpart's path below the arc's target prim | `reference` / `payload`, naming the arc's target; a counterpart inside a nested instance appends its own arc after `->` | as `local` |
| `style` | the scene's root layer | the class prim of the style in the chain (M7) that authors the value | `inherits`, targeting that class prim | `erhe:Owner:name` (a class prim is typeless) |
| `inherited` | the ancestor's | the ancestor's | the ancestor's | the ancestor's |
| `computed` | none | none | `none` | computed |
| `default_value` | none | none | `none` | schema fallback |

A glTF-backed scene answers in the same shape: the glTF file is the layer, the
item's M1 path is the prim path, and a value is authored as
`properties["Owner.name"]` of the `ERHE_*` extension that carries the item.
