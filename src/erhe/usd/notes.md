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
  `Usd_load_arguments::stage_metrics` says which of the two the file is: a
  `Stage_metrics::root` load is the root layer of its stage and its `upAxis` /
  `metersPerUnit` become the transform on the top-level prims, while a
  `Stage_metrics::referenced` load is a file composed under another stage as a
  reference or payload target, whose own `upAxis` / `metersPerUnit` USD never
  re-applies - it gets the identity, and the composing stage's correction
  reaches the content through the carrier prim. `Usd_data::up_axis` /
  `meters_per_unit` report the file's own values either way.
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
- The UsdGeom primitive schemas - `Cube`, `Sphere`, `Cone`, `Cylinder`,
  `Capsule` and the `Cylinder_1` / `Capsule_1` schema variants - import as
  the meshes they describe, so the erhe item is an `erhe::scene::Mesh` and a
  save writes `def Mesh` with points: the file's `Cube` spelling is not kept.
  The geometry is built from the prim's own schema attributes by the erhe
  generator of that shape (`erhe::geometry::shapes::make_box` /
  `make_sphere` / `make_cone` / `make_cylinder` / `make_conical_frustum` /
  `make_capsule`) rather than from Tydra's tessellation of the same prim,
  which is a triangle list with no shared vertices and therefore no usable
  topology. The tessellation is fixed and documented at the constants
  (32 slices around the axis, 16 stacks for a sphere, 8 per capsule cap, one
  along a cone or cylinder, whose side is ruled), the `axis` token is baked
  into the geometry rather than into the node transform so the prim's own
  xformOps stay what the file authored, and the geometry is normative the
  way a `subdivisionScheme = none` mesh is, so the item carries a `Geometry`
  and its edges. `Capsule_1` needs a tangent cone between its two cap
  spheres, so radii further apart than the height cost one warning and
  become a capsule of the larger radius. The prim binds a material the way
  any Gprim does; it holds no `GeomSubset`, so the mesh has one primitive.
  Its `primvars:displayColor` / `primvars:displayOpacity` become the corner
  color of every corner of the generated geometry, the same attribute a
  `Mesh` prim's primvars fill: a schema prim authors no topology, so the
  first element colors the whole surface and a prim authoring more than one
  element is named in a warning.
  The first save is a change of representation, so the round trip is a fixed
  point from the first reload on rather than from the first save.
- Tydra's `GetPropertyNames` knows a fixed set of prim types and answers
  "TODO: Prim type <name>" for the primitive schemas, so the authored
  property names of one of those prims are read from the prim itself: the
  attributes every `GPrim` carries and the custom properties of its `props`
  map. That is what lets `visibility`, `purpose` and the `erhe:` custom
  attributes of a `Cube` prim be read at all.
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
  loaded one did. An op whose value is time-sampled carries its samples too
  ("Time samples" below).
  Three cases keep the composed matrix and no stack, each reported in the
  log: a prim the stage lookup does not answer for or whose class carries no
  `xformOps`, an op of a value type erhe has no counterpart for (one
  warning), and a stack whose composition disagrees with the transform the
  stage evaluates by more than 1e-5 (one warning - the two agreeing is what
  `erhe_usd_tests` asserts for every prim of the fixtures). A stack that
  carries time samples skips that last comparison, for the reason "Time
  samples" gives. The root stage's
  `upAxis` / `metersPerUnit` correction reaches the top-level prims of a
  non-Y-up or non-metre stage; those prims write a transform that is not what
  their ops say, so they keep the composed matrix too (logged at debug
  level). A `Stage_metrics::referenced` load applies no correction, so the
  prims of a reference or payload target keep their authored stacks.
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
- A `Shader`, `NodeGraph`, `GeomSubset` or `DomeLight` prim whose subtree
  carries no mesh, camera, punctual light, skeleton or volume contributes no
  erhe prim: the shading network is namespace, a subset's facets already ride
  a primitive of its mesh and a dome light is the scene's ambient light, yet
  Tydra lists each as a transform node.
- A `DomeLight` is the scene's ambient light, not an `erhe::scene::Light`:
  erhe has no environment map, so the dome's constant radiance
  (`inputs:color * inputs:intensity * 2^inputs:exposure`) becomes
  `Usd_data::ambient_light` and the prim itself is recorded in
  `Usd_data::dome_lights`. The first dome of a file sets the ambient light; a
  second one is a warning. `inputs:texture:file` is named in a warning and
  not sampled - an environment map is future work, and until it exists a
  textured dome contributes its constant color only. A save writes the
  records of `Usd_save_arguments::dome_lights` back as `DomeLight` prims at
  the stage root, which is where the editor sends the domes the scene was
  opened from (`Scene_root::get_usd_dome_lights`); a scene that read no dome
  writes none and carries its ambient light in the `customLayerData` scene
  block instead.
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
  becomes); `doubleSided`, onto the geometry prim's `Gprim.double_sided`
  property; the `UsdPreviewSurface` inputs `diffuseColor`, `emissiveColor`,
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

### Texture coordinates

USD's `st` primvar has its origin at the bottom-left of the image (the
OpenGL convention); erhe's texture coordinates - and its image loading and
sampling - follow glTF, whose origin is the top-left. The two spaces differ
by `v' = 1 - v` alone, so one involution converts either way and
`flip_texcoord_v` is used by the importer on every texcoord it reads and by
the writer on every texcoord it writes. A vec2 primvar the file authors
under another name follows the same rule wherever it is used as a texcoord.

A `UsdTransform2d` composes `st_out = R(r) * (st_in * scale) + T`, with `r`
in degrees counter-clockwise; the erhe slot transform composes
`uv_out = R(rotation) * S(scale) * uv_in + offset`, with `rotation` in
radians. The erhe transform acts on the already-flipped texcoord, so with
`F(x) = (x.u, 1 - x.v)` the requirement is `M * F(st) + O = F(R * S * st + T)`
for every `st`, whose solution is

    rotation = -radians(r)
    scale    = scale
    offset   = (Tx - sin(r) * scale.y, 1 - Ty - cos(r) * scale.y)

and, the other way (`r = -rotation`),

    T        = (offset.x + sin(r) * scale.y, 1 - offset.y - cos(r) * scale.y)

`to_erhe_uv_transform` / `to_usd_uv_transform_2d` are these two formulas and
nothing else. The USD identity maps onto the erhe identity, so a texture with
no `UsdTransform2d` leaves the slot at its defaults, and the writer authors a
`UsdTransform2d` prim only for a slot whose transform is not the identity.

The reference renderer's own composition is the one above: usdview's Storm
generates `translation + mat2(cos, sin, -sin, cos) * (scale * in)` for a
`UsdTransform2d`. `test_usd_texture_channels.cpp` walks the six materials of
the USD working group's `TextureTransformTest` asset over a set of `st`
points and asserts that formula against `to_erhe_uv_transform` composed the
way `Material_buffer` and `erhe_texture.glsl` apply it, which is where any
future change to either end has to stay true.

### UsdPreviewSurface fallbacks and channel outputs

An unauthored UsdPreviewSurface input is the schema fallback, not an erhe
default (`doc/usd-compatibility-plan.md` I2). Of the inputs erhe carries only
`diffuseColor` differs: USD's fallback is the 0.18 grey usdview shows against
erhe's white `base_color` (`roughness` 0.5, `metallic` 0, `opacity` 1, `ior`
1.5, `emissiveColor` black all agree). So `c_usd_diffuse_color_fallback` is
written as a local value when the file authors no `diffuseColor`, and the
writer authors `inputs:diffuseColor` from the effective value whenever that
differs from the fallback - an erhe default white included, which is also
what makes a save reproduce itself.

