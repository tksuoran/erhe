# Property system (WPF dependency-property port)

Status: implemented. The library (`erhe::property`), the `Item_base`
integration, the editor operation / generic rows / MCP tools / startup
command, the `Material`, `Node`, `Light` and `Camera` migrations, observer
users, expressions and bindings, inherited flags, sealing, the style layer,
computed properties, the owner type id keyed registry, the geometry
and texture graph node migrations, object references (the material
texture slots) and property sub-objects (the mesh primitives with their
material) are all in and verified. This is a live document; section 6
holds the remaining work.

Document roles. This document is the design record: goal, requirements,
the design decisions with their WPF mapping and rationale, the item
migrations, the editor / MCP / glTF integration, future work and the
verification workflow. `src/erhe/property/notes.md` is the library
reference: the current types and semantics of `erhe::property` as one
type-by-type summary, kept in sync with the code and free of rationale.
`doc/property-inventory.md` is the status table: every registered
property by owner with its storage kind, and the hand-written Properties
rows left to migrate. Look a mechanism up in the notes; read here for why
it is shaped that way and how the rest of the codebase uses it; check the
inventory for what is and is not a property yet.

Updating together. A change to a registration touches all three: the
code, the inventory's row for it, and this document's section for the
owner (4.1 to 4.14) when the design changes; a new mechanism in
`erhe::property` touches the notes and the design decision here. A
migration of a hand-written row moves it from the inventory's "Not yet
migrated" table to the owner's table in the same commit.

Reference: the WPF property system, `https://github.com/dotnet/wpf` at the
commit named in `doc/property-system-wpf-comparison.md`, files under
`src/Microsoft.DotNet.Wpf/src/WindowsBase/System/Windows/`:
`DependencyProperty.cs` (registration, global index, owner metadata),
`PropertyMetadata.cs` (default value, changed / coerce callbacks),
`DependencyObject.cs` (`GetValue` / `SetValue` / `ClearValue` / `CoerceValue` /
`InvalidateProperty` / `ReadLocalValue` / `OnPropertyChanged`),
`EffectiveValueEntry.cs` (sparse per-object value store, base source, animated
and coerced modifiers), `DependencyPropertyKey.cs` (read-only properties), and
in `PresentationFramework/System/Windows/`: `FrameworkPropertyMetadata.cs`
(`Inherits`) and `TreeWalkHelper.cs` (inherited-value invalidation on tree
change and on inheritable property change).

## 1. Goal

A C++ library `erhe::property` that gives every erhe item (`Item_base` and
everything derived from it: `Node`, `Material`, `Light`, `Mesh`, ...) a WPF-style
property store, and the migration of items onto it so the
editor can edit, undo and inspect item state through one generic
mechanism instead of one hand-written path per field.

Value types in scope: `bool`, `int`, `float`, `double`, `glm::vec2`, `glm::vec3`,
`glm::vec4`, `glm::ivec2`, `glm::ivec3`, `glm::ivec4`, `glm::quat`,
`glm::mat4`, `std::string`, an asset path, homogeneous `float` and `int`
arrays, C++ enumerations (any `enum class` with an enumerator
table, see D2a), and references to other objects (D28).

## 2. Requirements

- R1 Registration. A property is registered once, statically, with a name, a
  value type, the owner type id of the registering class (D27) and default
  metadata, and gets a stable global index. Registration and lookup by
  (owner type, name), by (object, name) through the owner type chain, and
  by index are available at runtime for the editor and MCP.
- R2 Typed access. Callers read and write through a typed handle
  (`Property<T>`), so a `glm::vec3` property cannot be written with a `float`.
  An untyped path (`Property_value` variant) exists for generic code: the
  Properties window, undo, serialization, MCP.
- R3 Precedence. The effective value of a property on an object is, in
  decreasing precedence: coerced, local, style (D25), reference (D33),
  inherited, default. Each layer can be present or absent independently;
  clearing a layer exposes the next one.
  The layer order leaves room for an animated layer between coerced and
  local (future work, section 6).
- R4 Callbacks. A property has an optional validate callback (value only, no
  object), and its metadata has an optional coerce callback and an optional
  property-changed callback, both receiving the object. The object also gets a
  virtual `on_property_changed`. Changed notification fires only when the
  effective value actually changes.
- R5 Per-owner metadata. A derived item type can override the metadata
  (default value, callbacks, inherits flag) of a property registered by a base
  type, as WPF `OverrideMetadata` / `AddOwner` do.
- R6 Read-only. A property can be registered read-only: writes go through a
  key object the owner holds privately; the public handle reads only.
- R7 Attached. A property can be registered by a type other than the type of
  the objects it is set on (WPF attached properties), so the editor and tools
  can store per-item state without the item class knowing. The registration
  names the holder type: the class (and its descendants) whose objects may
  hold the value, the only ones it is listed and offered on.
- R8 Inheritance. A property flagged `inherits` reads the closest ancestor's
  value through `erhe::Hierarchy` when the object has no local value;
  descendants get changed callbacks when the ancestor's value or the tree
  structure changes.
- R9 Enumerations. An enumeration property carries its enumerator table
  (label and integer value per enumerator) in the registry, so generic code
  (Properties window combo, MCP, serialization) can present and parse it by
  name, and typed code reads and writes the C++ enum directly.
- R10 Enumeration. The local values of an object can be enumerated (WPF
  `LocalValueEnumerator`), and the properties registered for an item type can
  be enumerated in registration order.
- R11 Copy semantics. Copying or cloning an object copies its local values
  (with their coerced values, which derive from them). Inherited state,
  observers and pending batches are not copied.
- R12 Threading. Property values are item state and are guarded by the same
  item-host mutex as the rest of the item (`Item_host_lock_guard`). The
  registry's writes end with single-threaded early startup - C++ static
  initialization plus descriptor-driven registrations run from
  `run_editor()` before the first thread other than main exists (section
  4.5) - and reads after that are lock-free.
- R13 Undo. Every property write made from the editor UI is an `Operation` on
  the operation stack and restores the exact previous state on undo,
  including "no local value" (a clear), not just the previous effective value.
- R14 Performance gate for `Node`. After the `Node` transform migration, the
  per-frame physics transform writeback, animation playback and
  `Scene::update_node_transforms` on Sponza and VirtualCity stay within 10%
  of the pre-migration frame time.
  This held by construction: the transform properties are bridged (D18), no
  per-frame path changed, and `get_transform_update_stats` matched the
  pre-migration run on the default scene.
- R15 Consequence flags. Property metadata states what a change affects
  (transform, draw-list partitioning, shader variant, serialization), so
  generic code decides what to rebuild from the flags instead of from a
  per-field check (WPF `FrameworkPropertyMetadataOptions`, `AffectsRender`
  and friends).
- R16 Observers. Code outside the object can subscribe to changes of one
  property on one object and unsubscribe with a token (WPF
  `DependencyPropertyDescriptor.AddValueChanged`), so a consumer reacts to
  exactly the property it reads.
- R17 UI metadata. Property metadata carries what the Properties window
  needs to draw the row as well as the hand-written rows do today: range,
  step, presentation (color, angle in degrees, slider), group, tooltip and
  developer-only visibility.
- R18 String conversion. Every `Property_type` converts to and from a string
  through one function pair (WPF `TypeConverter`), shared by MCP, command
  scripts and serialization.
- R19 Value bags. A set of (property, value) pairs is a first-class value
  (`Property_set`) that can be read from an object, applied to an object,
  compared, and carried by the clipboard and by multi-selection editing.

## 3. Design decisions

- D1 Library placement. Library `src/erhe/property` (target
  `erhe_property`, alias `erhe::property`, namespace `erhe::property`),
  header prefix `erhe_property/`. Dependencies: `glm::glm-header-only`,
  `erhe::utility`, `erhe::profile` (public); `erhe::log`, `erhe::verify`,
  `fmt`, `tinyexpr` (private). `erhe::item` links it publicly and `Item_base`
  derives from `Dependency_object`. The library holds no scene, item or
  hierarchy knowledge; tree walking reaches it through one virtual on the
  object (D8).
- D2 Value representation.
  `Property_value = std::variant<bool, int, float, glm::vec2, glm::vec3,
  glm::vec4, glm::quat, std::string, Enum_value, glm::ivec2, glm::ivec3,
  glm::ivec4, Object_reference, double, glm::mat4, Asset_path,
  std::vector<float>, std::vector<int>>` (each addition appended
  so the variant indices of the earlier types are stable;
  `Object_reference` is D28, the five after it are the USD value
  types of `doc/usd-compatibility-plan.md` M6 - `Property_type::double_floating`,
  `mat4`, `asset_path`, `float_array` and `int_array`, spelled `double`,
  `mat4`, `asset`, `float[]` and `int[]` in text. A `double`
  is a one-component numeric value wherever a `float` is one; a `glm::mat4`
  is a whole value: it is neither an expression target nor an expression
  source, and its row is four drag rows, one per column, in glm's column
  order; `Asset_path` is a class wrapping one `std::string path`, kept
  distinct from `std::string` so a path is never taken for free text, not
  expressible, and its row is the path as text; an array is not
  expressible either and its row is read-only, showing how many values it
  holds and the first few of them. The array alternatives allocate when a
  `Property_value` is copied, as `std::string` already does; a
  `Property_value` is built at an edit, an import or an export, never per
  frame) with a matching
  `enum class Property_type : uint8_t` whose enumerators are the variant
  indices. `Property<T>` is valid for `T` in that list or for any C++
  enumeration type (constrained with a concept); an enumeration `T` maps to
  `Enum_value` per D2a.
- D2a Enumerations. `Enum_value` is `struct { int32_t value; }`, kept
  distinct from `int` so generic code can tell a combo from a drag-int.
  `Enum_info` is an immutable table `{ std::string_view type_name;
  std::span<const Enum_entry> entries; }` with `Enum_entry { std::string_view
  label; int32_t value; }`, plus `label_for(value)` and `value_for(label)`.
  A `Dependency_property` of type `Enum_value` holds a `const Enum_info*`;
  registration through `Property<E>::register_property` for an enumeration
  `E` requires the caller to pass the `Enum_info` (one `static const`
  table per enumeration, next to the enumeration's existing `c_str`
  function so the labels have one source). Validate for an enumeration
  property rejects any integer that is not in its table. `Property<E>::
  get_value` returns `E` by `static_cast` from the stored integer; `set_value`
  stores `static_cast<int32_t>(value)`.
- D3 Registry. `Dependency_property` is an immutable registration record:
  global index (`uint16_t`, assigned sequentially as WPF `GlobalIndex`),
  name, `Property_type`, owner type (the `Owner_type` id of the registering
  class, D27), flags (read-only, attached), validate callback, default
  `Property_metadata`, and a small vector of (owner type,
  `Property_metadata`) overrides. `Property_registry` is a function-local
  static (safe against static-initialization order) holding records by
  index, an index by (owner type, name), a per-owner-type list in
  registration order, and the owner type id table. Registration happens
  from static members of the owning class:

  ```cpp
  // material.hpp
  static const erhe::property::Property<glm::vec3> base_color_property;
  // material.cpp
  const erhe::property::Property<glm::vec3> Material::base_color_property =
      erhe::property::Property<glm::vec3>::register_property(
          "base_color", Material::property_owner_type(),
          erhe::property::Property_metadata{ .default_value = glm::vec3{1.0f} }
      );
  ```

  `Property<T>` is a copyable handle wrapping `const Dependency_property*`.
  `Property_key<T>` (R6) is the same handle plus write permission; the owner
  keeps it private and exposes a `Property<T>` obtained from it.

  An attached property (R7) is registered by `register_attached` under
  the registering type's owner type with a plain name (`align_x`); its
  qualified name `<owner>.<name>` (`Layout.align_x`, WPF `Grid.Row`) is
  the form serialization (D14), MCP (D13), the operation descriptions and
  the row labels (D12) use, produced by `Property_registry::qualified_name`.
  `find_for_object` resolves a qualified name to that owner's attached
  registration for any object (`find_owner_type` is the reverse of
  `get_owner_name`); its owner-chain walk never returns an attached
  property and a qualified name never resolves a non-attached one.
  `for_each_attached_property` lists every attached registration; the
  per-object listing is `for_each_property_of_object` plus the D12 rule.
  The value lives in the target object's entry store like any local
  value, so undo, copy / paste, styles, expressions, observers and the
  glTF properties map need nothing attached-specific.
- D4 Metadata. `Property_metadata` holds `default_value` (a
  `Property_value`), `property_changed` (`void(Dependency_object&, const
  Property_changed_args&)`), `coerce` (`Property_value(const
  Dependency_object&, const Property_value&)`), `bool inherits`, a
  `Property_flags` bitmask (R15: `affects_transform`,
  `affects_draw_list_partition`, `affects_shader_variant`, `serialize`;
  `serialize` is set by default) and a `Property_ui` block (R17:
  `std::optional<float> min, max, step`, `enum class Presentation { plain,
  color, angle_degrees, slider }`, `std::string_view group, tooltip, label`,
  `bool developer_only`, `Visible_when visible_when` - a predicate on the
  inspected object). The flags and the UI block are data only; the editor
  reads them (D11, D12) and the library never acts on them. Metadata
  resolution for (property, object) walks the object's owner type chain
  (D27) from its own id to the root and returns the first override
  registered for an id on it, else the default metadata, so an override on
  a base class applies to every derived class and a derived-class override
  wins. Override lists are short (usually empty), so each level is a
  linear scan with no cache.
- D5 Effective value store. `Dependency_object` owns
  `std::vector<Effective_value_entry>` sorted by property index and searched
  by binary search (WPF `EffectiveValueEntry` array). An entry holds: index,
  the local `Property_value`, and `std::optional<Property_value> coerced`
  set only when the coerce callback changed the local value (the future
  animated layer of section 6 is another optional in the entry). An entry
  exists only for properties with a local value; reading a property with no
  entry returns the inherited or default value without allocating.
- D6 Public object API (mirrors WPF names in erhe casing):
  - `get_value(property) -> T` / `get_value(const Dependency_property&) ->
    Property_value` (effective value, R3).
  - `set_value(property, value)` (local layer; validate, then coerce, then
    store, then notify), `clear_value(property)` (drops the local layer).
  - `read_local_value(property) -> std::optional<T>` (R10, R13).
  - `coerce_value(property)` re-runs the coerce callback against the current
    local value and notifies if the effective value changed (WPF
    `CoerceValue`); a property without a local value is coerced on every
    read and has nothing to re-run.
  - `get_value_source(property) -> Value_source`, `is_coerced(property)` for
    UI indicators; `has_local_value(property)`.
  - `for_each_local_value(callback)` (R10).
  - `add_observer(property, callback) -> Observer_token` (D15).
  - `capture_inheritance_snapshot()` / `apply_inheritance_snapshot()` (D8).
  - `virtual void on_property_changed(const Property_changed_args&)`.
  `Property_changed_args` carries the property, old and new effective
  values, and old and new `Value_source`.
