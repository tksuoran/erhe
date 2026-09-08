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
  The first save is a change of representation, so the round trip is a fixed
  point from the first reload on rather than from the first save.
  `PointInstancer` remains an `erhe::Typed` prim (plan section 5).
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

### Sublayers

A root layer's `subLayers` are the weakest layers of its layer stack (the `L`
of USD's LIVRPS), and LightUSD composes nothing at load, so the stage its
reader builds holds the root layer alone - a file whose content lives in a
sublayer would arrive empty. `erhe::usd::load_stage` composes the stack itself
before anything converts the stage: it re-reads the file as a
`lightusd::Layer`, hands it to LightUSD's `CompositeSublayers` (which resolves
each asset path against its own layer's directory, follows nested `subLayers`,
detects cycles and merges per property, so an `over` in a stronger layer lands
on the `def` of a weaker one as one prim) and turns the composed layer back
into the stage with `LayerToStage`. Everything downstream - the Tydra
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
- The reads that ask which layer authored a thing keep reading the root layer
  alone: the class prims and `inherits` arcs of X3, the `Brush` prims of E4a,
  the authored-opinion pass of I2 and the `xformOp` stacks of M8 all go
  through `find_root_layer_primspec`. A prim a sublayer authors is therefore
  content of the composed tree but contributes none of those: its transform
  arrives as the single composed matrix Tydra reports rather than as the op
  stack the sublayer spells, and a `class` prim of a sublayer is a prim like
  any other.
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
- A root-level `over` with `def` descendants and no `def` of its own - the
  shape a file uses when its own prims reference it - is a prim of the tree
  like any other: LightUSD reconstructs it whatever its specifier, and it
  imports as the typeless `Typed` prim it is, so a reference to it resolves.
  The writer spells it `def`, the round trip's fixed point.
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
  which layer an opinion on a composed prim came from, so the root layer is
  re-read once per file and its prim specs below the referencing prim are what
  is read: the specifier is what tells an `over` from a `def`. A prim spec is what a layer authored, so every property it
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
the root layer's own prim specs and the conversion skips the class prims
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

`read_layer_composition()` walks the root layer once, before the prims are
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

### Brush prims

A brush is editor state a USD file carries as a prim of its own type
(doc/usd-compatibility-plan.md E4a): `def Brush "<name>"` where the brush sits
in the tree, holding `erhe:Brush:density` and `erhe:Brush:normal_style` custom
attributes, a `material:binding` to the material a placed instance gets, and
its geometry as a child `def Mesh "geometry"` written with
`subdivisionScheme = none`, so a viewer without erhe sees a prim of unknown
type with a mesh below it. The spellings are `usd_impl.hpp` constants, which
the reader and the writer share.

The same root-layer walk that takes the class prims records a `Brush`-typed
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

### Variant sets

A variant set is resolved in composition, and LightUSD composes nothing, so a
variant contributes no property to the composed prim: the `variantSet` blocks
are read off the root layer's own prim specs the same walk takes the class
prims from, and the reader is what applies the selection
(doc/usd-compatibility-plan.md X4). Material bindings and property opinions
are carried; a prim a variant adds is not.

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
is what carries it, so nothing binds a material twice - and both `over` and
`def` children of a variant contribute their opinions.

`Usd_variant_set::base_values` is what the prims held for every path and
property name any variant of the set authors, read before the selected
variant's opinions were applied. A property with no local value there is a
`cleared` entry, so putting it back clears rather than writes. This is what
a switch to another variant restores first: a property the chosen variant
leaves unsaid goes back to what the file authored outside the variant blocks.

`unsupported_opinion_count` is what stays uncarried, reported once for the
set: a property the value reader has no place for, and a prim a variant adds
that the tree has no counterpart for - node subtree variants are the later
slice, so an override whose path reaches no prim is dropped when the base
values are captured.

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

Not yet imported: skeletons and skinning, blend shapes, animation clips,
`PointInstancer` / instanceable prototypes beyond what Tydra flattens,
volumes, MaterialX / OpenPBR shading networks, texture filter state, and the
per-channel output selection of a `UsdUVTexture` (erhe reads roughness from
green and metallic from blue, whichever channels the file's `outputs:*`
connections name).

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
  variant's opinions equal.
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
a `Cube`, a `Sphere`, a `Cone` and a `Capsule` on Z, a `Cylinder` on Y bound
to a material, and a `Cylinder_1` with a radius per end.
`test_usd_primitives.cpp` asserts that each becomes a `Mesh` prim with one
primitive, facets and edges, that the size / radius / height / `axis`
attributes land where the generator puts them, that the binding reaches the
primitive, that a save writes `def Mesh` with points and no `Cube` or
`Cylinder` spelling, and that the round trip settles after the first reload
(save two and save three are byte-identical).

`test/data/textured.usda` binds an image file through a `UsdUVTexture`
network; it is the round-trip script's texture case rather than a unit-test
input.

`test/data/brushes.usda` holds a `Brushes` scope with two `Brush` prims - one
binding a material from a `Looks` scope, one not - and a third without a
`geometry` child. `test_usd_brushes.cpp` asserts the two are recorded with
their geometry counts, density, token and material path, that the third is one
warning and no brush, that no brush geometry is scene content, and that a
brush item is written back as the same prim, twice byte for byte.

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
- The macOS and Linux configure wrappers still default to `none`; turning the
  option on there is part of the step that first needs USD on those platforms.
- The Quest launch with the option on - the editor coming up on the headset
  and answering `describe_usd_file` over the forwarded MCP port - is still
  pending; only the Android build and its size and build-time cost are
  measured (see "Configurations").