A mesh with no material binding at all is not given a `Material` - that would
author a binding the file never had - and renders through the scene renderer's
reserved default material slot, whose base color is the same 0.18 grey (see
`src/erhe/scene_renderer/notes.md`). A mesh's `primvars:displayColor` arrives
through Tydra as vertex colors and multiplies into the base color in the
fragment shader, bound material or not.

A scalar input is connected through a named output of its `UsdUVTexture`
(`outputs:r` / `g` / `b` / `a`), and that names the channel to read.
`erhe::primitive::Texture_channel` carries it per scalar slot on the material
(`metallic_channel`, `roughness_channel`, `occlusion_channel`,
`opacity_channel`); the defaults are glTF's fixed packing, so a glTF-derived
file leaves them all at the default. A scalar input connected to a
multi-channel output names no channel, so the default stands and the material
is named in one warning. erhe takes its fragment alpha from the base color
texture, so an `inputs:opacity` that reads an image of its own is one warning
and no channel.

`inputs:metallic` and `inputs:roughness` are separate UsdPreviewSurface
inputs of what erhe holds in one metallic-roughness slot, so a file may
texture one of them and give the other a plain value. The input that names no
texture then reads `Texture_channel::none`: it takes no channel of the bound
image and its own factor stands (the shader multiplies by one, and the writer
leaves that input unconnected and off the texture's `inputs:scale`). Without
it, a roughness map would modulate a constant metallic through whatever the
image holds in the channel the glTF default names - which is what
`test_assets/RoughnessTest` authors.

### Sublayers

A root layer's `subLayers` are the weakest layers of its layer stack (the `L`
of USD's LIVRPS), and LightUSD composes nothing at load, so the stage its
reader builds holds the root layer alone - a file whose content lives in a
sublayer would arrive empty. `erhe::usd::load_stage` composes the stack itself
before anything converts the stage: it reads the file as a `lightusd::Layer`
(once), hands it to LightUSD's `CompositeSublayers` (which resolves
each asset path against its own layer's directory, follows nested `subLayers`,
detects cycles and merges per property, so an `over` in a stronger layer lands
on the `def` of a weaker one as one prim) and turns the composed layer back
into the stage with `LayerToStage`, keeping a copy of that composed layer as
the one layer the importer reads (see below). Everything downstream - the Tydra
conversion, the prim tree, a referenced or payload file loaded through this
same function - sees that one composed stage.

- Strength is local-first: the root layer's own opinions beat every sublayer,
  and within the `subLayers` array the earlier entry is the stronger one. A
  prim absent from the stronger layers is added whole.
- Stage metadata (`defaultPrim`, `upAxis`, `metersPerUnit`,
  `timeCodesPerSecond`, `framesPerSecond`, `startTimeCode` / `endTimeCode`,
  `kilogramsPerUnit`, `customLayerData`) takes the root layer's value where it
  authors one and the strongest sublayer that authors one otherwise.
  `CompositeSublayers` keeps the root layer's metadata and drops every
  sublayer's, so erhe walks the sublayer tree a second time for the fields the
  root leaves unauthored, stopping as soon as all of them are answered. That
  walk parses those files again; the layers that author stage metadata are the
  small ones, because a stack's heavy content sits below a reference or a
  payload.
- The composed stage keeps the root layer's `subLayers` list as the record of
  which layers went into it. `Usd_data::sublayers` reports it, and
  `describe_stage` lists the entries as `sublayer` layer references.
- The composed top-level prims are sorted by name. A composed layer stack has
  no single authored top-level order - each layer authors its own - and
  `lightusd::Layer` holds its prim specs in a hash map, so sorting is what
  makes the composed tree the same on every run and every platform.
- The layer the stage was built from is kept on the stage
  (`Stage::Impl::layer`): the composed layer stack, with the prims of the
  variant blocks hoisted into it. It is the source of everything LightUSD does
  not compose, so every read that asks which layer authored a thing goes to it
  rather than parsing the file again: the class prims and `inherits` arcs of
  X3, the `Brush` prims of E4a and the `variantSet` blocks of X4 all go
  through `find_layer_primspec` or walk the layer directly. A prim any layer of
  the stack authors therefore contributes those the way a root-layer prim does
  - a sublayer's `class` prim is a style, its `over` below a reference carrier
  is an instance override, its `Brush` prim is a brush, and its `variantSet`
  block gets its table entry. (The `xformOp` stacks of M8 and the
  authored-opinion pass of I2 read the composed prims of the stage instead, so
  they see every layer either way.)
- `LayerToStage` consumes the layer it builds from, so the kept layer is one
  copy of the composed spec tree, taken per load. On the largest sublayer
  stack of the survey (`intent-vfx/scenes/teapotScene.usd`, four sublayers,
  ~4000 prim specs) the copy is ~14 ms of a ~32 s open in a Debug build.
- Provenance (X5) names the scene's own file as the layer of a value the
  composed layer supplies. `CompositeSublayers` merges per property and keeps
  no per-property source layer, so which layer of the stack authored a value
  is not recoverable without walking the stack again, which the tooltip does
  not do.
- A sublayer's asset path is resolved the way a `references` asset path is,
  parent-relative segments included: the composition resolver is given an
  erhe asset-resolution handler that anchors the path against the layer's
  directory and normalizes it. LightUSD's own file resolver refuses any path
  holding a `..` segment (`io::FindFile`), which is exactly how a stack that
  keeps its shared layers beside the tree names them.
- A sublayer inside a `.usdz` archive is not composed. The composition
  resolver reaches the file system, not the archive; such a file loads with
  the root layer's own content and one warning naming the entries.
- A save writes ONE layer holding the composed content and authors no
  `subLayers`. erhe edits the flattened stage, so it has no layer to write an
  edit back to; a sublayer stack the editor could edit layer by layer is
  future work (doc/usd-compatibility-plan.md section 5). The save logs one
  line naming the sublayers the file it was opened from had
  (`editor::save_scene_usd`, from `Scene_root::get_usd_sublayers()`).

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
- The carrier of an arc is transformable. USD gives a typeless referencing
  prim the type of the composed target, and LightUSD composes nothing, so the
  prim erhe reads is typeless; a typeless or `Scope` prim that authors an arc
  therefore imports as an `erhe::scene::Xform`, which is what holds the
  instances the arcs become and which carries the prim's own authored
  transform (the identity when it authors none). The item
  remembers nothing of having been typeless: the writer spells it
  `def Xform`, a legal and more explicit spelling of the same composition, and
  that spelling is the round trip's fixed point from the first save on. A
  carrier of a type that carries no transform - a `Material`, a `Cube` - stays
  the prim it is and its arcs are dropped, with one warning naming the type.
- The TARGET of an arc is any prim: an `Xformable`, a `Scope`, the `Typed`
  prim a typeless `def` or an unrecognized `typeName` becomes, or a prototype
  a `class` prim holds. A target that carries no transform composes what
  reached it through to its children, which is what the editor's template
  wrapper reproduces (`src/editor/parsers/notes.md`).