- D7 Validate and coerce semantics. Validate (type check, enumeration
  table, callback) runs on the incoming value before any store; a failing
  validate logs an error and drops the write, so a bad value from a file,
  MCP or a script never reaches the store and never aborts the editor.
  Coerce runs on the local value at write time and its result is stored as
  the coerced value; a coerce that returns its input stores none. A property
  without a local value is coerced on every read.
- D8 Inheritance mechanics. `Dependency_object` has
  `virtual auto get_inheritance_parent() const -> const Dependency_object*`
  returning `nullptr` and `virtual void for_each_inheritance_child(const
  std::function<void(Dependency_object&)>&)` doing nothing. `Hierarchy`
  overrides both with its parent and children; `Item_base` returns as
  parent the container set on it (`set_inheritance_container`), which the
  editor's content-library node maintains so a library folder's values
  reach the materials and brushes below it
  (`doc/content-library-folders.md` D1). An inherits-flagged property
  with no local value on an object reads the effective value from the
  closest ancestor that has one on every read (no cached copy: the walk is
  as deep as the tree and a cache would need tree-change invalidation).
  `set_value` / `clear_value` on an inherits-flagged property notifies every
  descendant that has no local value for it, stopping the walk at a
  descendant that has one (WPF
  `TreeWalkHelper.InvalidateOnInheritablePropertyChange`). A tree change
  captures the subtree's inherited values before the move
  (`capture_inheritance_snapshot`) and notifies the differences after it
  (`apply_inheritance_snapshot`; WPF `InvalidateOnTreeChange`);
  `Hierarchy::set_parent` does both.
- D9 Change batching. `Dependency_object::Change_batch` is an RAII guard:
  while one is alive on an object, changed notifications are queued and
  delivered once when the outermost guard ends, deduplicated per property
  with the value before the batch and the value after. `Material::
  set_values` and `Property_set::apply` use it.
- D10 Copy. `Dependency_object`'s copy constructor and assignment copy the
  entries and nothing else (R11). `Item_base` and `Hierarchy` clone paths
  keep working unchanged because they already copy-construct the base.
- D11 Editor undo. `Property_set_operation` in `src/editor/operations/`
  records the item (`std::shared_ptr<Item_base>`), the property, `before`
  as `std::optional<Local_state>` (the local value or expression text, or
  nullopt for "no local value") and `after` likewise. `execute` applies
  `after` (set or clear), `undo` applies `before`. It calls one editor hook,
  `App_context::on_item_property_changed(item, property)`, after each apply,
  which reads the property's `Property_flags` (D4) and runs the matching
  editor consequence: `affects_draw_list_partition` rebuilds the draw lists,
  `affects_shader_variant` re-derives the material's shader variant. The
  hook is the only place that maps flags to editor actions. The operation
  carries an optional sub-object index (D29): the target is then
  `item->get_property_sub_object(index)`, the item stays the one the
  operation names and seals against, and a sub-object that no longer
  exists at apply time is a logged no-op.
  `Material_change_operation` applies a whole `Material_data` snapshot
  through `Material::set_data` (the MCP `edit_material` tool). A
  `Property_set_apply_operation`
  applies a `Property_set` (D17) to a list of items and records one before
  bag per item, for paste and multi-selection edits.
- D12 Editor UI. `Property_editor` has a generic
  `dependency_properties(item)` section that lists the registered properties
  of the item's type in registration order and draws one widget per
  `Property_type`: checkbox, drag int, drag float, drag float2/3/4, quaternion
  as Euler-degree drag with a raw x/y/z/w readout, input text, a combo
  filled from the property's `Enum_info` labels, and for an object
  property the `item_reference_imgui` field (D28). Every such row's
  label is tinted by its value source - gray for the default, green for a
  local value, blue for a member-backed property (D18), cyan for an
  expression (D22), orange for a style (D25), pink for a reference (D33),
  purple for an inherited value, dim gray for a computed one (D26) - so a hand-written row of the
  same window (state not yet a registered property) and the layer a
  value comes from are both told at a glance; the tooltip names the
  source in words. The `Property_ui` block
  (D4) selects the widget variant and its limits: `color` draws a color
  edit for vec3 / vec4, `angle_degrees` converts radians to degrees for
  display, `slider` draws a slider within `min` / `max`, `group` collapses
  rows under a header, `tooltip` is the hover text, `label` replaces the
  property name as the row label, `developer_only` rows show only in the
  editor's developer mode, and a row with `visible_when` is listed only
  while the predicate holds for every selected item (Material hides its
  PBR rows for unlit materials, its anisotropy rows for BxDF models
  without anisotropy, and alpha cutoff outside the alpha-test blending
  mode). Each row's tooltip shows
  its `Value_source` (local, inherited, default), its default value and a
  quaternion's raw x y z w; the label carries a `*` prefix when a local
  value differs from the default; the context menu offers "Reset to default"
  (a clear). Drag edits follow the existing
  begin-edit-snapshot / commit-on-release pattern used by material editing
  (`m_material_state`), producing exactly one `Property_set_operation` per
  completed drag. With several items selected the window draws one
  section per selected property owner type (a physics material and a
  body selected together keep their full row sets), in the order the
  types first appear; within a section the items of that type share the
  rows, a row whose values differ across them is marked mixed, and a
  commit writes the edited property to every item of the section through
  one `Compound_operation` of per-item `Property_set_operation`s (D11).
  The
  section's context menu offers Copy Properties (reads a `Property_set` of
  the item's local values into an editor clipboard) and Paste Properties
  (applies it to the selection through `Property_set_apply_operation`,
  skipping properties the target type does not have).

  Attached properties (R7) follow the section's listing rule: an attached
  property is listed on an object of its holder type
  (`Dependency_property::applies_to`) when the registering type's
  `visible_when` holds for it, or when the object holds a local value for
  it (a stale hint stays visible and resettable); with several items
  selected, for every one of them. The rule is
  `is_extra_property_listed` in
  `src/editor/windows/attached_property_listing.{hpp,cpp}`, shared with
  D13; the same function lists a secondary property (D30) by the
  object's own value. Its row label is `Property_ui::label`
  or the qualified name (D3), under its group like any other row.

  Add Property. Every item section (not a sub-object's, D29) ends with an
  "Add Property" row whose button opens a popup with a text filter
  (focused on open) over the candidates: the attached registrations the
  listing rule does not list for the item
  (`collect_addable_attached_properties`; `developer_only` ones in
  developer mode only; with several items selected, the union over
  them), grouped by the registering owner type's name and matched
  case-insensitively against the qualified name and the label. Choosing
  one queues a `Property_set_operation` (D11) per item without a local
  value whose `after` is the item's current effective value, so the add
  changes nothing but the layer and the row appears; undo removes the row
  again. The button is disabled on a sealed item (D24) and when nothing is
  addable.

  Remove Property. An attached row's context menu offers "Remove
  Property" next to "Reset to default", enabled when a selected item holds
  a local value and the item is writable; a row listed only because of
  its local value (`visible_when` does not hold) also shows an "x" button
  after its widget, since removing it makes the row disappear. Both are
  the "Reset to default" clear, so undo brings the row back. A
  class-chain row keeps "Reset to default" as its only clear, because
  clearing it never removes the row.
- D13 MCP. Three tools in `src/editor/mcp/`: `get_item_properties(item)` lists
  (name, type, effective value, source, local value) and
  `set_item_property(item, name, value)` writes through
  `Property_set_operation`. Values travel as strings through D16, so
  enumeration values travel as their labels; an object value (D28)
  travels as the referenced item's name with `reference_id` (its session
  id) and `reference_type` alongside, and `set_item_property` also takes
  `reference_id`. The listing has `sub_objects` and the write takes
  `sub_object` (D29). An attached property (R7) is listed by
  its qualified name (D3) with `attached` true, under the D12 listing
  rule, and is written by that name. `get_addable_item_properties(item)`
  lists the D12 Add Property candidates with the same per-property
  fields (`include_developer_only` adds the `developer_only` ones); an
  add is `set_item_property` with the qualified name, a remove is
  `set_item_property` with a `null` value. A `scene.set_property` command
  (`config/editor/commands.json`, `doc/command_script.md`) with args
  `item`, `property`, `value` (and `sub_object`) uses the same conversion,
  so startup scripts can author properties.
- D14 Serialization. glTF import and export read and write the
  fields they natively map (`Node` TRS, `Material` PBR fields) through the
  typed accessors; the file format did not change except the D23 extras.
  Writing local values of non-glTF properties to extras
  (`item_local_properties_to_json` / `apply_item_local_property` in
  `erhe_gltf/gltf_item_flags.hpp`, honoring the `serialize` flag) is done
  for nodes, meshes (D23, `ERHE_node` `properties` / `mesh_properties`),
  lights (`ERHE_light` `properties`) and cameras (`ERHE_camera`
  `properties`); materials still export field-by-field through their
  native glTF fields plus the `ERHE_material` extension (section 6). An
  object value (D28) rides its native glTF carrier - a primitive's
  material index, a material's `textureInfo` - and never the extras:
  the primitive's is member-backed (D18), which the extras writer skips,
  and materials write no extras (section 6). A content-library folder's
  texture slot value (D30) rides `library_folders` by the referenced
  item's name.
- D15 Observers (R16). `Dependency_object::add_observer(property, callback)
  -> Observer_token` and `remove_observer(Observer_token)`; the token is a
  move-only RAII object that unsubscribes on destruction, and an object
  being destroyed invalidates its outstanding tokens (they become no-ops).
  Observers are stored in a per-object vector allocated on first
  subscription, and are invoked after the metadata callback and the virtual
  hook, with the same `Property_changed_args`, under the same batch rules as
  D9. The users are settled in D21.
- D16 String conversion (R18). `to_string(const Property_value&) ->
  std::string` and `parse_value(Property_type, std::string_view, const
  Enum_info*) -> std::optional<Property_value>` in
  `erhe_property/property_string.hpp`. Vectors and quaternions are
  space-separated components, a `glm::mat4` is its 16 components in glm's
  storage order (column 0 first, each column `x y z w`), an array is its
  components in the same space-separated form and the empty array is the
  empty text, booleans are
  `true` / `false`, enumerations are their `Enum_info` labels, strings and
  asset paths are verbatim. Both directions round-trip every value exactly for the
  non-floating-point types and to the shortest representation that
  round-trips for `float` and `double` (`std::to_chars`). An
  object reference (D28) needs the referencing object to parse, so a
  second overload `parse_value(const Dependency_object& context,
  property, text)` resolves the text through
  `context.resolve_expression_object` and delegates every other type to
  the plain one.
- D17 Value bags (R19). `Property_set` in `erhe_property/property_set.hpp`
  is a vector of (`const Dependency_property*`, `Property_value`) sorted by
  property index with `read_local_values(const Dependency_object&) ->
  Property_set` (R10 enumeration), `apply(Dependency_object&)` (one batch,
  D9), `diff(const Property_set&, const Property_set&) -> Property_set` and
  `operator==`. `Material::operator==` (section 4.1) compares the bags read
  from both materials.
- D18 Bridged storage. `Property_metadata::bridge` (`Property_bridge`
  with `get` / `set` callbacks) stores a property outside the entry store:
  the object's own member is the local value, `set_value` / `clear_value`
  go through `set` (clear writes the default), the property always reports
  `Value_source::local`, never inherits, is listed by
  `for_each_local_value` and `Property_set::read_local_values`, and is
  coerced on every read. For state that already has an engineered
  representation the object must keep. `Node`'s transform is the case
  (section 4.2): `Trs_transform` defers matrix decomposition and
  `set_parent_from_node` copies TRS components without a matrix round trip
  so rotation survives near-zero scale, and the physics writeback runs that
  path per body per frame; moving the components into the entry store would
  force decomposition on every matrix write and change the world matrix of
  glTF matrix nodes to compose(decompose(M)).
  `Property<T>::register_member(name, owner_type, member, metadata,
  after_set)` registers a bridged property without spelling the bridge
  out: the library builds `get` / `set` from a pointer-to-member or an
  accessor lambda (`[](auto& o) -> auto& { return o.a.b.c; }` for a
  nested member), `set` writes the member and then runs the optional
  `after_set(owner)` hook - the consequence a hand-written bridge ran
  inline - and a write of the value the member already holds does
  neither (R4). `Member_value_traits<Member>` converts between the member
  type and the stored value: the identity for every storable type, and
  for a `std::shared_ptr<U>` member the cast to and from an
  `Object_reference` (D28) plus a validate that rejects a pointee that is
  not a `U`. An enumeration member registers through the overload that
  takes its `Enum_info`, as `register_property` does. The mesh primitive
  material (section 4.9), the Camera projection fields (section 4.4) and
  the graph node parameters (section 4.5) register this way.

- D19 Item-side consequences through metadata callbacks. When a change of
  a property has a consequence inside the item library itself (not in the
  editor, which D11 covers), the owner registers the property with a
  `property_changed` callback (D4) that runs the consequence, so every
  writer - typed accessor, Properties row, `Property_set_operation`, MCP,
  `scene.set_property`, glTF import, `Property_set::apply` - reaches it
  and no call site has to remember a manual "notify" call. `Light` is the
  first user (section 4.3): the light-set re-resolve
  (`Scene_host::on_light_changed`) is the changed callback of every
  `Light` property, and `Light::notify_changed()` is not public API.
  A callback only fires on an effective change (R4), so a write of the
  current value costs nothing downstream, and D9 batching still collapses
  a multi-property write into one callback per property.
- D20 Logarithmic sliders. `Property_ui` has `bool logarithmic` (data
  only, like the rest of the block); the generic float slider row draws
  with `ImGuiSliderFlags_Logarithmic` and a `%.4f` no-rounding format when
  it is set. The light range / intensity and camera near / far rows are
  logarithmic and would lose usability as linear sliders over 0..20000.
