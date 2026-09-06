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
  lists onto it and back. The U steps of section 3 bring the model to
  this shape; the transform stays erhe's single TRS until M8 takes up
  USD's xformOp stacks.

## 2. What holds today

G1 holds for the schemas the importer covers, and G2 holds for scene
content (editor state beyond the scene block is E4). Each landed step is
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
  `create_node` takes `prim_type`. The Create menu has no `Scope` entry
  yet, object-reference candidates and `Layout` do not reach through a
  `Scope`, and a material `Scope` stays namespace until U4.
- U2 Mesh is a Gprim: `erhe::scene::Mesh` is `erhe::Item<Item_base,
  Gprim, Mesh>`, a child prim with its own transform; a parent holds any
  number of `Mesh` children; `get_mesh()`, `for_each_mesh_child()` and
  `set_mesh_parent()` replace the attachment accessors and `get_node()`
  returns the mesh itself until the U steps retire it
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

## 3. Remaining steps

Steps are grouped by what they touch: U = object model (C5, no USD
code), M = model generalization (no USD code), E = export, X =
composition; animation and physics are section 6, future work outside
every stage. Sizes are relative: S = an afternoon, M = a few days, L = a
week or more.

### M6 Value types USD needs (S each, as needed)

What: add `Property_type` alternatives when a step needs them (M8 is the
first, for `double` and `glm::mat4`): `double` (USD `double` transforms and time codes), `glm::mat4`
(xformOp matrices), an asset path (texture `inputs:file` today is an
object reference to a loaded texture; the path is the USD form), and
homogeneous arrays (`float[]`, `int[]`) for primvars that a node or
material might want to carry as a property. Each comes with its
`to_string` / `from_string` pair (D16) and Properties window row.

Why: listed so that a later step does not invent an ad hoc carrier.
Nothing is added ahead of a demonstrated need.

### M7 Style chains (S)

What: allow a `Style` item to have a style itself (`Item_base::style`
already exists on every item; the D25 lookup walks the chain, with a
cycle check at assignment).

Why: USD `class` prims inherit from other classes; an imported class
hierarchy maps onto style chains without flattening.

Verification: headless script: style B uses style A, an item uses B,
values of A reach the item; assigning A to B's style is refused.

### M8 xformOp stacks (M)

What: `Xformable` holds an authored USD xformOp stack (ordered
translate / rotate / scale / transform ops with their suffixes and
inversion flags) next to the single TRS it composes to, so an imported
stack round-trips as authored instead of collapsing to one
`xformOp:transform` matrix, and the transform tools edit the op the
stack designates. Until M8, the importer composes the stack to erhe's
TRS and the exporter writes one `xformOp:transform`.

Why: `UsdGeomXformable` transforms are more general than erhe's
`Trs_transform`; C5 names this as the deferred half of the transform
model.

Verification: `erhe_usd_tests` round-trips a prim with a three-op stack
byte for byte; a moved prim's edit lands in the designated op.

### E4 Editor state in a USD file (M; completes G2)

What: the editor state a USD-backed scene does not carry yet
(`doc/scene_serialization.md`, "USD-backed scenes", owns the list and
the `customLayerData` keys already in use) rides USD's own means (C1).
The resources are prims (U4), so brushes, styles, physics materials,
collision filters, joint settings and the geometry and texture node
graphs are written and read as prims where they sit in the tree, one
custom `typeName` per kind (the class token `Typed` already fixes) with
attributes named as the glTF fields are (a node graph as a JSON string
attribute until a prim form is wanted), and an empty folder `Scope` is
written as the `Scope` it is; animations, skins, prefab references and
the physics API schemas on nodes (section 6) stay listed as not
carried. A save no longer logs a kind it carries; the open side reads
every kind it writes. `.usdc` output follows once the `.usda` output
round-trips through E3 with all of it.

Verification: the E3 leg extended with a scene that holds one of each
kind (build it over MCP the way the glTF sections build theirs); a
fresh-session reload shows the same scopes, styles and brushes;
`scene-close leak` clean.

### E2 Material fidelity (M)

What: erhe-only material fields that `UsdPreviewSurface` cannot carry
(anisotropic roughness, transmission, brushed metal) export additionally
as an `OpenPBRSurface` / MaterialX network when
`LIGHTUSD_WITH_USDMTLX` is on; import prefers the OpenPBR network when
both are present.

