# USD compatibility plan

The concept and naming mapping every step relies on is
`doc/usd_compatibility.md` (referred to below as "the mapping"); this
document holds the goal, what already holds, the remaining steps, their
order and their verification.

## 1. Goal

Make erhe's scene model and property system general enough that a USD
stage can be loaded, edited and eventually composed inside the editor,
one small step at a time. Each step is useful on its own, without USD
code, or it is a thin USD adapter over a model that already fits. The
goal has three stages:

- G1 Load and edit USD. The editor opens a `.usd` / `.usda` / `.usdc` /
  `.usdz` file (some files, some schemas: the mapping's tables say
  which), shows it as an erhe scene and edits it with the normal tools.
  Saving that scene is not part of G1. glTF scenes keep loading, editing
  and saving as they do today.
- G2 Save USD. A scene loaded from USD saves back as USD, with the
  editor's own state carried as USD custom attributes and schemas, the
  way `ERHE_*` extensions carry it in glTF.
- G3 glTF or USD. A scene is either glTF-backed or USD-backed for its
  whole life: loaded, edited, saved and reloaded in the format it came
  from, with the full editor feature set in both. Mixing the two - a
  glTF scene saving as USD, a USD scene saving as glTF, a prefab of one
  format inside a scene of the other - is not a goal at any stage; an
  export that happens to work is a convenience, never a requirement.

Constraints every step respects:

- C1 Each format carries its own features. glTF and USD each have
  features the other cannot express, and neither format gets an
  extension written to carry the other's: a USD-only feature (a
  composition arc, a variant set, a time-sampled attribute) lives in the
  in-memory model and in USD files; an erhe feature that a USD file must
  keep is expressed with USD's own means (custom attributes, applied API
  schemas, `customData`), as `ERHE_*` extensions do it in glTF. The
  in-memory model is the shared ground, and the glTF round trip
  (`scripts/scene_roundtrip_verify.py`) keeps passing after every step.
- C2 A step lands in one commit series that a headless run can verify
  (`erhe-headless-verify`), with the same self-review-per-step discipline
  as the property migrations (`doc/property-system.md` section 4.18),
  worked through the roles and review gates of
  `doc/agent-orchestration-harness.md`.
- C3 A model change is expressed in erhe vocabulary and documented in the
  owning subsystem's record; the mapping gains or updates the row that
  connects it to USD. The plan never restates a mapping row.
- C4 USD library code is optional at build time (`ERHE_USD_LIBRARY=none`
  keeps every current configuration byte-identical). Every step is built
  and verified on desktop Windows; the Quest build and launch are verified
  once (Q1 below) and re-run only when a step changes the Android build.
- C5 One object model, shaped like a USD stage. Every scene, whichever
  format backs it, is one tree of prims under the scene root, and the
  erhe class of a prim sits in a class hierarchy that mirrors the USD
  schema hierarchy: `Typed` at the root, `Scope` beside `Imageable`,
  `Xformable` under `Imageable`, `Xform` and `Camera` under `Xformable`,
  `Boundable` under `Xformable`, `Gprim` under `Boundable`, `Mesh` under
  `Gprim` (`py -3 scripts/usd.py` prints the reference tree). Any prim
  may parent any other prim, and a resource (a material, a texture, a
  brush, a style, a physics material, a node graph) is a prim in that
  same tree, conventionally gathered under a `Scope`. A typed prim is a
  child prim of its parent, never an attachment of it, and a parent may
  hold several `Mesh` children. Every class under `Xformable` carries a
  transform, as erhe's `Node` does today (a `Mesh` under an `Xform`
  composes its own transform, identity unless authored, with its
  parent's); every class outside `Xformable` has none, and a prim's
  world transform composes with its nearest `Xformable` ancestor, so a
  transform passes through the prims that have none. What stays on a
  prim as an attachment is exactly what USD applies to a prim as an API
  schema (physics body and joint, layout hints, brush placement, prefab
  instance carrier). glTF is a serialization of that tree, as USD is
  (G3): the glTF reader and writer map their node + mesh + flat resource
  lists onto it and back. The U steps of section 2 brought the model to
  this shape; an `Xformable`'s transform is the TRS its authored xformOp
  stack composes to (M8), or that TRS alone when it has no stack.

## 2. What holds today

G1 holds for the schemas the importer covers, G2 holds for scene
content (editor state beyond the scene block is E4), and the reference-instance
part of G3 holds (X2). Each landed step is
listed with the record that now owns its behavior; `git log` on that
record has the history.