- D21 Observer users. Of the three D15 candidates, one is an observer
  user; the other two are settled without observers:
  - `Thumbnails` (the material and brush thumbnails of the Hotbar and the
    Inventory window). A slot was rendered once when claimed and again
    only while hovered, so editing a material left its thumbnails stale
    until the next hover or eviction. Now a slot subscribes to the item
    it shows when it is claimed, with an any-property observer (below),
    and the callback only marks the slot stale. `Thumbnails::draw` of a
    stale slot re-queues the render callback through the same deferred
    path the hover re-render uses (stored in the slot, run from
    `Thumbnails::update` at the start of the next frame), so only
    thumbnails that are drawn re-render, at most once per frame, and a
    hidden one re-renders when it is next drawn. The token lives in the
    slot and is replaced when the slot is reclaimed for another item; a
    destroyed item deactivates it (D15). A texture slot's texture is a
    property (D28, section 4.1) and refreshes the thumbnail, as do its
    transform and sampler properties. The Properties window material
    preview keeps its per-frame render: it must follow the window size.
  - `Shadow_render_node`. The `cast_shadow` consequence already arrives
    through the D19 changed callback: `Scene_host::on_light_changed`
    invalidates the scene's `Light_set` and `Light_set::resolve` recomputes
    lazily. The node has no per-light state of its own; a direct observer
    would have to follow light registration to know which lights to
    subscribe to, which is what the `Scene_host` register / unregister
    hooks and the light set already do. No change.
  - Geometry graph `transform_from_node`. The driver's world transform is
    a derived cache, not a stored property (section 4.2); the direct
    `Trs_transform` writers (transform tool, physics writeback, animation)
    raise no property notification, and an ancestor move changes the
    driver's world transform with no property change on the driver at all.
    The world transform is a computed property since D26, and a computed
    property raises no observer notification, so `update_live` keeps
    polling (the transform serial is the cheap compare).
  - Library: `Dependency_object::add_observer(callback)` without a
    property subscribes to every property of the object, for a consumer
    that mirrors the whole object (a thumbnail, a preview, a serializer)
    rather than one value; same token, ordering and batch rules as D15.
    WPF has no equivalent (`DependencyPropertyDescriptor` is per
    property); it saves one token per registered property of the type.
  - `Node_physics` is the second any-property observer user: it observes
    its physics material so the backend body re-snapshots the material
    values on an edit (section 4.12).
- D22 Expressions and bindings (WPF `Expression`, `DependentList`,
  `SetCurrentValue`). A property of an object can be driven by a formula
  instead of a stored value; the formula sits at the local level of R3
  (WPF stores an expression as the local value) and its result goes
  through validate and coerce like a stored value. A binding is a formula
  that is one reference, so both share one implementation.
  - Text form. The formula string is the only authored representation
    (rows, MCP, `scene.set_property`, undo, future serialization). It is a
    comma-separated list of tinyexpr expressions, one per component of
    the target (`bool`, `int`, `float`, enumeration: one; `vec2` / `vec3`
    / `vec4`: two to four; `quat`: four, `x y z w`); a single expression
    on a vector target broadcasts. Top-level commas split components,
    commas inside parentheses are function arguments. A reference is
    written in braces: `{[object/]property[.component]}`, where `object`
    absent means the target's own object, `..` its inheritance parent,
    and anything else an item name resolved by the object
    (`resolve_expression_object`, below); `component` is `x`, `y`, `z`
    or `w`. A reference without a component in the formula of component
    `k` reads component `k` of the source (a scalar source reads its
    value), so `{../scale}` on a `vec3` target mirrors the parent's scale.
    `bool` sources read as `0` / `1`, enumerations as their integer.
    `string` and object (D28) properties are neither sources nor targets. Results convert
    per target type: `bool` is `value != 0`, `int` and enumeration round
    to nearest (an enumeration value outside its table is an error, the
    previous value stays), `float` truncates from `double`.
  - Store. `Effective_value_entry` carries `std::unique_ptr<Expression>`;
    `local` holds the last evaluated value, so every existing read path
    stays as it is. On a bridged property (D18) the entry carries only the
    expression and the evaluated value goes through `bridge.set`, so a
    node's `translation` can be driven the same way as any stored
    property. `get_value_source` reports `Value_source::expression`.
    `set_value` on a driven property replaces the formula with the value
    (WPF semantics); `set_current_value` writes the value and keeps the
    formula (WPF `SetCurrentValue`); `clear_value` drops both. A copy of
    the object (D10) copies the formula text unresolved; the copy resolves
    its references itself, lazily.
  - API. `set_expression(property, text) -> bool` compiles the text and
    installs it (a syntax error, a `string` target or a formula that
    references its own target property on the same object is rejected the
    way a failed validate is, D7: logged, nothing changes, `false`);
    `get_expression(property) -> std::optional<std::string_view>`;
    `get_expression_error(property) -> std::string_view` (empty when the
    last evaluation succeeded; else the unresolved reference, the type
    problem or the cycle); `set_current_value`; `read_local_state` /
    `apply_local_state` over `Local_state = std::variant<Property_value,
    Expression_text>`, the exact local layer for undo (D11).
  - Resolution and evaluation. References resolve lazily: at compile
    time only the syntax is checked, and each evaluation retries the
    references still unresolved through
    `virtual auto resolve_expression_object(std::string_view path) const
    -> Dependency_object*` (the library default resolves the empty path to
    the object itself). `Item_base` resolves `..` to
    `get_inheritance_parent()` and a name through its `Item_host`
    (`Item_host::find_hosted_item(name)`, implemented by `Scene_host`
    over the scene's nodes and attachments and extended by the editor's
    `Scene_root` with the content library's materials), so a source is
    always hosted by the target's own host and evaluation runs under the
    target's item-host lock without a cross-host read order. A property
    named in a reference is looked up by `find_for_type` on the resolved
    object. tinyexpr binds each reference to a slot in a per-expression
    value array (variables `ref0`, `ref1`, ... substituted in parentheses
    for the braces); the compiled tree is kept for the life of the
    expression.
  - Push and pull. Each resolved source object keeps a dependent list of
    (target object, target property, source property), registered at
    resolution and removed when the expression is replaced or dropped,
    when the target is destroyed, or when the source is destroyed (which
    also turns the reference unresolved again). A change delivered on a
    source property (`deliver`, so batches collapse first) re-evaluates
    every dependent target and notifies it with the value before and
    after, through the target's own `notify` (batches, observers, D19
    callbacks and the editor hook all see an expression result exactly as
    a stored write). `invalidate_dependents(property)` is the public entry
    for a source whose storage changed outside `set_value`:
    `Node::handle_transform_update` calls it for `translation`, `rotation`
    and `scale`, so the transform tool, physics and animation drive
    expressions too; with no dependents it is one null check. A read of
    a driven property whose references are still unresolved retries the
    resolution first (pull), so a source that appears later, a loaded
    scene or a clone converge without a frame hook.
  - Cycles. `set_expression` rejects a direct self reference and walks the
    resolved expression graph from each reference back to the target,
    rejecting a formula that reaches it; a cycle closed later through lazy
    resolution is caught at evaluation by a per-expression re-entry
    guard, which reports `cycle` in the error and keeps the last value.
  - Undo (D11). `Property_set_operation` `before` / `after` are
    `std::optional<Local_state>`, so attaching, editing and removing a
    formula are undoable and undo of a value write restores the formula
    it replaced. `Property_set` (D17) and paste stay value bags: reading
    the local values of a driven property bakes its current result.
  - Rows (D12). A row whose first item is driven draws the formula text
    in place of the widget (commit on Enter or deactivation, one
    operation), a `=` label prefix, the error as a tooltip with a red
    frame, and `Source: expression` in the tooltip. The context menu
    has `Edit as expression` (starts from the current value in formula
    form) and `Remove expression` (bakes the current result as the local
    value); `Reset to default` clears both.
  - MCP and command (D13). `get_item_properties` reports `expression` (the
    text or `null`) and `expression_error`; `set_item_property` and
    `scene.set_property` accept `expression` instead of `value`.
  - Serialization stays with D14: formulas are session state until the
    property serialization work of section 6 lands.