- An undefined prim - one the composed stage gives the specifier `over`,
  because no layer defines it - is a prim of the tree like any other:
  LightUSD reconstructs it whatever its specifier, so a reference to it and to
  its `def` descendants resolves. It imports with `defined = false`, which
  takes it and its whole subtree out of render, pick and simulation through
  the derived `Item_flags::active` bit, the way USD's default traversal
  predicate reaches neither an undefined prim nor anything below one. The
  writer spells the specifier from `defined`, the round trip's fixed point.
  This is the shape a file uses when its own prims reference a root-level
  `over` holding the assets they instance.
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
- An `over` prim the referencing layer authors below a referencing prim is
  the sparse override of one instance item (doc/usd-compatibility-plan.md X2),
  reported in `Usd_prim_references::overrides` as an
  `erhe::scene::Instance_override`: the item's path below the carrier, its
  values as name / D16 text pairs, its authored xformOp stack, and the
  absolute stage path its `material:binding` relationship names. What an
  override is - and so what the writer authors - is stated once, in
  `src/erhe/scene/erhe_scene/instance_override.hpp`. LightUSD does not report
  which layer an opinion on a composed prim came from, so the prim specs of
  the composed layer below the referencing prim are what is read: the specifier is
  what tells an `over` from a `def`. A prim spec is what a layer authored, so every property it
  carries is an authored opinion and no `authored()` test is needed; the
  `erhe:Owner:name` custom attributes, `visibility`, `purpose`, the `active`
  metadatum, the xformOps (through LightUSD's own
  `ReconstructXformOpsFromProperties` and then the M8 reader) and the
  `material:binding` relationship are what is taken. The `MaterialBindingAPI`
  the `over` applies is what makes the relationship a binding and carries no
  value of its own, so it is not read as one. An `over` on a GeomSubset is an
  entry whose path ends in the subset's name, which is how the group of facets
  a binding covers is named. The reader applies nothing: the instance content
  does not exist until the caller attaches the arcs' targets, and a binding
  can name a material another carrier's arc supplies, so the caller applies
  every carrier's overrides once every arc of the file is instantiated.
- A `def` below a referencing prim adds a prim to a reference, which a
  reference does not allow (plan section 5): it is named in one warning and
  dropped.

### Class prims and inherits arcs

A `class` prim defines no scene content: it holds the opinions its `inherits`
arcs hand to the prims that name it, which is what an erhe style holds
(`doc/style-library.md` D25). Tydra's render-scene conversion reports a class
prim as a transform node all the same, so the reader takes the class prims off
the composed layer's own prim specs and the conversion skips the class prims
themselves (doc/usd-compatibility-plan.md X3).

A class prim's `def` descendants are prototypes: prims the class holds
abstract, which a reference names and clones. Each is converted as an ordinary
prim with `Item_flags::content` clear - that is what USD's class abstraction
means, so the render, pick and shadow filters leave it out - and is reported in
`Usd_data::class_prototypes` with its stage path and the path of the class prim
holding it. The prototype is parented where the class prim's own holder is,
because `erhe::usd` cannot make the Style item; the caller moves it under the
Style, which is what gives it the path the stage spells. A reference into a
prototype clones it, and the clone carries `content` again. A `class`
descendant of a class prim is a class of its own.

`read_layer_composition()` walks the composed layer once, before the prims are
converted, and fills two things:

- `Usd_data::classes`, one `Usd_class_prim` per `class` prim: its stage path,
  its name, its `inherits` targets as absolute prim paths, its authored
  opinions in the same neutral name / text form an `over` prim's are read in,
  and the classes it holds. Every descendant of a class prim is itself a
  class, so a class holding classes is a scope of styles; a `def` descendant
  is a prototype rather than a class. The layer keeps its top-level prim specs in a hash map and the
  ascii reader fills no ordering metadatum for a layer, so the top level is
  read in name order; the children of a prim spec keep the order the layer
  spells them in.
- `Usd_data::prim_inherits`, one entry per non-class prim that authors
  `inherits`: the erhe item the prim became, its stage path and the targets.
  A material prim is one of these, so a `Material` prim can name a class too.

The list-edit rule is the one the arc reader repeats, over target paths.
`erhe::usd` creates no item for a class: it cannot name `editor::Style`, so
the caller makes the Style items, applies the values with
`erhe::scene::apply_property_values` and sets the styles.

### Point instancers

A `PointInstancer` prim is expanded into prims
(doc/usd-compatibility-plan.md S1). The prim itself is an
`erhe::scene::Point_instancer` - a boundable prim, which is what USD's
`UsdGeomPointInstancer` is - carrying its own transform, and below it:

- every prototype the prim's `rel prototypes` names, converted where the file
  put it and held abstract with `Item_flags::content` clear, the way a class
  prim's prototype is;
- one child `Xform` per instance, named `<prototype name>_<index>` (or
  `<prototype name>_<id>` when the prim authors `ids`), carrying the instance
  transform and an INTERNAL reference to its prototype - an entry of
  `Usd_data::references` with an empty asset path. The caller instantiates it
  the way it instantiates every other arc, so every instance of one prototype
  shares one prefab template and the clones share their GPU primitives.

`Usd_data::point_instancers` records what the expansion made: the instancer
item and its stage path, the prototype paths, and one entry per instance with
its prototype index and its transform. The transforms come from LightUSD's own
`ComputeInstanceTransformsAtTime` at the default time, so the composition is
USD's (scale, then orientation, then position) and an `invisibleIds` /
`inactiveIds` entry is skipped.

The instance prims are the only record of `positions`, `orientations`,
`scales` and `protoIndices`: a save recomputes all four from them - the first
three from their local transforms and the fourth from the prototype each one
references - so an instance the user moved, deleted, duplicated or reordered
persists and no copy of an array can drift out of step with the tree.
`erhe::scene::Point_instancer` therefore holds no array of its own.
`orientations` and `scales` are written when any instance needs them, so an
instancer of plain translations stays as compact as the file that authored it.

Which prototype an instance references is read off its `Prefab_instance`
attachment: the arc target ends with the prototype's path below the
instancer (`Prototypes/teapot`, `Pawn`), longest match first. An instance
whose arc names none of the instancer's prototypes is one warning and is not
written.

`rel prototypes` is written from the tree: the prims below the instancer that
carry no content, in tree order. That is exactly what the load holds abstract,
so the two agree - and it is why the load remaps the authored `protoIndices`
onto the tree order rather than keeping the relationship's. A prototype the
stage does not answer for is one warning and its instances are left out; a
prototype the instancer does not hold is one warning saying that a save
relocates it under the instancer.

The prototype's own arcs are instantiated where the prototype sits, so the
prim keeps them and a save writes them back, but the content they bring in is
held abstract with the rest of the prototype - only the instances' clones of
it draw. The editor tells an instance from a prototype by those two facts: an
instance is a content child of the instancer carrying a `Prefab_instance`
attachment, and a prototype is an abstract one.

Not carried: time-sampled instancer arrays (the arrays are read at the default
time), `velocities` / `accelerations` / `angularVelocities`, per-instance
primvars, and `ids` beyond naming the instance prims. erhe draws one instance
per prim - there is no GPU instancing - so a large instancer costs one draw
per instance over one shared set of GPU primitives.

### Brush prims

A brush is editor state a USD file carries as a prim of its own type
(doc/usd-compatibility-plan.md E4a): `def Brush "<name>"` where the brush sits
in the tree, holding `erhe:Brush:density` and `erhe:Brush:normal_style` custom
attributes, a `material:binding` to the material a placed instance gets, and
its geometry as a child `def Mesh "geometry"` written with
`subdivisionScheme = none`, so a viewer without erhe sees a prim of unknown
type with a mesh below it. The spellings are `usd_impl.hpp` constants, which
the reader and the writer share.

