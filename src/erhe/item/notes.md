# erhe::item

## Purpose

Foundational entity system for erhe. Provides identity, flags, naming, tags, parent/child hierarchy, polymorphic cloning, and type-safe filtering for all items in the scene graph and editor.

## Key Types

- **`Unique_id<T>`** - Thread-safe atomic ID generator. Non-copyable, movable. Each template instantiation has an independent counter.
- **`Item_base`** - Base class for all items. Provides ID, name, flags, tags, source path, debug label. Inherits `enable_shared_from_this` - all instances must be created via `std::make_shared`. Also derives from `erhe::property::Dependency_object` (see `src/erhe/property/notes.md`): every item carries a property store, and `get_property_owner_type()` returns `get_type()` so property metadata resolves by item type.
- **`Item_flags`** - Bitmask constants for item state (visible, selected, hovered, opaque, etc.) with `to_string()`. `visible`, `shadow_cast` and `lightmapped` (`Item_flags::derived`) are mirrors of the `Item_base::visible_property` / `shadow_cast_property` / `lightmapped_property` effective values (inherits-flagged bool properties, `doc/property-system.md` D23): `set_flag_bits` rejects them (logged, dropped); write the property (`set_visible`, `show`, `hide`, `set_value`). The bit is written by the property changed callback, so an inherited change and a tree move keep it current and every `Item_filter` / `is_visible()` reader stays a bit test.
- **`Purpose`** - USD purpose token (`default_` / `render` / `proxy` / `guide`, see "Purpose") with `c_purpose_enum_info`, the enumerator table `Item_base::purpose_property` is registered with.
- **`Item_type`** - Bitmask constants for item types (mesh, camera, light, node, etc.) used by the `is<T>()` template.
- **`Item_filter`** - Four-criteria bitmask filter (all-set, any-set, all-clear, any-clear) with AND semantics.
- **`Item<Base, Intermediate, Self, Kind>`** - CRTP template providing `clone()`, `get_type()`, `get_type_name()`. Three clone modes: copy constructor, custom clone constructor, not clonable.
- **`Hierarchy`** - Parent/child tree built on `Item_base`. Supports reparenting, depth tracking, recursive traversal (`for_each`), removal (splice or recursive), and cloning with `adopt_orphan_children()`. Implements the `Dependency_object` inheritance virtuals (`get_inheritance_parent`, `for_each_inheritance_child`) so `inherits`-flagged properties flow down the tree; `set_parent` captures an inheritance snapshot before the move and applies it after, so the subtree's property-changed notifications carry the old values. `child_count_property` is a computed property (D26, owner types `node | content_library_node`) reading `get_child_count()`; `handle_add_child` / `handle_remove_child` push it to expressions.
- **`Typed`** - A typed prim (`doc/usd-compatibility-plan.md` C5): the level of the prim class hierarchy that carries the USD `typeName` token, see "Prim classes".
- **`Scope`** - A `Scope` prim: children only, no transform, and every class's value properties as its secondary properties, see "Prim classes".
- **`Item_host`** - Abstract host for items, provides a mutex for synchronized access. `Item_host_lock_guard` falls back to a static orphan mutex when no host is available.

## Public API

