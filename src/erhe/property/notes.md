# erhe::property

## Purpose

A port of the WPF dependency-property system (`DependencyProperty`,
`PropertyMetadata`, `DependencyObject`, `EffectiveValueEntry`,
`DependencyPropertyKey`, the `Inherits` metadata flag) restricted to the value
types erhe items need. `erhe::Item_base` derives from `Dependency_object`, so
every scene item carries a property store.

This file is the library reference: the current types and semantics of
`erhe::property`, kept in sync with the code. The design record - goal,
requirements, the decisions (the D-numbers cited below) with their WPF
mapping and rationale, the item migrations, the editor / MCP / glTF
integration, future work and the verification workflow - is
`doc/property-system.md`; the status table of every registered property
by owner with its storage kind, and of the item fields not yet migrated,
is `doc/property-inventory.md`. A change to a mechanism here updates this
file and the design decision; a change to a registration updates the
inventory (and the owner's design section when the design changed).

## Key types

- **`Property_value`** - `std::variant<bool, int, float, glm::vec2, glm::vec3,
  glm::vec4, glm::quat, std::string, Enum_value, glm::ivec2, glm::ivec3,
  glm::ivec4, Object_reference, double, glm::mat4>`; `Property_type`
  enumerators are the variant indices. `double` (`Property_type::double_floating`,
  text `double`) and `glm::mat4` (`Property_type::mat4`) are the USD value
  types (`doc/usd-compatibility-plan.md` M6): a `double` behaves as `float`
  does everywhere, a `mat4` is a whole value - not an expression target or
  source, and its Properties row is four drag rows, one per column. `Enum_value` wraps the integer of a C++ enumeration so
  generic code can tell an enumeration from an `int`. `Object_reference`
  (D28) is a strong `std::shared_ptr<Dependency_object>` compared by
  identity; its text form is the pointee's `get_reference_path()` and it
  is parsed with the context overload of `parse_value` (below). Object
  properties are neither expression sources nor targets.
- **`Enum_info`** - immutable enumerator table (label, value) referenced by
  every enumeration property of that C++ type; one `static const` table per
  enumeration, next to its `c_str()`.
- **`Dependency_property`** - one registration record: global index, name,
  type, owner type id (below), read-only / attached flags, validate
  callback, default `Property_metadata` and per-owner-type overrides.
- **`Property_metadata`** - default value, property-changed and coerce
  callbacks, `inherits`, `Property_flags` (what a change affects),
  `Property_ui` (how the Properties window draws the row) and an optional
  `Property_bridge`. Flags and UI are data the editor reads; the library
  never acts on them.
- **`Property_bridge`** - `get` / `set` callbacks that store a property in
  the object's own member instead of the entry store. A bridged property is
  always `Value_source::local`, never inherits, is coerced on every read,
  and clearing it writes the default. `Node`'s translation / rotation /
  scale are bridged onto its `Trs_transform`; the editor's geometry graph
  node parameters are bridged onto the node members (keyed on the node
  kind's owner type id, with `set` ending in the node's `mark_dirty`,
  `doc/property-system.md` section 4.5).
- **`Property<T>::register_member`** - a bridged registration built by
  the library from a pointer-to-member or an accessor lambda returning a
  reference to the member, with an optional `after_set(Owner&)` hook run
  after a write that changed the member (an unchanged write does
  nothing). `Member_value_traits<Member>` converts member and stored
  value: identity for every storable type; a `std::shared_ptr<U>` member
  is stored as an `Object_reference`, cast to and from `U`, with a
  validate rejecting a pointee that is not a `U` (instantiate where `U`
  is complete). User: `erhe::scene::Mesh_primitive::material`.
- **`Property_registry`** - function-local static registry; registration
  happens from static members of owning classes and from single-threaded
  early startup (descriptor-driven registrations whose tables are
  function-local statics, e.g. the editor's
  `register_texture_graph_properties`), before the first concurrent
  reader exists; lookups by index, exact by (owner type, name) (`find`),
  by (object, name) through the owner type chain (`find_for_object`, what
  an object of that class means by the name; a qualified `<owner>.<name>`
  resolves the attached property `name` registered by the owner type
  `<owner>` on an object of its holder type (`applies_to`) -
  `qualified_name` produces that form,
  `find_owner_type` the reverse of `get_owner_name`), enumeration of the
  non-attached properties of an object's class
  (`for_each_property_of_object`: root-first, each level in registration
  order, a shadowed name or a multiply-owned property once at its nearest
  level), of every attached registration (`for_each_attached_property`)
  and of the attached registrations an object type may hold
  (`for_each_attached_property_of`; an attached registration names its
  holder type, R7).
- **`Owner_type`** (`owner_type.hpp`, D27) - per-class owner type id with
  a parent link; id 0 is the root every `Dependency_object` belongs to.
  `allocate_owner_type(parent, name)` appends to the registry's id table;
  `get_owner_type_parent`, `get_owner_type_name` and
  `is_owner_type_or_descendant` read it. An object reports its id through
  `Dependency_object::get_property_owner_type()`: `erhe::Item<>` allocates
  one per class under its C++ parent's id, a class outside an `Item<>`
  chain keeps a `property_owner_type()` function-local static, and a
  runtime-defined kind allocates one id per kind under its class id. A
  property registered for an id applies to every descendant id; a
  registration on a descendant shadows an ancestor's of the same name.
  Ids are run-local; properties serialize by name.
- **`Property<T>` / `Property_key<T>`** - typed handles. `T` is a variant
  alternative or any C++ enumeration. `Property_key<T>` is the write
  permission for a stored read-only property.
- **Computed property** (`Property<T>::register_computed`, D26) - a
  read-only property whose effective value is `Property_metadata::compute`
  called on every read (`Value_source::computed`). It has no entry and no
  layer: not a local value (skipped by `for_each_local_value`,
  `Property_set`, copies and the glTF extras), the style, inheritance,
  validate and coerce do not apply, and every write is rejected as
  read-only. The owner calls `invalidate_dependents` where the provider's
  inputs change so expressions reading it re-evaluate; changed callbacks
  and observers never fire for it (no previous value exists). The
  overload taking a `Compute_set_callback` and the stored property it
  writes (`Property_metadata::compute_set` / `compute_writes`) makes it
  writable: `set_value` hands the value to the setter, clearing and
  expressions stay rejected (`erhe::scene::Light`'s `flux` over
  `intensity`). Users:
  `erhe::Hierarchy::child_count_property`, `erhe::scene::Node`'s
  `world_translation` / `world_rotation` / `world_scale`,
  `erhe::scene::Mesh`'s `world_bounds_min` / `world_bounds_max`.
- **Per-object default** (`Property_metadata::compute_default`, D31) - an
  ordinary entry-stored property whose DEFAULT layer is computed for the
  object instead of taken from `default_value`. Every layer above it still
  applies, an object with no authored value reports
  `Value_source::default_value` and has no entry, and
  `Dependency_object::get_default_value(property)` is what a reader asks
  for "the default of this object". When the inputs move, the owner reads
  the effective value and its source, changes the inputs, and calls the
  protected `refresh_computed_default`, which notifies that object alone
  (a default is below every inherited layer, so no descendant moves with
  it). User: `erhe::Item_base::purpose_property`, whose default follows the
  item's editor-only flag bits.
- **`Dependency_object`** - the per-object store: sparse vector of entries
  sorted by property index, binary-searched; an entry exists only for a
  property with a local value and holds that value plus its coerced value
  when the coerce callback changed it. Two virtuals serve object
  references: `get_reference_path()` (the text form of a reference to
  this object, empty by default; `Item_base` returns its name) and
  `get_shared_reference()` (the owning pointer a reference stores, null
  by default; `Item_base` returns `shared_from_this`).
- **`Observer_token`** - RAII subscription to one property on one object,
  or to every property of it (`add_observer` without a property).
- **`Expression`** (`expression.hpp`) - a compiled formula driving one
  property (`doc/property-system.md` D22): comma-separated tinyexpr
  expressions, one per component, with `{[object/]property[.x|.y|.z|.w]}`
  references; `set_expression` installs it as the local layer,
  `Value_source::expression` reports it, `Local_state` (value or
  `Expression_text`) is the exact local layer for undo. References resolve
  lazily through `resolve_expression_object` (Item_base: `""` self, `..`
  parent, a path or a name via `Item_host::find_hosted_item`); a resolved source keeps
  a dependent list and re-evaluates its targets from `deliver`, or from
  `invalidate_dependents` when its storage changed outside `set_value`
  (`Node::handle_transform_update`). Beyond tinyexpr's own functions:
  `min`, `max`, `clamp`, `lerp`, `step`, `smoothstep`, `sign`, `round`,
  `frac`, `deg`, `rad`, and the logic set `lt le gt ge eq ne not and or
  select`. Depends on `tinyexpr` (private).
- **`Property_set`** - sorted bag of (property, value): local values of an
  object, clipboard payload, diff.
- **`Property_style`** - a named, immutable `Property_set` shared through
  `std::shared_ptr<const Property_style>`; an object's style layer (D25).
- **`property_string.hpp`** - `to_string` / `parse_value` for every type;
  `parse_value(const Dependency_object& context, property, text)` resolves
  an object reference through `context.resolve_expression_object` (empty
  text is the null reference) and delegates every other type.

## Value precedence and callbacks

Effective value = coerced(base), base = local > style > inherited > default
(the default being `Property_metadata::compute_default` for the object when
that is bound, D31); a computed property (D26) bypasses all of it and reads
its provider.
The
coerced value of a local value is stored in the entry and refreshed by
`set_value` and `coerce_value`; a property without a local value is coerced
on every read. `set_value` runs validate (type check, enumeration table,
callback), stores, coerces, then notifies when the effective value or its
source changed: metadata `property_changed`, the virtual
`on_property_changed`, then observers. `Change_batch` queues notifications on
an object and delivers one per property when the outermost batch ends, with
the value before the batch and after it.

## Style

`set_style(std::shared_ptr<const Dependency_object>)` / `get_style()` (D25,
WPF Style setters): the source's local values sit between the user's local
and inherited layers (`Value_source::style`); a local value or an
expression shadows them, a bridged property ignores them. `set_style`
notifies every property either the old or the new source holds whose
effective value or source changes, through the normal path, and leaves
locals alone. A source keeps its users (`get_style_user_count`): when its
local layer changes, `notify` forwards the change to every user without a
local value of that property, so an edited style is live. A style value of
an inherits-flagged property is what descendants inherit, and it stops an
ancestor's propagation like a local value. A copy carries the style
pointer; a sealed object rejects `set_style`. `Property_style` is a named
source filled from a `Property_set`; the editor's style items
(`doc/style-library.md`) are sources of their own and are the ones a scene
saves.

## Sealing

`seal()` / `unseal()` / `is_sealed()` (D24, WPF `Freeze`): while sealed
every write of the local layer (`set_value`, `set_current_value`,
`clear_value`, `set_expression`, `apply_local_state`) is rejected like a
read-only write - one logged error, `false`, nothing changes. Reads,
inherited values and their notifications, observers and an installed
expression keep working. A copy is not sealed. A property flagged
`Property_flags::writable_when_sealed` is the exception: its writes are
accepted while sealed, for the property that owns the seal;
`is_write_sealed(property)` is the per-property check editors use in
place of `is_sealed()`. `erhe::Item_base` ties the seal to
`Item_flags::lock_edit` and flags `lock_edit_property` that way.

## Secondary owner type

`Dependency_object::get_secondary_property_owner_type()` (nullopt by
default) names a second owner type whose non-attached, non-bridged,
non-computed properties - registered on that type, an ancestor or a
descendant of it - the object may hold as local values for its
inheritance descendants to read (`doc/property-system.md` D30).
`Property_registry::is_secondary_property(object, property)` is the
predicate, `for_each_secondary_property(object, callback)` the
enumeration (the secondary type's chain, then each descendant type's own
registrations), and the object forms `find_for_object(object, name)` /
`qualified_name(object, property)` address such a property by
`<Owner>.<name>` on the holder and by its plain name on an object of the
owning type. A holder never runs a secondary property's `property_changed`
metadata callback (it belongs to the registering class); the virtual hook
and observers run as usual. The editor's content-library folder and
`erhe::scene::Node` (secondary type `Node_attachment`) are the users.

## Inheritance

`inherits` metadata makes a property without a local value read the closest
ancestor's effective value through two virtuals the object provides
(`get_inheritance_parent`, `for_each_inheritance_child`; `erhe::Hierarchy`
implements them, `erhe::scene::Node` adds its attachments as children,
`Node_attachment` names its node as parent, and an `Item_base` with no
structural parent names the container that holds it -
`set_inheritance_container`, maintained by the editor's content-library
node, which visits the item as a child). Inherited values are not cached: a read walks up until an
ancestor with a local value. A set or clear on an inherits property notifies
every descendant without a local value, stopping at descendants that have
one. A tree change uses `capture_inheritance_snapshot` before and
`apply_inheritance_snapshot` after so the subtree's notifications carry the
right old values.

## Metadata resolution

`Dependency_property::get_metadata(object_type)`: the override registered
for the nearest id on the object's owner type chain (its own id first, then
each parent up to the root), else the default metadata. Override lists are
short so each level is a linear scan, no cache.

## Authored values and default elision

A local value is an authored value (`doc/property-system.md` D32).
`clear_default_valued_local_properties(object)` is the free function that
enforces it for a caller that had to write every field to load one: it
clears each stored, serializable, non-driven local value that equals the
object's own default layer (D31), leaving bridged, computed, attached,
read-only and write-sealed properties alone, and keeping a local value
that shadows an inherited or style layer so no effective value moves. The
rule is format independent - the glTF and USD importers both run it -
which is why the function lives here and not in an importer.
`Property_flags::native_gltf` is the companion registration flag: data
only, read by serializers, listed in `doc/property-inventory.md`.

## Copy semantics

Copying a `Dependency_object` copies its entries (local and coerced values).
Observers, pending batches and inherited state are not copied.

## Threading

The registry is written during static initialization and the
single-threaded startup registrations; its lookups (`find`,
`find_for_object`, `for_each_property_of_object`, the owner type table)
take the registry mutex, uncontended after that window. Property values are
item state guarded by the item's host mutex, like the rest of the item.

## Tests

`test/` (gtest): registry and overrides, every value type, validate, coerce,
change notifications and batching, inheritance through a `Test_object` tree,
observers, enumerations, string conversion, property sets, bridged storage,
computed properties, object references and `register_member`.