The same composed-layer walk that takes the class prims records a `Brush`-typed
prim into `Usd_data::brushes`: the stage path and name, the density and the
normal-style token pulled out of the neutral value list, the absolute path the
`material:binding` names, and every other authored opinion in the neutral
form. The geometry follows once the meshes are converted - the child mesh is
converted the way every other mesh of the file is, and its geometry is taken
off the first primitive. The scene conversion stops at a `Brush` prim the way
it stops at a class prim, so the brush geometry is no mesh of the scene and
the prim becomes no `Typed` item; a `Brush` prim without a `geometry` child is
one warning and no brush record. `erhe::usd` creates no item: the caller makes
the editor Brush at the path the prim has.

### Texture node graphs

A texture graph is a `NodeGraph` prim carrying the custom attribute
`erhe:graph:format`, which is the marker that says the network is erhe's
(doc/usd-texture-graphs-plan.md). A `NodeGraph` without it is a foreign
shading network and is left to the material conversion. Each node of the graph
is a generic `Shader` child whose `info:id` is `erhe:texture:<type name>` -
the factory type name the caller makes the node with - carrying its editor
position as `custom float2 erhe:ui:position`, and each link is an attribute
connection. The spellings are `usd_impl.hpp` constants, which the reader and
the writer share.

- The `inputs:` / `outputs:` properties of a node are read by one rule: an
  `inputs:` attribute carrying a value is a parameter, an `inputs:` attribute
  carrying a connection or nothing at all is an input pin, and every
  `outputs:` attribute is an output pin. So a pin without a link is still in
  the file, as the typed attribute alone.
- A parameter travels as its USD type and the USD literal spelling of its
  value (`float` `1.5`, `int` `3`, `bool` `true`, `token` / `string`
  `"name"`, `float2` `(1, 2)`, `color3f` / `color4f`). `erhe::usd` knows no
  node vocabulary: the caller decides which USD type a parameter takes, and
  the writer authors exactly the (type, text) pair it is handed. A value with
  no USD form - a gradient, a curve - travels as its text in a `string`, one
  rule for both. A text with a double quote of its own does not survive a
  round trip: LightUSD's USDA parser hands an escaped quote back with its
  backslash, so the value grows a level of escaping on every save. Callers
  therefore hand over a quote-free spelling - the editor writes a nested JSON
  value with a single quote in place of the double quote - until the parser
  round-trips an escaped quote (future work).
- The graph's own `outputs:<pin>` connections are its interface outputs: the
  value a material can name. A `UsdPreviewSurface` input connected to one is
  recorded in `Usd_data::material_graph_bindings` as (material, slot, graph
  path), keyed the way a texture binding is, and the writer connects a slot
  named in `Usd_save_arguments::material_graph_bindings` to the graph's first
  interface output instead of writing a `UsdUVTexture` for it. The baked image
  is not written: a graph loads born dirty and the first evaluation re-bakes
  it.
- The same composed-layer walk that takes the class prims records a marked
  `NodeGraph` into `Usd_data::node_graphs`, and the scene conversion stops at
  the prim the way it stops at a `Brush` prim, so no node becomes a scene prim.
  A `Shader` child whose `info:id` is not under the `erhe:texture:` prefix is
  one warning and no node, and the links into it are dropped with it, as is a
  connection that leaves the graph.
- Tydra's render-scene conversion fails a whole material over a
  `UsdPreviewSurface` input whose connection resolves to no `UsdUVTexture`, so
  the stage it converts is built without that wiring: `load_stage` strips every
  connection into a marked graph from a copy of the composed layer and builds
  the stage from that copy (`compose_node_graph_stage`), while the kept layer
  - which is what the graphs and the slot bindings are read off - keeps it.
  The material then takes its schema fallback for the stripped input, and the
  caller binds the slot to the rebuilt graph asset. A graph inside a `.usdz`
  archive is not resolved that way, for the reason a sublayer inside one is
  not composed, and is named in one warning. The strip is a downstream
  answer to a LightUSD limit: once the `tksuoran/LightUSD` fork's Tydra
  leaves an input whose connection is no `UsdUVTexture` unset instead of
  failing the material, `compose_node_graph_stage` goes and the stage is
  built from the composed layer as it stands (future work).
- `erhe::usd` creates no graph asset: the caller rebuilds it from the record,
  and hands the writer one `Usd_save_node_graph` per graph prim. Being named
  in that list is what makes an item a graph prim to the writer - a graph
  carries no class token of its own, the way a brush does - and the node prims
  are named by the identifier rule and the M2 sibling-unique rule, in the
  record's order, so a second save spells the same file.

### Variant sets

A variant set is resolved in composition, and LightUSD composes nothing, so a
variant contributes nothing to the composed prim: the `variantSet` blocks are
read off the composed layer's own prim specs the same walk takes the class
prims from - so a set any layer of the stack authors is in the table - and the
reader is what applies the selection
(doc/usd-compatibility-plan.md X4). Material bindings, property opinions and
the prims a variant adds are all carried.

The prims come across at load: `load_stage` copies the `def` children of every
variant block into the prim carrying the set - `hoist_variant_prims`, on the
composed layer before it is built into a stage, so Tydra and everything
downstream see ordinary prims - and gives each the sibling-unique name of the
M2 rule,
because two variants of one set are free to author the same name and the tree
is not. The prims of the variant that is not selected are marked
`active = false`, which prunes each one and its subtree from the render, the
pick and the simulation the way USD's own `active` does (X2). So every
variant's prims are in the tree whichever variant is selected, and a switch is
a property write like every other one rather than a rebuild of the tree.
`Stage::Impl::variant_prims` records what was hoisted where, and `Usd_variant`
carries it as `prims`: the name below the carrier and the name the file gave
it. A `def` the hoist does not reach - one below an `over` child of a variant,
or any of them in a `.usdz` archive, whose asset paths resolve through the
archive rather than the file system - is counted for the set instead.

`Usd_data::variant_sets` holds one `Usd_variant_set` per set: the erhe item
the carrying prim became, the prim's stage path, the set name, one
`Usd_variant` per variant, and the selection, which is the prim's `variants`
metadatum or the first variant when the layer authors none.

A `Usd_variant` carries two things. Its `bindings` are the
`material:binding` relationships it authors, each as the M1 path of the bound
prim below the carrying prim and the absolute stage path of the `Material`
prim. Its `overrides` are the property opinions it authors, recorded exactly
the way an `over` below a reference carrier is (X2): one
`erhe::scene::Instance_override` per path, an empty path being the carrying
prim itself, holding the `erhe:Owner:name` custom attributes, `visibility`,
`purpose`, the `active` metadatum and the authored xformOps in the neutral
name / text form. `material:binding` is never among the values - `bindings`
is what carries it, so nothing binds a material twice - and only the `over`
children of a variant contribute opinions: a `def` child is a prim of the tree
carrying its own attributes.

`Usd_variant_set::base_values` is what the prims held for every path and
property name any variant of the set authors, read before the selected
variant's opinions were applied. A property with no local value there is a
`cleared` entry, so putting it back clears rather than writes. This is what
a switch to another variant restores first: a property the chosen variant
leaves unsaid goes back to what the file authored outside the variant blocks.

`unsupported_opinion_count` is what stays uncarried, reported once for the
set: a property the value reader has no place for, and a `def` prim of a
variant the hoist did not reach. An override whose path reaches no prim of the
tree is dropped when the base values are captured, and counted the same way.