### Item_base
- `get_id()`, `get_name()`, `set_name()`, `describe(level)`
- `get_flag_bits()`, `set_flag_bits()`, `enable_flag_bits()`, `disable_flag_bits()`
- `is_visible()`, `is_selected()`, `is_hovered()`, `show()`, `hide()`, `set_visible()`, `set_selected()` - `show` / `hide` / `set_visible` write a local `visible_property` value; `clear_value(visible_property)` returns to the inherited / default value
- `visible_property`, `shadow_cast_property`, `lightmapped_property` - owner type 0 (listed for every item type), default true / false / false, `inherits`
- `purpose_property`, `get_purpose()`, `derive_purpose_from_flags(flag_bits)` (static, `constexpr`) - owner type 0, `inherits`, per-object default derived from the flag bits, see "Purpose"
- `get_reference_path()` / `get_shared_reference()` - the text and `shared_from_this` an object reference (`doc/property-system.md` D28) uses to name and hold this item. `Item_base` names an item by its name; `Hierarchy` overrides it with the item's path (see "Item paths")
- `get_property_sub_object_count()` / `get_property_sub_object(index)` / `get_property_sub_object_label(index)` - property sub-objects (D29): Dependency_objects the item owns by value that the editor addresses as (item, index); the defaults report none (`Mesh` overrides them with its primitives)
- `is_lock_edit()` / `set_lock_edit()` - the `lock_edit` flag is the property-store seal (`Dependency_object::seal`, D24): while set, every local property write is refused (`set_value` returns false, logged); `is_sealed()` agrees with the flag, including after a copy
- `add_tag()`, `remove_tag()`, `has_tag()`, `get_tags()`, `clear_tags()`
- `set_source_path()`, `get_source_path()`
- `set_gltf_uid()`, `get_gltf_uid()` - glTF 2.1 unique ID (persistent file-scoped identity; assigned once at import or first export by `erhe::gltf`, never changed afterwards, NOT copied by copy/clone)
- `clone()` - polymorphic deep copy via CRTP
- `get_type()`, `get_type_name()` - virtual, overridden by CRTP `Item<>`

### Hierarchy
- `set_parent(shared_ptr)`, `set_parent(shared_ptr, position)` - reparent with depth update
- `get_parent()`, `get_children()`, `get_depth()`, `get_root()`
- `get_path()`, `get_reference_path()` - the item's path, see "Item paths"
- `make_sibling_unique_name(parent, wanted_name, exclude)` (static), `is_name_available(name)`, `handle_sibling_unique_rename(unique_name)` - see "Sibling-unique names"
- `get_child_count()`, `get_child_count(filter)`, `get_index_in_parent()`, `get_index_of_child()`
- `is_ancestor()`
- `remove()` - splice out node, reparent children to parent
- `recursive_remove()` - remove node and all descendants
- `for_each<T>(callback)`, `for_each_child<T>(callback)` - type-filtered subtree traversal with early termination
- `adopt_orphan_children()` - fix parent back-pointers after copy construction
- `hierarchy_sanity_check()` - validates parent/child consistency and detects cycles

### Typed / Scope
- `get_prim_type_name()`, `set_prim_type_name(token)`, `get_class_type_name()` (virtual), `type_name_property` - the prim's USD `typeName` token, see "Prim classes"
- `handle_parent_update()`, `handle_item_host_update()` (virtual) - the item-host hook of the prim class hierarchy, see "Prim classes"
- `Scope::get_secondary_property_owner_type()` - the root owner type, see "Prim classes"

### Free functions
- `erhe::find_by_path(root, path)` - the item a path names below `root`, see "Item paths"
- `erhe::is<T>(item)` - bitmask-based type check (raw pointer and shared_ptr overloads)
- `resolve_item_host()`, `resolve_item_host_mutex()` - find the first non-null host among items

## Purpose

`Purpose` is the USD purpose vocabulary (`doc/usd-compatibility-plan.md`
M3): `default_` for ordinary content, `render` for the high-quality member
of a pair, `proxy` reserved for the low-cost stand-in a future proxy mesh
provides, and `guide` for editor-only content the user works WITH rather
than ON - a tool, a brush preview, a controller, a rendertarget panel.

`Item_base::purpose_property` is an `inherits`-flagged enumeration whose
DEFAULT layer is per-object (`doc/property-system.md` D31): it is
`derive_purpose_from_flags(get_flag_bits())`, which answers `guide` when any
of `Item_flags::purpose_guide_when_set` (`tool`, `brush`, `controller`,
`rendertarget`) is set or `show_in_ui` is clear, and `default_` otherwise.
So an item reports the purpose its flags already imply without authoring
anything, a local value (or one from a style, or one inherited from an
ancestor) overrides it, and clearing that value returns to the derived
value. `set_flag_bits` reads the effective value before it moves one of
`Item_flags::purpose_inputs` and refreshes the property's default layer
after, so observers, expressions and the Properties window row see the
change.

