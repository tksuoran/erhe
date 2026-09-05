# USD compatibility plan

Status: M1, M2, M3, L1 and I1 have landed; M4 is the next step. The concept and naming mapping every step
relies on is `doc/usd_compatibility.md` (referred to below as "the mapping");
this document holds the steps, their order and their verification.

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
  once, as step Q1, after every other step has landed.

## 2. Step catalogue

Steps are grouped by what they touch: M = model generalization (no USD
code), L = library integration, I = import, E = export, X = composition;
animation and physics are section 5, future work outside every stage.
Within a group the order is the recommended one; across groups the
dependencies are in section 3. Sizes are relative: S = an afternoon,
M = a few days, L = a week or more.

### M1 Item paths (S)

What: give every item a namespace path: `Hierarchy` gets
`get_path()` (slash-separated names from the scene root, the folder
path form `ERHE_scene` `library_folders` already uses for library
nodes) and `find_by_path(root, path)`; `Item_base::get_reference_path()`
returns the path when the item is in a hierarchy and the name
otherwise; `resolve_expression_object` accepts a path. MCP
`get_node_details` / `select_items` / `get_item_properties` accept a
path where they accept an id or a name today.

Why first: every later step addresses items (an `over` targets a path,
a reference maps a source path to a destination path, a relationship
targets a path). It also fixes a standing gap: D28 object references and
D22 expressions resolve by name, so two items with one name are
ambiguous.

Verification: `erhe_item_tests` for path build and lookup, including a
renamed and a reparented item; MCP `get_node_details` by path on the
default scene.

### M2 Sibling-unique names (S)

What: the editor keeps sibling names unique. Node creation, paste,
duplicate, glTF import and prefab instantiation give a colliding name a
numeric suffix (`Cube`, `Cube_1`); the Properties window name row and
the MCP rename refuse a collision with a log line. A name is a valid USD
identifier or the exporter sanitizes it (E1) - the editor does not
restrict what the user types.

Why: USD prim names are sibling-unique identifiers; with M1 this makes
an erhe path a valid, unambiguous prim path.

Verification: `erhe_item_tests` for the suffix rule; import a glTF
whose nodes share a name (Sponza has several) and check `get_scene_nodes`
shows suffixed names; undo restores the original names.

### M3 Visibility and purpose vocabulary (S)

What: register two `Item_base` properties that mirror the flag bits
the way `visible` does today: `visibility` is already `visible` (no
change); add `purpose` as an enumeration `default | render | proxy |
guide`, `inherits`, whose `guide` value is derived from the editor-only
flags (`tool`, `brush`, `controller`, `rendertarget`, `show_in_ui` off)
at registration time through a computed default, and whose `proxy` value
is for a future proxy mesh. Draw-list partitioning reads `purpose` where
it reads those flags.

Why: it gives erhe the two inherited tokens USD uses for the same
decisions and moves several flags behind one enumeration, which is the
form both a USD importer and the Properties window want.

Verification: `get_item_properties` on a tool node reports
`purpose = guide`; a viewport screenshot is unchanged before and after.

### M4 Local values are the authored set (S)

What: make "has a local value" mean "authored" for every item type in
the in-memory model, independent of any file format: an importer sets a
local value only for a field the file authored, and a field left at its
default stays default. For glTF this is the default-elision pass of
`doc/gltf-properties-extension-plan.md` (its `native_gltf` flag and the
one generic post-import pass; whether the rest of that plan - the
`ERHE_*_properties` extensions - is implemented is a glTF-side decision
under C1, not a prerequisite of anything here). The USD importer (I2)
applies the same rule with USD's own authored / fallback distinction.

Why: a USD layer distinguishes an authored opinion from a schema
fallback, and the property system already has the same two states
(`Value_source::local` / `default`). Keeping them honest in memory is
what lets a loaded USD scene show only the values its author set, and
lets G2 write local values and nothing else. Materials are the case that
matters most: today a glTF round trip bakes their effective values into
local ones.

Verification: import a glTF and (after I1) a USD file with a material at
defaults; `get_item_properties` reports `default` for every untouched
field.

### M6 Value types USD needs (S each, as needed)

What: add `Property_type` alternatives only when an import or export step
hits them: `double` (USD `double` transforms and time codes), `glm::mat4`
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

### L1 LightUSD as an optional CPM dependency (M)

What: `ERHE_USD_LIBRARY` CMake option, `lightusd | none`, default `none`.
`lightusd` adds LightUSD through `CPMAddPackage` (its own CMake supports
`add_subdirectory`; link `lightusd::lightusd_static`), with Tydra on and
every optional module off: no bundled image loaders, no audio, no
MaterialX (until E2 wants it), `LIGHTUSD_WITH_GEOGRAM` off (erhe has its
own geogram; the LightUSD option bundles a second copy of geogram
sources), no MCP server, no tools, tests or examples. A new library
`erhe::usd` (`src/erhe/usd/`, with `notes.md`) is the only erhe code that
includes LightUSD headers; it exposes erhe types only. Its first content
is `load_stage(path) -> Stage handle` and `describe_stage()` (prim count,
schema types, layer list) plus an MCP query `describe_usd_file(path)`.

