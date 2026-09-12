# USD compatibility plan

The concept and naming mapping every step relies on is
`doc/usd_compatibility.md` (referred to below as "the mapping"); this
document holds the goal, what holds today, the candidate next steps
and their order, what is out of scope and the future work.

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

G1, G2 and G3 hold as section 1 states them, for the schemas the
mapping's tables cover. Each landed step is listed with the record that
now owns its behavior; `git log` on that record has the history.

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
  (`src/erhe/usd/notes.md`); the other resource kinds and a folder are E4.
  `scene_roundtrip_verify.py` covers the placements.
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
  (`doc/gltf_extensions/ERHE_scene.md`); USD carries a style as a `class`
  prim and the chain as its `inherits` (X3).
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
  that (a pivot pair, a sampled matrix op, `[orient, translate]`, two ops
  of a kind) is posed and composed at the union of its sample times and
  baked into translation, rotation and scale channels, exact at every
  sample, with one info line naming the reason; the samples stay on the
  ops either way. `erhe_usd_tests` round-trips a sampled stack to a fixed
  point (`src/erhe/usd/notes.md`, "Time samples").
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
  point (`src/erhe/usd/notes.md` "Export"). A typeless or `Scope` prim
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
  composed layer's prim specs, a typeless `def` below the carrier being
  the same override of the child of that name (a typed `def` adds
  structure and is dropped with a warning, section 5)
  (`src/erhe/usd/notes.md`); glTF carries the
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