Only an authored value is a local value, so glTF writes `purpose` for an
item that authored one and nothing for every other item (the generic local
property list of `erhe::gltf::item_local_properties_to_json`).

`get_purpose()` reads the effective value; it allocates nothing and is a
pure function of the item's own flag bits whenever nothing is authored.
The editor's draw-list filters still test the individual flag bits: each of
them separates one KIND of editor-only content (the tool pass, the brush
pass, the rendertarget overlay pass), which `purpose` cannot express.

## Prim classes

Every scene is one tree of prims and the erhe class of a prim sits in a
class hierarchy that mirrors the USD schema hierarchy
(`doc/usd-compatibility-plan.md` C5). A level's class has its own
`Item_type` bit and a concrete class's static type is the OR of its chain
(`Scope::get_static_type()` is `typed | scope`), so `is<Typed>(scope)`
holds by the ordinary subset test and the property owner-type chain
(`doc/property-system.md` D27) follows the same levels. `erhe::item` holds
the two levels that need no transform and no scene:

- **`Typed`** (USD `UsdTyped`, base `Hierarchy`) carries the prim's
  `typeName` token. It is instantiated as itself for a prim whose type has
  no erhe class - `Cube`, `PointInstancer`, `SkelRoot`, a typeless `def` -
  so that prim's name, place in the tree and children survive a round trip.
- **`Scope`** (USD `Scope`, base `Typed`) holds children and nothing else:
  no transform exists on it, so a transform composes through it to the
  nearest transformable ancestor. Resources are conventionally gathered
  under one.

The token is `Typed::type_name_property`, a bridged string property
(`doc/property-system.md` D18) over `get_prim_type_name()` /
`set_prim_type_name()`, so it is always the prim's own local value and
never inherits. A class that fixes its token overrides
`Typed::get_class_type_name()` with it - `Scope` returns `"Scope"` - and
then reports that constant and refuses every write, through the bridge's
object-level `validate` for the property path and a logged error for a
direct `set_prim_type_name()`. A class that fixes none, a plain `Typed`,
returns an empty `get_class_type_name()` and carries the token an importer
authors.

Every content-library kind is a typed prim of this hierarchy directly -
`erhe::primitive::Material` (token `Material`, USD's `UsdShadeMaterial`),
`erhe::graphics::Texture`, `erhe::scene::Animation`, `erhe::scene::Skin`,
`erhe::physics::Physics_material`, `Collision_filter`,
`Physics_joint_settings`, and the editor's `Brush`, `Style`, `Graph_mesh` and
`Graph_texture`. Each fixes its own token; the kinds USD has no prim type for
carry the erhe class name as a custom `typeName`, the form a USD file writes
them in. A `Texture` is a prim of a scene only when a loader registers it as
content - a render target, shadow map or other device-internal texture is the
same class and is never placed in a tree.

A prim with no parent of its own is not yet placed in a tree, so
`Hierarchy::get_inheritance_parent()` and `Hierarchy::is_name_available()`
answer with what `Item_base` answers for it: the inheritance container that
holds it. A content-library resource inherits its folder's values and shares
its entry node's namespace through that container
(`doc/content-library-folders.md` D1).

A prim's item host is the host of the prim it is parented to. `Typed` owns
that rule: `Typed::handle_parent_update()` takes the new parent's
`get_item_host()` and `Typed::handle_item_host_update()` adopts it and carries
it to every `Typed` child, so attaching a prim anywhere in a hosted tree gives
the whole subtree below it the host, and detaching it takes the host away
again. A `Scope` between two transformable prims therefore passes the scene
host through to the prims below it, and answers `get_item_host()` with the
scene itself. `erhe::scene::Xformable` overrides the second hook with the
scene registration a transformable prim needs (see
`src/erhe/scene/notes.md`).

The levels that need a transform or a scene - `Imageable`, `Xformable`
(spelled `Node` through most of erhe), `Xform`, `Boundable` and `Gprim` - live
in `erhe::scene`, see `src/erhe/scene/notes.md` "Prim levels".

`Scope::get_secondary_property_owner_type()` is the root owner type, as an
editor `Style` item's is (`doc/property-system.md` D30), so a scope holds
any class's value properties by qualified name (`Material.roughness` on a
materials scope) and its descendants inherit them - the content-library
folder rule.

