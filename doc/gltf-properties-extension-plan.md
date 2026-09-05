# `ERHE_*_properties` glTF extensions - implementation plan

Status: INCOMPLETE DRAFT. Steps 0 and 1 are implemented (they are what
`doc/usd-compatibility-plan.md` M4 needed); steps 2 to 5 - the
`ERHE_*_properties` extensions themselves - are not, and the plan is not
ready to implement them: it needs more work before any of them is started
(the open points are in the sections that raise them; a reader must expect
gaps). The design record's future-work item for property serialization
(`doc/property-system.md` section 6) points here and carries no content of
its own; every decision recorded so far lives in this document.

## Context

Replace the `properties` / `mesh_properties` members scattered across the
exporter's `ERHE_node`, `ERHE_light` and `ERHE_camera` extensions, extend
property serialization to materials, and serialize expressions (D22
formulas), which are session state today. Decisions made with the user
during planning (2026-09-02):

- **One extension per item type**: `ERHE_node_properties`,
  `ERHE_mesh_properties`, `ERHE_light_properties` (all three on the glTF
  node, since erhe meshes / lights are node attachments),
  `ERHE_camera_properties` (on the camera) and `ERHE_material_properties`
  (on the material) - not a single `ERHE_properties` with sub-objects.
- **Import semantics: default-elision.** After native-field import, a local
  value equal to the property's default is cleared (one generic pass,
  applied to every imported file - third-party glTF included; effective
  values never change), then the extension entries re-create the true local
  layer. Restores the local / default distinction without ever discarding
  an external tool's edit to a native field.
- **Export: stop baking light temperature into the KHR color.** The KHR
  light color carries the raw `color` property value (default white when
  unset); `temperature` rides `ERHE_light_properties`. External readers
  lose the tint; erhe round trips become exact (removes the 4.8 gotcha).
- **No duplication of native glTF carriers.** A property whose value
  already rides a native glTF field or an ERHE typed field gets a
  registration flag `Property_flags::native_gltf` and is NOT written as a
  value entry - the native field plus default-elision reconstructs it, and
  an external edit to the native field always wins. The extension carries
  value entries only for properties with no native carrier (`visible`,
  `shadow_cast`, `lightmapped`, light `temperature`, future erhe-only
  properties) plus expression entries for every driven serializable
  property (`native_gltf` and bridged ones included). Accepted edge-case
  loss: a local value set equal to its default imports as default (same
  effective value, only the `*` marker is lost).

## Extension shape

Each extension's payload is the name->entry map directly (no `"properties"`
nesting; a future non-property member, if one is ever demonstrated to be
needed, uses a reserved `$`-prefixed key - property names never start with
`$`). On a glTF node (attachments ride the node, as `ERHE_light` does):

```json
"ERHE_node_properties":  { "visible": "false", "translation": {"expression": "{cube/translation}", "value": "1 2 3"} },
"ERHE_mesh_properties":  { "shadow_cast": "true" },
"ERHE_light_properties": { "temperature": "3000" }
```

`ERHE_camera_properties` sits on the glTF camera, `ERHE_material_properties`
on the glTF material, same payload shape.

Entry forms: a **string** = stored local value (D16 `to_string`); an
**object** = expression-driven: `"expression"` (formula text) plus
`"value"` (last evaluated result as a D16 string, for graceful
degradation). Expressions are emitted for bridged properties too (node
TRS, camera projection); plain values on bridged properties stay excluded
(they ride native glTF fields).

The exporter writes only the new extensions (drops `properties` /
`mesh_properties` from `ERHE_node`, `properties` from `ERHE_light` /
`ERHE_camera`, keeps all `flags` members); the importer keeps reading the
old members so existing files load.

## Steps (each: edit -> build primary tree -> self-review diff -> commit)

### 0. `native_gltf` flag - `src/erhe/property/` + registrations (implemented)

`Property_flags::native_gltf` (`property_metadata.hpp`) is data only, like
the other R15 flags. It marks a registration whose value the exporter
writes through a native glTF field or a typed `ERHE_*` field whenever it
differs from the property's default; `doc/property-inventory.md`
("Registration flags") owns the list of registrations that carry it and
the reason each conditionally carried field is left out.

### 1. Shared helpers

`clear_default_valued_local_properties` is implemented, in
`erhe::property` rather than in `erhe_gltf`: the rule is format
independent and the USD importer runs the same pass
(doc/property-system.md D32 owns its definition and its ordering against
the `ERHE_*` extension pass). The helpers below are what the extensions of
step 2 still need:

- `item_properties_extension_to_json(const Item_base&) -> std::string` -
  returns the `{...}` object (`{}` when nothing to write; callers skip
  emission then). Two passes:
  - values: as today's `item_local_properties_to_json`
    (`for_each_local_value`; skip non-`serialize`, `bridge.is_bound()`,
    driven entries), additionally skipping `native_gltf`-flagged
    properties;
  - expressions: `Property_registry::for_each_property_of_type(owner_type,
    item.get_property_owner_subtype())` + `get_expression(property)`; skip
    non-`serialize`; bridged and `native_gltf` included; emit the object
    form with `to_string(get_value(property))` as `"value"`. Use the
    existing `append_json_string` (gltf_item_flags.cpp:96) to escape the
    formula text.
- `apply_item_local_expression(Item_base&, name, text) -> bool` - mirror
  of `apply_item_local_property` (gltf_item_flags.cpp:151) calling
  `set_expression`; warn + false on unknown name / rejected formula.