- D23 Inherited flags (`visible`, `shadow_cast`, `lightmapped`).
  - Before the change. `Item_flags::visible` was a self bit; the only
    propagation was `Node_attachment::handle_node_update` /
    `handle_node_flag_bits_update` copying the node's bit onto each
    attachment, so a mesh was visible exactly when its node was and a hidden
    node did not hide its child nodes. `shadow_cast` and `lightmapped` were
    self bits on meshes with no propagation. `Item_flags::invisible_parent`
    is not visibility propagation: `Scene_root` sets it on the scene root
    node and the item tree skips that row and lists its children in its
    place; it stays as it is.
  - Properties. `Item_base` registers `visible` (default `true`) with
    owner type `Item_base::property_owner_type()`, the id every item class
    descends from (D27), so every item type lists it; `erhe::scene::Mesh`
    registers `shadow_cast` (default `false`) and `lightmapped` (default
    `false`), the two the renderers read on meshes only, so a mesh lists
    them and a node or a style holds `Mesh.shadow_cast` for the meshes
    below it (D30) instead of every item carrying a meaningless row. All
    three are inherits-flagged `bool` properties. The semantics are the D8 ones: the closest ancestor with a
    local value wins, as CSS `visibility` (a child under a hidden parent can
    be shown with a local `true`); WPF `IsVisible` (parent AND self) is not
    reproduced, because the coerced value of a local write is stored at
    write time (D7) and cannot follow a later parent change. None of the
    three carries an R15 editor flag: the draw lists read the derived bits
    (next point), so nothing in the editor hook is needed. `shadow_cast`
    and `lightmapped` carry `.ui.group = "Rendering"`; `visible` is
    ungrouped. All three keep `serialize`. Each property's changed
    callback mirrors the effective value into its `Item_flags::derived`
    bit (`Item_base::on_flag_property_changed`,
    `Mesh::on_render_flag_property_changed`); a copy rederives the bits
    from the copied entries (`Item_base::rederive_flag_bits`,
    `Mesh::rederive_render_flag_bits`).
  - Inheritance tree. A `Node_attachment` is not a `Hierarchy`; it overrides
    `get_inheritance_parent()` to return its node and `Node::
    for_each_inheritance_child` visits child nodes, then attachments.
    `Node_attachment::set_node` (the one path that changes the node
    pointer) captures the attachment's inheritance snapshot before the move
    and applies it after, the way `Hierarchy::set_parent` does for a
    subtree, so a mesh moved between nodes is notified of its new
    inherited visibility.
    This replaces the `visible` half of the attachment flag mirroring; the
    `selected` / `hovered_*` mirroring stays (those are not properties).
  - Derived bits (R14). The three bits stay in the flag word as derived
    bits: the properties' shared changed callback writes the new effective
    value into the item's bit through a private `Item_base::
    set_derived_flag_bit`, which bumps the mutation serial and runs
    `handle_flag_bits_update` as `set_flag_bits` does. An inherited change
    reaches every descendant without a local value through D8, and a tree
    move through the snapshots, so the bit is always the effective value.
    Every reader is unchanged: `Item_filter` over the draw-list entry flag
    words, `is_visible()`, the shadow / lightmap / raytrace mask tests, the
    glTF exporter's flag list. `Item_base`'s constructor starts the word
    with `visible` set (the property default, no parent yet), and the copy
    constructor and assignment re-derive the three bits from the copied
    entries after `Dependency_object` is copied (a copy has no parent, so
    an inherited `false` does not survive the copy; the bit must agree).
  - Writers. `set_flag_bits` (and the `enable_` / `disable_` wrappers) with
    a derived bit in the mask is a programming error: it logs one error
    naming the bit and drops those bits from the mask. `set_visible` /
    `show` / `hide` write the `visible` property (local value), which is
    what the item tree, the tools and the raytrace hide-restore need;
    `Node_raytrace` restores the previous *local state* (`read_local_state`
    / `apply_local_state`) around its hide, so an inherited value is not
    baked into a local one. Construction-site
    `enable_flag_bits(... | visible | ...)` masks dropped the `visible` bit:
    with the default `true` the item is visible, and a local `true` on a
    mesh would stop the node's hide from reaching it (the point of the
    change). A construction site that relied on the bit being absent to
    start hidden calls `hide()` explicitly. `shadow_cast` and `lightmapped`
    construction writes are `set_value(Mesh::shadow_cast_property, true)`
    (a local value, the per-mesh semantics; a group node's local
    value reaches only meshes without one).
  - Editor. The generic property rows (D12) draw the three checkboxes with
    source, reset and undo for free; the `Properties::item_flags` grid
    skips the derived bits. "Enable / Disable Lightmap (Recursive)" in the
    item context menu is one `Compound_operation` that sets the local
    value on the selected items and clears it on their descendants, so the
    subtree follows the ancestor afterward; `Item_set_flag_bits_operation`
    keeps serving `no_transform_update`. MCP `set_item_flags` rejects the
    three names with a message pointing at `set_item_property`;
    `get_item_properties` lists them.
  - Serialization (the first user of the D14 extras work). The
    `ERHE_node` extension carries `"properties"` (the node's) and
    `"mesh_properties"` (its mesh's) objects of local values, written with
    D16 `to_string` for every non-bridged, non-expression local value whose
    property has `serialize`, and read back through
    `parse_value` and `set_value` (a failed parse or an unknown name is
    logged and skipped). The same mechanism serves the `ERHE_light` and
    `ERHE_camera` `properties` objects (D14). The exporter does not list `visible`,
    `shadow_cast` and `lightmapped` in the `flags` arrays; the importer
    keeps reading them from old files (`visible` absent from a listed
    `flags` array sets local `false`; `shadow_cast` / `lightmapped` present
    set local `true` on a mesh) so existing scenes load as before.

- D24 Sealing (WPF `Freezable.Freeze`, reversible).
  - Before the change. `Item_flags::lock_edit` is the user's edit lock
    (toggled in the Properties window Locks row, MCP `lock_items` /
    `unlock_items`, persisted by name in glTF) and the prefab editing model
    seals an instance subtree with it (`seal_instance_subtree`, together
    with the two viewport locks, never unsealed: a refresh re-clones). It
    was honored only by hand-written checks: the Properties window disables
    its typed blocks and the name field, MCP `edit_material` refuses a
    locked material, delete skips locked items, and the transform and
    selection tools read the viewport locks. The generic property rows
    (D12), `Property_set_operation` (D11), MCP `set_item_property` and
    `scene.set_property` did not read it, so every registered property of a
    locked item, prefab interiors included, was editable through them; the
    seal at the write funnel closes that for every writer at once.
  - Library. `Dependency_object::seal()` / `unseal()` / `is_sealed()`.
    While sealed, the local layer is frozen: `set_value`,
    `set_current_value`, `clear_value`, `set_expression` and
    `apply_local_state` are rejected the way a read-only write is (D7: one
    logged error naming the property, nothing changes). Reads, inherited
    values and their notifications, coerce, observers and the evaluation
    of an expression already installed keep working: a sealed prefab
    interior still follows its instance root's `visible`, and a formula
    on a sealed object still tracks its sources; only authoring the local
    layer is closed. A property flagged
    `Property_flags::writable_when_sealed` is written even while sealed:
    it is the flag of the property that owns the seal
    (`Item_base::lock_edit_property`), so the lock is lifted through the
    same row, operation and MCP call that set it.
    `Dependency_object::is_write_sealed(property)` is the per-property
    form of the check for editors that disable a widget. `apply_local_state` and the untyped `set_value` /
    `clear_value` return `bool` so undo and MCP can report a rejected
    write instead of recording a no-op. A copy (D10) is not sealed (WPF
    `Clone` of a frozen object is unfrozen); `Item_base` re-derives the
    seal from its copied flags (next point). Unlike WPF, `unseal` exists
    because the editor's lock is a user toggle; the prefab code simply
    never calls it.
  - Item integration. The seal is the `lock_edit` flag: `Item_base::
    set_flag_bits` calls `seal()` / `unseal()` when the bit changes and
    the copy paths re-sync, so every existing writer of the flag (the
    Locks row, `lock_items`, `set_item_flags`, `seal_instance_subtree`,
    the scene builder's floor, glTF import) seals through the one path and
    `is_lock_edit()` and `is_sealed()` agree. `set_name`, the flag word
    and the tags are not properties and keep their own checks.
  - Editor. The generic rows draw disabled for a sealed first item, queue
    nothing, and disable Reset to default, Edit as expression, Remove
    expression and Paste Properties (Copy stays); the row tooltip says
    `Sealed (lock_edit)`. `Property_set_operation` and
    `Property_set_apply_operation` log a warning when the write was
    rejected. MCP `set_item_property` and `scene.set_property` refuse a
    sealed item with a message naming `lock_edit` / `unlock_items`, and
    `get_item_properties` reports `sealed` on the item. The typed blocks'
    `edit_disabled` and the delete / transform / selection checks stay:
    they guard state that is not a property.

- D25 Style layer (WPF `Style` setters, `BaseValueSourceInternal.Style`).
  - Context. The graphics presets are a generated POD
    (`Graphics_preset_entry` inside `Graphics_settings`) that the Settings
    window edits in place, with no local-override concept to preserve; they
    stay as they are until `Graphics_settings` is an item (section 6). The
    layer is generic for every item type; its users are the content
    library's style items (`doc/style-library.md`), which any item,
    node or folder can take.
  - Library. A style source is a `Dependency_object` whose LOCAL values
    are the style: `Dependency_object::set_style(std::shared_ptr<const
    Dependency_object>)` / `get_style()` install one source per object.
    `Property_style` (`erhe_property/property_style.hpp`) is the library's
    plain named source, filled from a `Property_set` (D17); the editor's
    style items are their own sources (`doc/style-library.md` D2). R3 is
    coerced > local (a stored value or an expression) > style > inherited
    > default, with `Value_source::style`.
  - Style chain. A source is itself an object with a style, so the style
    layer of an object is the chain of the LOCAL values of its style, that
    style's style, and so on, nearest first: the first local value found on
    the chain is the style value. A value a style inherits from its own
    tree is not style - only what a style holds itself reaches its users -
    so the chain is a chain of local layers. `set_style` refuses (false,
    logged, nothing changes) a source whose chain reaches the object, the
    source being the object included, so a chain never cycles;
    `style_chain_reaches` is the same test for a picker leaving a candidate
    out. A local edit anywhere on the chain reaches every user that reads
    the chain past it, because the propagation forwards a style-sourced
    change to the source's own users the way it forwards a local one. A bridged property (D18) is
    always local and ignores a style entry (the node transform). A style entry for an inherits-flagged property is the
    object's effective value and so flows to descendants exactly as a
    local value would: the inheritance walk, the descendant notification
    and the tree-change snapshot treat "has a local value" as "has a local
    or style value". `set_style` notifies, through the normal path
    (batches, D19 callbacks, observers, descendants), every property in
    the union of the old and new source's local values whose effective
    value or source changes; local values are untouched and shadow the
    style (WPF `IsSetOnContainer`). A source keeps the list of its users
    (`set_style` registers, the copy of a user registers the copy, a
    user's destructor unregisters); when the source's local layer changes
    for a property, `notify` forwards the change to every user without a
    local value of it, with the user's old value taken from the old style
    value (or from the user's inherited / default value when the source
    held none) and its new value from the user's effective value, so an
    edited style is live. A sealed object (D24) rejects `set_style`. A
    copy (D10) carries the style pointer (it is authored state, WPF copies
    `StyleProperty`). `read_local_value`, `for_each_local_value` and
    `Property_set::read_local_values` stay local-only;
    `Material::operator==` also compares the style pointers.
  - Serialization. A style item and the style each material and library
    folder uses are saved by name (`doc/style-library.md` D4); a source
    that is not an item is session state. The `properties` extras of D23
    stay local-only, and a material's own values export baked as before.
  - Editor. Every item has a `style` property, a bridged object reference
    over the source (`Item_base::style_property`, `doc/style-library.md`
    D3) drawn as a row with the picker; `Style_set_operation` (item, style
    before, style after) is the undo step of the paste and MCP paths. The
    generic rows show `Source: style (<name>)` in the tooltip and the `*`
    prefix only for a local value; the context menu has `Paste Properties
    as Style` (the clipboard bag becomes a style item in the scene's
    Styles folder, named after the item it was copied from, and the style
    of every selected item, in one compound) and `Clear Style`. MCP:
    `set_item_style(item, source_item)` makes the style item from the
    source item's local values, `clear_item_style(item)`, and
    `get_item_properties` reports `style` (the name or `null`) on the
    item, the `style` row, and `style` as a property source.
  - Material library. `add_default_materials` creates one style item
    `Brushed metal` (roughness, metallic, BxDF model, circular brushed
    metal, anisotropy control) in the library's Styles folder and gives
    each metal only its base color as a local value plus that style, so
    the twelve materials share the traits and a material edited in the
    Properties window keeps its override when the style is swapped or
    cleared. `Material` has a name-only constructor that writes no local
    values; the `Material_create_info` constructor keeps writing every
    value field as a local value (a full snapshot, which would shadow a
    style).

- D26 Computed properties (WPF read-only dependency property whose value
  the owner provides; R6).
  - Context. `Property_key<T>` (D3) is the write permission for a
    stored read-only property, and nothing registers one: every read-only
    candidate (world transform, world bounds, child count) is
    derived state that its owner already keeps or can compute on demand
    (`Node_transforms::world_from_node`, `Mesh::get_aabb_world`,
    `Hierarchy::get_child_count`). Storing it through a key would keep a
    second copy in the entry store and cost a validate / store / notify
    write on every transform update of every node in a moving subtree (R14).
    `Property_key<T>` stays for a read-only property whose value is authored
    state with no other home; computed properties use a value provider.
  - Library. `Property_metadata::compute` (`Compute_callback =
    std::function<Property_value(const Dependency_object&)>`), registered
    through `Property<T>::register_computed(name, owner_type, compute,
    metadata)`, which sets `read_only`. A computed property reads
    `compute(*this)` on every `get_value` with `Value_source::computed`
    (`c_str` `computed`); it has no entry, so
    `has_local_value` is `false`, `read_local_value` is `nullopt`,
    `for_each_local_value` skips it, and with that `Property_set`, copy
    and paste, `operator==` of items, the glTF `properties` extras and a
    copy of the object (D10) never see it. The style layer, inheritance,
    validate and coerce do not apply: the provider's value is the effective
    value. Every write path (`set_value`, `set_current_value`,
    `clear_value`, `set_expression`, `apply_local_state`) is rejected by
    the read-only check (D7 style: one logged error,
    `false`, nothing changes). `default_value` stays the type's zero value
    and is not shown.
  - Push. The owner calls `invalidate_dependents(property)` where the
    provider's inputs change, exactly as bridged storage does (D22), so an
    expression reading a computed property re-evaluates on the change;
    with no dependents that is one null check. Changed callbacks and
    observers do not fire for a computed property: the store never holds
    its previous value, so `Property_changed_args` could not be filled. A
    consumer that needs to react to one reads it through an expression on a
    stored property of its own, or keeps polling its serial (the geometry
    graph `transform_from_node` of D21 stays a poll for this reason).
  - Writable. A computed property may carry a setter
    (`Property_metadata::compute_set`, `Compute_set_callback`) registered
    through the `register_computed` overload that also names the stored
    property the setter writes (`Property_metadata::compute_writes`); the
    property is then not read-only. `set_value` and `set_current_value`
    hand the value to the setter after the sealed check and validation
    and return `true`; the setter writes the stored property, which
    notifies as usual, and the computed property has no entry, no
    previous value and no notification of its own. `clear_value`,
    `set_expression` and `apply_local_state` with no value or an
    expression stay rejected (one logged error, `false`): there is no
    local layer. The value it presents is still the provider's
    (`Value_source::computed`), so a set through the setter is read back
    through the compute. The editor and the MCP server record an
    undoable edit of such a property as a `Property_set_operation` of the
    stored property (`make_computed_write_operation`,
    `Dependency_property_rows::recorded_property`): the value goes
    through the setter at once and the operation carries the stored
    property's local state before and after, so undo restores exactly
    that state; the row's tooltip names the property it writes
    (`Writes: intensity`) and `get_item_properties` reports it as
    `writes`. The context menu offers no expression entry for a computed
    property, writable or not. The Light flux slider (section 4.3) is the
    user: flux over intensity and the emission solid angle, its setter
    writing intensity.
  - Node. `world_translation` (vec3), `world_rotation` (quat) and
    `world_scale` (vec3) read the components of `world_from_node_transform()`
    (group `World`, tooltips naming the world space); `child_count` (int)
    reads `get_child_count()`. `Node::handle_transform_update` invalidates
    the three world properties next to the three bridged ones: it runs on
    the node that was written and, through `Scene::update_node_transforms`
    and `Node::update_transform`, on every descendant whose world transform
    the pass recomputes, so a child's `world_translation` follows a parent
    move one propagation pass later, which is when the value itself
    changes. `Hierarchy::handle_add_child` / `handle_remove_child`
    invalidate `child_count`; the property is registered by `Hierarchy`
    with its own owner type, so `Node` and `Content_library_node`, the
    two classes deriving from it, both list it.
  - Mesh. `world_bounds_min` and `world_bounds_max` (vec3, group `World`)
    read `get_aabb_world()`; a mesh whose box is invalid (no primitives)
    reports `0 0 0` for both. `Mesh::handle_node_transform_update` and
    the primitive mutation paths (`clear_primitives`, `add_primitive`,
    `set_primitives`, the primitive-data change notification) invalidate
    both. A skinned mesh's posed bounds move with its joints without any of
    these running; reading the property gives the current box, an
    expression on it follows only the pushes above.
  - Editor (D12). A computed row draws its widget disabled like any
    read-only row, with no `*` prefix (no local value), `Source: computed`
    in the tooltip, no `Default:` line, and the context menu's per-property
    entries (`Reset to default`, `Edit as expression`, `Remove expression`)
    disabled through the existing read-only test; the bag entries (copy,
    paste, style) act on the item and stay. MCP `get_item_properties` reports `source`
    `computed`, `local` `null` and `read_only` `true`; `set_item_property`
    refuses it as read-only. Expression references (D22) reach a
    computed property through `find_for_type` and `get_value` with no
    special case: `{cube/world_translation.y}` is a valid source.

- D27 Owner type id (WPF per-class `DependencyObjectType` keying). The
  registry key is a per-class `Owner_type` id (`erhe_property/owner_type.hpp`)
  with a parent link, not the `Item_type` bit mask: bits are shared by
  every graph node kind and carry no ancestor relation, so they can
  neither give a torus node and a sphere node different parameter sets nor
  resolve a metadata override unambiguously. Id 0 is the root that every
  `Dependency_object` belongs to; `allocate_owner_type(parent, name)`
  appends to the registry's id table. An object reports its id through
  `Dependency_object::get_property_owner_type()`. `Item<Base,
  Intermediate, Self>` allocates `Self`'s id under
  `Intermediate::property_owner_type()` in a function-local static, so
  every class in an `Item<>` chain has an id that follows its C++
  inheritance with no code of its own (`Mesh` -> `Node_attachment` ->
  `Item_base` -> root, `Node` -> `Hierarchy` -> `Item_base` -> root);
  `Item_base::property_owner_type()` is the id under the root that every
  item descends from. A class outside an `Item<>` chain (the geometry
  graph node kinds derive plainly from `Graph_editor_node`) keeps a
  `property_owner_type()` function-local static allocated under its base's
  id plus a `get_property_owner_type()` override; a runtime-defined kind
  allocates one id per kind under its class id (the texture graph
  descriptors, section 4.5). Lookup and listing walk the chain:
  `Property_registry::find(id, name)` is the exact registration,
  `find_for_object(id, name)` tries the object's id then each ancestor and
  the nearest registration wins (a derived-class registration shadows a
  base one by name), and `for_each_property_of_object(id)` lists the
  root's registrations first down to the object's own, each level in
  registration order, a shadowed name or a multiply-owned property
  (`add_owner`) once at its nearest level. Ids are run-local: nothing
  persists them, properties serialize by name everywhere (glTF extras,
  MCP, clipboard); the id's name serves logs only. `Item_type` bits keep
  serving `Item_filter`, `is<T>`, the content library cache and the
  editor's type masks, and the library knows nothing of them.

- D28 Object references. `Object_reference` (the last `Property_value`
  alternative, `Property_type::object`) holds a strong
  `std::shared_ptr<Dependency_object>` compared by identity: the same
  lifetime the members it replaced had (a material keeps its textures
  alive, a mesh its materials), so a copy of the object (D10) shares the
  pointee and the store holds exactly what the member held. The library
  knows `Dependency_object`, not `Item_base` (D1).
  - Class restriction. The registration's validate (R4, value only)
    rejects a non-null pointee that is not of the owner's accepted class
    (`Member_value_traits<std::shared_ptr<U>>` supplies it for a
    member-backed registration, D18). For the editor, `Property_ui`
    carries `reference_item_types` (the `Item_type` mask the row accepts,
    opaque to the library) and `show_clear_button`.
  - Text form. `to_string` is the pointee's `get_reference_path()`, a
    `Dependency_object` virtual that is the inverse of
    `resolve_expression_object` (`Hierarchy`: the item's path,
    `doc/usd-compatibility-plan.md` M1; `Item_base`: the item name; empty
    for a null reference), and the pointer an `Object_reference` stores comes
    from `get_shared_reference()` (`Item_base`: `shared_from_this`, null
    for an item no `shared_ptr` owns, which no reference can then hold).
    The library's context parse (D16) walks from the referencing object's
    `Item_host`; the editor resolves the text itself through
    `resolve_reference_by_name` (`scene/item_lookup`), the lookup in the
    scene `find_scene_root_for_item` finds for the item - its host, else
    the content library that lists it, else the asset manager's defining
    record - because an asset-typed item such as a material has no host.
    On glTF load the `properties` maps are applied before the items have
    a host and before the import's operations create the library items a
    reference may name, so `erhe::gltf` records such a value in
    `Gltf_data::unresolved_object_properties` instead of applying it, and
    the editor's `import_gltf_editor_state` appends one undoable operation
    per entry, after every item-creating operation, that resolves the text
    with `find_item_in_scene_by_reference` and sets the local value (a
    node-held `Node_physics.physics_material` survives a reload this way).
    Both forms resolve everywhere a stored reference is read: a text
    holding `/` is a path and every other text is a name, so a file
    written before paths existed keeps loading. A name is not unique,
    exactly as for D22 references; a path is, and MCP also disambiguates
    with `reference_id`.
  - Editor write funnel. `apply_item_property` applies an object value
    only when the referenced item belongs to the target's scene, or is a
    manager-owned asset (material, brush, animation) the asset manager
    reports cross-scene referenceable; a scene-hosted texture, graph
    texture or node never crosses scenes (the manager treats every item
    without a defining record as referenceable, which is meant for
    assets). A failing check logs a warning naming both items and
    applies nothing. `Property_set_operation` and
    `Property_set_apply_operation` adopt an `Asset_reference` usership
    for every managed asset in their before and after states at first
    execute (asset-manager plan R5.4) and report those items from
    `collect_item_references`.
  - Rows (D12). The row is `item_reference_imgui`: a drop target
    filtered by `reference_item_types`, a drag source, a picker over the
    target scene's items of that mask - the content library items, and
    the scene's nodes and node attachments for a node-typed reference
    (`collect_reference_candidates`, a caller-owned scratch cleared after
    the draw) - and a clear button when `show_clear_button` holds; it
    commits on selection like the enumeration combo.
  - Everything else applies as to any type: inheritance, styles, coerce,
    computed and read-only, `Property_set` bags (a pasted bag shares the
    pointee), and `operator==` of items.

- D29 Property sub-objects. A `Dependency_object` an item owns by value
  that is not an item itself - a `Mesh_primitive` - is addressed as
  (item, index) through three `Item_base` virtuals with empty defaults:
  `get_property_sub_object_count()`, `get_property_sub_object(index)`
  (const and non-const) and `get_property_sub_object_label(index)`. The
  index is stable for the item's current list. `Property_set_operation`
  carries the optional index (D11), `Dependency_property_rows::
  add_sub_object_rows` draws a sub-object's registered properties with
  the same edit, undo and context-menu machinery (the bag entries - copy,
  paste, style - act on items and are not offered), MCP lists
  `sub_objects` and takes `sub_object` (D13), and a sub-object of a
  sealed item (D24) is as sealed as the item, checked in the editor
  funnel because the sub-object has no seal of its own. Section 4.9 is
  the user.
- D30 Secondary owner type. `Dependency_object::
  get_secondary_property_owner_type()` (default nullopt) names a second
  owner type whose properties the object may hold as local values for its
  inheritance descendants to read. A *secondary property* of an object
  (`Property_registry::is_secondary_property`) is a non-attached property
  registered on the secondary type, one of its ancestors or one of its
  descendants, not on the object's own owner chain, and neither bridged
  nor computed for the object (a bridge and a compute callback read the
  state of the registering class, which the object is not). For the
  same reason a holder never runs the registering class's
  `property_changed` callback for a secondary property (`deliver` skips
  it; the virtual hook and the observers still run) - the callback runs
  on the descendant of that class when the change propagates to it.
  `for_each_secondary_property` lists the secondary type's own chain
  first, then the own registrations of each descendant type in
  allocation order. It is addressed on the object by its qualified name
  (`Property_registry::find_for_object(object, name)` and
  `qualified_name(object, property)`, the object forms of D3's lookup and
  naming), so `Material.base_color` on a folder and `base_color` on a
  material name the same registration. The D12 rule lists a secondary
  property on the object when it holds an own value - local, or from
  its style (its `visible_when` belongs to the secondary type's objects
  and is never evaluated on the holder), Add Property offers every
  secondary property the rule does not list, "Remove Property" and the
  inline "x" clear a local one, MCP lists and writes it by the qualified
  name, and the glTF extras carry it under that name. Reading is D8
  unchanged: an `inherits` property of a descendant with no local value
  walks up to the holder. The users are the content-library folder
  (`doc/content-library-folders.md` D8): a Materials folder holds
  `Material` values for the materials below it, which is why every
  `Material` value property of section 4.1 is registered `inherits`;
  and the scene node (section 4.2), whose secondary owner type is
  `Item_base` so that it holds the values of every other item class
  (`Light.color` on an empty node, `Mesh.shadow_cast` on a group) for the
  prims and attachments below it, which is why every `Light` property of
  section 4.3 is registered `inherits`. `Item_base` rather than a narrower
  type because those classes no longer share one base: a `Mesh` is a prim
  under `Xformable` (`doc/usd-compatibility-plan.md` C5) while `Light` and
  `Camera` are attachments. Section 6 records the registration-time check this makes
  wanted.
- D31 Per-object default. `Property_metadata::compute_default`, when
  bound, is the property's DEFAULT layer for an object: the value it
  returns for that object replaces `default_value` wherever the default
  layer is read (`Dependency_object::get_base_value`,
  `get_effective_value_below_style`, and the public
  `get_default_value(property)` that the Properties window tooltip, its
  "*" marker and the MCP `default` field ask). `default_value` stays the
  registration-time default, so the type check and the enumeration
  fallback are unchanged. Every layer above the default - inherited,
  style, expression, local - still wins, `Value_source::default_value` is
  what an object without one of those reports, and the object gets no
  entry, so an unauthored value is not a local value and is not
  serialized. A per-object default is below every inherited layer, so it
  never changes a descendant's effective value: when its inputs move, the
  owner reads the effective value and its source first and then calls the
  protected `refresh_computed_default(property, old_value, old_source)`,
  which notifies THIS object only (`notify_self`, the queue-or-deliver
  half of `notify` without the inheritance propagation). The user is
  `Item_base::purpose_property` (`doc/usd-compatibility-plan.md` M3),
  whose default is derived from the item's editor-only flag bits and
  refreshed by `Item_base::set_flag_bits`.

- D32 A local value is an authored value. `Value_source::local` means
  the value was authored - by a user edit, or by a file that carried an
  opinion for it - and `Value_source::default_value` means nothing
  authored it; the two states are the same distinction a USD layer draws
  between an authored opinion and a schema fallback
  (`doc/usd-compatibility-plan.md` M4). An importer that fills an item
  field by field cannot say "the file left this alone", so every field it
  writes becomes a local value;
  `erhe::property::clear_default_valued_local_properties(object)` restores
  the distinction afterwards. It clears every stored local value that
  equals the object's own default layer (D31, `get_default_value`),
  skipping bridged (D18), computed (D26), attached (R7), read-only,
  write-sealed (D24), non-`serialize` and expression-driven (D22)
  properties, and it leaves a local value that shadows an inherited (R8),
  style (D25) or reference (D33) layer alone so no effective value moves. The pass is
  format independent: `parse_gltf` runs it over every parsed node, mesh,
  light, camera and material after the native glTF fields and before the
  `ERHE_*` extension pass whose `properties` maps carry the authored local
  set, and `erhe::usd`'s importer runs it at the end of its conversion
  (reading USD's own authored / fallback distinction instead is that
  plan's step I2). A whole-value snapshot applies the same rule at its
  source rather than relying on the pass: `Material::set_values` and
  `Material::set_data` clear the local value of a field equal to the
  default, because a snapshot cannot say "inherit".
  `Property_flags::native_gltf` marks the registrations whose value the
  glTF exporter writes through a native glTF field or a typed `ERHE_*`
  field whenever it differs from the default, so a property serializer
  writes no value entry of its own for them; the flag is data only, and
  `doc/property-inventory.md` owns the list of registrations that carry
  it.
- D33 Reference layer (a USD reference arc, `doc/usd-compatibility-plan.md`
  X2).
  - Context. An instance of a template is the template's structure with
    per-item overrides, not a copy of its values: the values the template
    supplies sit in a layer of their own, below the instance's own local
    values, so a local value inside an instance is an override and
    clearing it exposes the template's value again. USD composes a
    reference arc weaker than inheritance, so the layer sits between style
    and inherited (R3).
  - Library. `Dependency_object::set_reference(std::shared_ptr<const
    Dependency_object>)` / `get_reference()` install one reference source -
    the counterpart - per object, with `Value_source::reference`. nullptr
    clears. `get_reference_user_count()` is the source's user count and
    `reference_chain_reaches` the chain test a picker asks.
  - What the layer reads. The reference layer of an object is the value
    its counterpart SUPPLIES ITSELF: the counterpart's base value when
    that value's source is local, expression, computed, style or
    (recursively) reference, and nothing when the counterpart would fall
    to its own inherited or default layer (`get_supplied_value`, the layer
    walk of `get_base_value` stopped before the inherited branch). A value
    the counterpart inherits from its own tree is not carried, because the
    instance's own tree provides inheritance: the instance mirrors the
    template's structure, and an override on an instance ancestor reaches
    the instance's descendants through the ordinary inherited walk, which
    a reference layer on every descendant would shadow. It is also what a
    USD reference arc composes: the target prim's opinions, not those of
    its ancestors.
  - Chain. A counterpart is itself an object with a reference, so what it
    supplies includes what its own reference supplies, nearest first;
    `set_reference` refuses (false, logged, nothing changes) a source
    whose reference chain reaches the object, the source being the object
    included, so a chain never cycles.
  - Inheritance and notification. A reference value of an inherits-flagged
    property is the object's effective value and flows to descendants
    exactly as a local value would: the inheritance walk, the descendant
    notification and the tree-change snapshot treat "has a local value" as
    "has a local, style or reference value" (`has_own_value`).
    `set_reference` notifies, through the normal path (batches, D19
    callbacks, observers, descendants), every property in the union of
    what the old and the new source supply whose effective value or source
    changes; local and style values are untouched and shadow the
    reference. A source keeps the list of its users (`set_reference`
    registers, the copy of a user registers the copy, a user's destructor
    unregisters); when a value it supplies changes, `notify` forwards the
    change to every user without a local or style value of that property,
    with the user's old value taken from the old supplied value (or from
    the user's own value below the reference layer when the source
    supplied none) and its new value from the user's effective value, so a
    template edit is live the way a style edit is (D25).
  - Local layer, copy and sealing. A bridged property (D18) is always
    local and ignores the reference layer. `read_local_value`,
    `for_each_local_value` and `Property_set::read_local_values` stay
    local-only, so an instance's overrides are exactly its local values.
    Default elision (D32) keeps a local value that shadows a reference
    value, as it keeps one shadowing an inherited or style value. A copy
    (D10) carries the reference pointer as it carries the style pointer. A
    sealed object (D24) rejects `set_reference`.


## 4. Implementation

Each subsection is the design of one owner's migration; the per-field
list of what each owner registers, with storage kinds, is
`doc/property-inventory.md`, updated together with these subsections.

### 4.1 Material

The following former `Material_data` fields are registered properties of
`Material` (owner type `Material::property_owner_type()`), with the previous initializers
as defaults: `base_color` (vec3), `opacity`, `roughness` (vec2), `metallic`,
`reflectance`, `emissive` (vec3), `ior`, `transmission`,
`normal_texture_scale`, `occlusion_texture_strength`, `double_sided` (bool),
`alpha_cutoff`, `use_circular_brushed_metal` (bool), `use_aniso_control`
(bool), and the enumerations `normalmap_encoding` (`Normalmap_encoding`),
`bxdf_model` (`Bxdf_model`), `blending_mode` (`Material_blending_mode`) and
`circular_brushed_metal_texgen_mode` (`Texgen_mode`), each with an
`Enum_info` table placed next to its existing `c_str` in
`erhe_primitive/enums.hpp` (D2a), with the tables defined next to the
`c_str` implementations in `primitive.cpp`. Validate callbacks reject
values outside [0, 1] for `opacity`, `metallic`, `transmission`,
`alpha_cutoff` and `occlusion_texture_strength`; `ior` has no validate so a
file value outside the UI slider range still loads.

`double_sided`, `blending_mode` and `bxdf_model` register with
`affects_draw_list_partition`, `normalmap_encoding` and `bxdf_model` with
`affects_shader_variant`, so the D11 hook owns the rebuilds. Each migrated
field carries the `Property_ui` block of the hand-written row it replaced
(ranges, color presentation for `base_color` and `emissive`).

Every value property above is registered `inherits`, so a material with
no local value for it reads the closest content-library folder that
holds one (D30; a material's own values are local, set at construction
or by an edit, and "Reset to default" on the material is what lets a
folder value through). A save writes effective values into the glTF
material, so after a round trip every value is local again; the folder
keeps its own values in `ERHE_scene` `library_folders`.

`Material_data` keeps only `texture_samplers`, every field of which
mirrors a property (below). `Material` exposes typed
getters and setters for every migrated field (`get_base_color()`,
`set_base_color()`, ...) implemented on the property store, so call sites
(`Material_buffer` upload, shader variant derivation, glTF import/export, the
material preview, MCP tools, tests) use the accessor and nothing else
changed. `operator==(Material)` compares the remaining `Material_data`, the
property bags of D17 and the style pointers (D25).

The five texture slots are object properties (D28) in the entry store,
registered `inherits` like the value properties: `base_color_texture`,
`metallic_roughness_texture`, `normal_texture`, `occlusion_texture` and
`emissive_texture` (group `Textures`, `reference_item_types` texture |
graph texture, the PBR slots `visible_when` lit, validate
`Member_value_traits<Texture_slot>::validate` so only a
`Texture_reference` pointee is accepted), flagged `affects_shader_variant`
because a bound slot selects the texture-using variant and a normal
texture's two-component flag rides the texture. The slot transforms are
entry-store properties the same way: `<slot>_texture_texgen_mode`
(`Texgen_mode`, flagged `affects_shader_variant` because `Shader_key`
selects the texgen variant from it), `<slot>_texture_uv_rotation`
(`angle_degrees`), `<slot>_texture_uv_offset` and `<slot>_texture_uv_scale`
(vec2), each in the `<Slot> Texture` UI group and `visible_when` the slot
is bound (the PBR slots also lit). Each slot field of
`Material_data::texture_samplers` is a mirror of its property's effective
value - local, inherited from a content-library folder (D30) or the
default - kept current by `Material::on_property_changed`, so the
per-frame readers (`Material_buffer::gather_texture`, `Shader_key::derive`)
and the glTF export read the member with no variant access and see a
folder's texture like the material's own. The slot sampler is seven
entry-store properties the same way: `<slot>_texture_wrap_u`, `_wrap_v`
(`Sampler_address_mode`), `_min_filter`, `_mag_filter` (`Filter`),
`_mipmap_mode` (`Sampler_mipmap_mode`), `_max_anisotropy` (1..16) and
`_lod_bias` (developer-only), in the `<Slot> Sampler` group, `visible_when`
the slot is bound, mirrored into the slot's `Material_sampler_state`
(plain data whose defaults are those of `Sampler_create_info`). The
renderer resolves the GPU sampler from the state
(`Material_sampler_cache` in `Material_buffer`: one `Sampler` per
distinct state, created on first use, so equal states share one object
and the texture heap keys by it); the glTF export dedups its samplers by
state the same way, and the importer's `bind_material_textures` writes
the parsed glTF sampler into the slot with `set_slot_sampler`. No
`Sampler` object is ever stored on a material, so no writer needs a
device. `erhe::primitive` links `erhe::graphics` publicly for the sampler
enums the state names.
Writers of a live material use `set_base_color_texture()` and the other
setters, `set_slot_texture(slot, texture)` and `set_slot_sampler(slot,
state)` for a slot held by pointer (the glTF importer, the graph-texture
binders, `Rendertarget_mesh`), `set_value` on a slot property, or
`set_data(Material_data)` (what `Material_change_operation` applies:
every slot field through its property in one change batch). A
`Material_data` cannot say "inherit", so a slot field at its default - an
unbound texture, an identity transform, a default sampler field - clears
the local value and any other value becomes local; the constructor seeds
the store from `Material_create_info.data` by the same rule. The
Properties window draws every material row as a generic row.

### 4.2 Node

`Node` registers `translation` (vec3), `rotation` (quat) and `scale` (vec3)
with the identity defaults, flagged `affects_transform`, as bridged
properties (D18) over `Node_data::transforms.parent_from_node`: `get` reads
the `Trs_transform` component, `set` writes it and runs the same
`update_world_from_node` + `handle_transform_update` sequence as
`set_parent_from_node`, so every existing transform writer (transform tool,
physics writeback, animation, glTF import, MCP) is unchanged and every
property writer (Properties rows, `Property_set_operation`, MCP
`set_item_property`, `scene.set_property`) reaches the same state. The
transform's skew component stays internal. `world_from_node` stays a
derived cache; the world components are exposed as computed properties
(D26), not stored ones.

`Animation_sampler::apply` keeps writing the `Trs_transform` directly; the
bridged properties read that storage, so playback and the property view
agree. Playback still overwrites the authored transform; the animated layer
that would preserve it is section 6 work.

R14 holds by construction: no per-frame path changed.

`Node::get_secondary_property_owner_type()` is `Node_attachment::
property_owner_type()` (D30): a node holds the properties of every
attachment class by qualified name (`Light.color`, `Camera.fov_y`) for
the attachments below it - directly on it and on its descendant nodes -
to inherit, and takes a style whose target is an attachment class
(`Item_base::style_applies`, `doc/style-library.md` D3). The
`Item_base` chain is the node's own, so `visible` and its siblings stay
plain node properties, and `Mesh`'s computed world bounds are left out
by the D30 rule. The editor's `Style` item names the root type the same
way and so holds every class's values (`doc/style-library.md` D2). `ERHE_node` carries the held values in its `properties`
map by qualified name and the node's style by name (`style`).

### 4.3 Light

Every `Light` public field except `layer_id` is a registered property
(owner type `Light::property_owner_type()`) stored in the entry store, the Material way
(section 4.1), every one `inherits` (a light without a local value reads
its node chain, section 4.2 / D30), with the previous initializers as
defaults: `light_type`
(`Light_type`, `Enum_info` table `c_light_type_enum_info` defined next to
`Light::c_type_strings` in `light.cpp`, labels Directional / Point / Spot;
named `light_type` with `get_light_type()` / `set_light_type()` because
`get_type()` is the `Item_base` type-bits accessor),
`color` (vec3, color presentation), `intensity` (float, logarithmic slider
0.01..20000, tooltip stating the KHR_lights_punctual units: lux for
directional, candela for point and spot), `temperature` (float, slider
0..12000, 0 = off), `range` (float, logarithmic slider 1..20000),
`inner_spot_angle` and `outer_spot_angle` (float, `angle_degrees`
presentation, 0..pi, `visible_when` the light is a spot light) and
`cast_shadow` (bool). `layer_id` stays a plain member: it is a resolver
output, not authored state. No property carries a validate callback:
`is_active()` already interprets non-positive range, intensity and spot
angles as "inactive", so a file that stores them must still load.

Every `Light` property registers with one shared `property_changed`
callback (D19) that calls the light-set re-resolve; `Light::notify_changed()`
is private and only called from that callback.

`Light` exposes `get_light_type()` ... `set_cast_shadow()` accessors on the
store (the same shape as Material's); every call site (glTF import / export,
scene builder, light buffer, shadow renderer, light set, lightmap baker,
light mesh, brush preview, MCP tools, debug visualizations, icon set, sky
renderer, example, tests) reads through them. The photometric helpers
(`get_solid_angle`, `get_luminous_flux`, `set_luminous_flux`,
`get_effective_color`, `is_active`, `casts_shadow`) read the store.
`Light(const Light&, for_clone)` copies only `layer_id`; the entries
copy through D10.

The derived rows are computed properties (D26): `flux` (float,
logarithmic slider 0.01..200000, label `Flux (lm)`, `visible_when` the
light is a point or spot light) reads `get_luminous_flux()` and is
writable, its setter `set_luminous_flux` writing `intensity`, which the
editor records for undo; `blackbody` (vec3, color presentation, read-only,
`visible_when` temperature is positive) reads `blackbody_color(temperature)`.
The shared changed callback invalidates both for expressions before it
re-resolves the light set, so `{flux}` in a formula follows an intensity
change. `Properties::light_properties` keeps only the zero-range warning
for point lights, a diagnostic; every other Light row comes from the
generic section.

### 4.4 Camera

`Camera` registers its `Projection` fields as entry-stored properties, the
Material way (section 4.1), every one `inherits` (a camera without a local
value reads its node chain, section 4.2 / D30, so an empty node or a style
holds `Camera.fov_y` for the cameras below it). `Camera::projection()`
returns a const `Projection` that mirrors the effective values:
`Camera::on_property_changed` refreshes the mirror on every change of a
`Camera` property, whatever its source, so the renderers, the shadow fit
and the XR views keep reading a plain struct. Writers use the setters
(`set_fov_y()` ... `set_infinite_z_far()`, `set_projection(const
Projection&)` for a whole projection); the XR per-frame projection update
calls `set_projection` and pays nothing while the values are unchanged
(the store early-outs on an equal value). The properties, all in the
`Projection` UI group:
`projection_type` (`Projection::Type`, table `c_projection_type_enum_info`
defined next to `Projection::c_type_strings` in `projection.cpp`),
`fov_x`, `fov_y`, `fov_left`, `fov_right`, `fov_up`, `fov_down`
(`angle_degrees`, each `visible_when` the current projection type uses it),
`ortho_left`, `ortho_width`, `ortho_bottom`, `ortho_height`,
`frustum_left`, `frustum_right`, `frustum_bottom`, `frustum_top`
(logarithmic sliders 0..1000, `visible_when` per type as the hand-written
rows switched), `z_near`, `z_far` (logarithmic sliders 0..1000) and
`infinite_z_far` (bool, `visible_when` perspective types). The
`visible_when` callbacks read the mirror and are evaluated on `Camera`
objects only.

`exposure` and `shadow_range` (logarithmic sliders 0..800000 and 1..1000)
inherit the same way; `get_exposure()` / `set_exposure()` /
`get_shadow_range()` / `set_shadow_range()` read and write the store. The
clone constructor copies `m_projection`; the entries copy through D10.
`ERHE_camera` keeps writing every projection field explicitly and the
`properties` map of local values; on load the map is the camera's
complete local set, the same rule as `ERHE_light`
(`doc/gltf_extensions/ERHE_camera.md`).

`Properties::camera_properties` is gone: every row it drew is a generic
row now.

### 4.5 Graph nodes (geometry and texture)

Every geometry graph node kind registers its parameters as properties
keyed on its own owner type id (D27) with `register_member` over its
existing members (D18), `editor::mark_node_dirty`
(`src/editor/graph_editor/graph_node_property_bridge.hpp`) as the
`after_set` hook and `Mesh_torus_node` as the recipe: a `property_owner_type()`
function-local static allocated under `Graph_editor_node`'s id, a
`get_property_owner_type()` override, one
static `Property<T>` member per parameter whose `Property_ui` carries the
canvas widget's ranges, and `flags = Property_flags::none` because the
graph asset's JSON (`write_parameters` / `read_parameters`) stays the
only serializer - the members are the storage, so evaluation (including
the off-main-thread shadow-clone snapshot) and serialization are
untouched by the migration. Enumeration parameters carry `Enum_info`
tables built from the canvas combo labels; per-mode rows use
`visible_when` (the Conway operator floats, the transform node's
rotation representations, the lattice cage bounds). A parameter whose
canvas edit has a side effect keeps it in a hand-written bridge `set`
(the lattice `auto_fit` cage freeze and `divisions` offset resample).

Hosting and reachability: graph nodes share their owning asset's
`Item_host` (the `Scene_root` that hosts the asset through the content
library; `Graph_asset::set_item_host` forwards it, the owning-setter
covers nodes added later, and `erase_node` clears it so a node held only
by the undo stack cannot dangle). A D22 expression on a node parameter
therefore resolves scene items as same-host sources under one item-host
lock - `{cube/world_translation.x}` on a torus radius re-bakes the mesh
when the cube moves, pushed through `Node::handle_transform_update`,
with no polling - and `find_item_in_scene` also walks the library's
graph assets and their nodes, so `Scene_root::find_hosted_item` and the
MCP property tools reach graph nodes by name or id.

Writers and undo: canvas `imgui()` widgets keep writing the members
directly plus `mark_dirty()`, and their undo stays the whole-node JSON
`Graph_parameter_operation` (one per edit gesture); the generic rows in
the Node Properties window, MCP `set_item_property` and expression
deliveries write through the bridge (one `Property_set_operation` per
edit, reset-to-default and formulas included). The two undo owners do
not interact: `Graph_editor_node::on_property_changed` refreshes the
committed JSON baseline when a parameter property changes outside a
canvas gesture, and `Graph_editor_node::mark_dirty` re-runs expressions
reading the node (`invalidate_dependents` behind a `has_dependents`
check; shadow clones never have dependents, so the worker path stays one
null check). A canvas drag on an expression-driven parameter is
overwritten at the next delivery, the same way the gizmo behaves on a
driven translation (D22).

The Node Properties window draws the generic rows (D12) for any node
whose owner type id is not `Graph_editor_node`'s own (a kind with
registered properties); what cannot be a property - `Asset_reference`
pickers, the lattice control point editing - draws through the node's
`unregistered_parameters_imgui` override in an `Extra` row, and nodes
with no registered properties (transform_from_node, passthrough, join,
unary-op, groups, brush source) keep the hand-rolled `Parameters` entry.

Texture graph: the parameters are per *descriptor*, not per C++ type
(`Texture_descriptor_node` holds a `Parameter_value` vector parallel to
its `erhe::texgen::Node_descriptor`), and the descriptors are
function-local statics built on first use, so registration cannot ride
C++ static initialization: `register_texture_graph_properties()`
(`src/editor/texture_graph/texture_graph_properties.cpp`), called once
from `run_editor()` while still single-threaded (the registry's write
window, D3 / R12), allocates one owner type id per descriptor under
`Texture_descriptor_node`'s id and registers
one property per float / color (vec4) / enum / bool / size parameter
plus the seed of a seeded descriptor, with `Enum_info` tables built from
the descriptor labels (pointer-stable process-lifetime storage) and
bridges reaching the node's `Parameter_value` by parameter index.
Gradient and curve parameters have no `Property_value` form and stay in
`imgui()`.

### 4.6 Usage

- Editor. The Properties window draws the generic rows for every selected
  item (D12): source and default in the tooltip, `*` prefix on a local
  value differing from the default, `=` prefix on a driven row, mixed
  multi-selection, and the context menu (Reset to default, Copy / Paste
  Properties, Paste Properties as Style, Clear Style, Edit as expression,
  Remove expression). Every edit is one undoable operation.
- MCP tools: `get_item_properties`, `set_item_property` (with `value` or
  `expression`), `set_item_style`, `clear_item_style`; `lock_items` /
  `unlock_items` toggle the seal; `set_item_flags` rejects the derived
  bits (`visible`, `shadow_cast`, `lightmapped`) with a hint to use
  `set_item_property`. Undo is verifiable through `undo` / `redo` and
  `get_undo_redo_stack`.
- Command scripts: `scene.set_property` (`config/editor/commands.json`,
  `doc/command_script.md`) with `item`, `property` and `value` or
  `expression`, using the D16 string conversion (enumerations by label).
- Registering a new property: follow the D3 example; `Material`
  (section 4.1, stored, with member mirrors) and `Camera` (section 4.4,
  hand-written bridge) are the recipes. An enumeration needs its
  `Enum_info` table next to the `c_str`; a member that already exists is
  `register_member` (D18); a reference to another item is an
  `Object_reference` property (D28) with `reference_item_types` set.

### 4.7 Tests

- `src/erhe/property/test/` (gtest): registry and overrides (owner type
  chains included: listing order, shadowing, nearest-ancestor overrides,
  `add_owner` on the chain), every
  value type (the integer vectors included), validate, coerce, change
  notifications and batching, inheritance through a `Test_object` tree,
  observers, enumerations, string conversion, property sets, bridged
  storage, expressions (integer-vector targets and component sources
  included), sealing, styles, computed properties, object references and
  `register_member` (`test_object_reference.cpp`).
- `src/erhe/item/test/`: `test_properties.cpp` (inheritance through
  `Hierarchy`, reparent, clone), `test_item_visibility.cpp` (derived
  bits), `test_item_sealing.cpp` (`lock_edit` seal sync).
- `src/erhe/scene/test/`: `test_node_properties.cpp` (bridged transform),
  `test_attachment_inheritance.cpp` (mesh inherits from its node),
  `test_light_properties.cpp`, `test_camera_properties.cpp`,
  `test_node_computed.cpp` (world properties, `child_count`, mesh bounds).
- `src/erhe/primitive/test/test_material_style.cpp` (the `Brushed metal`
  style), `test_material_textures.cpp` (the texture slot properties).
- `src/erhe/scene/test/test_mesh_primitive_material.cpp` (the primitive
  material property, the sub-object addressing, the owner stamp across
  copies).

### 4.8 Notes and gotchas

- Every binary that initializes `erhe::item` logging must call
  `erhe::property::initialize_logging()` first; the first library log
  call otherwise dereferences a null logger (bit the editor, the example
  and the test mains).
- `Node_attachment::set_node` holds a `shared_ptr` to itself for its
  duration: `Node::~Node` / `Node::detach` reach it through a raw pointer
  and `handle_remove_attachment` can erase the last owning `shared_ptr`
  mid-call, and the inheritance snapshot apply touches the object after
  that point.
- `Material_preview::render_preview(texture, material)` generates the
  mipmap levels below the rendered one after the render; without that a
  thumbnail sampled at a fraction of its size reads uninitialized memory
  (was solid magenta on Metal).
- Behavior changes from the inherited `visible` default (D23): `grid_tool`
  "Add Grid" creates a visible grid (it previously relied on the omitted
  bit); `Quad_view` hides its rendertarget node explicitly at
  construction.
- The camera's glTF `properties` extras object repeats `exposure` /
  `shadow_range` next to the native `ERHE_camera` fields; both import to
  the same value.
- glTF export bakes a light's `temperature` into the exported color
  (KHR_lights_punctual has no temperature), so an export / import round
  trip matches on every light property except `temperature`.

### 4.9 Mesh primitives

`Mesh_primitive` derives from `Dependency_object` with its own owner type
(allocated under the root, named `Mesh_primitive`) and registers `material`
as an object property (D28) with `register_member` over its `material`
member (`reference_item_types` material, no clear button: a primitive keeps
a material). The registration's `after_set` calls the owning mesh's
`notify_primitive_material_changed()`, the `Scene_host::
on_mesh_material_changed` notification `set_primitive_material` ran inline
before; `Mesh::set_primitive_material` writes the property, so it stays the
one writer (draw list material set plan R4) and the generic rows, undo and
MCP reach the same funnel. Each primitive carries an owner link (mesh,
index) that `Mesh::stamp_primitive_owners()` writes after every change of
the primitive list - `add_primitive`, `set_primitives`, the clone
constructor and the move operations - so the link survives the vector
copies. The mesh addresses its primitives as property sub-objects (D29):
the count, `&m_primitives[index]`, and the primitive's name (`Primitive N`
when it has none) as the label; the Properties window's `Primitive N`
groups draw the sub-object rows.

Storage rule: `Mesh::m_primitives` is a `std::vector`, and a reallocation
copy-constructs the `Dependency_object` base, which keeps neither
observers nor expression dependents (D10). A `Mesh_primitive` therefore
registers member-backed properties only (no entries to lose) and nothing
subscribes an observer or an expression to a primitive; `get_primitives()`
is const, so no caller reaches `set_value` on a primitive except through
`Mesh`.

### 4.10 Node_physics

The editor's `Node_physics` attachment registers its authored rigid body
state as entry-stored properties the Light and Camera way (sections 4.3,
4.4; owner type `Node_physics::property_owner_type()`, UI group `Rigid
Body`), every one `inherits` (D30), so an empty node above or a style
holds `Node_physics.physics_material`, `Node_physics.motion_mode`, ...
for every body below it. The `IRigid_body_create_info` the attachment
keeps and its intended motion mode are MIRRORS of the effective values:
`Node_physics::on_property_changed` refreshes the mirror for every
source of a change (local, style, inherited) and applies the consequence
in the same place, so the body is still (re)created from a plain struct
at scene attach. The constructor that takes a create info writes a local
value for each field that differs from the property default and leaves
the rest unset, so a body created with the defaults is open to a holder.

The attachment holds what describes this body instance: its role and the
`KHR_physics_rigid_bodies` motion fields. `motion_mode`
(`erhe::physics::Motion_mode`, `Enum_info` table `c_motion_mode_enum_info`
next to `c_motion_mode_strings` in `erhe_physics/irigid_body.hpp`, the
four authorable modes) mirrors into the intended mode, re-derives the
effective mode and sets it on the body; `is_trigger` mirrors into the
create info and recreates the body; `gravity_factor` (0..2, `visible_when`
the mode is not static; the callback casts to `Node_physics` and is
evaluated on bodies only) mirrors and sets the body's gravity factor
while the body is not static; the two initial velocities (world space,
applied at (re)creation) mirror with no live consequence; `mass`
(validated positive) with a value from any source stores it in the create
info and scales the body's local inertia with the mass ratio, and with no
value anywhere (source `default`) leaves the create info's mass unset and
recreates the body so it is back at its shape mass scaled by the material
density (section 4.12); `center_of_mass_offset` is realized as the
offset-center-of-mass wrapper around the collision shape (rewrapped and
recreated on a change, and re-applied by `set_collision_shape()` around
a new shape). `physics_material` and `collision_filter` are object
properties (D28, `reference_item_types` the physics material and
collision filter type bits; `find_item_in_scene` walks the content
library's physics materials and collision filters so a name resolves)
that mirror into the create info and set the body's material or filter.
What describes the kind of matter - friction, restitution, damping, wind
receptivity, density - is the material's (section 4.12): the attachment,
the create info and `ERHE_physics` hold no such scalar of their own, and
a body without a material behaves like one with the material defaults;
`reapply_physics_material()` / `reapply_collision_filter()` push the
current one again after the referenced item itself was edited, because a
write of the pointer the mirror already holds is a no-op (R4): the
material observer of section 4.12 calls the first, and
`scene/physics_edits` walks the scenes for the second.

The typed accessors (`set_motion_mode()`, `set_trigger()`, `set_mass()`,
`set_gravity_factor()`, ... `set_collision_filter()`) write through the
properties, so the MCP `edit_physics_body` tool, the glTF physics import
overrides, the geometry graph mesh binding and the Properties rows all
notify; the physics tool's drag-time overrides of friction, damping and
gravity go to the rigid body only and are transient (a body's own
friction takes part in contact resolution only while it has no
material). The glTF exporters read the accessors, not the live body;
`ERHE_physics` writes `motion_mode` explicitly and the `properties` map
of local values, and on load the map is the attachment's complete local
set (`clear_local_properties_not_listed`, the `ERHE_light` rule), the
object references keeping the identity the KHR collider gave the create
info (`doc/gltf_extensions/ERHE_physics.md`).
`Properties::node_physics_properties` keeps only the diagnostics (body
label, position, active state, collision shape, local center of mass and
inertia).

### 4.11 Grid and Brush_placement

`Grid` (the editor's grid attachment, owned by `Grid_tool` and optionally
attached to a node) registers its settings as entry-stored properties,
the Material way (section 4.1), every one `inherits` (a grid without a
local value reads its node chain, section 4.2 / D30, so a node or a
style holds `Grid.cell_size` for the grids below it): `plane_type`
(`Grid_plane_type`, `Enum_info` `c_grid_plane_type_enum_info` next to
`grid_plane_type_strings`), `center` and `rotation` (degrees,
`visible_when` the plane is not the Node plane), `intersect_enable`,
`snap_enabled`, `cell_size` (logarithmic 0.01..10), `cell_div` (1..10),
`cell_count` (the snap region bound), the four `level<N>_color` and
`level<N>_width` rows (group `Lines`) and `label_enable` /
`label_text_fraction` / `label_spacing` / `label_fade` / `label_color`
(group `Axis Labels`). The private members the render, snap and
intersection paths read are a mirror of the effective values:
`Grid::on_property_changed` refreshes them on every change of a `Grid`
property, whatever its source, re-derives the grid transform after
`plane_type`, `center` or `rotation`, and touches the
`Editor_settings_store` the owning `Grid_tool` handed the grid (D19), so
a property edit from any writer schedules the settings autosave that
`Grid_tool::write_config` feeds from the mirror; `Grid::read_config`
writes the config fields as local values through the store (before the
store is handed over, so the load schedules no autosave). The `visible`
flag (an `Item_base` property) reaches the same store through
`handle_flag_bits_update`. The clone constructor copies the mirror; the
entries copy through D10. The
Grid window draws the name field and the Node plane's attach / detach
buttons by hand and everything else as generic rows through its own
`Property_editor` and `Dependency_property_rows`, so a grid edit is a
`Property_set_operation` with undo; a grid selected as an item gets the
same rows in the Properties window.

`Brush_placement` registers `brush` as an object property (D28,
`reference_item_types` the brush type bit, validated to null or a
`Brush`) and `facet` and `corner` as developer-only integers (-1 is
`GEO::NO_INDEX`), all three entry-stored and inheriting (D30). The
members `get_brush()` / `get_facet()` / `get_corner()` read are a mirror
refreshed by `Brush_placement::on_property_changed`; `set_corner()` and
the placing constructor write the store, the constructor only where an
argument differs from the property default so a default-constructed
placement stays open to a holder. Placements are not persisted, so
there is no carrier to keep in step.
`Properties::brush_placement_properties` keeps the brush's polygon
count diagnostics.

### 4.12 Physics_material

`Physics_material` (`erhe::physics`, the KHR_physics_rigid_bodies material
item; the content library's Physics Materials category holds them, with
"Create Physics Material" on the folder, the Operations window and the
MCP `create_physics_material` making new ones and the "Default" item
`add_default_physics_materials` adds to every scene) describes how a kind
of matter behaves and registers its nine fields as entry-stored,
inheriting properties (owner type
`Physics_material::property_owner_type()`, so a folder or a style holds
them, D30) with the `c_default_*` constants of
`erhe_physics/physics_material.hpp` (also what a body without a material
behaves like) as the property defaults. The `KHR_physics_rigid_bodies`
fields: `static_friction` and `dynamic_friction` (0.6, validated
non-negative; the extension and the physics sense set no upper bound, so
a file value above the 0..1 slider still loads), `restitution` (0,
validated to [0, 1]) and the enumerations `friction_combine` and
`restitution_combine` (`Combine_mode`, `Enum_info` table
`c_combine_mode_enum_info` next to the enumeration in
`erhe_physics/physics_material.hpp`, defined in `physics_material.cpp`).
The erhe fields, which the KHR material has no carrier for and which
ride `ERHE_scene` `physics_materials` as the material's local values:
`linear_damping` and `angular_damping` (0.05, validated to [0, 1]),
`wind_receptivity` (0 kg/s, validated non-negative; the scene wind reads
it each fixed step) and `density` (1, validated positive; the mass of a
body without an explicit mass is its shape mass scaled by it).
The typed accessors (`get_static_friction()`, `set_static_friction()`, ...)
read and write the store, and every reader - the Jolt body snapshot, the
glTF physics import and export, the MCP physics tools, the scene wind,
the default library material - uses them.

The material is the only carrier of these scalars: neither `Node_physics`
nor `IRigid_body_create_info` holds one of its own (section 4.10; the
body keeps its own mass, and a brush-placed body's mass is explicit). The
consequence of an edit stays backend-neutral. A backend may snapshot the
material per body at `IRigid_body::set_physics_material()` (Jolt does,
so its contact listener reads the values without locks; the same call
applies the damping to the body's motion properties and, while the body
has no explicit mass, re-derives the mass from the density), and the
material does not know the bodies that reference it, so the holder of
the reference pushes it again: the editor's `Node_physics` subscribes an
any-property observer (D21) to its material whenever its
`physics_material` property is set (and in its constructors, which copy
the create info), with `reapply_physics_material()` as the callback; the
token lives in the attachment, so the callback never outlives it, and a
material edit from any writer - Properties row, `Property_set_operation`,
MCP `edit_physics_material` or `set_item_property`, glTF import - reaches
every live body through the `IRigid_body` interface. The Properties
window draws the material as generic rows only.

### 4.13 Layout

`Layout` (`erhe::scene`, the node attachment that arranges its node's
children inside a volume) registers its parameters as entry-stored
properties, the Material way (section 4.1), every one `inherits` (a
layout without a local value reads its node chain, section 4.2 / D30, so
an empty node or a style holds `Layout.gap` for the layouts below it),
owner type `Layout::property_owner_type()`, UI group `Layout`: `type`
(`Layout_type`, `Enum_info` `c_layout_type_enum_info` next to the
enumeration in `erhe_scene/layout.hpp`, defined in `layout.cpp`),
`volume_min` and `volume_max`, `primary`, `secondary` and `tertiary`
(`Axis_direction`, `Enum_info` `c_axis_direction_enum_info`, signed axis
labels `+X` .. `-Z`), `gap` (0..10000 per component) and
`grid_track_count` (1..1000, validated to at least 1 per axis,
`visible_when` the type is grid). The private members `Layout::update()`
reads each frame are a mirror of the effective values:
`Layout::on_property_changed` refreshes them on every change of a
`Layout` property, whatever its source. `get_layout_type()`,
`set_layout_type()` and the other typed accessors read the mirror and
write the store, so the glTF `ERHE_layout` import, the export and any
editor writer notify. The accessor is named `get_layout_type()` because
`get_type()` is the `Item_base` virtual item type. The clone constructor
copies the mirror; the entries copy through D10. `ERHE_layout` keeps
writing every field explicitly and the `properties` map of local values;
on load the map is the layout's complete local set, the same rule as
`ERHE_light` (`doc/gltf_extensions/ERHE_layout.md`). The per-track
extent lists (`get_grid_track_extent(axis)`) are not properties: the
Properties window keeps their custom / per-track rows and draws
everything else as generic rows.

### 4.14 Layout per-child hints (attached properties)

`Layout` registers the per-child hints as attached properties (R7, D3,
the first attached-property user; WPF `Grid.Row`): `align_x`, `align_y`,
`align_z` (`Layout_alignment`, `Enum_info` `c_layout_alignment_enum_info`
in `erhe_scene/layout.hpp`), `margin_min`, `margin_max`, `grid_cell_auto`,
`grid_cell` (validated non-negative) and `grid_span` (validated to at
least 1 per axis), UI group `Layout Item`, qualified `Layout.align_x` ..
`Layout.grid_span`, holder type `Node`. The value is set on the child
`Node`; `Node` knows nothing about layouts, and `Layout::update()` reads each direct child's
values (a child without local values gets the defaults). Each hint's
`visible_when` is "the object is a Node whose parent node has a Layout",
the grid hints additionally "that layout is a grid" and `grid_cell` "and
`grid_cell_auto` is off", so the D12 rule lists the rows on exactly the
children a layout arranges. There is no per-child attachment class,
catalog entry or hand-written row; a hint rides the child node's `ERHE_node` properties map (D14) by
its qualified name, and the glTF importer reads a legacy `ERHE_layout`
`layout_item` block into the attached values.

### 4.15 Rendertarget_mesh

`Rendertarget_mesh` (the editor's mesh that carries a render target
texture as its material) has its own owner type under `Mesh`'s (D27,
`Rendertarget_mesh::property_owner_type()`) and registers its size as
read-only computed properties (D26, group `Rendertarget`, not
serialized): `width` and `height` (the texture size in pixels; the mesh
is that over the pixels per meter, in meters) and `pixels_per_meter`
(set at creation). The size is
authored through `resize_rendertarget`, which needs a device, a command
buffer and the mesh memory, so it is not a property setter;
`resize_rendertarget` invalidates the two size properties for
expressions. The Properties window draws them as generic rows; it has no
hand-written rendertarget rows.

### 4.16 Animation

`Animation` (`erhe::scene`) registers its keyed time range and its
sampler and channel counts as read-only computed properties (D26, group
`Animation`, not serialized): `first_time` and `last_time` over
`get_first_time()` / `get_last_time()`, `sampler_count` and
`channel_count` over the two vectors. The samplers and channels stay
public vectors edited in place by the animation editing functions and
the glTF loader, so the values are always current on read and
`Animation::notify_keyframes_changed()` is the push for expression
readers: `reset_channel_seek_state` (the tail of every keyframe edit in
`src/editor/animation/animation_edit.cpp`) and the channel creation
call it. `Properties::animation_properties` keeps only the "Open in
Animation Window" button; the generic section draws the four rows.

### 4.17 Node_joint

`Node_joint` (the editor's physics joint attachment, section 4.10's
sibling) registers `connected_node`, `joint_settings` and
`enable_collision` (UI group `Joint`). `connected_node` is a node-typed
object reference (D28, `reference_item_types` the node bit, validated to
null or a `Node`) bridged (D18) over the weak member: attachments detach
only in `Node::~Node`, so a strong node-to-node reference through two
joints connected to each other's nodes would be a cycle no scene close
breaks; the bridge keeps the joint's reference weak, which makes the
property local only (no holder, no inheritance). The bridge's set refuses
the joint's own node with a warning; `set_connected_node` invalidates
the property for expressions. `joint_settings` (an object reference to a
shared `Physics_joint_settings`, a content-library item) and
`enable_collision` (bool) are entry-stored and inherit (D30), so a node
or a style holds them for the joints below. The members are a mirror
refreshed by `Node_joint::on_property_changed`, which also rebuilds the
constraint (the former setters' consequence); the setters write the
store, and the placing constructor writes local values only where an
argument differs from the property default. The KHR_physics_rigid_bodies
joint carrier keeps writing the effective values and reading them back
as local ones (it carries no `properties` map). `Properties::
node_joint_properties` keeps the "Connect to Selected Node" and "Rebuild
Joint" actions and the constraint state diagnostic; the generic section
draws the three rows.

### 4.18 Migration recipe

How a remaining member-backed registration or hand-written row becomes
an entry-store property, the verification it passes, and the traps
already hit. `doc/property-inventory.md` owns the per-field status and
is updated in the same commit as every registration change.

#### Recipe for a bridged owner

The Light (section 4.3), Camera (section 4.4), Node_physics (section
4.10) and Layout (section 4.13) migrations are the template; a
member-backed (bridged, D18) registration is always local, so no node,
folder or style can hold it and no descendant inherits it, and moving it
to the entry store is what makes it holdable. A property that models "this body
instance" belongs to the attachment; one that models "what kind of matter
this is" belongs to the shared material (Node_physics kept the KHR motion
fields and its role, the physics material took damping, wind receptivity
and density) - settle that split before registering anything.

- Register every field with `register_property` in the entry store,
  `.inherits = true`, the previous initializer as `.default_value`. An
  enumeration passes its `Enum_info` table.
- Keep the engineered struct the readers use (the rigid body create info,
  the `Projection`) as a MIRROR of the effective values: override
  `on_property_changed` (the virtual hook), test the property's owner
  type against the class's own with `is_owner_type_or_descendant`, and
  refresh the mirror from `get_value`. The hook runs for every source of
  a change - local, style, inherited - so a value held by the node above
  reaches the struct.
- Route every writer through setters (`set_x()` per field, plus a whole
  struct setter where writers set several at once). The non-const
  accessor to the struct goes away; `grep -rn "->struct()"` finds the
  writers. A per-frame writer (the XR camera) calls the setter and pays
  nothing while the value is unchanged: the store early-outs.
- Keep the consequence a change has (recreate the body, set the body's
  damping) in the same hook, where the old `after_set` did it.
- `visible_when` callbacks may keep casting to the class: the D12
  listing rule never evaluates them on a holder (a holder lists a
  secondary property by its own value, D30).
- A `property_changed` METADATA callback (as opposed to the virtual hook)
  also casts to the class; `deliver` skips it on a holder. Prefer the
  virtual hook.
- Object references (D28) stay `Property<Object_reference>`; a holder
  resolves the reference by identity, and the picker's candidate list
  comes from `collect_reference_candidates`. On glTF load a reference in
  an `ERHE_node` / `ERHE_light` / `ERHE_camera` `properties` map resolves
  late through `Gltf_data::unresolved_object_properties` (D28); an owner
  with a native KHR carrier for the reference (Node_physics: the
  collider's material and filter) keeps the identity the constructor got
  and lets its own map decide only whether the value stays local.
- A constructor that takes an engineered struct (a create info) writes a
  local value only for the fields that differ from the property defaults,
  so a default-constructed instance stays open to a holder.
- A value that is "unset" in the engineered struct (an optional mass)
  maps to source `default`: test `get_value_source` in the hook instead
  of adding an "is set" property.
- glTF: the owner's extension keeps writing its explicit fields and the
  `properties` map of local values; on load the map is the item's
  complete local set (`clear_local_properties_not_listed`, the rule
  `ERHE_light` and `ERHE_camera` follow), so a value inherited from the
  node survives a reload. A post-load pass that "widens" or "adjusts" a
  value (the scene-open content fit) writes a LOCAL value only
  (`has_local_value`), never one that comes from a node or a style: the
  editor-state operations that assign styles run after such passes.
- Update `doc/property-inventory.md` (storage kind `entry`, `inherits`)
  and the owner's subsection in `doc/property-system.md` in the same
  commit. Add a test where the owner has a test directory
  (`src/erhe/scene/test/test_camera_properties.cpp` is the shape:
  defaults, setter to mirror, untyped access with enum labels, node-held
  value inherited into the mirror, clone).

#### Recipe for a hand-written row

The Material recipe of section 4.1 for a stored value; for a derived
value a writable computed property (D26, the Light flux slider of
section 4.3 is the shape): `register_computed` with the provider, a
setter that writes the stored property, and that property, so the
generic row edits it and undo records the stored property's change.
Delete the hand-written row in the same commit; the generic section
draws it.

#### Verification

Headless, from the repo root, the loop of `erhe-headless-verify`, for
every migration:

1. `cmake --build build_vs2026_vulkan_headless --target editor --config Debug`,
   launch with `ERHE_AI_DRIVER=1`, wait for `Main loop: completed frame 12`
   in `logs/log.txt`.
2. Over `scripts/mcp_call.py` (or a small `urllib` script; `scene_name`
   is required by the node tools, item ids reshuffle per launch):
   `get_addable_item_properties` on an empty node lists `<Owner>.<name>`
   for every migrated field; `set_item_property` of one on the node and
   `null` on the attachment below makes the attachment read it with
   source `inherited`; `create_style` plus `set_item_property` of
   `<Owner>.<name>` on the style and the style on the node reads
   `inherited` on the attachment too; the engineered struct reflects it
   (`get_node_details` / the owner's query tool); `save_scene`,
   `open_scene`: sources are still `inherited`; `close_scene` and
   `scene-close check: all N released` in the log.
3. `./scripts/build_ninja_win_vulkan.bat editor` links the windowed
   editor; the owner's gtest suite passes (`build_tests`, serially).
4. Hand off for the user's interactive check; do not keep headless
   testing after that.

#### Traps already hit

- A computed property of a descendant class (Mesh world bounds) listed as
  secondary on a node ran its compute on the node: the D30 rule excludes
  computed and bridged properties for that reason. Do not weaken it.
- `for_each_local_value` needs its bridged list sorted by index; the
  registration order across translation units is not the chain order.
- `find_scene("")` in the MCP server does not default to the first scene
  even where a tool's schema says so; default explicitly.
- MCP `edit_physics_body` is not undoable: an `undo` after it pops the
  previous operation.
- The Visual Studio MCP server may refuse connections; the crash fallback
  is the headless editor launched with stderr redirected to a file (the
  crash handler prints a symbolized backtrace there).

## 5. Out of scope

Kept out deliberately, as they are the WPF parts that serve XAML UI rather
than a scene model: templates and triggers, `UncommonField`, dispatcher
thread affinity, the attached-property browsable attributes, and two-way
bindings (they exist for UI controls writing back to
a model; erhe's ImGui windows are immediate-mode and read the item
directly). Expressions and one-way bindings are D22, sealing is D24, the
style layer is D25 and the reference layer is D33.

## 6. Future work

- Animated value layer (WPF `SetAnimatedValue` / `GetAnimationBaseValue`):
  a layer between coerced and local in R3, stored as a second optional in
  the entry (D5), set and cleared by `Animation_sampler::apply` and
  animation stop so playback never overwrites the authored local value and
  keying (`doc/animation-keyframing-plan.md`) reads the local value as the
  authored pose. No prerequisites; the keyframing plan and non-destructive
  playback of generalized animation channels (below) both wait on it.
- Property serialization to glTF: expression text of driven properties
  (D22), material local values (materials export field by field, and
  default elision plus `Material::set_values` keep a round trip from
  turning effective values into local ones - D32 - but a local value that
  no native field carries, such as `reflectance`, is still lost), and one
  carrier per item type in place of the `properties` / `mesh_properties`
  members scattered across `ERHE_node`, `ERHE_light` and `ERHE_camera`
  (D14, D23). Future work that is not yet fully planned:
  `doc/gltf-properties-extension-plan.md` is the draft, explicitly
  incomplete and not ready to implement; the decisions it records so far
  live there and nowhere else. Its `native_gltf` flag and its elision pass
  are implemented (D32); the `ERHE_*_properties` extensions are not.
- Style users beyond the content library's style items (D25): the graphics presets once
  `Graphics_settings` is an item with registered properties.
- Further computed properties (D26) as their consumers appear: a node's
  world bounds over its subtree, a scene's item counts.
  `Rendertarget_mesh`'s size (section 4.15) and `Animation`'s time range
  and counts (section 4.16) are ones already.
- Entry storage for the geometry graph and texture graph node parameters
  (section 4.5), the one member-backed (D18) family left whose values a
  node or a style could hold and a descendant inherit (D30): a bridged
  property is always local, so it is neither offered on a holder nor
  inherited. Low value - sharing a node parameter through a style is
  rarely wanted - and large (59 member registrations across 16 files plus
  the texture graph bridges), so it is done only when a user asks for it;
  the recipe is section 4.18. `doc/property-inventory.md` lists the
  remaining hand-written rows (the "Not yet migrated" table: list-valued
  state with no property form). `Node`'s transform stays bridged for the
  reasons D18 gives.
- Shader graph (`src/editor/graph/`) node parameters as properties. The
  oldest graph editor has no parameter serialization and no undo
  operations at all; migrating it starts with adopting the
  `Graph_editor_node` base (or retiring the prototype), not with
  registrations.
- Editor per-item state as attached properties registered by the editor:
  item tree expansion, sheet-window formulas. The naming, lookup and
  listing side exists (D3, D12, section 4.14); each needs its own
  `visible_when` and a registering owner type on the editor side.
- Animation channels targeting arbitrary properties (not only node TRS),
  which becomes possible once `Animation_channel` stores a
  `Dependency_property` index instead of `Animation_path`. For playback
  that does not overwrite the authored local value it also depends on the
  animated value layer (above); without it a generalized channel would
  clobber local values the way TRS playback clobbers the transform today.

## 7. Verification workflow (macOS, Metal build tree)

- Configure once after adding files: `bash scripts/configure_xcode_metal.sh`
  (or `cmake .` inside `build_xcode_metal` when only CMake lists changed).
  The generated Xcode project lists sources from the CMake files; an
  undefined-symbol link error after adding a file means the project is
  stale, re-run the configure script.
- Build: `cd build_xcode_metal && xcodebuild -project erhe.xcodeproj -target
  <target> -configuration Debug -parallelizeTargets -quiet`, targets
  `erhe_property_tests`, `erhe_item_tests`, `erhe_scene_tests`,
  `erhe_primitive_tests`, `erhe_scene_renderer_tests`,
  `erhe_scene_renderer_gpu_tests`, `editor`. Run the test executables one at
  a time from `build_xcode_metal/src/erhe/<lib>/test/Debug/`.
- Editor: from the repo root `ERHE_MCP_PORT=3743
  build_xcode_metal/src/editor/Debug/editor &`, wait for
  `MCP server: listening` in `logs/log.txt`, then drive it with
  `python3 scripts/mcp_call.py <tool> '<json>'`. Useful id sources:
  `list_scenes`, `get_scene_nodes`, `get_scene_materials`,
  `get_scene_lights`, `get_scene_cameras`, `get_node_details` (attachment
  ids). `reparent_node` builds subtrees; `get_undo_redo_stack` shows the
  operations a check queued.
- `capture_screenshot` works on the Metal swapchain (the same one-frame
  arm-then-collect protocol as the Vulkan windowed build), so the visual
  check of the Properties rows is `capture_screenshot` with a `path` in
  the scratch directory, cropped to the Properties window; system screen
  capture tools are not used (they trip the endpoint security agent).
- Gotchas seen during the phase verifications:
  - Imported lights appear in `get_scene_lights` only after the import
    settles; a query in the same second lists the originals only.
  - The scene builder's insert compound sits low on the undo stack; count
    the `undo` calls a check needs instead of undoing until empty.
  - The generic rows sit below the Mesh section for a mesh-carrying node;
    select an empty node for a Properties-window screenshot.
  - VirtualCity is not used for measurements on Metal: a present-stall
    hang there can wedge the machine.
  - The cube and cuboctahedron of the default scene are outside the
    default view; use the dodecahedron / icosahedron for visibility
    screenshots.