## Item paths

An item in a hierarchy has a namespace path (`doc/usd-compatibility-plan.md`
M1): the names of the item and of its ancestors below the root, outermost
first, separated by `/`. The root's own name is not part of the path, so a
child of the root is named by its name alone, a deeper item by
`Parent/Child`, and the root itself has an empty path. This is the form the
`ERHE_scene` `library_folders` entries store for content-library folders, so
one form addresses scene nodes and library folders alike, and a path is the
unambiguous identifier a name is not.

- `Hierarchy::get_path()` builds it on demand and `erhe::find_by_path(root,
  path)` inverts it: an empty path is the root, and every other path is a
  sequence of child names, each naming a child of the item the previous name
  reached. Both are cold-path (references, lookups, diagnostics) - never per
  frame.
- `Hierarchy::get_reference_path()` returns the path, falling back to the
  name when the path is empty (a root, an item outside a hierarchy), which is
  what `Item_base::get_reference_path()` returns for every non-hierarchy item.
- A stored reference text holding `/` is a path and every other text is a
  name, so a file written before paths existed keeps resolving:
  `Item_host::find_hosted_item` takes either form
  (`erhe::scene::Scene_host` walks the node tree, then names; the editor's
  `Scene_root` adds the content library through
  `find_item_in_scene_by_reference`).

## Sibling-unique names

The children of one parent hold distinct names
(`doc/usd-compatibility-plan.md` M2), so an item path names exactly one item
and is the identifier a USD prim path is.

- `Hierarchy::make_sibling_unique_name(parent, wanted_name, exclude)` is the
  rule: `wanted_name` when no child of `parent` other than `exclude` holds it,
  otherwise the first free `<base>_<number>` counting from 1, where the base is
  `wanted_name` without a trailing `_<digits>`. So a colliding `Cube` becomes
  `Cube_1`, a colliding `Cube_1` becomes `Cube_2` (not `Cube_1_1`), a gap in
  the series is filled, and a name that is nothing but `_<digits>` is its own
  base. A null `parent` imposes no namespace.
- `Hierarchy::handle_add_child` applies it, which is the one place a child
  reaches a parent: node creation, paste, duplicate, glTF import and prefab
  instantiation all attach through it, so none of them carries naming code. A
  site that names an item it has already attached calls
  `make_sibling_unique_name` itself. A site that needs the name an item was
  created with looks the item up by path or id, not by name.
- `handle_sibling_unique_rename(unique_name)` is the rename the attach
  performs. The base renames the item; the editor's `Content_library_node`
  also renames the item an owning entry wraps, because the item's name is the
  one the user sees and the entry node's name is the one the path is built
  from. `Item_base::set_name` mirrors the other way, renaming the entry node
  that wraps the item, so the two never drift apart. A reference entry lists
  an item owned by another scene and never renames it.
- Owning entries win the name over reference entries: when an owning
  content-library entry attaches to a folder where a reference entry holds the
  wanted name, `Content_library_node::handle_add_child` gives the suffix to the
  reference ENTRY NODE instead, so a scene's authored names survive a reload
  whatever order the entries attach in; between two owning entries, or two
  reference entries, the first-come rule above decides.