- M1 Item paths: `Hierarchy::get_path()`, `erhe::find_by_path`,
  `Item_base::get_reference_path()`; stored references, expressions and
  MCP take a path (`src/erhe/item/notes.md`, the mapping's "Identity and
  addressing").
- M2 Sibling-unique names: `Hierarchy::make_sibling_unique_name`
  (`<base>_<n>` from 1) at child attach; a rename into a collision is
  refused (`src/erhe/item/notes.md`).
- M3 Visibility and purpose: `Item_base::purpose` (`default | render |
  proxy | guide`, `inherits`, derived from the editor-only flag bits)
  next to `visible` (`doc/property-system.md` D31).
- M4 Local values are the authored set: a local value means authored,
  for every item type and independent of the file format
  (`doc/property-system.md` D32; glTF's default-elision pass in
  `doc/gltf-properties-extension-plan.md`).
- L1 LightUSD as an optional CPM dependency: `ERHE_USD_LIBRARY`
  (`lightusd | none`), `erhe::usd` as the only code that includes
  LightUSD headers, `describe_usd_file` over MCP; the Windows wrappers
  and the Android build pass `lightusd` (`src/erhe/usd/notes.md`,
  "Configurations" and "Duplicate symbols").
- I1 Import a USD file as an asset: `load_usd` -> `Usd_data` through
  Tydra, the asset browser's Import, viewport drop and MCP `import_usd`,
  undoable through the glTF import's operation path
  (`src/erhe/usd/notes.md` "Import", `src/editor/parsers/notes.md`).
- I2 Authored opinions become local values: `Importer::is_authored`
  gates every field; `erhe:Owner:name` custom attributes become property
  values (`src/erhe/usd/notes.md` "Import").
- E1 Save as USDA: a USD-backed scene (`Scene_root::get_source_format()`)
  saves back through `save_usda` with local values only, `erhe:` custom
  attributes for erhe-only properties, tags as collections and the scene
  block as `customLayerData` (`src/erhe/usd/notes.md` "Export",
  `doc/scene_serialization.md` "USD-backed scenes").
- E3 Round-trip script: the `usd-roundtrip` section of
  `scripts/scene_roundtrip_verify.py` (`doc/scene_serialization.md`,
  "Verifying round-trips"); `usdchecker` runs when an OpenUSD build is
  available.
- Q1 Quest build and launch: the `quest` flavor builds with the option on
  and answers `describe_usd_file` over the forwarded MCP port; the size
  and build-time cost are tabulated in `src/erhe/usd/notes.md`
  "Configurations".
- U1 Prim class hierarchy: `erhe::Typed` and `erhe::Scope`
  (`src/erhe/item/notes.md` "Prim classes"), `erhe::scene::Imageable`,
  `Xformable` (today's `Node`, alias kept), `Xform`, `Boundable` and
  `Gprim` (`src/erhe/scene/notes.md`); a transform composes with the
  nearest `Xformable` ancestor and the item host is carried through every
  prim; the USD reader and writer map class and `typeName` one to one and
  glTF carries `Scope` / `Typed` on `ERHE_node` (`src/erhe/usd/notes.md`
  "Import" / "Export", `doc/gltf_extensions/ERHE_node.md`); MCP
  `create_node` takes `prim_type`. Object-reference candidates and
  `Layout` do not reach through a `Scope` yet: the candidate walk visits
  the registered transformable prims and the resource index, and
  `Layout` arranges its direct `Node` children.
- U2 Mesh is a Gprim: `erhe::scene::Mesh` is `erhe::Item<Item_base,
  Gprim, Mesh>`, a child prim with its own transform; a parent holds any
  number of `Mesh` children; `get_mesh()`, `for_each_mesh_child()` and
  `set_mesh_parent()` replace the attachment accessors
  (`src/erhe/scene/notes.md`); the glTF reader folds a node with a mesh
  into one `Mesh` prim and the writer inverts it
  (`doc/scene_serialization.md`); USD takes a `Mesh` prim as it stands
  (`src/erhe/usd/notes.md`); `Xformable`'s secondary owner type is
  `Item_base` (`doc/property-system.md` D30).
- U3 Camera and Light are Xformables: `Camera` and `Light` are
  `erhe::Item<Item_base, Xformable, X>` child prims with their own
  transform (`Light` keeps `light_type`; per-schema light classes wait
  for a light type that needs its own properties); `set_prim_parent()`,
  `get_camera()` and `get_light()` are the helpers and no typed prim has
  a `get_node()` (`src/erhe/scene/notes.md`); `Node_attachment` remains
  for `Node_physics`, `Node_joint`, `Layout`, `Brush_placement`,
  `Prefab_instance`, `Frame_controller` and `Grid`; the hierarchy
  context menu's "Create" lists every prim kind (child of the clicked
  prim) and "Add Attachment" the API-schema kinds (`Attachment_kind`), the
  hierarchy accepts a drag payload named for the prim's class, and MCP
  `get_node_details` carries `mesh` / `camera` / `light` on the node
  entry (`mcp_server_usage.md`). The interactive drag gesture has not
  been exercised since the payload fix.
- U4 Resources are prims: every content-library kind is `erhe::Item<
  Item_base, Typed, X>` and a prim of the scene tree, by default under
  the kind `Scope` created on its first resource (`Materials`, `Brushes`,
  ...) or under any prim; `Content_library` is an index fed by
  `Item_host::register_prim` (`src/erhe/item/notes.md`,
  `src/editor/content_library/notes.md`); a folder is a `Scope`
  (`doc/content-library-folders.md`); resources enter and move through
  `Item_insert_remove_operation` and `Item_parent_change_operation`;
  there is no reference listing - a material a scene renders but does
  not own reaches the material set through the mesh binding
  (`doc/asset_manager.md`); glTF carries a resource's tree position in
  `ERHE_scene` `library_folders` (`doc/gltf_extensions/ERHE_scene.md`)
  and USD writes and reads a `Material` prim where it sits
  (`src/erhe/usd/notes.md`). `scene_roundtrip_verify.py` covers the
  placements (215 checks). Not carried in USD yet: the other resource
  kinds and an empty folder scope (E4).
- M6 The value types USD needs: `Property_type::double_floating`,
  `mat4`, `asset_path`, `float_array` and `int_array`, each with its D16
  text form, expression rule and Properties row
  (`doc/property-system.md` D2, D16), written and read as USD `double`,
  `matrix4d`, `asset`, `float[]` and `int[]` attributes
  (`doc/usd_compatibility.md` Property-system rows);
  `erhe_property_tests` and `erhe_usd_tests` cover all five. No shipped
  property is of any of these types yet: the xformOp stack (M8) is not
  exposed as properties, and a texture slot is still an object
  reference.
- M7 Style chains: a style has a style of its own; the style layer is
  the chain of the styles' local values, nearest first, and `set_style`
  refuses a cycle (`doc/property-system.md` D25, `doc/style-library.md`);
  `ERHE_scene` `styles[].style` carries it
  (`doc/gltf_extensions/ERHE_scene.md`). USD carries no styles until E4.
- M8 xformOp stacks: an `Xformable` carries the xformOp stack it was
  authored with (op types, suffixes, `!invert!`, authored precisions,
  `!resetXformStack!` stored only) and composes it to its TRS; a
  transform edit lands in the op the stack designates or, when no op can
  carry it, collapses the stack to one `transform` op
  (`src/erhe/scene/notes.md`); the USD reader builds the stack from the
  raw prim and the writer emits it as authored, falling back to one
  `xformOp:transform` for a prim without a stack
  (`src/erhe/usd/notes.md`, `doc/usd_compatibility.md`). `erhe_usd_tests`
  round-trips a three-op stack, a pivot pair and a matrix op byte for
  byte and lands a move in the translate op alone. glTF keeps writing the
  composed TRS (C1).
- Transform time samples: a time-sampled `xformOp:*` attribute carries its
  samples on the op, in the file's own time codes; the stage is evaluated
  at one time code - `startTimeCode` when authored, else the earliest
  sample - so the pose a load gives the scene is the reference frame of
  its clips, and a save writes the samples back beside the value with the
  layer's `timeCodesPerSecond`, `startTimeCode` and `endTimeCode`. The
  playable projection is one `erhe::scene::Animation` per file, keyed in
  seconds, holding one channel per sampled op of every prim whose stack is
  a `[translate, rotate, scale]` the channels can drive; a stack outside
  that keeps its samples, keeps its start-time pose, and is named in one
  warning. `erhe_usd_tests` round-trips a sampled stack to a fixed point
  (`src/erhe/usd/notes.md`, "Time samples").
- X1 References as prefab instances: LightUSD composes nothing at load
  (its composition option is declared and not implemented), so a
  referencing prim arrives as authored with its `references` and
  `payload` metadata and erhe resolves the arcs itself:
  `Usd_data::references` lists each prim's arcs in the order USD
  composes them, the importer stops below a referencing prim, and the
  editor attaches one `Prefab_instance` per arc (source path, prim path,
  arc kind) and clones the target through `Prefab_library`, keyed by
  (file, prim path) and loading a USD file at a prim as a template
  (`src/erhe/usd/notes.md` "Import", `src/editor/parsers/notes.md`,
  `doc/gltf-prefabs-plan.md`). The instance content is the target prim
  itself and its subtree under the carrier, one level more than USD's
  own composition; a save writes the carrier as the referencing prim
  with its arcs and none of the content, so the round trip is a fixed
  point (`src/erhe/usd/notes.md` "Export"). An arc inside a template is
  instantiated through the same library (a cycle is refused); `Prefab_library::reload` refreshes a carrier with several
  arcs from its first attachment.
- X2 Editable instances with sparse overrides: a USD reference is a
  composition arc, not a copy. Every item inside an instance names its
  template counterpart as its reference source
  (`doc/property-system.md` D33, R3 is coerced, local, style,
  reference, inherited, default): the layer reads what the counterpart
  supplies itself, local, style or its own reference, and not what it
  inherits, so the instance's own tree provides inheritance and an
  override on an instance ancestor reaches its descendants; a template
  edit reaches every instance live. `attach_prefab_instance` links the
  clones to the template in lockstep and clears the copied locals, so a
  template value reads `reference` and a local inside an instance is an
  override (`doc/gltf-prefabs-plan.md`). A reference protects structure
  only: `src/editor/prefabs/instance_structure.hpp` owns the two
  refusals every structural entry point asks (nothing added under a
  carrier or inside one; nothing inside removed or reparented), while
  every property inside is editable and clearing a local exposes the
  reference value. USD seals nothing, so a USD-backed instance is not
  sealed; the sealed editing model stays glTF-only. `Item_base::active`
  (own opinion, default true) with the derived `Item_flags::active`
  bit takes an item and its whole subtree out of rendering, picking,
  simulation and every content walk and dims it in the hierarchy; USD
  carries it as the prim's `active` metadatum, glTF in
  `ERHE_node.properties`. `Item_base::defined` (own opinion, default
  true) is the prim's composed specifier and feeds the same derived bit,
  so an undefined prim (`over`, no defining opinion anywhere) and its
  whole subtree are out the way USD's default traversal predicate leaves
  them out, while the prim stays a valid reference target; USD carries it
  as the specifier itself, glTF in `ERHE_node.properties`. Persistence: `erhe::scene::instance_override`
  states once what an override is (`src/erhe/scene/notes.md`); a USD
  save writes each overriding item as an `over` prim below the carrier
  holding its local values and `active` only, the clone of the target
  prim being the carrier prim itself, and the reader reads them off the
  composed layer's prim specs (`src/erhe/usd/notes.md`); glTF carries the
  list as `ERHE_node.overrides` on the carrier
  (`doc/gltf_extensions/ERHE_node.md`); a prefab reload captures and
  re-applies them. Attachments inside an instance (applied API schemas)
  are not walked for overrides (section 6). MCP
  `set_prefab_template_property` edits a template in place.
- X3 Class inheritance: a `class` prim is a Style item. The reader takes
  the class prims off the composed layer's own prim specs (Tydra never walks
  one) into `Usd_data::classes` with their `inherits` targets, authored
  opinions and nested classes, and every other prim's `inherits` arcs into
  `Usd_data::prim_inherits`; the editor makes one Style item per class at
  the place the class prim has, applies the opinions
  (`erhe::scene::apply_property_values`, shared with the X2 override
  path) and gives each prim its first target that resolved to a Style as
  its style, a second target, a non-class target and a dangling one being
  one warning each (`src/editor/parsers/notes.md`). The writer inverts it:
  a Style item (told by its class token) is a typeless `class` prim where
  it sits, every value an `erhe:Owner:name` custom attribute since a class
  prim carries no schema, and every prim with a style carries
  `inherits = </path>` by the path the prim actually got
  (`src/erhe/usd/notes.md`; the mapping's style rows). An `over` prim
  inside an instance writes schema-named values the same custom way, for
  the same reason. glTF keeps `ERHE_scene.styles` (C1).
- X4 Variants: material bindings and property opinions. The reader
  records each prim spec's `variantSet` blocks (`Usd_data::variant_sets`:
  per variant, the `material:binding` relationships by path below the
  prim and the property opinions as `Instance_override` entries in the
  same neutral form X2 records an `over` in; the `variants` selection or
  the first variant; and the set's base values, what the prims held for
  every path and name any variant authors). It binds the selected
  variant's materials itself, converting a material only a variant binds
  through Tydra's per-material converter, and applies its opinions
  through `apply_property_values`. The prims a variant adds are in the
  tree whichever variant is selected: `load_stage` copies the `def`
  children of every variant block into the prim carrying the set, under
  sibling-unique names (M2), and marks the unselected variants' prims
  `active = false`, so a switch is a property write rather than a rebuild
  of the tree. What stays uncarried is a property the value reader cannot
  express and a `def` the hoist does not reach: counted per set, reported
  once, and named by the save warning
  (`src/erhe/usd/notes.md` "Variant sets"). The editor keeps one
  `Variant_table` per scene (`Scene_root`; weak prim and materials,
  pruned on `items_removed`), the selection in
  `Scene_settings::variant_selections`, and `Scene_root::select_variant`
  switches as one undoable compound of a selection record, one property
  write per opinion any variant of the set authors - the chosen variant's
  value where it authors one, the set's base value where it does not -
  one `active` write per prim any variant of the set adds, and one
  material assignment per binding; the Scene section of the
  Properties window draws one combo per set, MCP has
  `get_scene_variants` / `select_variant`, and a USD save writes the
  blocks and the selection back (`src/editor/scene/notes.md`,
  `doc/scene_serialization.md`). On the glTF side `KHR_materials_variants`
  is the same table as one set named `materials` on the file's root
  prim, a primitive named `<mesh path>#<index>`, the selection in the
  scene block (`src/erhe/gltf/notes.md`); it carries bindings only, so a
  variant's property opinions and the prims it adds are USD features: a
  scene saved to glTF writes those prims as plain children with their
  `active` flags and loses the membership.