The reader then binds the selected variant's materials itself: a binding at a
`Mesh` prim's path covers the mesh's primitives that the same variant does not
bind by subset, and a binding at a `GeomSubset` path covers that subset's
primitive. Tydra converts the materials the composed stage's meshes bind and no others,
so a `Material` prim only a variant binds - or one no mesh of the file binds at
all, which is every material of a file whose meshes live in another layer -
would reach the tree as nothing. Every `Material` prim of the stage is wanted:
a material is a prim of the erhe tree (U4) and a reference into the file is
what gives it its meshes, so the `Material` prims the render-scene conversion
left out are converted one by one with
`RenderSceneConverter::ConvertMaterial` and appended to the render scene
before the materials are converted. The converter moves its own texture
and image lists into the render scene, so an extra conversion fills them again
from index zero: the new entries are appended and the ids shifted by what was
already there, for the six UsdPreviewSurface texture slots erhe reads.

How a bound texture is sampled and how its texels are read comes across with
it:

- `inputs:wrapS` / `inputs:wrapT` become the slot's `wrap_u` / `wrap_v`.
  `repeat` and `mirror` map onto the erhe address mode of the same name;
  `clamp`, `black` and the `useMetadata` default all become clamp-to-edge,
  because erhe has no border color, so `black` samples the edge texel rather
  than transparent black.
- A `UsdTransform2d` feeding the texture's `st` becomes the slot's rotation,
  offset and scale. USD composes `in * scale`, then the rotation, then the
  translation, which is the order the erhe slot transform applies, so the
  three values map across unchanged apart from the degrees USD spells the
  rotation in.
- A connected UsdPreviewSurface input takes its value from the texture, so
  the erhe factor - which the shader multiplies the texel with - is the
  texture's `inputs:scale`, on the channel the surface reads that input
  through, and never the plain value the connected input still carries. That
  is what makes a textured `emissiveColor` visible: erhe's emissive factor is
  zero by default, so an emissive texture without it renders black.
- The normal slot carries the texel decode itself:
  `inputs:scale` and `inputs:bias` become `Material::normal_texture_decode_scale`
  and `normal_texture_decode_bias`, which the shader applies as
  `texel * scale + bias`.
- A `bias` on any other slot, and a `scale` on a slot with no factor, is one
  warning naming the material and the input.

A texture packed inside a `.usdz` is read out of the archive: LightUSD takes
only the root layer out of the package when it opens the stage, so the import
reads the archive a second time (`ReadUSDZAssetInfoFromFile`) and hands the
entry's bytes over in `Usd_image::bytes`. A packed image has no file of its
own, so `Usd_image::path` names no existing file and the bytes are the only
source; the caller decodes them with the memory overload of
`erhe::graphics::Image_loader::open`.

The archive key is the packaged asset path exactly as the file authors it,
relative to the archive root and directory components included, so a texture
packed under `0/` is the entry `0/texture.png` (a leading `./` is not part of
the key). The Tydra converter resolves that path against the file system,
where it does not exist, and reports it as a texture it could not load; that
line is dropped from the converter's warning for every path the archive holds
(`filter_converter_warning`), because the image does reach the material.

Not yet imported: blend shapes, animation clips, volumes, MaterialX / OpenPBR
shading networks, and texture filter state.

### Skinning