- E4 Editor state in a USD file: every resource kind of the content
  library is written and read as a prim where it sits in the tree, one
  `typeName` per kind (the class token `Typed` fixes) with attributes named
  as the glTF fields are, `erhe::usd` recording what the layer authors and
  the editor creating the item, as X3 does for a class prim
  (`doc/scene_serialization.md`, "USD-backed scenes", owns the list of what
  the layer carries and the `customLayerData` keys). A save logs no kind
  it carries and the open side reads every kind it writes. The output is
  `.usda` only (section 6).
  - Brushes: a brush is a `Brush`-typed prim where it sits, its geometry
    the child `Mesh` prim the mesh writer emits with
    `subdivisionScheme = none` (normative on reload, the `ERHE_geometry`
    role in glTF), facet materials as the child's `GeomSubset` bindings,
    its own material as `material:binding` on the brush prim, `density`
    and `normal_style` as `erhe:Brush:` custom attributes and
    `purpose = guide` derived rather than authored; `Usd_data::brushes`
    is the record and the scene conversion skips the brush's subtree
    (`src/erhe/usd/notes.md` "Brush prims"). The round-trip script's
    brushes leg holds; the first save of a brush built in memory differs
    from the second in vertex order, since the mesh reader re-indexes,
    and the file is a fixed point from the first reload on.
  - Texture node graphs: a `Graph_texture` is the `UsdShade` network it
    is - a `NodeGraph` prim where the asset sits, one `Shader` child per
    node with an `erhe:texture:` `info:id`, parameters and pins as
    `inputs:` / `outputs:` attributes, links as attribute connections, and
    a material slot fed by the graph connected to the graph's interface
    output in place of a `UsdUVTexture`. `doc/usd-texture-graphs-plan.md`
    owns the design and the record between `erhe::usd` and the editor
    (`src/erhe/usd/notes.md` "Node graphs"; MCP `get_scene_node_graphs`);
    the round-trip script's `texture_graph.usda` leg holds with a
    byte-identical second save.
  - Geometry node graphs: a `Graph_mesh` reuses the texture-graph prim
    form with `erhe:geometry:` node ids and the evaluated geometry as a
    child `Mesh "result"` written the way a brush writes its geometry;
    `doc/usd-texture-graphs-plan.md` section 4 states the rule, the
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
    (`Content_library::adopt_kind_scopes`, `src/editor/parsers/notes.md`).
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
  `doc/usd-survey-gap-loop.md` walks the entries, fixes a gap or records
  why it is by design, and runs clean on every scope: of the 146 entries,
  138 work, 7 work with a named gap and 1 fails, and every non-working
  entry is the MaterialX item of section 6. The survey doc is the
  checklist a later fix takes its target from, and it re-runs per fix on
  the affected entries only (`--only`), whole only after every identified
  gap is closed. What the survey drove into the importer and the
  renderers is stated where it holds: `src/erhe/usd/notes.md` for the
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
  only a separate `.mtlx` document, section 6). The importer reads the
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
  `erhe:Material:<name>` custom-attribute path (`src/erhe/usd/notes.md`
  "OpenPBR networks"; the round-trip script's `open_pbr.usda` leg).

Verification of all of the above: `erhe_usd_tests` (302 cases, built in
`build_vs2026_vulkan` since `ERHE_BUILD_TESTS=ON` is passed by the main
configure wrapper), the `usd-roundtrip` section of
`scripts/scene_roundtrip_verify.py` (`doc/scene_serialization.md`,
"Verifying round-trips"), and the survey run of S1.

## 3. Candidate next steps

No step of the original plan remains: G1, G2 and G3 hold. What follows is
the review of section 6 and of the future-work lists of
`src/erhe/usd/notes.md` and `doc/usd_compatibility.md`, ranked by what
each buys the editor; every item's substance is the section 6 entry it
names, and nothing here restates one.

1. Physics in USD (section 6 "Physics on load"). The largest editor
   feature a USD-backed scene silently loses today: the importer counts
   the `UsdPhysics` prims it skips and a save writes none, so a physics
   scene cannot live in USD at all. The plain-data carrier the glTF
   physics import already takes makes the reader a filling of that
   carrier and the writer its inverse; it closes the last "not carried"
   line `doc/scene_serialization.md` lists for a USD-backed scene.
2. The animated value layer, then time samples on any attribute and the
   write-back of an edit into the samples (three section 6 items). The
   layer pays for itself without USD (it is the prerequisite of
   `doc/animation-keyframing-plan.md`), and with it a keyed edit made in
   erhe becomes something a USD save can carry instead of writing the
   file's original samples. This is the second editor feature a USD scene
   loses today.
3. Composition the real assets use (section 6 "Composition authored
   inside a variant block", "An xformOp named in a prim's xformOpOrder
   that one of its arcs supplies", "An override path that crosses a
   reference inside an instance"). The usd-wg Teapot model, DrawModes and
   the intent-vfx teapot asset are the surveyed files that stay empty or
   warn per prim; each item names the two-part change that closes it.
4. The LightUSD fork fixes (section 6 "Two LightUSD limits worked around
   downstream", "Relationship targets a weaker sublayer contributes as a
   single path", the `texCoord2f` finding of "Writer findings of
   usdchecker"). Four defects in one dependency, each already diagnosed to
   the function; a fork branch carrying them removes a stripping pass, a
   quoting workaround, 2816 skipped instances and a validator finding.
5. Load performance (section 6 "Load performance"). The scenes holding
   thousands of prims take minutes and trip the stall watchdog; the three
   fixes are named in order and the first, a shape-to-meshes index at the
   change sites, is the one the other scene loaders benefit from too.
6. Load and save on a worker, and `.usdc` / `.usdz` output (section 6
   "Asynchronous load" and "Binary and packaged output"). The load moves
   onto the asset manager's request path once the manager learns a second
   format; the output formats are what LightUSD's writer already offers.
7. The round-trip residue (section 6 "Node-held secondary values",
   "Camera infinite_z_far", the `.usdz` path finding of "Writer findings
   of usdchecker"). Small, each one a value that leaves through a save
   and does not come back.
8. Shading and imaging the survey names (section 6 "A UsdPreviewSurface
   input fed by a UsdPrimvarReader", "A material slot that a texture
   graph feeds AND that carries an authored factor", "Image formats",
   "An environment map from a DomeLight texture", "MaterialX"). The
   PrimvarReader case and the slot factor are importer work; the rest
   need a renderer or decoder erhe does not have, MaterialX documents a
   LightUSD option erhe's build leaves off.
9. Platform coverage (section 6 "macOS and Linux wrappers"): the option
   is on for Windows and Android only.
10. Composition beyond what erhe resolves (section 6 "Layer-stack
    editing", "inherits and specializes arcs whose target is not a class
    prim", "Variant opinions a variant set does not carry", "Overrides on
    applied API schemas inside an instance"). Each is a real USD feature
    with no surveyed asset that visibly depends on it, so they wait for a
    file that does.

### P1 Physics in USD (M; in progress)

What: the section 6 item "Physics on load", worked as the mapping's
"Physics" table states it. The plain-data physics description the glTF
reader fills and the editor's physics import consumes is made a
format-neutral record of `erhe::scene` so the USD reader fills the same
one and the editor's physics import and export take it from either
format; `erhe::usd` reads and writes the `UsdPhysics` prims and API
schemas as that record, and the editor's open, import and save paths
carry it. The commits, in order:

1. `erhe::scene` physics description: the classes of
   `src/erhe/gltf/erhe_gltf/gltf_physics.hpp` move to
   `src/erhe/scene/erhe_scene/physics_description.hpp` as
   `erhe::scene::Physics_description` and its parts, every user renamed,
   no behavior change (`src/erhe/scene/notes.md`, `src/erhe/gltf/notes.md`).
2. `erhe::usd` reader: `Usd_data::physics` filled from the `UsdPhysics`
   schemas per the mapping, with a fixture and `erhe_usd_tests` cases
   (`src/erhe/usd/notes.md` "Physics").
3. `erhe::usd` writer: `Usd_save_arguments::physics` written per the
   mapping, `erhe_usd_tests` round-trips a body with each shape kind, a
   material, a filter, a joint with limits and drives, and the scene prim
   to a fixed point; `usdchecker` passes when available.
4. Editor: `open_scene_usd` and `import_usd` build the physics items
   through the import the glTF path uses, now taking the neutral record;
   `save_scene_usd` fills the record through the builder the glTF save
   uses; the physics warning of the importer goes; the round-trip
   script's USD leg checks a body, a material and a joint the way the
   glTF leg does; `doc/scene_serialization.md` no longer lists physics as
   uncarried; `src/erhe/usd/notes.md` "Future work" and the memory bank
   follow.

Verification: `erhe_usd_tests` after commits 2 and 3; after commit 4 a
headless session opens a USD scene, adds a body, a material and a joint
over MCP (`create_physics_material`, `edit_physics_body`,
`create_physics_joint`), saves, reopens and reads them back with
`get_physics_items`, closes clean, and `scripts/scene_roundtrip_verify.py`
stays green.

## 4. Order

Items 1 and 2 of section 3 are independent of each other and of the rest,
and each restores an editor feature to USD-backed scenes; take them first,
in either order, through the harness of `doc/agent-orchestration-harness.md`
one commit at a time (C2). Item 4 goes with a fork tag bump and is best
taken when a fork clone is at hand (`memory-bank/local/context.md`
records it). The remaining items have no ordering constraint among them.

## 5. Out of scope

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
- OpenUSD as a build dependency (`src/erhe/usd/notes.md` "Dependency"
  says how it is used instead: schema reference, validator and the
  survey's reference renderer only).
- Live re-composition of an edited stage (X5 names it as the step after
  provenance).
- `specializes`, payload load policies and the session layer: no erhe
  feature maps onto them; the mapping lists them as having no erhe
  counterpart. A root layer's sublayers are composed at load and a save
  writes one flattened layer (the mapping's `subLayers` row); editing the
  stack layer by layer is section 6.
- The schema attributes of a `Typed` prim of unsupported type (U1): the
  prim and its place survive a round trip, its attributes do not, until
  a step wants a generic property dictionary on `Typed` (the mapping's
  `Property_set` row is the shape).

## 6. Future work

Each item is independent of the others except where named; section 3
ranks them. A USD scene loads, edits and saves without any of them.

- Physics on load: `UsdPhysics` API schemas become `Node_physics`,
  `Node_joint`, `Physics_material` and `Collision_filter` per the
  mapping's physics table, by filling
  `erhe::scene::Physics_description` from USD (the physics import
  operations already take that plain-data carrier); today the importer counts the prims carrying such
  schemas in one warning and reads none. The matching save applies the
  API schemas per the same table; erhe-only physics properties (damping,
  wind receptivity, gravity factor, combine modes) ride `erhe:` custom
  attributes under C1.
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
  channels are K1, which leaves time samples on any other attribute, and
  `Ts` splines re-encoded as cubic samplers.
- Reconciling an edit with the authored ops: the samples an op carries are
  what the file authored, so keying an animation or moving an animated
  prim changes the playable channels and the composed pose but not the
  ops a save writes, and an edited clip saves as the file's original
  samples. Writing an edit back into the samples is what the animated
  value layer above makes well defined.
- Composition authored inside a variant block: a reference or payload arc
  authored on a variant (usd-wg `full_assets/Teapot/Teapot_Geometry.usd`
  prepends the reference to `UtahTeapot.usd` on its `Utah` variant), and a
  prim defined inside a variant block over a referencing prim (the
  `Materials` scope of `Teapot_Materials.usd`). The hoist carries a
  variant's `def` children and its property opinions, not the arcs they
  author, and the section 5 reference-structure rule drops a def a variant
  authors over a reference, so the whole model stays empty:
  `full_assets/Teapot/Teapot.usd` imports 0 of its 1 composed mesh and 0 of
  its 2 materials, and `full_assets/Teapot/DrawModes.usd`, which references
  it once per draw mode, 0 of 35 and 0 of 70. Closing it is two changes: the
  variant hoisting carrying a hoisted prim's reference and payload list-ops
  into the arc loop that already runs for a prim's own arcs, and the
  reference-structure rule admitting the defs a variant authors, which are
  the variant's own content rather than an edit made over someone else's
  structure.
- An `xformOp` named in a prim's `xformOpOrder` that one of its arcs
  supplies: `full_assets/Teapot/DrawModes.usd` gives each duplicate
  (`/World/FancyTeapot_1` and its siblings) the order
  `["xformOp:transform", "xformOp:transform:duplicate1"]` while authoring
  only the second, because the first arrives through its internal reference
  to `/World/FancyTeapot_0`. The stack is reconstructed from the prim's own
  properties, so the missing op makes the whole stack unreadable and the
  prim keeps the composed transform instead, with one warning per prim.
  Closing it means resolving an op the order names against the prim's arc
  targets, in the same place the stack is reconstructed.
- An override path that crosses a reference inside an instance: a carrier's
  overrides are recorded as the paths USD composes them at, and X1 gives the
  clone of an arc's target one level more than USD, which
  `erhe::scene::find_instance_item` skips for the carrier's own clone and not
  for a nested carrier deeper down. The intent-vfx teapot asset is the shape:
  `assets/teapot/mtl.usd` authors `over "geo" { over "default" { over "Body" }
  }` on the prim that references `geo.usd`, whose `default` scope references
  `geo/UtahTeapot.usd` in turn, so the item the override names sits at
  `teapot/geo/default/UtahTeapot/Body` and the lookup of `geo/default/Body`
  misses it with a warning per carrier. Closing it means resolving an override
  path segment by segment and looking through a `Prefab_instance` carrier's
  clone at each step rather than only at the first.
- Two LightUSD limits worked around downstream (`src/erhe/usd/notes.md`,
  "Node graphs"): Tydra fails a material whose input connects to a
  `NodeGraph`, so `load_stage` strips that wiring from the copy Tydra sees;
  the USDA parser does not round-trip an escaped double quote, so nested
  parameter text travels with single quotes. Both go with a fork fix.
- Relationship targets a weaker sublayer contributes as a single path:
  LightUSD's `CombinePrimSpecRec` (`src/composition.cc`) merges two layers'
  `prepend` / `append` relationship opinions only when both are stored as a
  path vector, and a single-target `prepend rel foo = </path>` is stored as a
  path, so the weaker layer's target is dropped and the stronger layer's list
  stands alone. The intent-vfx `scenes/teapotScene.usd` composes each
  instancer's `prototypes` from two of its sublayers -
  `teapotScene_layoutOverrides.usd` prepends the three coloured prototypes and
  `teapotScene_layout.usd` prepends the single plain one - so pxr reads four
  targets where erhe reads three, and every instance whose `protoIndices`
  entry names the fourth is skipped, 2816 of them over the file's 23
  instancers. The same merge puts the weaker layer's prepended targets before
  the stronger's where USD puts the stronger's first, which would misname a
  prototype even once the single-path opinion is admitted. Both go with a fork
  fix, next to the two LightUSD limits above.
- Writer findings of `usdchecker` (`src/erhe/usd/notes.md`, "Future work"):
  the `texCoord2f` typing of `UsdUVTexture` `inputs:st` (LightUSD's own
  member type, a fork fix), and a texture packed in a `.usdz` written as a
  path that names no file (the packed bytes extracted next to the file, or
  the `archive.usdz[entry]` form).
- Load performance of a scene holding thousands of prims (the intent-vfx
  scenes, several minutes with the stall watchdog firing - the teapot ones,
  and `simpleAssetScene.usd`, whose 2000 instanced copies of one asset
  arrive as 9862 prims and take 92 s to settle - and the
  usd-wg `Vehicles/USD_Mini_Car_Kit` vehicle and wheel variant sets, where
  hoisting every variant turns a 91-prim, 6-mesh composed stage into 2373
  prims and 146 meshes and the watchdog reports the tick stuck in
  `raytrace: BVH commit`): each queued
  raytrace commit scans every mesh of every layer
  (`collect_meshes_sharing_primitives`, O(N) per commit, N commits per
  load), hover traces the linear path every frame while the TLAS cannot
  settle, and `finalize_imported_meshes` builds the per-shape BVHs serially
  on the tick thread. A shape-to-meshes index maintained at the change
  sites, a hover that does not trace while a load is in flight, and the
  proxy build on the deferred path are the fixes, in that order.
- Asynchronous load: `load_usd` runs on the calling thread and the editor's
  import and open are synchronous, where a glTF import goes through the
  asset manager's `Asset_load_request` and the droppable-payload import
  operation (`doc/reloadable-asset-loads.md`). The conversion creates no
  GPU object, so it moves onto a worker once the asset manager learns a
  second format.
- Binary and packaged output: the writer emits `.usda` only; a scene opened
  from `.usdc` or `.usdz` saves back as `.usda` beside it. LightUSD writes
  both formats; the `.usdz` case also needs the packed-texture answer of the
  `usdchecker` item above.
- Node-held secondary values: a node-held value of another class (D30,
  `Light.color` on a plain `Xform`) is written as `erhe:Light:color`, and
  the import resolves neither the qualified nor the bare name against a
  node, so such a value does not come back (`src/erhe/usd/notes.md`,
  "Future work").
- Camera `infinite_z_far`: no USD form; the finite `clippingRange` is
  written and one warning says so.
- A material slot that a texture graph feeds AND that carries an authored
  factor: the connection replaces the value in both terminals (a
  `UsdPreviewSurface` or OpenPBR input is either connected or valued), so
  the factor of such a slot is not written and reads back as the default.
  Closing it means carrying the factor as the graph connection's
  `inputs:scale` the way a `UsdUVTexture` carries erhe's factor, which
  needs the graph's interface output to pass through a multiplying node.
- A `UsdPreviewSurface` input fed by a `UsdPrimvarReader`: Tydra accepts
  only a `UsdUVTexture` output on a shader input, so a network that reads a
  primvar into one - usd-wg
  `full_assets/SubdivisionSurfaces/Creases_SpinningPyramids.usda` connects
  `inputs:diffuseColor` to a `UsdPrimvarReader_float3` reading
  `displayColor` - fails the whole material, and the file's meshes arrive
  with no material at all (0 of 3 there). Closing it is either a fork change
  in Tydra or erhe reading the network from the composed layer itself and
  mapping the named primvar onto the value the input would take, which for
  `displayColor` is the vertex colors erhe already carries.
- Image formats: Radiance `.hdr` and OpenEXR `.exr` need decoders erhe
  does not build (`stb_image.h` sits in the CPM cache of fpng and LightUSD,
  and nothing in the tree reads `.exr`); the StandardShaderBall scene's six
  neutral `.exr` maps are the surveyed assets that ask for the second.
- An environment map from a `DomeLight` texture: erhe has no environment
  map, so a dome's `inputs:texture:file` is named in one warning and not
  sampled, and the dome contributes the constant radiance of its `color`,
  `intensity` and `exposure` only (`src/erhe/usd/notes.md`, DomeLight). The
  usd-wg McUsd entries are the surveyed assets that author one. Taking it
  up means an image-based ambient term in the renderer first; the reader
  already keeps the dome prim and its texture path.
- MaterialX: a `.mtlx` document as a reference target (the editor refuses
  the arc; the usd-wg chess set, MaterialXTest and the MaterialX color-space
  tests are the surveyed assets), and the LightUSD usda reader's rejection
  of `colorSpace` metadata on a shader attribute (the survey's one failing
  entry). The `.mtlx` reader is behind `LIGHTUSD_WITH_USDMTLX`, off in
  erhe's build. An inline `ND_standard_surface_surfaceshader` or
  `ND_open_pbr_surface_surfaceshader` network needs none of that and is
  read and written (E2).
- Grid depth: the grid's depth does not agree with the content's, so grid
  lines cross opaque objects below the horizon (`doc/editor_rendering.md`,
  Grid). Needs a RenderDoc session on the windowed build.
- `inherits` and `specializes` arcs whose target is not a `class` prim
  (usd-wg inherit_and_specialize.usda inherits from a `def Cube`): X3 makes
  a style only of a class prim, so such an arc composes nothing and is
  warned about; the surveyed file overrides every inherited opinion locally,
  so nothing visible depends on it there.
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
- Layer-stack editing: a root layer's sublayers are composed at load and a
  save writes one flattened layer with no `subLayers` (the mapping's
  `subLayers` row; `src/erhe/usd/notes.md` "Sublayers"), so an edit cannot
  be written back to the layer that authored the value. A stack the
  editor edits layer by layer needs per-value layer provenance, which
  `CompositeSublayers` does not keep (X5).
- macOS and Linux wrappers: `configure_xcode_*.sh` and
  `configure_ninja_linux_*.sh` leave `ERHE_USD_LIBRARY` at `none`; turning
  it on there is the step that first needs USD on those platforms
  (`src/erhe/usd/notes.md` "Configurations").
