# USD compatibility design

Stability: mostly stable

The concept and naming mapping every step relies on is
`doc/usd_compatibility.md` (referred to below as "the mapping"); this
document holds the goal, what holds today and what is out of scope.
The work that is left is `doc/plans/usd_compatibility.md`.

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
  as the property migrations (`doc/property_system.md` section 4.18),
  worked through the roles and review gates of
  `doc/agent_orchestration_harness.md`.
- C3 A model change is expressed in erhe vocabulary and documented in the
  owning subsystem's record; the mapping gains or updates the row that
  connects it to USD. This document never restates a mapping row.
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

G1, G2 and G3 hold as section 1 states them, for the schemas the
mapping's tables cover. Each landed step is listed with the record that
now owns its behavior; `git log` on that record has the history.

- M1 Item paths: `Hierarchy::get_path()`, `erhe::find_by_path`,
  `Item_base::get_reference_path()`; stored references, expressions and
  MCP take a path (`doc/erhe_item.md`, the mapping's "Identity and
  addressing").
- M2 Sibling-unique names: `Hierarchy::make_sibling_unique_name`
  (`<base>_<n>` from 1) at child attach; a rename into a collision is
  refused (`doc/erhe_item.md`).
- M3 Visibility and purpose: `Item_base::purpose` (`default | render |
  proxy | guide`, `inherits`, derived from the editor-only flag bits)
  next to `visible` (`doc/property_system.md` D31).
- M4 Local values are the authored set: a local value means authored,
  for every item type and independent of the file format
  (`doc/property_system.md` D32; glTF's default-elision pass in
  `doc/plans/gltf_properties_extension.md`).
- L1 LightUSD as an optional CPM dependency: `ERHE_USD_LIBRARY`
  (`lightusd | none`), `erhe::usd` as the only code that includes
  LightUSD headers, `describe_usd_file` over MCP; the Windows wrappers
  and the Android build pass `lightusd` (`doc/erhe_usd.md`,
  "Configurations" and "Duplicate symbols").
- The `tksuoran/LightUSD` fork carries the behavior erhe needs beyond the
  build fixes the pin's comment names: the Tydra fallback for a shading
  connection it does not model (`doc/erhe_usd.md` "Node graphs"),
  the USDA parse of an escape pair in a string literal (same section),
  the composition of a relationship's targets as a list op across the
  layer stack ("Sublayers") and the `float2` typing of a `UsdUVTexture`
  `inputs:st` ("Export", the textures bullet). Each is described where the
  behavior it produces is described.
- I1 Import a USD file as an asset: `load_usd` -> `Usd_data` through
  Tydra, the asset browser's Import, viewport drop and MCP `import_usd`,
  undoable through the glTF import's operation path
  (`doc/erhe_usd.md` "Import", `doc/editor_parsers.md`).
- I2 Authored opinions become local values: `Importer::is_authored`
  gates every field; `erhe:Owner:name` custom attributes become property
  values (`doc/erhe_usd.md` "Import").
- E1 Save as USDA: a USD-backed scene (`Scene_root::get_source_format()`)
  saves back through `save_usda` with local values only, `erhe:` custom
  attributes for erhe-only properties, tags as collections and the scene
  block as `customLayerData` (`doc/erhe_usd.md` "Export",
  `doc/scene_serialization.md` "USD-backed scenes").
- E3 Round-trip script: the `usd-roundtrip` section of
  `scripts/scene_roundtrip_verify.py` (`doc/scene_serialization.md`,
  "Verifying round-trips"); `usdchecker` runs when an OpenUSD build is
  available.
- Q1 Quest build and launch: the `quest` flavor builds with the option on
  and answers `describe_usd_file` over the forwarded MCP port; the size
  and build-time cost are tabulated in `doc/erhe_usd.md`
  "Configurations".
- U1 Prim class hierarchy: `erhe::Typed` and `erhe::Scope`
  (`doc/erhe_item.md` "Prim classes"), `erhe::scene::Imageable`,
  `Xformable` (today's `Node`, alias kept), `Xform`, `Boundable` and
  `Gprim` (`doc/erhe_scene.md`); a transform composes with the
  nearest `Xformable` ancestor and the item host is carried through every
  prim; the USD reader and writer map class and `typeName` one to one and
  glTF carries `Scope` / `Typed` on `ERHE_node` (`doc/erhe_usd.md`
  "Import" / "Export", `doc/gltf_extensions/ERHE_node.md`); MCP
  `create_node` takes `prim_type`. Object-reference candidates and
  `Layout` do not reach through a `Scope` yet: the candidate walk visits
  the registered transformable prims and the resource index, and
  `Layout` arranges its direct `Node` children.
- U2 Mesh is a Gprim: `erhe::scene::Mesh` is `erhe::Item<Item_base,
  Gprim, Mesh>`, a child prim with its own transform; a parent holds any
  number of `Mesh` children; `get_mesh()`, `for_each_mesh_child()` and
  `set_mesh_parent()` replace the attachment accessors
  (`doc/erhe_scene.md`); the glTF reader folds a node with a mesh
  into one `Mesh` prim and the writer inverts it
  (`doc/scene_serialization.md`); USD takes a `Mesh` prim as it stands
  (`doc/erhe_usd.md`); `Xformable`'s secondary owner type is
  `Item_base` (`doc/property_system.md` D30).
- U3 Camera and Light are Xformables: `Camera` and `Light` are
  `erhe::Item<Item_base, Xformable, X>` child prims with their own
  transform (`Light` keeps `light_type`; per-schema light classes wait
  for a light type that needs its own properties); `set_prim_parent()`,
  `get_camera()` and `get_light()` are the helpers and no typed prim has
  a `get_node()` (`doc/erhe_scene.md`); `Node_attachment` remains
  for `Node_physics`, `Node_joint`, `Layout`, `Brush_placement`,
  `Prefab_instance`, `Frame_controller` and `Grid`; the hierarchy
  context menu's "Create" lists every creatable prim kind, resources
  included, on every prim row (child of the clicked prim) and "Add
  Attachment" the API-schema kinds (`scene/attachment_types.hpp`), the
  hierarchy accepts a drag payload named for the prim's class, and MCP
  `get_node_details` carries `mesh` / `camera` / `light` on the node
  entry (`mcp_server_usage.md`). The interactive drag gesture has not
  been exercised since the payload fix.
- U4 Resources are prims: every content-library kind is `erhe::Item<
  Item_base, Typed, X>` and a prim of the scene tree, by default under
  the kind `Scope` created on its first resource (`Materials`, `Brushes`,
  ...) or under any prim; `Content_library` is an index fed by
  `Item_host::register_prim` (`doc/erhe_item.md`,
  `doc/editor_content_library.md`); a folder is a `Scope`
  (`doc/content_library_folders.md`); resources enter and move through
  `Item_insert_remove_operation` and `Item_parent_change_operation`;
  there is no reference listing - a material a scene renders but does
  not own reaches the material set through the mesh binding
  (`doc/asset_manager.md`); glTF carries a resource's tree position in
  `ERHE_scene` `library_folders` (`doc/gltf_extensions/ERHE_scene.md`)
  and USD writes and reads a `Material` prim where it sits
  (`doc/erhe_usd.md`); the other resource kinds and a folder are E4.
  `scene_roundtrip_verify.py` covers the placements.
- M6 The value types USD needs: `Property_type::double_floating`,
  `mat4`, `asset_path`, `float_array` and `int_array`, each with its D16
  text form, expression rule and Properties row
  (`doc/property_system.md` D2, D16), written and read as USD `double`,
  `matrix4d`, `asset`, `float[]` and `int[]` attributes
  (`doc/usd_compatibility.md` Property-system rows);
  `erhe_property_tests` and `erhe_usd_tests` cover all five. No shipped
  property is of any of these types yet: the xformOp stack (M8) is not
  exposed as properties, and a texture slot is still an object
  reference.
- M7 Style chains: a style has a style of its own; the style layer is
  the chain of the styles' local values, nearest first, and `set_style`
  refuses a cycle (`doc/property_system.md` D25, `doc/style_library.md`);
  `ERHE_scene` `styles[].style` carries it
  (`doc/gltf_extensions/ERHE_scene.md`); USD carries a style as a `class`
  prim and the chain as its `inherits` (X3).
- M8 xformOp stacks: an `Xformable` carries the xformOp stack it was
  authored with (op types, suffixes, `!invert!`, authored precisions,
  `!resetXformStack!` stored only) and composes it to its TRS; a
  transform edit lands in the op the stack designates or, when no op can
  carry it, collapses the stack to one `transform` op
  (`doc/erhe_scene.md`); the USD reader builds the stack from the
  raw prim and the writer emits it as authored, falling back to one
  `xformOp:transform` for a prim without a stack
  (`doc/erhe_usd.md`, `doc/usd_compatibility.md`). `erhe_usd_tests`
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
  that (a pivot pair, a sampled matrix op, `[orient, translate]`, two ops
  of a kind) is posed and composed at the union of its sample times and
  baked into translation, rotation and scale channels, exact at every
  sample, with one info line naming the reason; the samples stay on the
  ops either way. `erhe_usd_tests` round-trips a sampled stack to a fixed
  point (`doc/erhe_usd.md`, "Time samples").
- X1 References as prefab instances: LightUSD composes nothing at load
  (its composition option is declared and not implemented), so a
  referencing prim arrives as authored with its `references` and
  `payload` metadata and erhe resolves the arcs itself:
  `Usd_data::references` lists each prim's arcs in the order USD
  composes them, the importer stops below a referencing prim, and the
  editor attaches one `Prefab_instance` per arc (source path, prim path,
  arc kind) and clones the target through `Prefab_library`, keyed by
  (file, prim path) and loading a USD file at a prim as a template
  (`doc/erhe_usd.md` "Import", `doc/editor_parsers.md`,
  `doc/plans/gltf_prefabs.md`). The instance content is the target prim
  itself and its subtree under the carrier, one level more than USD's
  own composition; a save writes the carrier as the referencing prim
  with its arcs and none of the content, so the round trip is a fixed
  point (`doc/erhe_usd.md` "Export"). A typeless or `Scope` prim
  that authors an arc is an `Xform` carrier defined by the arc, keeping
  the `xformOp` stack it authors, and a carrier's authored stack composes
  in place of the target's own (`xformOpOrder` is one attribute, so the
  referencing layer's stack wins), the target's clone getting the identity;
  a carrier without a stack lets the target's transform stand. An arc
  inside a template is instantiated through the same library (a cycle is
  refused); `Prefab_library::reload` refreshes a carrier with several arcs
  from its first attachment.
- X2 Editable instances with sparse overrides: a USD reference is a
  composition arc, not a copy. Every item inside an instance names its
  template counterpart as its reference source
  (`doc/property_system.md` D33, R3 is coerced, local, style,
  reference, inherited, default): the layer reads what the counterpart
  supplies itself, local, style or its own reference, and not what it
  inherits, so the instance's own tree provides inheritance and an
  override on an instance ancestor reaches its descendants; a template
  edit reaches every instance live. `attach_prefab_instance` links the
  clones to the template in lockstep and clears the copied locals, so a
  template value reads `reference` and a local inside an instance is an
  override (`doc/plans/gltf_prefabs.md`). A reference protects structure
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
  states once what an override is (`doc/erhe_scene.md`); a USD
  save writes each overriding item as an `over` prim below the carrier
  holding its local values and `active` only, the clone of the target
  prim being the carrier prim itself, and the reader reads them off the
  composed layer's prim specs, a typeless `def` below the carrier being
  the same override of the child of that name (a typed `def` adds
  structure and is dropped with a warning, "Out of scope")
  (`doc/erhe_usd.md`); glTF carries the
  list as `ERHE_node.overrides` on the carrier
  (`doc/gltf_extensions/ERHE_node.md`); a prefab reload captures and
  re-applies them. Attachments inside an instance (applied API schemas)
  are not walked for overrides (`doc/plans/usd_compatibility.md`,
  "Overrides on applied API schemas inside an instance"). MCP
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
  one warning each (`doc/editor_parsers.md`). The writer inverts it:
  a Style item (told by its class token) is a typeless `class` prim where
  it sits, every value an `erhe:Owner:name` custom attribute since a class
  prim carries no schema, and every prim with a style carries
  `inherits = </path>` by the path the prim actually got
  (`doc/erhe_usd.md`; the mapping's style rows). An `over` prim
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
  (`doc/erhe_usd.md` "Variant sets"). The editor keeps one
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
  blocks and the selection back (`doc/editor_scene.md`,
  `doc/scene_serialization.md`). On the glTF side `KHR_materials_variants`
  is the same table as one set named `materials` on the file's root
  prim, a primitive named `<mesh path>#<index>`, the selection in the
  scene block (`doc/erhe_gltf.md`); it carries bindings only, so a
  variant's property opinions and the prims it adds are USD features: a
  scene saved to glTF writes those prims as plain children with their
  `active` flags and loses the membership.
- K1 Skinning: a `Mesh` with the `SkelBindingAPI` is skinned the way a
  glTF mesh with a skin is, and saves back with it (`doc/erhe_usd.md`
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
- C6 Composition the real assets use, three forms erhe's own
  fixtures do not have. An arc a variant block authors (usd-wg
  `full_assets/Teapot/Teapot_Geometry.usd` prepends the reference to
  `UtahTeapot.usd` on its `Utah` variant) is the arc of the prim carrying
  the set while that variant is selected: `read_prim_references` resolves
  the selected variant's `references` and `payload` list ops after the
  prim's own (the prim's are the stronger opinion), every `Usd_reference`
  names the set and variant it came from, and the writer authors it back
  inside that block, so such a file is a fixed point; a prim holds one
  list of arcs and not one per variant, so an unselected variant's arcs
  count toward the set's `unsupported_opinion_count`. A `def` a variant
  block authors on a prim that also references (the `Materials` scope of
  `Teapot_Materials.usd`) is the variant's own content rather than an
  edit over the reference, so a carrier converts and writes back exactly
  its hoisted variant children while its arcs supply the rest. An op a
  prim's `xformOpOrder` names but one of its arcs supplies (the
  duplicates of `DrawModes.usd`, whose first op arrives through an
  internal reference) is resolved against the arc targets where the stack
  is reconstructed - the arcs in order, the first target authoring the
  property winning, a carrier target followed under a cycle guard, the
  target's raw prim spec as the source so type and precision are kept;
  an arc into another file is not followed and such an op leaves the
  prim on its composed transform with one warning; a save authors the
  resolved op as the carrier's local value, which composes to the same
  transform. And an override path is resolved one segment at a time,
  looking one level down through the clone of every carrier it crosses
  and not only the first (`erhe::scene::find_instance_item`; a carrier
  is a prim holding an attachment with the `prefab_instance` type bit),
  so the intent-vfx teapot's `over "geo" { over "default" { over "Body"
  } }` reaches the mesh below the second reference
  (`doc/erhe_usd.md` "Variant sets", "xformOp stacks";
  `doc/erhe_scene.md`). Teapot.usd imports its 1 mesh and 2
  materials and DrawModes.usd its 35 meshes at the composed bounds, which
  C10 renders as usdview renders them.
- C7 Variant selection through a composition arc. A prim that references
  or payloads a target may author `variants = { ... }` for the sets the
  target declares, and LIVRPS resolves that selection stronger than the
  target's own. `Usd_reference::variant_selections` is what the referencing
  prim authors for what its arcs bring in (the same entries on every arc of
  the prim; a `variants` on an `over` child below the carrier is the entry
  of that child's path), `Usd_load_arguments::variant_selections` hands it
  to the target's load rooted at the prim the arc names (an empty root is
  the layer's `defaultPrim`, resolved inside `load_stage`), `load_stage`
  validates every entry once against the layer - the prim must exist,
  declare the set and hold the variant, each failing entry one warning and
  dropped - and the hoist and the reader both consult the kept entries
  before the prim's own `variants` metadatum. The editor keys a template by
  the selection (`Prefab_key`, `Prefab_variant_selection`), so two carriers
  selecting different variants of one target load two templates, which is
  what USD composes; a selection also reaches the arcs of the prims it
  names, because a variant set is composed once out of the prim's whole
  index. The key carries only the entries the target CONSUMES - the ones
  naming a variant set the target's own file declares, or a file below it
  does (`Prefab::consumed_variant_sets`, reported by
  `load_usd_prefab_template` from `Usd_data::variant_sets` and the nested
  templates' own lists) - so a selection travelling down an arc to a file
  that declares no set of that name parses that chain once
  (doc/editor_parsers.md, "A `variants` selection an arc carries").
  `Prefab_instance` records
  the arc's FULL selection, a read-only Properties
  row and `get_node_details` show it, and the writer authors it back on the
  carrier - merged into the carrier's own `variants` metadatum, a deeper
  entry inside the `over` prim of its path. A template's own variant sets
  are not tabled, so the selection is not a `Variant_table` entry; a switch
  of it means re-targeting the instance to the template of the other
  selection, which is taken when a file needs it. Fixtures
  `references_variants.usda` / `references_variants_target.usda`;
  DrawModes.usd loads 25 distinct templates - seven `Teapot.usd` prim
  indexes, seven internal-arc targets, seven `Teapot_Payload.usd` ones and
  two each of `Teapot_Geometry.usd` and `geo/*.usd`, which consume the
  model variant alone - and its Fancy column holds
  the Fancy geometry (`doc/erhe_usd.md` "Variant sets", "Composition
  arcs"; `doc/editor_parsers.md`).
- C8 A `UsdPreviewSurface` input fed by a `UsdPrimvarReader`. Tydra
  resolves a connected input to a `UsdUVTexture` or fails the whole
  material, so `load_stage` takes a connection to a `UsdPrimvarReader_*`
  out of the layer copy the stage is built from (the texture-graph strip's
  shape) and records the material, the input and the primvar; the
  importer maps `displayColor` onto `Material::base_color_source =
  vertex_color` and `displayOpacity` onto `opacity_source = vertex_color`
  (`erhe::primitive::Material_input_source`; any other primvar is one
  warning and the input keeps its own value), and the writer authors the
  reader prim and the connection back. The pair rides the material record
  as `input_sources` (a `uvec2` beside `texture_channels`); with
  `vertex_color` the shaders take the mesh's color attribute alone, the
  factor and the texture unread, and with `value` the factor, the texture
  and the vertex color multiply as before. A material's `outputs:surface`
  is followed through `NodeGraph` prims to the Shader that holds the
  inputs. The usd-wg Teapot's `Ceramic` and SubdivisionSurfaces'
  `PyramidMaterial` convert (3 of 3 there; `doc/erhe_usd.md`
  "UsdPreviewSurface fallbacks and channel outputs";
  `doc/erhe_scene_renderer.md`). A material converted after Tydra's
  pass takes its texture and image ids from the render scene's own lists,
  so two appended materials reading one file share the image.
- C9 Nested variant sets and the constant displayColor opinion. A
  `variantSet` a variant block declares is a set of the prim carrying the
  outer set, tabled beside it and naming the block it is declared in
  (`Usd_variant_set::enclosing_set_name` / `enclosing_variant_name`;
  editor `Variant_set_key`); one rule resolves every selection - the
  arc-carried one (C7), the enclosing block's own `variants`, the prim's,
  the first block - and a nested set's blocks contribute only while every
  enclosing block is the selected one, at the hoist (its prims active only
  then), at the read (opinions, bindings, base values, arcs) and at a
  switch (the sets of the block being left go off, the chosen block's come
  on with the selections they hold, one compound). A variant's material
  path is resolved through the prims its own chain of blocks hoisted, so
  the M2 rename does not break a binding into the variant's own content.
  A constant `primvars:displayColor` is `Gprim.display_color`, an
  entry-store vec3 that inherits (default USD's 0.18 grey, authored exactly
  when local): a write states the change to the scene host, which rebuilds
  the mesh's primitives with the color once (`Build_info::constant_color`
  for a geometry build, a recolored soup copy for a soup build); the
  importer fills it from the constant it bakes and admits the primvar as a
  variant / over opinion; the writer authors it back as the constant
  primvar; a clone whose color the reference layer supplies rebuilds when
  it enters its host. An opinion or binding whose path a composition arc
  supplies is kept pending by the reader and applied by the editor once
  the arcs are in the tree (`apply_pending_variant_opinions`, through
  `find_instance_item`, which is transparent at every carrier level), on
  the import, open-scene and template paths alike. DrawModes.usd's six
  Utah columns render in their shading variant's color
  (`doc/erhe_usd.md` "Variant sets"; `doc/erhe_scene.md`;
  `doc/editor_parsers.md`; `doc/editor_scene.md`).
- C10 `GeomModelAPI` draw modes. A model prim's `UsdGeomModelAPI` is an
  `editor::Draw_mode` attachment of that prim (`Item_type::draw_mode`),
  holding every attribute of the schema as an entry property named as
  `doc/usd_compatibility.md` "Draw modes" names it; the neutral record is
  `erhe::scene::Draw_mode_description`, whose enumerations spell USD's
  tokens, read per prim applying the schema (or authoring a `model:`
  attribute) and written back in the schema's spelling. A value of the
  record is named `Draw_mode.<property>` wherever a name addresses it: a
  variant block's `model:` attributes are carried under that name,
  `find_override_property_target` resolves it to the attachment - made on
  the spot through the applied-schema attachment registry
  (`erhe::scene::register_applied_schema_attachment`) when the prim holds
  none, which is what `prepend apiSchemas` inside the block means - and a
  carrier's attachment reads its arc target's through the reference layer
  (`link_carrier_attachments_to_target`). A prim whose own mode asks for a
  proxy takes its children's subtrees out of render, pick and simulation
  (`Item_base::set_prunes_children`, ANDed into the derived active bit;
  the prim and its attachments stay) and supplies the proxy: `bounds` and
  `origin` as lines per viewport from `Draw_mode_renderer` (the authored
  `extentsHint`, else the measured bounds of the meshes below), `cards` as
  a session-only child `Mesh` flagged `Item_flags::draw_mode_proxy`
  (exempt from the pruning, pick redirected to the model prim, never
  written), one unlit single-sided face per card cut the way
  `UsdImagingDrawModeAdapter` cuts it (the `cross` pair of an axis shares
  the mid plane and faces opposite ways; back-face culling resolves the
  pair, where the adapter's 2^-23 offset z-fights), its image alpha-tested at the
  adapter's 0.1 threshold, in `drawModeColor` when the face has no image;
  an inactive prim owns no proxy. `resolved_draw_mode()` walks `inherited`
  up to the nearest authored ancestor. DrawModes.usd renders as usdview
  renders it: seven teapots and 28 proxies in their columns' colors and
  card images; the survey's Storm render draws `bounds` and `origin` as
  filled slabs where usdview draws lines, which is the entry's remaining
  image difference and what its expected-results record states
  (`doc/editor_scene.md` "Draw modes"; `doc/erhe_usd.md`
  "Draw modes"; `doc/erhe_item.md`).
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
  `doc/editor_windows.md`, `mcp_server_usage.md`). A glTF-backed
  scene answers in the same shape with the glTF file as the layer and
  `properties["Owner.name"]` of the item's `ERHE_*` extension as the
  attribute. The `pcp` DAG engine stays the option for a full-stack case
  (sublayers, "Out of scope"); live re-composition after an edit is not
  planned.

- E4 Editor state in a USD file: every resource kind of the content
  library is written and read as a prim where it sits in the tree, one
  `typeName` per kind (the class token `Typed` fixes) with attributes named
  as the glTF fields are, `erhe::usd` recording what the layer authors and
  the editor creating the item, as X3 does for a class prim
  (`doc/scene_serialization.md`, "USD-backed scenes", owns the list of what
  the layer carries and the `customLayerData` keys). A save logs no kind
  it carries and the open side reads every kind it writes. The output is
  `.usda` only (`doc/plans/usd_compatibility.md`, "Binary and packaged
  output").
  - Brushes: a brush is a `Brush`-typed prim where it sits, its geometry
    the child `Mesh` prim the mesh writer emits with
    `subdivisionScheme = none` (normative on reload, the `ERHE_geometry`
    role in glTF), facet materials as the child's `GeomSubset` bindings,
    its own material as `material:binding` on the brush prim, `density`
    and `normal_style` as `erhe:Brush:` custom attributes and
    `purpose = guide` derived rather than authored; `Usd_data::brushes`
    is the record and the scene conversion skips the brush's subtree
    (`doc/erhe_usd.md` "Brush prims"). The round-trip script's
    brushes leg holds; the first save of a brush built in memory differs
    from the second in vertex order, since the mesh reader re-indexes,
    and the file is a fixed point from the first reload on.
  - Texture node graphs: a `Graph_texture` is the `UsdShade` network it
    is - a `NodeGraph` prim where the asset sits, one `Shader` child per
    node with an `erhe:texture:` `info:id`, parameters and pins as
    `inputs:` / `outputs:` attributes, links as attribute connections, and
    a material slot fed by the graph connected to the graph's interface
    output in place of a `UsdUVTexture`. `doc/usd_node_graphs.md`
    owns the design and the record between `erhe::usd` and the editor
    (`doc/erhe_usd.md` "Node graphs"; MCP `get_scene_node_graphs`);
    the round-trip script's `texture_graph.usda` leg holds with a
    byte-identical second save.
  - Geometry node graphs: a `Graph_mesh` reuses the texture-graph prim
    form with `erhe:geometry:` node ids and the evaluated geometry as a
    child `Mesh "result"` written the way a brush writes its geometry;
    `doc/usd_node_graphs.md` section 4 states the rule, the
    scene-block carrier for the bindings (each naming a prim by the path
    the write plans for it, `erhe::usd::plan_usd_prim_paths`) and the
    identifier spelling a pin and a parameter name travel in. The
    round-trip script's geometry-graph leg holds with the save a fixed
    point from the first reload.
  - Folders: every `Scope` of the tree is written where it sits, whatever
    it holds - a content-library folder, a kind scope (`Materials`,
    `Brushes`, ...) and a `Scope` a file authored are one kind of prim, so
    the folder tree survives a save empty; on reload a kind scope is
    recognized by its name and adopted rather than made a second time
    (`Content_library::adopt_kind_scopes`, `doc/editor_parsers.md`).
    A top-level `Scope` does not force the `World` wrapper: a scene with
    one prim of its own beside its kind scopes names that prim as the
    stage's `defaultPrim` and writes the scopes beside it. A skin and an
    animation are library resources the `Skeleton` prim and the sampled
    `xformOp`s carry, so those items are not written as prims. The
    round-trip script's library-folder leg drives the editor's own folders
    through a save and a reload.
- S1 USD Assets Working Group survey: every entry asset of the ASWF USD
  Assets Working Group repository (`<usd-wg-assets>`, a local clone of
  github.com/usd-wg/assets: `full_assets/*`, `test_assets/*` and
  `intent-vfx/scenes/*`) is opened in a headless editor by
  `scripts/usd_wg_asset_survey.py`, which regenerates `doc/usd-wg-assets.md`:
  per entry whether it loads, the prim, mesh, material and light counts
  against the composed stage (measured with pxr when an OpenUSD build is
  at hand), a Storm render of erhe's own view compared by normalized cross
  correlation, the bounds against pxr's, the diagnostics the load logs, a
  screenshot and a verdict. Diagnostics an entry reports by design are
  listed in `doc/usd-wg-assets-expected.json` and do not count against it;
  by-eye verdicts live in `doc/usd-wg-assets-eye.json`. The gap loop of
  `doc/usd_survey_gap_loop.md` walks the entries, fixes a gap or records
  why it is by design, and runs clean on every scope: of the 146 entries,
  138 work, 7 work with a named gap and 1 fails, and every non-working
  entry is the MaterialX item of `doc/plans/usd_compatibility.md`. The
  survey doc is the
  checklist a later fix takes its target from, and it re-runs per fix on
  the affected entries only (`--only`), whole only after every identified
  gap is closed. What the survey drove into the importer and the
  renderers is stated where it holds: `doc/erhe_usd.md` for the
  headlight, `DomeLight` ambient, arc carriers, `class` prototypes,
  composed sublayers, `UsdGeom` primitive schemas, `PointInstancer`
  expansion, the `st` V flip with `UsdTransform2d`, texture channels,
  `UsdPreviewSurface` fallbacks, `.usdz` packed textures, vertex colors as
  an unbound mesh's albedo, `doubleSided`, hole faces, baked sampled
  stacks and the specifier of an undefined prim; `doc/editor_rendering.md`
  for the grid as a blended overlay that writes no depth.
- E2 Material fidelity: the erhe material fields `UsdPreviewSurface` has no
  input for - anisotropic roughness and transmission - travel in an
  `OpenPBRSurface` network beside the `UsdPreviewSurface` one, an inline
  `UsdShade` network Tydra converts in every build, so neither direction
  is conditional (`LIGHTUSD_WITH_USDMTLX`, off in erhe's build, concerns
  only a separate `.mtlx` document, `doc/plans/usd_compatibility.md`
  "MaterialX"). The importer reads the
  OpenPBR network wherever a `Material` prim offers one and names a
  material that offers both in one line saying which was read; the writer
  authors one for exactly the materials that need it - a roughness whose
  components differ, or a transmission that is not zero - through
  `outputs:mtlx:surface`, reading the very texture prims the preview
  surface reads. The pair the OpenPBR parameterization cannot spell
  exactly rides as `erhe:Material:roughness`, applied after the network
  on reload, so the erhe round trip is bit-exact while another reader
  shades from the network; the fields no OpenPBR input carries
  (`reflectance`, the brushed-metal block, `use_aniso_control`) keep their
  `erhe:Material:<name>` custom-attribute path (`doc/erhe_usd.md`
  "OpenPBR networks"; the round-trip script's `open_pbr.usda` leg).

- P1 Physics in USD: the physics of a scene travels as the `UsdPhysics`
  prims and API schemas of the mapping's "Physics" table, and the same
  editor code serves both formats. `erhe::scene::Physics_description`
  (`doc/erhe_scene.md`, "Physics description") is the format-neutral
  record: the glTF reader fills it from `KHR_physics_rigid_bodies` and the
  USD reader from `UsdPhysics` (`Usd_data::physics`, with the stage path,
  the erhe-only property values and the guide collider prims of each
  record beside it in `Usd_data::physics_prims`), and the editor's
  `import_physics()` builds the library items, bodies, triggers and joints
  from it while `build_physics_description()` is the export builder both
  writers consume (`doc/editor_parsers.md`). A body is its prim's
  `PhysicsRigidBodyAPI` with its colliders at or below it, an implicit
  shape a `purpose = guide` primitive-schema child prim (a box of unequal
  extents a unit `Cube` scaled per axis, the prim's own scale applied by
  the fold and never baked into the shape), a mesh shape the `Mesh` prim
  with `PhysicsMeshCollisionAPI`, a physics material a `Material` prim
  with `PhysicsMaterialAPI` and no surface output, a collision filter a
  `PhysicsCollisionGroup` prim, a joint-settings item a typeless prim with
  per-axis `PhysicsLimitAPI` / `PhysicsDriveAPI` instances, a joint a
  `PhysicsJoint` child prim naming its settings prim, a trigger the body's
  own `erhe:Node_physics:is_trigger`, and the physics world's gravity one
  `PhysicsScene` prim; every erhe-only value rides an `erhe:Owner:name`
  custom attribute (`doc/erhe_usd.md`, "Physics" under Import and
  Export). A physics material, a collision filter and a joint-settings
  item take the place of the prim the file authored them on; a joint prim
  and the scene prim are not prims of the tree. `erhe_usd_tests` reads
  `physics.usda` value for value, writes it, reads it back and writes it a
  second time byte for byte, `usdchecker` passes on the written file, and
  the round-trip script's USD physics leg checks a body, a material and a
  joint the way the glTF leg does. A `convexHull` collider whose mesh spans
  no volume gets no shape: `erhe::geometry::make_convex_hull` refuses a
  point set that `erhe::math::classify_affine_span` finds flat, collinear
  or coincident before geogram is reached, with one warning naming the
  reason (`doc/geogram.md` "Degenerate convex hull input (erhe-side
  guard)"). A joint prim's two frames are two nodes, since erhe's six-dof
  joint reads its frames off the node the `Node_joint` sits on and the
  node it names: a non-identity `localPos0` / `localRot0` is an `Xform`
  frame prim `<joint>_frame0` below the first body carrying the joint,
  `localPos1` / `localRot1` likewise `_frame1` below the second as the
  connected node, and the writer names each side's nearest body prim with
  that side's node transform in the body's space, so a file erhe wrote
  reloads to the same tree (the `Node_joint` row of the mapping;
  `physics.usda`'s `Flap`). A hull or triangle collider remembers the
  `Mesh` prim it was built from (`Node_physics::collision_mesh`, empty for
  the body's own mesh), so a collider on a prim below its body
  (`Rock/shell`) is written back on that prim in both formats and the
  fixture's second save is byte for byte the first. A body whose file
  authors a velocity enters the world active
  (`IWorld::add_rigid_body` decides from the body's own velocity;
  `erhe_physics_tests`), so `physics.usda`'s `Crate` moves as authored
  while a body at rest still loads asleep. The kind `Scope` an import's
  first resource of a kind brings into the tree (`Physics Joints`, and
  every other kind, in both formats) is a step of the operation that
  needed it (`Kind_scope_operation`, composed by
  `make_library_insert_operation`), so an undo takes the scope out with
  the resources while a scope another operation has since filled stands
  (`doc/editor_content_library.md`).
- An animation plays through a value layer of its own and an edited clip
  saves as edited (A1). `Animation_sampler::apply` writes the animated
  layer of `doc/property_system.md` D5, so the transform a prim authored
  stays readable under the pose and every serializer writes that base;
  stopping playback drops the layer. A save reconciles each sampled
  `xformOp` stack with the channels driving the prim: the authored samples
  are written when the keys are still their projection, and the keys are
  written as `timeSamples` when they are not, op by op for a stack the
  channels drive and through the composed pose for a baked one
  (`doc/erhe_usd.md`, "Time samples"). `erhe_usd_tests` edits a key
  value, a key time, an added key, a rotation key and a baked stack of the
  time-samples fixture, saves and reloads each, and asserts the unedited
  save is byte-identical and an edit made while the clip plays saves as an
  edit.
- Time samples beyond the transform. An `erhe::scene::Animation_channel`
  names the property it drives (`doc/erhe_scene.md`, "Animation
  playback"), so the per-file animation carries a closed list of
  non-`xformOp` attributes as channels of the same clip: a UsdLux light's
  `inputs:intensity` and `inputs:color`, a `UsdPreviewSurface`'s
  `inputs:diffuseColor`, `inputs:roughness`, `inputs:metallic` and
  `inputs:opacity`, and any prim's `visibility` (the time-sample rows of
  the mapping; `doc/erhe_usd.md`, "Time samples"). The samples are
  read raw off the composed layer's prim spec and a save writes them from
  the clip's keys alone, so the second save of `attribute_samples.usda`
  is byte for byte the first. A `Ts` spline on one of the four scalar
  attributes of that list reads as a cubic sampler (knot slopes per time
  code become erhe's tangents per second; all-held and all-linear knots
  make a STEP or LINEAR sampler) and a cubic channel writes back as a
  hermite spline (`attribute_splines.usda`); a spline anywhere else is one
  warning per prim. `erhe_usd_tests` covers the keys, the pose, the
  playback, the save, the fixed point and an edited key of both fixtures,
  and cross-checks the tangent conversion against LightUSD's own spline
  evaluator.
- A load of a stage holding thousands of prims costs the content it
  brings in, not the square of it. Three things make that so. A queued
  raytrace commit reads the scene's shape-to-meshes index
  (`Scene_root::collect_meshes_sharing_primitives`, maintained at the
  change sites), so finding the sharers of a committed shape costs the
  sharers and not the scene. The hover holds still while a load is in
  flight (`Scene_view::update_hover_with_raytrace()` asks
  `App_context::is_scene_load_in_flight()`, `doc/editor_scene.md`),
  so a load pays for no top level acceleration structure that is rebuilt
  every frame and reused by nothing. And a deferred finalize commit
  collects those sharers only when it swapped a shape
  (`commit_real_raytrace()` and `commit_geometry_buffer_mesh()` report
  whether they did): a shape is committed once however many meshes share
  it, so an asset instanced N times has one commit that changes a shape
  and N-1 that refresh their own mesh alone (`doc/async_asset_loading.md`, "Deferred load
  finalize"). `full_assets/Teapot/DrawModes.usd` settles in 14.6 s and
  `intent-vfx/scenes/simpleAssetScene.usd`, 2000 instanced copies of one
  asset arriving as 6686 prims, in 202 s, measured headless from the
  request to the second consecutive idle `get_async_status`. The per-shape
  BVH build is not part of that cost: it runs on executor workers, and the
  17 ms the tick thread of a `DrawModes.usd` import spends in BVH commits
  is 84 two-triangle draw-mode cards and 2 AABB proxies. What is left is
  the load itself, which runs on the tick thread
  (`doc/plans/usd_compatibility.md`, "Asynchronous load").
  The phases of the import and the open set breadcrumbs of their own, so
  the stall watchdog names the phase a long load is in rather than the
  last breadcrumb the tick happened to pass (`doc/editor_parsers.md`).

Verification of all of the above: `erhe_usd_tests` (358 cases, built in
`build_vs2026_vulkan` since `ERHE_BUILD_TESTS=ON` is passed by the main
configure wrapper), the `usd-roundtrip` section of
`scripts/scene_roundtrip_verify.py` (`doc/scene_serialization.md`,
"Verifying round-trips"), and the survey run of S1.

## 3. Out of scope

- Structural edits inside a reference (adding, removing or reparenting
  a prim under a referencing prim): an instance is the template's
  structure with value overrides, and a prim that is not wanted is
  deactivated (X2). A typeless `def` below a carrier is an override of the
  child of that name, not a structural edit (X2).
- Converting between the formats: a glTF scene saved as USD or a USD
  scene saved as glTF, and prefabs of one format inside a scene of the
  other (G3).
- glTF extensions that carry USD-only features, and USD schemas that
  exist only to carry glTF-only encodings (C1).
- OpenUSD as a build dependency (`doc/erhe_usd.md` "Dependency"
  says how it is used instead: schema reference, validator and the
  survey's reference renderer only).
- Live re-composition of an edited stage (X5 names it as the step after
  provenance).
- `specializes`, payload load policies and the session layer: no erhe
  feature maps onto them; the mapping lists them as having no erhe
  counterpart. A root layer's sublayers are composed at load and a save
  writes one flattened layer (the mapping's `subLayers` row); editing the
  stack layer by layer is `doc/plans/usd_compatibility.md`
  "Layer-stack editing".
- The schema attributes of a `Typed` prim of unsupported type (U1): the
  prim and its place survive a round trip, its attributes do not, until
  a step wants a generic property dictionary on `Typed` (the mapping's
  `Property_set` row is the shape).

## Future work

- [plans/usd_compatibility.md](plans/usd_compatibility.md) - the work that
  is left, ranked, and the items themselves.