UsdSkel poses a bound point as `skelLocalToWorld * sum_j w_j *
jointSkelSpace_j * inverse(bind_j) * geomBindTransform * p`, and the mesh's
own transform plays no part; erhe's `Joint_buffer` poses it as `sum_j w_j *
world_from_joint_j * inverse_bind_j * p`, the glTF rule. The two agree when
every joint is a prim whose world transform is `skelLocalToWorld *
jointSkelSpace_j` and the mesh's inverse bind matrix for joint `j` is
`inverse(bind_j) * geomBindTransform` (doc/usd-compatibility-plan.md K1), so
the conversion builds exactly that.

- A `Skeleton` prim is a transformable prim of the tree carrying the authored
  `Skeleton` token, the way a generic `Model` prim carries its own. The
  `SkelRoot` stays the `Typed` prim it is: it carries no transform of its own
  and encapsulates nothing erhe needs.
- Each entry of the skeleton's `joints` is an `erhe::scene::Xform` prim under
  the skeleton along the joint path, so `Bone_1/Bone_001_1` is `Bone_001_1`
  under `Bone_1`, and its local transform is the joint's `restTransforms`
  entry. A joint is a prim of the erhe tree rather than a row of an array,
  which is what lets a joint channel of an animation drive it the way it
  drives any other prim.
- A skinned `Mesh` prim names an `erhe::scene::Skin` whose joints are those
  prims in `joints` order, whose pivot is the skeleton prim and whose
  `inverse_bind_matrices[j]` is `inverse(bind_j) * geomBindTransform`. That
  depends on the mesh's own `geomBindTransform` alone, so the meshes one
  skeleton skins through the same bind transform share a skin and a mesh with
  a bind transform of its own gets a skin of its own. `Usd_data::skins` lists
  them.
- `primvars:skel:jointIndices` / `jointWeights` are vertex-variability
  primvars, so they land on the vertex the USD point became - on the geogram
  vertex for a geometry-normative mesh and on every corner of that point for a
  soup mesh. erhe carries two sets of four influences per vertex; the strongest
  `4 * set_count` of the `elementSize` USD authors are kept, their weights
  normalized to sum to one, and an `elementSize` above eight is one warning
  per mesh.
- Tydra reorders the skin primvars only when it builds vertex indices, which
  this conversion leaves off, so an influence is addressed by the USD point
  index the way `points` is. Tydra also remaps a mesh-local `skel:joints`
  order onto the skeleton's own, so a joint index is a skeleton joint index.
- A `SkelAnimation` becomes joint channels of the file's one
  `erhe::scene::Animation` ("Time samples" below), targeting the joint prims,
  with translation, rotation and scale per joint. Tydra keys a skeletal
  sampler in the file's time codes, so the channels are divided by
  `timeCodesPerSecond` into seconds exactly as the sampled `xformOp`s are, and
  a `quatf` sample arrives as its four floats in `(x, y, z, w)` order. A
  skeleton with no animation source contributes no channel and its joints stay
  at the rest pose, which is what pxr renders too.

A save writes that model back. `Mesh::skin` is all the writer reads: a skin
names its joints in `joints` order and its pivot, and that pivot is the prim
the `Skeleton` is written on, so `Usd_save_arguments` carries no skin list.
The one exception is the joint channels, which are channels of an
`erhe::scene::Animation` rather than of the tree, so the caller hands the
scene's animations over in `Usd_save_arguments::animations`.

- The prim that is a skin's pivot is written as a `Skeleton` prim - as is a
  prim still carrying the authored `Skeleton` token that skins nothing, then
  with no joint arrays and one warning. Its `joints` are the joint prims'
  paths below it, sanitized segment by segment, its `restTransforms` are the
  joint prims' own local transforms, and its `bindTransforms` are recomputed
  from the skin. A joint is not a prim of the written stage - USD carries it
  as those array entries - so the planning pass leaves the joint prims out;
  a prim the user parented under a joint is written where the joint sits,
  with the joint's local transform composed into it, the way an `import_root`
  container's children are.
- erhe keeps only the product `inverse_bind_j = inverse(bind_j) *
  geomBindTransform`, never the two factors, so the writer picks the split:
  the first skin registered for a skeleton is written through the identity
  geometry bind transform, with `bindTransforms[j] = inverse(inverse_bind_j)`,
  and every further skin of that skeleton keeps those bind transforms and
  carries the difference as its own `primvars:skel:geomBindTransform`,
  `bind_0 * inverse_bind_0`. Reading either back gives exactly the inverse
  bind matrices the skins hold, which is what makes a save a fixed point
  although the file's original bind pose is not kept; UsdSkel poses the mesh
  the same way either split is written.
- A skinned `Mesh` prim applies the `SkelBindingAPI` next to its
  `skel:skeleton` relationship - usdchecker fails a prim that has one without
  the other - and writes `primvars:skel:jointIndices` / `jointWeights` as
  `vertex` primvars indexed by the point index, which is the domain erhe
  carries them in. `elementSize` is the narrowest width that holds every
  influence of the mesh: erhe pads a vertex out to the width of the sets it
  fills and reading a narrower `elementSize` back pads it again, so the
  narrowest width is what makes a second save byte-identical. The weights are
  the normalized ones erhe holds, not the file's own.
- The joint channels of `Usd_save_arguments::animations` are written as one
  `SkelAnimation` prim below the skeleton, which names it in
  `skel:animationSource`. USD keys the translation, rotation and scale of
  every joint of a skeleton on one shared timeline, so the time codes written
  are the union of the times those channels key and a joint no channel
  reaches contributes its rest pose at each of them. The prim is rebuilt from
  the channels rather than written from the tree - Tydra hands the import the
  channels, not the prim, so the tree's `SkelAnimation` prim carries nothing -
  and it keeps the name the file authored. A skeleton no channel drives
  writes neither the prim nor the relationship.

### Time samples

A stage is evaluated at one time code: the root layer stack's
`startTimeCode` when it authors one, else the earliest time any prim of the
stage samples an `xformOp` at, else USD's default time code. That is the time
a viewer opens a stage at, so the pose the import gives the scene is the
reference frame of its clips. Tydra's render-scene conversion, the
`xformOp` stacks and every value the conversion reads use that one time code.

A time-sampled `xformOp` attribute carries its samples on the erhe op
(`erhe::scene::Xform_op::samples`), in the file's own time codes and with the
op's own value type, and `Xform_op::value` is its value at the evaluation time
code. The samples are what the file authored, so:

- The samples win over the attribute's `default`, which is what USD says at
  every time code they reach. LightUSD evaluates an op that carries both at
  its `default` whatever time code it is asked for, so a prim whose stack
  carries samples keeps its stack without the composition comparison above:
  the stack is the authority and the composed matrix is not.
- A save writes the samples back as `timeSamples` beside the value, and
  authors `timeCodesPerSecond`, `startTimeCode` and `endTimeCode` exactly when
  it wrote at least one sampled op, with the range the samples span.
  `Usd_save_arguments::time_codes_per_second` supplies the rate, which the
  caller carries from the load, so a file keeps its own. A sampled stack is
  written whatever transform the prim holds at the time of the save: that
  transform is the pose the animation player put it in, and the stack is what
  the file authored.

The playable projection of the samples is an `erhe::scene::Animation` - one
per file, named after the file, listed in `Usd_data::animations` and attached
to the content library the way a glTF file's animations are. It holds one
channel per sampled op, keyed in seconds (`time code / timeCodesPerSecond`)
and interpolated linearly, which is what USD does with the time samples of a
floating-point attribute; USD authors no per-attribute interpolation for a
reader to pick another one from. A `rotate*` op's Euler samples and an
`orient` op's quaternions both become a quaternion rotation channel, with the
sampled quaternions kept on one hemisphere so the interpolation never takes
the long way round.

erhe applies a channel by writing the component into the target's TRS, so a
stack the channels can drive is one whose composition is that TRS: at most one
`translate`, one rotate and one `scale` op, in that order, none inverted and
none suffixed. A stack outside that - a sampled `transform` matrix op, a pivot
pair, an op order like `[orient, translate]`, two ops of one kind - is named in
one warning per prim and contributes no channel; it keeps the pose the
evaluation time code gives it, and its samples still travel through a save.
Editing an animation's keys does not write back into the ops, and neither does
moving an animated prim: the stack is the authored record, and reconciling the
two is future work (`doc/usd-compatibility-plan.md` section 6).

A `UsdSkel` `SkelAnimation` contributes its joint channels to this same
animation ("Skinning" above). Not carried: time samples on any attribute other
than an `xformOp` or a `SkelAnimation` array, and `Ts` splines.

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
  primvars written `faceVarying`. Every prim the writer gives a
  `material:binding` - a mesh, a subset, a brush, an `over`, a variant block -
  also applies the `MaterialBindingAPI` in its `apiSchemas`
  (`apply_material_binding_api`, called wherever `bind_material` /
  `add_material_binding` reports a binding written): usdchecker's
  `MaterialBindingAPIAppliedChecker` fails a prim that has the relationship
  without the applied schema. `subdivisionScheme` is always `none`: the
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
  carries materials, styles and brushes; the other resource kinds are plan
  steps E4b to E4d.
- Two passes. The first decides every prim's stage path - the sanitized,
  sibling-unique name under each parent, and the `World` wrapper when the
  scene has several top-level prims - and records where each material and
  each style landed; the second writes the prims, so a mesh binds its
  material, and a prim names its style's `class` prim, by the path that prim
  actually got. `Usd_save_arguments::materials` is
  the caller's texture index, not a placement list: a material of that list
  which is not a prim of the tree is not written, and a material a mesh
  binds but the scene does not own has no prim, so its binding is dropped
  with one warning naming it (a prefab template's materials live in the
  template's own file, which is what the arc names).
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
- `doubleSided` of a geometry prim. `Gprim.double_sided` is written into the
  `Mesh` prim's own `doubleSided` attribute when the value is local, which is
  the authored-only rule every native attribute follows, so a prim erhe never
  made double sided carries no `doubleSided` line. `is_native_usd_property`
  lists it under the `Gprim` owner, so it never also travels as an
  `erhe:Gprim:double_sided` custom attribute - except on an `over` prim, which
  is typeless and carries no schema attribute at all.
- Prim `active` metadata (`doc/usd-compatibility-plan.md` X2). The item's
  `active` property is USD's prim `active` metadatum: written through
  `PrimMeta::set_active` when the value is local, so a prim erhe never
  deactivated carries no `active` line, and read back through
  `has_active()` / `get_active()`, which is what says the metadatum was
  authored (metadata has no `authored()` wrapper). It is metadata, not an
  attribute, so it never travels as an `erhe:` custom attribute
  (`is_native_usd_property` lists it). USD prunes the whole subtree of an
  inactive prim; erhe carries that as the derived `Item_flags::active` bit
  rather than as a property value, so only the item's own opinion is
  written. `Prim::IsActive` notes that inactive prims are pruned from
  traversal, but the Tydra render-scene conversion the importer walks does
  not prune them - the item is created inactive, which is what lets the
  opinion round-trip.
- The prim's specifier is the item's `defined` property, and is its own
  carrier - it never travels as metadata or as an `erhe:` custom attribute.
  The importer reads it from the composed layer's `PrimSpec::specifier()`
  (`find_layer_primspec`), never from the stage `Prim`: LightUSD's
  `LayerToStage` copies the specifier into the typed prim struct only and
  leaves `Prim::specifier()` at `Specifier::Invalid`, so every stage prim
  answers `Invalid`. The writer lowers the typed struct's `spec` field from
  `def` to `over` for an item whose `defined` is false, for the same reason;
  `class` prims are Style items (X3) and keep their specifier.
- The name a value is authored under. `native_usd_property_name` is the one
  list of the erhe properties a prim carries in an attribute of its schema and
  of the USD spelling each of them gets (`surface.inputs:diffuseColor`,
  `clippingRange`, `inputs:shaping:coneAngle`, ...); the writer's
  custom-attribute pass asks it whether to spell a value `erhe:Owner:name`
  instead. `get_usd_authored_as` is that question as a public answer, for the
  Properties window's composition-provenance line
  (`doc/usd-compatibility-plan.md` X5); `Native_property_form` says which of
  the two forms the prim being written offers.
- Name sanitizing. `sanitize_usd_identifier` replaces every character outside
  `[A-Za-z0-9_]` with `_` and prefixes `_` to a name starting with a digit.
  Sanitizing can map two distinct item names onto one spelling, so the writer
  then applies erhe's own sibling-unique suffix rule (M2, `<base>_<n>` from
  1). The prim name is the item name: an item whose name needed sanitizing
  comes back under the sanitized spelling.
- Styles and inherits arcs (doc/usd-compatibility-plan.md X3). A style item is
  written as a typeless `class` prim where it sits in the tree, and every prim
  that has a style names that class prim in one explicit `inherits` list op -
  the same way a mesh binds a material, by the path the prim actually got. The
  writer recognizes a style by the class token `erhe::Typed` fixes for it
  (`"Style"`), because `erhe::usd` depends on no editor type. A class prim
  carries no schema, so every one of its values travels as an
  `erhe:Owner:name` custom attribute - `is_native_usd_property` is asked with
  the `custom_attributes` form, which answers only for `visible`, `purpose`
  and `active` - and `visibility` / `purpose` travel as the plain token
  attributes a typeless prim spells them with. The style property itself is
  bridged and so never becomes an `erhe:Item_base:style` attribute; the arc is
  the only thing written. A style whose item is not a prim of the written tree
  - a style of another scene - is one warning and no arc.
- Brushes (doc/usd-compatibility-plan.md E4a). A brush item is written as the
  `Brush`-typed prim of the "Brush prims" section above, where it sits in the
  tree. The writer recognizes a brush by the class token `erhe::Typed` fixes
  for it (`"Brush"`), for the reason it recognizes a style that way, and takes
  what the prim carries from `Usd_save_arguments::brushes` - `erhe::usd` names
  no editor type, so the caller hands over the geometry, the density, the
  normal-style token and the material. `Brush.material` is in
  `native_usd_property_name`, so the material travels as the prim's
  `material:binding` and not also as a custom attribute. The geometry child is
  written by `write_geometry_mesh_prim`, which shares the vertex-array fill and
  the `subdivisionScheme = none` rule with the mesh writer; an erhe geometry
  binds no material of its own, so the child carries no `GeomSubset`. A brush
  prim of the tree the caller did not list is written without its geometry, and
  named in a warning.
- Composition arcs. A prim the caller names in
  `Usd_save_arguments::references` is written as the referencing prim it is:
  its own class, name, transform and authored values, plus one explicit
  `references` list op holding its reference arcs and one `payload` list op
  holding its payload arcs, each in the order the caller gave. The prims below
  it are not written - the arcs' targets supply them - so the file keeps the
  instance structure instead of the flattened subtree
  (doc/usd-compatibility-plan.md X1). An arc's `asset_path` is
  `Usd_save_reference::source_path` relative to the file being written, and
  empty when the two are the same file (compared after `weakly_canonical`),
  which is USD's spelling of an internal reference; its `prim_path` is empty
  when the arc names the target's default prim. A child of a carrier that
  names no template counterpart is one the user parented there: it is left out
  too, and the writer names it in a warning - a reference protects its
  structure (plan section 5). `erhe::usd` knows nothing of prefabs: the editor
  fills the arcs from the carrier's `Prefab_instance` attachments.
- An `over` prim is typeless, so it carries no schema attribute: every value
  of an overriding item travels as an `erhe:Owner:name` custom attribute -
  `is_native_usd_property` is asked with the `custom_attributes` form, which
  answers only for `visible`, `purpose` and `active` - and that is what lets a
  schema-named value of a resource inside an instance travel. A `Material`
  item's `roughness` is `erhe:Material:roughness` on its `over`, because the
  `inputs:` a `UsdPreviewSurface` carries live on the def'd `Shader` prim
  below a `Material` prim, which an `over` of that material does not have.
  The X2 reader reads that form back into the item's local layer.
- What a carrier does write of the instance below it is the overrides its
  items hold (doc/usd-compatibility-plan.md X2), collected through
  `erhe::scene::collect_instance_override_items`, which owns the rule for what
  an override is. The clone of an arc's target prim is the carrier prim itself
  - X1 gives the instance one level more than USD's own composition - so its
  values are authored on the carrier prim, below the carrier's own: an
  attribute both author is the carrier's, with a warning naming it, and a
  transform the target clone overrides is not writable at all, because the
  carrier prim's own xformOps occupy that slot. Every deeper item becomes an
  `over` prim at the path it has below the carrier, with an attribute-less
  `over` for an item on the way down that holds none, so the path exists. An
  `over` is written as a prim with no typeName, which contributes opinions and
  defines nothing: the local values as the same `erhe:Owner:name` custom
  attributes an authored prim carries, `visibility` and `purpose` as the plain
  token attributes they are, `active` as the prim metadatum, an overridden
  transform as the `xformOp:*` attributes and `xformOpOrder` a typed prim
  writes from its `xformOps`, and an overridden material as a
  `material:binding` relationship with the `MaterialBindingAPI` applied. A
  mesh of several groups of facets binds on the `over` of the GeomSubset prim
  of each group, and a mesh of one group binds on its own `over`; a binding
  the clone of an arc's target holds is the carrier prim's own the same way
  its values are. The material is named by the path the writer planned for it,
  which for a material an instance's content supplies is the carrier's path
  plus the path the material has below the arc's target clone - the path the
  composed stage gives it.
- Variant sets. `Usd_save_arguments::variant_sets` names the prim carrying
  each set, its variants with their material bindings and property opinions,
  and the selection; the writer gives the prim an `append variantSets` list
  op, a `variants` selection and one `variantSet` block per set. A binding or
  an opinion of the carrying prim itself is written on the variant, and a
  deeper one on an `over` prim at its relative path, exactly as the X2
  override writer spells them: `visibility` and `purpose` as the native
  tokens, `active` as prim metadata, the xformOps of an overridden transform,
  and every other value as an `erhe:Owner:name` custom attribute. An opinion
  travels as text, so the property registry is what types it again; a name
  that reaches no property, or text that does not parse, is one warning and
  no attribute. The prim's own attributes outside the variants are what the
  writer writes for the state the scene holds today, which the selected
  variant's opinions equal. A prim a variant adds
  (`Usd_save_variant::prims`) is written inside that variant's block as the
  `def` it is, with its whole subtree, under the name the file gave it rather
  than the name it has in the tree, and is not among the plain children of the
  prim carrying the set. Its `active` metadatum is written only for the
  selected variant's prims: a prim of another variant is inactive because its
  variant is not the selection, which USD says by not building the prim at
  all. glTF has no counterpart for any of this: a scene saved to glTF writes
  those prims as plain children with their `active` flags, and the membership
  is lost.
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
  network with one warning. Each written `UsdUVTexture` carries the slot's
  wrap modes and its `inputs:scale`: the scale is the erhe factor of the
  surface input the slot feeds (base color and emissive on rgb, roughness on
  green and metallic on blue), and for the normal slot it is the texel decode,
  written with `inputs:bias`. All of them are written whatever the erhe value
  is, because USD's fallbacks are not erhe's and an unwritten value would not
  read back. A wrap value on a slot with no texture has no `UsdUVTexture` to
  ride on and is not written.

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

## Survey of the USD Assets Working Group repository

`doc/usd-wg-assets.md` records what this library and the editor make of every
entry asset of github.com/usd-wg/assets - the load result, the counts, the
warnings and errors, a capture framed through the MCP `frame_scene` tool, the
renders the repository ships beside the asset, and a verdict per asset, with
the gaps that list orders by how many assets each affects;
`scripts/usd_wg_asset_survey.py` regenerates it against a local clone.

The OpenUSD binary distribution (NVIDIA's pre-built OpenUSD, unpacked to a
folder the docs call `<usd_root>`, its wrappers under `<usd_root>/scripts/`)
is the reference implementation to compare against, and every tool below
runs headless on Windows:

- `usdrecord.bat <file> <out.png>` renders the composed stage with Storm
  (skinning, instancing and variants applied), which is the reference image
  for an asset that ships none, and the image a capture is compared with
  when the shipped screenshot was made from another version of the file.
- `usdchecker.bat <file>` validates a file erhe wrote; the round-trip script
  runs it when `ERHE_USDCHECKER` names the wrapper.
- `set_usd_env.bat && python <script.py>` runs a script against the `pxr`
  module: `UsdGeom.XformCache` gives every prim's local and world matrix,
  `UsdSkel.Cache` / `SkinningQuery.ComputeSkinnedPoints` the skinned
  points, which is how a "wrong transform" report is split into the node
  chain (compare per prim with `get_node_details`) and the deformation.
- `usdtree.bat`, `usdcat.bat` (also `.usdz` and `.usdc` to text),
  `usddiff.bat` and `usdview.bat` for the rest.

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

`test/data/active.usda` covers the prim `active` metadatum: an inactive
`Xform` with a child and an active sibling. `test_usd_active.cpp` asserts
that the inactive prim still becomes an item, that the metadatum lands as a
local `active` value, that the child of an inactive prim is inactive without
a local value of its own, that a prim without the metadatum has none, and
that all of it survives a `save_usda` / `load_usd` round trip.

`test/data/double_sided.usda` covers `doubleSided`: three `Mesh` prims, one
authoring `1`, one authoring `0` and one authoring nothing.
`test_usd_double_sided.cpp` asserts that both authored opinions land as local
`Gprim.double_sided` values (including the `0` that equals the default), that
the unauthored prim has none, that a save writes exactly the two attributes
and no `erhe:Gprim:double_sided`, that the values survive a reload, and that
a second save is byte-identical.

`test/data/looks.usda` covers where materials sit: two `Scope`s below one
`Xform` holding three materials, two of them named alike, each bound by a
mesh of its own. `test_usd_materials.cpp` asserts that the material prims
land at their stage paths, that the binding follows the path rather than the
name, that a save writes each `Material` prim back where it sits (and invents
no `Materials` scope) and that the second save is byte-identical.

`test/data/variants.usda` covers material-binding variant sets: an `Xform`
with a two-variant `look` set selecting `"blue"`, whose variants bind the
`Mesh` below it and that mesh's `GeomSubset` to two materials, one of which
nothing outside the variants binds; and a second `Xform` whose set authors
`visibility` rather than a binding, for the unsupported-opinion report.
`test_usd_variants.cpp` asserts the recorded table, that the selected
variant's materials are the ones the mesh's primitives bind, that the
non-binding opinions are reported once for their set, that a save writes the
`variantSets` / `variants` / `variantSet` lines back and that the second save
is byte-identical.

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

`test/data/primitives.usda` holds one prim of each UsdGeom primitive schema -
a `Cube` with a `displayColor` and `displayOpacity`, a `Sphere`, a `Cone` and
a `Capsule` on Z, a `Cylinder` on Y bound to a material, and a `Cylinder_1`
with a radius per end.
`test_usd_primitives.cpp` asserts that each becomes a `Mesh` prim with one
primitive, facets and edges, that the size / radius / height / `axis`
attributes land where the generator puts them, that the binding reaches the
primitive, that the cube's display color and opacity reach every corner while
the uncolored sphere carries none, that a save writes `def Mesh` with points and no `Cube` or
`Cylinder` spelling, and that the round trip settles after the first reload
(save two and save three are byte-identical).

`test/data/textured.usda` binds an image file through a `UsdUVTexture`
network; it is the round-trip script's texture case rather than a unit-test
input.

`test/data/point_instancer.usda` holds one `PointInstancer` with two
prototypes - an `Xform` holding a `Cube` and one holding a `Sphere` - and four
instances with per-instance scales. `test_usd_point_instancers.cpp` asserts
that the prim becomes an `erhe::scene::Point_instancer` carrying no array of
its own, that the record names the prototypes and the four instance
transforms, that every instance is a child prim carrying an internal reference
to its prototype, that the prototype subtree is held abstract while the
instances are content, that a save writes the arrays back with the prototypes
as plain children and the instance prims left out, that a moved instance
persists through the save, and that the round trip settles after the first
reload the way the primitive-schema one does.

`test/data/brushes.usda` holds a `Brushes` scope with two `Brush` prims - one
binding a material from a `Looks` scope, one not - and a third without a
`geometry` child. `test_usd_brushes.cpp` asserts the two are recorded with
their geometry counts, density, token and material path, that the third is one
warning and no brush, that no brush geometry is scene content, and that a
brush item is written back as the same prim, twice byte for byte.

`test/data/skinning.usda` is the skinning case: a `SkelRoot` holding a
two-joint `Skeleton` whose `bindTransforms` put the tip twice as far out as
its `restTransforms` do, and a `Mesh` with the `SkelBindingAPI`, `elementSize`
2 skin primvars whose weights are unnormalized and out of order, and a
non-identity `geomBindTransform`. `test/data/skel_animation.usda` is the same
rig with a `SkelAnimation` and no `geomBindTransform`.
`test_usd_skinning.cpp` asserts the `SkelRoot` / `Skeleton` prim classes and
tokens, the joint prim paths and rest transforms, the skin's joints, pivot and
inverse bind matrices, the normalized per-vertex influences, and the joint
channels with their samples in seconds. It round-trips both files as well:
that the `Skeleton` prim comes back with its joint arrays and that no joint
is written as a prim of its own, that the mesh applies the `SkelBindingAPI`
next to its `skel:skeleton` relationship and carries the influences at the
`elementSize` erhe uses, that the reload gives the same skin and the same
per-vertex influences, that the joint channels come back keyed at the same
seconds with the same values, and that a second save is byte-identical.

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
  travel with them, and the brushes and styles travel as prims of their own.
  The geometry and texture node graphs and the remaining resource kinds have
  no USD form yet (C1); a save logs one line per kind the scene holds. The writer also emits no `.usdc` or `.usdz`, no
  MaterialX, and none of the composition structure of the file it loaded -
  the first version flattens what it read (plan steps X1 and X2).
- A node-held secondary value (D30, `Light.color` on a plain Xform) is written
  as `erhe:Light:color` but the import resolves neither the qualified nor the
  bare name against a node, so such a value does not come back.
- A camera's `infinite_z_far` has no USD form; the finite `clippingRange` is
  written and one warning says so.
- usdchecker on a written file reports two things the writer still does:
  `UsdUVTexture` `inputs:st` is typed `texCoord2f` where the schema says
  `float2` (LightUSD's `UsdUVTexture::st` member is a `texcoord2f`
  attribute, so the spelling is the dependency's and a fork change fixes it),
  and a texture that came out of a `.usdz` archive is written with the path
  the archive authored, resolved against the written file's directory,
  which names no file on disk (`MissingReferenceChecker`); writing such a
  scene needs the packed bytes extracted next to the file, or the
  `archive.usdz[entry]` form.
- The macOS and Linux configure wrappers still default to `none`; turning the
  option on there is part of the step that first needs USD on those platforms.
- The Quest launch with the option on - the editor coming up on the headset
  and answering `describe_usd_file` over the forwarded MCP port - is still
  pending; only the Android build and its size and build-time cost are
  measured (see "Configurations").