### X1 References as prefab instances (M)

What: an imported stage's `references` arcs that target a whole file
become `Prefab_instance` carriers pointing at that file (imported through
`erhe::usd` into the prefab library, which today parses only glTF), so
the erhe scene keeps the instance structure instead of a flattened copy.
LightUSD's `ArcOrigin` tagging says which prims came from which arc.

### X2 Editable instances with sparse overrides (L, after X1)

What: the prefab plan's per-instance override model (`doc/gltf-prefabs-plan.md`
lists it as out of scope for the sealed-instance phase): unseal
instance subtrees for property edits; a local value on an item inside
an instance is an override, stored by (path inside the instance,
property, value) and re-applied after the instance is re-cloned on
reload. In a glTF-backed scene the list rides the carrier node in an
`ERHE_*` extension; in a USD-backed scene each is an `over` prim on the
referencing prim, the file's native form (C1). This step retires the
last remaining reason the old `ERHE_overrides` design existed: the
property system's local layer is the override, and M1 paths are the
addressing.

### X3 Class inheritance (S, after M7)

What: `class` prims with `inherits` arcs import as `Style` items with
style chains (M7) instead of being flattened, and styles export as
`class` prims with `inherits`.

### X4 Variants (L)

What: first slice: material-binding variant sets import as a per-scene
variant selection the user can switch (re-import of the affected prims
under the new selection through LightUSD); a `KHR_materials_variants`
adoption on the glTF side keeps the selection across an erhe save. Node
subtree variants follow the same path later.

### X5 Composition provenance in the Properties window (M)

What: an imported stage keeps its LightUSD `Layer` alive in `erhe::usd`;
a property row shows, next to the erhe `Value_source`, the USD arc and
layer the value came from (LightUSD `ArcOrigin`, or the `pcp` DAG engine
when full provenance is wanted). This is the "composition in the editor"
feature in its read-only form; live re-composition after an edit is the
step after it and is not planned here.

## 4. Order

Each step independently landable, in this order:

1. M6 value types M8 needs (`double`, `glm::mat4`), then M7 style
   chains, then M8 xformOp stacks
2. E4 editor state in a USD file (completes G2)
3. X1 references as prefab instances, then X2 editable instances (G3)

The rest of M6 (asset paths, arrays) lands when the step that needs it
is next (an importer hitting a missing type). E2 and X3 to X5 have no
fixed place: each waits for its dependencies and is taken when wanted.

Dependencies: M8 needs M6's `double` and `glm::mat4`; X2 needs X1; X3
needs M7; M7, E4, X1, E2, X4 and X5 need nothing that has not landed.

## 5. Out of scope

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

Animation and physics are outside G1, G2 and G3: a USD scene loads,
edits and saves without them until the items below are taken up, and E1
writes neither time samples nor `UsdPhysics` schemas. Each item is
independent of the others and of every step in section 3 except where
named.

- Animated value layer: the property-system section 6 item, an animated
  value between coerced and local in R3, set by `Animation_sampler::apply`
  and cleared when playback stops, so playback never overwrites the
  authored local value. USD resolves time samples above `default`;
  importing a time-sampled attribute without this layer would clobber
  the authored pose, and saving would write the playback pose as
  `default`. It is also the prerequisite of the keyframing plan
  (`doc/animation-keyframing-plan.md`) and of animation channels on
  arbitrary properties, so it pays for itself without USD.
- Time samples on load (after the animated value layer):
  time-sampled `xformOp:*` attributes become erhe animation channels
  (linear samples; `Ts` splines re-encoded to cubic samplers);
  `UsdSkel` `SkelAnimation` goes through the existing skin path. The
  matching save writes the channels back as time samples.
- Physics on load: `UsdPhysics` API schemas become
  `Node_physics`, `Node_joint`, `Physics_material` and `Collision_filter`
  per the mapping's physics table, through a USD-filled sibling of
  `Gltf_physics_data` (the physics import operations already take a
  plain-data carrier). The matching save applies the API schemas per the
  same table; erhe-only physics properties (damping, wind receptivity,
  gravity factor, combine modes) ride `erhe:` custom attributes under C1.