- `Item_base::is_name_available(name)` is the refusal side: a rename to a name
  a sibling holds is refused rather than suffixed, because the name is the
  one the user typed. `Hierarchy` answers from its siblings; an item wrapped
  by a content-library entry node answers from the entry node's siblings; every
  other item has no namespace and accepts every name. The `name` property's
  bridge validation calls it, so the Properties window row and the MCP
  `set_item_property` inherit the refusal, and the MCP `new_name` arguments
  check it directly.

## Dependencies

- `erhe::property` - `Dependency_object` base of `Item_base`
- `erhe::profile` - `ERHE_PROFILE_MUTEX` for Tracy-aware mutexes
- `erhe::utility` - `Debug_label`, `test_bit_set()`, `test_any_rhs_bits_set()`
- `erhe::log` (private) - spdlog-based logging
- `erhe::verify` (private) - `ERHE_VERIFY()` assertion macro
- `fmt` (private) - string formatting

## Implementation Notes

- All `Item_base`/`Hierarchy` instances must be `std::make_shared` due to `enable_shared_from_this`. Stack allocation will throw `bad_weak_ptr` on `set_parent()` etc.
- `Hierarchy` copy constructor cannot call `shared_from_this()` (object not yet managed by shared_ptr). Children's `m_parent` weak_ptrs are left empty and must be fixed by calling `adopt_orphan_children()` after construction, or by calling `set_parent()` which does both the fix-up and depth correction automatically.
- Copy constructor uses `set_depth_recursive()` to ensure correct depths for the entire cloned subtree.
- Tags (`m_tags`) are intentionally not copied during cloning - cloned items start with an empty tag set.
- `Item_flags::count` and `Item_type::count` are the number of defined bits, not bitmasks. The `c_bit_labels` arrays have exactly `count` entries each.

## Testing

169 unit tests in `test/` using Google Test (CPM-fetched). Run with `ERHE_BUILD_TESTS=ON`.

| File | Tests | Coverage |
|------|-------|----------|
| `test_unique_id.cpp` | 8 | ID generation, move semantics, no-copy, reset, independent counters |
| `test_item_flags.cpp` | 6 | Bit distinctness, label count, `to_string()` |
| `test_item_filter.cpp` | 14 | All four filter criteria, combined conditions, describe |
| `test_item_base.cpp` | 25 | Construction, flags, copy, source path, describe, tags |
| `test_item_crtp.cpp` | 8 | Type/name, clone modes, `is<T>()` |
| `test_hierarchy.cpp` | 35 | Construction, reparent, traversal, removal, copy/assign depth + parent correctness |
| `test_hierarchy_path.cpp` | 14 | Path build (root, child, deep, orphan), rename and reparent, `find_by_path` lookup and misses, bare-name fallback |
| `test_hierarchy_smoke.cpp` | 1 | Randomized stress test (create/reparent/remove/clone/iterate) with deterministic seed |
| `test_item_host.cpp` | 6 | Host resolution, lock guard with/without host |
| `test_properties.cpp` | 4 | Metadata by item type, inheritance through `Hierarchy`, reparent / remove re-reads, clone keeps local values |
| `test_item_visibility.cpp` | 6 | Derived flag bits follow local, inherited and tree-change values of the visible / shadow_cast / lightmapped properties; `set_flag_bits` drops derived bits; copy re-derives |
| `test_typed_scope.cpp` | 6 | Composed type bits of `Typed` / `Scope`, class type names, authored and class-fixed `type_name`, path through a `Scope`, category values a `Scope` holds for its descendants |
| `test_item_sealing.cpp` | 3 | `lock_edit` seals / unseals through every flag writer; inherited values still reach a sealed child; copies follow the copied flag |

Test harness (`main.cpp`) bootstraps `erhe::file::log_file` before `erhe::item::initialize_logging()` to break a circular dependency.