- K1 Skinning: a `Mesh` with the `SkelBindingAPI` is skinned the way a
  glTF mesh with a skin is, and saves back with it (`src/erhe/usd/notes.md`
  "Skinning" owns the rules). The `Skeleton` prim is a transformable prim
  carrying its token and holding one `Xform` per joint at the joint's
  `restTransforms` entry, so a joint's world transform is
  `skelLocalToWorld * jointSkelSpace_j`; a skinned mesh gets a `Skin` per
  (skeleton, `geomBindTransform`) whose `inverse_bind_matrices[j]` is
  `inverse(bind_j) * geomBindTransform`, and the joint primvars are the
  `joint_indices_n` / `joint_weights_n` vertex attributes on both build
  paths; `SkelAnimation` joint channels are channels of the file's
  animation on the joint prims. The editor attaches the skins to the
  content library as it does glTF skins; registration and the skinned
  build were format-neutral. The writer reads `Mesh::skin`: joints are
  written as the skeleton's `joints` / `restTransforms` entries rather
  than prims, the bind pose is recomputed with an identity
  `geomBindTransform` for a skeleton's first skin and `bind_0 *
  inverse_bind_0` as the `geomBindTransform` of any further skin, the
  mesh applies the `SkelBindingAPI` with `vertex` primvars at the
  narrowest `elementSize`, and the joint channels become one
  `SkelAnimation` below the skeleton. A save is a fixed point;
  `usdchecker` passes; the survey's CarbonFrameBike cables sit where
  pxr's `ComputeSkinnedPoints` puts them (4 mm).
- X5 Composition provenance in the Properties window: erhe resolves every
  arc itself - references and payloads as prefab instances with the
  reference layer (X1, X2), `over` opinions as local values (X2), class
  inherits as styles (X3), variant selections as bindings (X4) - so the
  erhe value source IS the composition provenance, and a value's origin in
  USD terms is a function of `Value_source` plus what the editor knows
  about the item: its scene's file, its prim path, the `Prefab_instance`
  carrier above it and the `Style` it uses.
  `editor::describe_property_origin`
  (`src/editor/windows/property_origin.hpp`) derives it on demand - layer,
  prim path, arc, arc target and the attribute a save spells the value as,
  the last from `erhe::usd::get_usd_authored_as`, which owns the writer's
  naming rule - and keeps nothing alive: LightUSD's `ArcOrigin` is
  prim-level and records implied inherits only, so a live `Layer` would add
  nothing erhe cannot already say. A Properties row appends the origin to
  its tooltip while it is hovered (`Property_editor::set_entry_tooltip_extra`)
  and MCP `get_item_properties` reports it as each property's `origin`
  (`doc/usd_compatibility.md` "Where a value comes from",
  `src/editor/windows/notes.md`, `mcp_server_usage.md`). A glTF-backed
  scene answers in the same shape with the glTF file as the layer and
  `properties["Owner.name"]` of the item's `ERHE_*` extension as the
  attribute. The `pcp` DAG engine stays the option for a full-stack case
  (sublayers, section 5); live re-composition after an edit is not planned.

## 3. Remaining steps

Steps are grouped by what they touch: M = model generalization (no USD
code), E = export, X = composition; animation beyond the skeleton (K1)
and physics are section 6, future work outside every stage. Sizes are relative: S = an afternoon, M = a few days, L = a
week or more.

### E4 Editor state in a USD file (M; completes G2)

What: the editor state a USD-backed scene does not carry yet
(`doc/scene_serialization.md`, "USD-backed scenes", owns the list and
the `customLayerData` keys already in use) rides USD's own means (C1).
The resources are prims (U4), so each kind is written and read as a
prim where it sits in the tree, one custom `typeName` per kind (the
class token `Typed` already fixes) with attributes named as the glTF
fields are; `erhe::usd` records what the layer authors and the editor
creates the item, as X3 does for a class prim. Animations and the
physics API schemas on nodes (section 6) stay listed as not carried;
skins are carried (K1). A save no longer logs a kind it carries; the open side reads
every kind it writes. `.usdc` output follows once the `.usda` output
round-trips through E3 with all of it. The parts below land
independently, in the order E4a, S1, E4c, E4b, E4d.

#### E4a Brushes (S; landed, `src/erhe/usd/notes.md` "Brushes")

A brush (`ERHE_brushes` in glTF: name, geometry, material, density,
normal style; the collision shape is rebuilt from the geometry) is a
`Brush`-typed prim where it sits:

```
def Scope "Brushes" {
    def Brush "Cube" {
        custom float erhe:Brush:density = 1
        custom token erhe:Brush:normal_style = "polygon_normals"
        rel material:binding = </World/Materials/Copper>
        def Mesh "geometry" { points, faceVertexCounts, ... }
    }
}
```

- The geometry is the child `Mesh` prim, written by the mesh writer from
  the brush's geometry with `subdivisionScheme = none`, which the I1
  reader keeps normative on reload (the `ERHE_geometry` role in glTF).
  The brush prim itself holds it, so no node-unreferenced carrier is
  needed. Facet materials of the geometry, when it has them, ride the
  child's `GeomSubset` bindings as on any `Mesh`.
- `material:binding` on the brush prim is the brush's own material
  property (the material a placed instance gets), by path as U4 binds.
- `density` and `normal_style` are `erhe:Brush:` custom attributes, the
  token spelled as the glTF field is.
- `purpose = guide` is derived from the brush flag (M3) and not
  authored, so a foreign viewer does not render the brush mesh; `Brush`
  is not a USD schema, so such a viewer sees a prim of unknown type with
  a `Mesh` child, and the U1 `Typed` fallback keeps its place when the
  file comes back.
- Reader: `Usd_data::brushes` (path, name, the child mesh's converted
  geometry, material path, density, normal style); the editor creates
  the `Brush` item at the path, as the glTF loader rebuilds one from
  the carrier mesh, binds the material by path and places it in the
  tree; the `Brush` prim's subtree is skipped by the scene conversion
  (its mesh is not scene content).

Verification (holds): the round-trip script's brushes leg over
`brushes.usda` and a headless session with two brushes built over MCP,
one with a material; reload lists both with the same counts, density,
normal style and material, `place_brush` works on the reloaded brush,
the file is a fixed point from the first reload on (the first save of a
brush built in memory differs in vertex order, since the mesh reader
re-indexes), and the scene closes clean.

#### S1 USD Assets Working Group survey (M; first run landed, `doc/usd-wg-assets.md`)

What: every entry asset of the ASWF USD Assets Working Group repository
(`<usd-wg-assets>`, a local clone of github.com/usd-wg/assets:
`full_assets/*`, `test_assets/*` and `intent-vfx/scenes/*`) is opened in
the editor and its outcome recorded in `doc/usd-wg-assets.md`: the
entry file, whether it loads, the prim, mesh, material and light counts
against what the file authors, the warnings and errors the load logs,
a screenshot, and a verdict (works, works with a named gap, fails with
a named cause). The document is the checklist of USD support the editor
still lacks, ordered by how many assets each gap blocks, and every
later step of this plan takes its next fix from that list.
`scripts/usd_wg_asset_survey.py` drives a headless editor over the
whole set and regenerates the table, so the survey re-runs after each
fix.

The survey re-runs after each fix and the document is regenerated.
What holds: an unlit scene is lit by a per-viewport headlight the way
usdview's camera light lights it, a `DomeLight` is the scene's ambient
light, a typeless or `Scope` prim that authors arcs is an `Xform`
carrier, any prim is a reference target and a `class` prim's `def`
descendants are prototypes held abstract, a root layer's subLayers are
composed (the save writes one flattened layer) and every prim of the
composed layer stack carries its authored opinions, `class` prims and
`xformOp` stacks whichever layer authored it, every `Material` prim
converts, a material binding authored as an `over` is an instance
override and a template's material has an owner, the `UsdGeom`
primitive schemas import as the meshes they describe, a `PointInstancer`
is expanded into a prototype held abstract and one prim per instance
holding an internal reference to it, with the instance prims the only
record of what a save writes back, USD `st` crosses
the V flip with `UsdTransform2d` carried through it, a scalar input
reads the texture channel the file connects, an unauthored
`diffuseColor` is USD's 0.18, a `UsdUVTexture`'s wrap, transform,
scale and per-channel normal decode reach the material, with a texture
packed in a `.usdz` read out of the archive, a mesh with no material
of its own renders erhe's default look, and a prim's `doubleSided`
opinion is a `Gprim.double_sided` property the renderers take together
with the material's own flag, so a cross-shaped card shows both of its
faces. A `UsdTransform2d` places
its texture where usdview does, measured face on against the
reference render and pinned by the placement case of
`src/erhe/usd/test/test_usd_texture_channels.cpp`. An alpha-blended and
an alpha-tested primitive reach the frame in a viewport that draws the
grid, which is what puts McUsd's stained glass and its sunflower and
fern cards back into the render: the grid is a blended overlay and
writes no depth (`doc/editor_rendering.md`, Grid). The current run (146
entries, 51 work as they are, none crash) leaves nothing in the S1 order:
what it still shows - 16-bit and CMYK images undecoded, the intent-vfx
teapot scenes tripping the stall watchdog on load, grid lines crossing
opaque objects, a keyed animation edit not written back, MaterialX - is
section 6. The document's rows are what the subset re-runs after each fix
merged in; the next full run regenerates it whole.

Verification (holds): the script runs over every entry asset without
leaving the editor down, and the document lists every entry file once.

#### E4c Texture node graphs (M; landed)

A `Graph_texture` is the `UsdShade` network it is: a `NodeGraph` prim
where the asset sits, one `Shader` child per node with an `erhe:texture:`
`info:id`, parameters and pins as `inputs:` / `outputs:` attributes,
links as attribute connections, and a material slot that samples the
graph connected to the graph's interface output in place of a
`UsdUVTexture`. `doc/usd-texture-graphs-plan.md` owns the design, the
record between `erhe::usd` and the editor, the two phases and the
verification. Both phases hold: `erhe::usd` reads and writes the prims as
the neutral record (`src/erhe/usd/notes.md`, "Texture node graphs";
`erhe_usd_tests` 245) and the editor collects and rebuilds a
`Graph_texture` from it, binds the material slots, and the round-trip
script's `texture_graph.usda` leg is green with a byte-identical second
save (MCP `get_scene_node_graphs`).

#### E4b Geometry node graphs (M)

A `Graph_mesh` reuses E4c's prim form with `erhe:geometry:` node ids and
the evaluated geometry as a child `Mesh "result"` prim written the way
a brush writes its geometry (E4a); `doc/usd-texture-graphs-plan.md`
section 4 states the rule. E4c lands first.

#### E4d Content-library folders (S)

A folder holding nothing the file carries is still written as the
`Scope` it is, and an empty `Scope` on reload is a folder in its place,
so the folder tree survives a save whatever it holds.

### E2 Material fidelity (M)

What: erhe-only material fields that `UsdPreviewSurface` cannot carry
(anisotropic roughness, transmission, brushed metal) export additionally
as an `OpenPBRSurface` / MaterialX network when
`LIGHTUSD_WITH_USDMTLX` is on; import prefers the OpenPBR network when
both are present.

## 4. Order

Each step independently landable, in this order:

1. E4 editor state in a USD file: E4c, E4b, E4d in that order (E4a landed; completes G2)
2. E2 material fidelity

Dependencies: E4b to E4d and E2 need nothing that has not landed.

## 5. Out of scope

- Structural edits inside a reference (adding, removing or reparenting
  a prim under a referencing prim): an instance is the template's
  structure with value overrides, and a prim that is not wanted is
  deactivated (X2).
- Converting between the formats: a glTF scene saved as USD or a USD
  scene saved as glTF, and prefabs of one format inside a scene of the
  other (G3).
- glTF extensions that carry USD-only features, and USD schemas that
  exist only to carry glTF-only encodings (C1).
- OpenUSD as a build dependency (`src/erhe/usd/notes.md` "Dependency"
  says how it is used instead: schema reference and validator only).
- Live re-composition of an edited stage (X5 names it as the step after
  provenance).
- `specializes`, payload load policies, sublayer stacks and the session
  layer: no erhe feature maps onto them yet; the mapping lists them as
  having no erhe counterpart.
- The schema attributes of a `Typed` prim of unsupported type (U1): the
  prim and its place survive a round trip, its attributes do not, until
  a step wants a generic property dictionary on `Typed` (the mapping's
  `Property_set` row is the shape).

## 6. Future work

Physics, and the animation the transform time samples do not cover, are
outside G1, G2 and G3: a USD scene loads, edits and saves without them
until the items below are taken up, and E1 writes no `UsdPhysics`
schemas. Each item is independent of the others and of every step in
section 3 except where named.

- Stale world bounds on the first framing after a prefab instantiation:
  `frame_scene` on the first scene a session opens through a reference or
  payload reads the pre-instantiation bounds (the survey's
  `payload_child_folder.usda` row at 4950% is this; a second framing reads
  the right ones). The same load-settle family as the load-performance item.
- The bounds rows the survey still shows in `full_assets`
  (`doc/usd-wg-assets.md`, "bounds disagree"): Creases_SpinningPyramids
  (8.3x, node-subtree variants) and vehicleVariants (0.11), measured before
  the survey's own bounds comparison was corrected (Z-up conversion, lights
  in the pxr bound); re-measure with the gap loop
  (`doc/usd-survey-gap-loop.md`) before diagnosing. Every `test_assets`
  bounds row is closed.
- `inherits` and `specializes` arcs whose target is not a `class` prim
  (usd-wg inherit_and_specialize.usda inherits from a `def Cube`): X3 makes
  a style only of a class prim, so such an arc composes nothing and is
  warned about; the surveyed file overrides every inherited opinion locally,
  so nothing visible depends on it there.
- Two LightUSD limits worked around downstream (`src/erhe/usd/notes.md`,
  "Texture node graphs"): Tydra fails a material whose input connects to a
  `NodeGraph`, so `load_stage` strips that wiring from the copy Tydra sees;
  the USDA parser does not round-trip an escaped double quote, so nested
  parameter text travels with single quotes. Both go with a fork fix.
- 16-bit PNG and CMYK JPEG decoding: the survey's TextureFileFormatTests
  tiles for them render blank. wuffs can decode both to 8-bit RGBA, so this
  is a failure in erhe's use of it that no log names per file yet
  (`src/erhe/graphics/erhe_graphics/image_loader_wuffs.cpp`); diagnose with a
  per-file decode-failure log line first. Radiance `.hdr` needs a decoder
  erhe does not build (`stb_image.h` sits in the CPM cache of fpng and
  LightUSD); no surveyed asset in that folder needs it.
- Load performance of a scene holding thousands of prims (the intent-vfx
  teapot scenes, several minutes with the stall watchdog firing): each queued
  raytrace commit scans every mesh of every layer
  (`collect_meshes_sharing_primitives`, O(N) per commit, N commits per
  load), hover traces the linear path every frame while the TLAS cannot
  settle, and `finalize_imported_meshes` builds the per-shape BVHs serially
  on the tick thread. A shape-to-meshes index maintained at the change
  sites, a hover that does not trace while a load is in flight, and the
  proxy build on the deferred path are the fixes, in that order.
- Grid depth: the grid's depth does not agree with the content's, so grid
  lines cross opaque objects below the horizon (`doc/editor_rendering.md`,
  Grid). Needs a RenderDoc session on the windowed build.
- Animation edits written back: a keyed edit made in erhe changes the
  Animation channels and the pose but not the `Xform_op` samples a USD save
  writes, so an edited clip saves as the file's original samples.
- MaterialX: a `.mtlx` document as a reference target (the editor refuses
  the arc), a `Material` whose surface is a `ND_standard_surface_surfaceshader`
  or `ND_open_pbr_surface_surfaceshader` network (Tydra converts it into
  `RenderMaterial::openPBRShader`, which the importer does not read; import
  prefers that network when both are present, per E2), and the LightUSD usda
  reader's rejection of `colorSpace` metadata on a shader attribute (the
  survey's one failing entry). The `.mtlx` reader is behind
  `LIGHTUSD_WITH_USDMTLX`, off in erhe's build.
- Animated value layer: the property-system section 6 item, an animated
  value between coerced and local in R3, set by `Animation_sampler::apply`
  and cleared when playback stops, so playback never overwrites the
  authored local value. USD resolves time samples above `default`;
  importing a time-sampled attribute without this layer would clobber
  the authored pose, and saving would write the playback pose as
  `default`. It is also the prerequisite of the keyframing plan
  (`doc/animation-keyframing-plan.md`) and of animation channels on
  arbitrary properties, so it pays for itself without USD.
- Time samples beyond the transform: a time-sampled `xformOp:*` attribute
  is carried as authored and played as an `erhe::scene::Animation`
  (`src/erhe/usd/notes.md`, "Time samples") and `SkelAnimation` joint
  channels are K1, which leaves time samples on any other attribute, and `Ts` splines re-encoded as cubic
  samplers. A stack the TRS channels cannot drive keeps its samples and
  its start-time pose and is named in one warning; driving it needs a
  transform channel that composes ops rather than a TRS.
- Reconciling an edit with the authored ops: the samples an op carries are
  what the file authored, so keying an animation or moving an animated
  prim changes the playable channels and the composed pose but not the
  ops a save writes. Writing an edit back into the samples is what the
  animated value layer above makes well defined.
- Variant opinions a variant set does not carry: the `def` children of a
  variant block that the hoist does not reach - one authored below an
  `over` child of the variant, and any of them in a `.usdz` archive, whose
  asset paths resolve through the archive rather than the file system -
  and a property the value reader cannot express. Each is counted in
  `Usd_variant_set::unsupported_opinion_count`, reported per set, and
  named by the save warning. (`GeomModelAPI` draw-mode cards are the
  common case: Teapot.usd's two variants author nothing else.) Taking the
  first up means hoisting through the `over` children too; the second is
  the value reader's own coverage.
- Overrides on applied API schemas inside an instance: the override walk
  of `erhe::scene::instance_override` visits prims only, so a local value
  on a `Node_physics`, `Node_joint` or other attachment below a carrier is
  neither written as part of the carrier's `over` prims nor kept across a
  prefab reload. Taking it up means walking the attachments in the same
  lockstep the counterpart link uses and giving each an `over` path
  (USD authors an applied schema's attributes on the prim itself).
- Writer findings of `usdchecker` (`src/erhe/usd/notes.md`, "Future work"):
  the `texCoord2f` typing of `UsdUVTexture` `inputs:st`, and a texture
  packed in a `.usdz` written as a path that names no file.
- Physics on load: `UsdPhysics` API schemas become
  `Node_physics`, `Node_joint`, `Physics_material` and `Collision_filter`
  per the mapping's physics table, through a USD-filled sibling of
  `Gltf_physics_data` (the physics import operations already take a
  plain-data carrier). The matching save applies the API schemas per the
  same table; erhe-only physics properties (damping, wind receptivity,
  gravity factor, combine modes) ride `erhe:` custom attributes under C1.