Why: proves the dependency builds everywhere before anything depends on
it. LightUSD is C++17, exception-free, dependency-free, Apache 2.0,
already builds for Windows MSVC / clang-cl, macOS, Linux, Android NDK,
iOS and Emscripten, and its composition engine is the reason the end
goal is reachable at all. OpenUSD stays out: it is the schema reference
(`<OpenUSD>/pxr/usd/*/schema.usda`) and, once built on a desktop
machine, the validator (`usdchecker`, `usdcat`), never a build
dependency (`doc/gltf-scene-roundtrip-plan.md`, "Alternative
considered", records why).

Traps to check during the step: LightUSD bundles its own copies of
libraries erhe also has (meshoptimizer, fpng, miniz, libjpeg-turbo,
nlohmann json); a static link of both must not produce duplicate
symbols, so the step verifies the link on every configuration and
prefers LightUSD options that drop the duplicate before any rename or
fork. erhe compiles with `/W4 /WX` and LightUSD does not; wrap its
targets in the same warning suppression the other CPM dependencies get.

Verification: the Windows configure scripts build with the option on and
off; `describe_usd_file` on a file from `<LightUSD>/models/` returns its
prim list. The Quest build is Q1.

### I1 Import a USD file as an asset (L)

What: `Import USD` next to `Import glTF` (File menu, drag-drop by
extension, MCP `import_usd`), through `erhe::usd`: LightUSD Tydra
`RenderScene` for meshes (positions, normals, texcoords, colours,
subsets), materials (`UsdPreviewSurface` per the mapping), nodes and
transforms, cameras and lights. Time samples are read at the stage's
default time code and `UsdPhysics` schemas are skipped with a log line
(section 5). The import goes through the same
`Item_insert_remove_operation` path as glTF import so it is undoable and
announces removals on undo (`doc/import-undo-reference-clearing.md`).
Polygon meshes with `subdivisionScheme = none` arrive as geometry-normative
erhe geometry (facet counts and indices straight into geogram, primvars
into attributes per the element table); everything else arrives as a
triangle soup, as glTF import does. Composition is LightUSD's business:
the stage is composed (references, inherits, variants resolved with
their default selections) before Tydra sees it.

Why: the first user-visible payoff, and the largest one for the least
model change: LightUSD does the parsing and composition, erhe does what
glTF import already does. Test content is plentiful (`<LightUSD>/models`,
Kitchen_set, ALab).

Verification: import Kitchen_set headlessly, screenshot; import, undo,
`scene-close leak` grep clean; `undo_reference_clearing_smoke_test.py`
extended with a USD import.

### I2 Authored opinions become local values (S, after M4 and I1)

What: an attribute the composed stage has an authored opinion for
becomes a local value; an attribute at its fallback stays default.
Namespaced custom attributes `erhe:<Owner>.<name>` become property
values by their qualified name (D30 form). `visibility` and `purpose`
land on `visible` and `purpose` (M3).

Why: makes an imported stage look, in the Properties window, exactly as
an erhe author would have made it: only authored values carry the local
marker. Without M4 there is no such distinction to preserve.

### E1 Save as USDA (M, after M1, M2, I1; the G2 step)

What: a scene loaded from a USD file saves back to it (File > Save
Scene follows the scene's source path and format; MCP `save_scene`),
written as one `.usda` layer through `erhe::usd` and LightUSD's
`SaveAsUSDA`: stage constants, `Xform` / `Mesh` / `GeomSubset` /
`Material` / `Camera` / `UsdLux` prims per the mapping, local property
values only, erhe-only properties as `erhe:` custom attributes, tags as
collections, editor state (`Scene_root` settings, brushes, node graphs,
library folders, styles) as `customLayerData` and custom prims mirroring
what `ERHE_scene` and the asset-root extensions hold in glTF (C1). The
first version flattens what it loaded; keeping the source file's
composition structure on save is X1 and X2. `.usdc` follows once the
`.usda` output validates; `.usdz` last. Whether a glTF-loaded scene may
also save as USD is left to whoever wants it: nothing in the plan needs
it (G3).

Why: `.usda` is diffable, so a round trip is verifiable by eye and by
text diff, and the OpenUSD tools validate it independently of LightUSD.

Verification: `usdchecker` from an OpenUSD build passes; `usdview` or
LightUSD `lusdview` renders the file; import the export (I1) and diff
against the scene before the save with the same MCP diff
`scene_roundtrip_verify.py` uses for glTF.

### E2 Material fidelity (M, after E1)

What: erhe-only material fields that `UsdPreviewSurface` cannot carry
(anisotropic roughness, transmission, brushed metal) export additionally
as an `OpenPBRSurface` / MaterialX network when
`LIGHTUSD_WITH_USDMTLX` is on; import prefers the OpenPBR network when
both are present.

### E3 Round-trip script (S, after E1)

What: extend `scripts/scene_roundtrip_verify.py` with a USD leg that
mirrors the glTF leg on USD content: load a USD file, save it as USDA,
reload the save into a fresh scene, MCP-diff the two scenes, and run
`usdchecker` when an OpenUSD build is available. The two legs share the
diff code and nothing else (G3).

### X1 References as prefab instances (M, after I1, M1)

What: an imported stage's `references` arcs that target a whole file
become `Prefab_instance` carriers pointing at that file (imported through
`erhe::usd` into the prefab library, which today parses only glTF), so
the erhe scene keeps the instance structure instead of a flattened copy.
LightUSD's `ArcOrigin` tagging says which prims came from which arc.

### X2 Editable instances with sparse overrides (L, after X1, M1, M4)

What: the prefab plan's per-instance override model (`doc/gltf-prefabs-plan.md`
lists it as out of scope for the sealed-instance phase): unseal
instance subtrees for property edits; a local value on an item inside
an instance is an override, stored by (path inside the instance,
property, value) and re-applied after the instance is re-cloned on
reload. In a glTF-backed scene the list rides the carrier node in an
`ERHE_*` extension; in a USD-backed scene each is an `over` prim on the
referencing prim, the file's native form (C1). This step retires the last remaining reason the
old `ERHE_overrides` design existed: the property system's local layer
is the override, and M1 paths are the addressing.

### X3 Class inheritance (S, after I1, M7)

What: `class` prims with `inherits` arcs import as `Style` items with
style chains (M7) instead of being flattened, and styles export as
`class` prims with `inherits`.

### X4 Variants (L, after I1)

What: first slice: material-binding variant sets import as a per-scene
variant selection the user can switch (re-import of the affected prims
under the new selection through LightUSD); a `KHR_materials_variants`
adoption on the glTF side keeps the selection across an erhe save. Node
subtree variants follow the same path later.

### X5 Composition provenance in the Properties window (M, after I1)

What: an imported stage keeps its LightUSD `Layer` alive in `erhe::usd`;
a property row shows, next to the erhe `Value_source`, the USD arc and
layer the value came from (LightUSD `ArcOrigin`, or the `pcp` DAG engine
when full provenance is wanted). This is the "composition in the editor"
feature in its read-only form; live re-composition after an edit is the
step after it and is not planned here.

## 3. Order

Recommended first sequence, each step independently landable:

1. M1 item paths
2. M2 sibling-unique names
3. M3 visibility and purpose
4. L1 LightUSD optional dependency
5. I1 import a USD file as an asset
6. M4 local values are the authored set
7. I2 authored opinions become local values

Steps 1 to 7 reach G1 for the schemas I1 covers.

8. E1 save as USDA (G2)
9. E3 round-trip script
10. Q1 Quest build and launch (S): the Android build passes
    `ERHE_USD_LIBRARY=lightusd`, Debug APK size and link time are measured
    against the option off, and the editor launches on the headset and
    answers `describe_usd_file` over the forwarded MCP port
    (`erhe-quest-launch`). Q1 is the last step: every other step is
    verified on desktop Windows only until then.

M6 and M7 land when the step that needs them is next (any importer
hitting a missing type, X3). Everything in X waits for I1 and E1 to have
shown the mapping holds on real content; X1 and X2 are what turns E1's
flattened save into one that keeps the source file's structure (G3).

Dependencies: I1 needs L1; I2 needs M4 and I1; E1 needs M1, M2 and I1; E2 and E3 need E1; X1 needs I1 and M1;
X2 needs X1, M1 and M4; X3 needs I1 and M7; X4 and X5 need I1.

## 4. Out of scope

- Converting between the formats: a glTF scene saved as USD or a USD
  scene saved as glTF, and prefabs of one format inside a scene of the
  other (G3).
- glTF extensions that carry USD-only features, and USD schemas that
  exist only to carry glTF-only encodings (C1).
- OpenUSD as a build dependency (L1 says how it is used instead).
- Live re-composition of an edited stage (X5 names it as the step after
  provenance).
- `specializes`, payload load policies, sublayer stacks and the session
  layer: no erhe feature maps onto them yet; the mapping lists them as
  having no erhe counterpart.

## 5. Future work

Animation and physics are outside G1, G2 and G3: a USD scene loads,
edits and saves without them until the items below are taken up, and E1
writes neither time samples nor `UsdPhysics` schemas. Each item is
independent of the others and of every step in section 2 except where
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
- Time samples on load (after the animated value layer and I1):
  time-sampled `xformOp:*` attributes become erhe animation channels
  (linear samples; `Ts` splines re-encoded to cubic samplers);
  `UsdSkel` `SkelAnimation` goes through the existing skin path. The
  matching save writes the channels back as time samples.
- Physics on load (after I1): `UsdPhysics` API schemas become
  `Node_physics`, `Node_joint`, `Physics_material` and `Collision_filter`
  per the mapping's physics table, through a USD-filled sibling of
  `Gltf_physics_data` (the physics import operations already take a
  plain-data carrier). The matching save applies the API schemas per the
  same table; erhe-only physics properties (damping, wind receptivity,
  gravity factor, combine modes) ride `erhe:` custom attributes under C1.