- Delete `item_local_properties_to_json` once its export call sites are
  gone (step 2); `apply_item_local_property` stays (legacy import + new
  value entries).

### 2. Exporter - `src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp`

Line refs as of a39af8059.

- `record_node_extensions` (:5727): drop `"properties"` /
  `"mesh_properties"` from `ERHE_node` and `"properties"` from the
  `ERHE_light` member; append `"ERHE_node_properties":{...}`,
  `"ERHE_mesh_properties":{...}` and `"ERHE_light_properties":{...}`
  members, each only when its object is non-empty.
- `record_camera_extensions` (:5201): drop `"properties"`; append
  `"ERHE_camera_properties":{...}` when non-empty.
- `process_material` / `record_material_extensions` (:4799 / :4810):
  append `"ERHE_material_properties":{...}` to
  `m_internal_material_extensions` when non-empty - independent of
  `ERHE_material`'s all-defaults early return (restructure so either
  member can be emitted alone). `ERHE_asset_reference` proxy materials
  keep skipping both.
- `process_light` (:5758): remove the temperature->color baking; write the
  raw `color` property value.
- Track emission per extension name (one bool each) and
  `declare_extension_used(...)` for each emitted name next to the existing
  declarations (:6666-6684). The existing `merge_extension_members` /
  `setExtensionsWriteCallback` plumbing needs no changes (members are
  comma-separated fragments in the same per-category maps).

### 3. Importer - `src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp`

Import capture is generic (`starts_with("ERHE_")`, :3522), so the payloads
arrive with no capture changes. Add a **second pass after** the existing
library-domain extension pass (:3779-3944), so it runs after native
fields, flags, legacy `properties` members and the `ERHE_camera` /
`ERHE_material` typed fields.

For every imported item (each node, its Mesh / Light attachments, each
camera, each material):

1. The elision pass already runs before this point (D32). Legacy-file
   caveat, acceptable: an old file's local value equal to the default
   imports as default (same effective value).
2. If the glTF object carries the item's `ERHE_*_properties` extension:
   iterate its members; string member -> `apply_item_local_property`;
   object member -> apply `"value"` first (if present) via
   `apply_item_local_property`, then `apply_item_local_expression`. Lazy
   resolution (`dependency_object.cpp:622`) makes cross-item references
   converge once items get a host; no ordering work needed. A rejected
   formula leaves the applied value - graceful degradation by
   construction. Note: value before expression is required
   (`set_value` after `set_expression` would replace the formula).

Light color import is unchanged (the exporter no longer bakes, so the
round trip is exact; elision drops a default-white color).

### 4. Documentation

- `doc/property-system.md`: move the section 6 extension item into the
  implemented record, renamed to the per-type `ERHE_*_properties`
  extensions (extend D14 or add a D28); update the D22 and D25
  serialization bullets and the D23 serialization paragraph; replace the
  light-temperature gotcha in 4.8 with the new exporter behavior (external
  readers lose the tint); note default-elision + the legacy caveat and the
  `native_gltf` flag.
- `src/erhe/gltf/notes.md`: add the `ERHE_*_properties` extensions to the
  extension list (:64), note the dropped members and the legacy read path.

### 5. Verification (once, at the end)

- Builds: the Windows opengl and vulkan Debug trees (locate with
  `Glob build_*`; primary tree built every step, sweep at the end).
- Unit tests: `erhe_property_tests`, `erhe_scene_tests`,
  `erhe_primitive_tests` (no glTF test dir exists; property behavior must
  not regress).
- End-to-end via editor MCP (`scripts/mcp_call.py`, ERHE_MCP_PORT=3743):
  1. Set a local material value differing from default, a local value
     equal to its default, and an expression on a node property
     (`set_item_property` with `expression`), plus a light `temperature`.
  2. Save / export the scene; inspect the glTF JSON for the
     `ERHE_*_properties` members, the absence of the old `properties`
     members, and the unbaked KHR light color.
  3. Reload / import; `get_item_properties` to confirm: expression
     restored with `source: expression`, defaults report `source: default`
     (elision), explicit locals report `local`, light `temperature`
     round-trips exactly, and a native-carried local (e.g. light
     `intensity`) round-trips through the KHR field without appearing in
     the extension.
  4. Load one pre-change erhe scene file to confirm the legacy read path.

## Background found during planning (saves re-exploration)

- Export mechanism: raw JSON member fragments (comma-separated, no braces)
  in `m_internal_node_extensions` / `m_internal_camera_extensions` /
  `m_internal_material_extensions`, keyed by glTF index, merged by
  `merge_extension_members` and served via `setExtensionsWriteCallback`.
- Import: `setExtensionsParseCallback` captures every `ERHE_*` member as
  minified JSON into `Gltf_data::*_extensions`; the apply pass re-parses
  with simdjson (`get_extension_object`, :3788). Non-string JSON member
  values are currently skipped by the `get_string()` guard at :583-586 -
  the object entry form needs its own branch.
- `read_local_state` / `apply_local_state` (`Local_state =
  std::variant<Property_value, Expression_text>`) already round-trip
  value-vs-expression; `set_expression` never needs an `Item_host` and
  converges lazily (test `Expressions.lazy_resolution_and_source_lifetime`).
- `for_each_local_value` reports a driven entry's last evaluated result
  and cannot reveal a bridged-with-expression entry - hence the separate
  registry-enumeration pass for expressions.
- Materials are deduplicated per `Material*` (`m_exported_materials`);
  lights ride the node's extensions object, not the KHR light object.
