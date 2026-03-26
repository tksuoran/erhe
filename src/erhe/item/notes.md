# erhe::item

## Purpose

Foundational entity system for erhe. Provides identity, flags, naming, tags, parent/child hierarchy, polymorphic cloning, and type-safe filtering for all items in the scene graph and editor.

## Key Types

- **`Unique_id<T>`** - Thread-safe atomic ID generator. Non-copyable, movable. Each template instantiation has an independent counter.
- **`Item_base`** - Base class for all items. Provides ID, name, flags, tags, source path, debug label. Inherits `enable_shared_from_this` - all instances must be created via `std::make_shared`.
- **`Item_flags`** - Bitmask constants for item state (visible, selected, hovered, opaque, etc.) with `to_string()`.
- **`Item_type`** - Bitmask constants for item types (mesh, camera, light, node, etc.) used by the `is<T>()` template.
- **`Item_filter`** - Four-criteria bitmask filter (all-set, any-set, all-clear, any-clear) with AND semantics.
- **`Item<Base, Intermediate, Self, Kind>`** - CRTP template providing `clone()`, `get_type()`, `get_type_name()`. Three clone modes: copy constructor, custom clone constructor, not clonable.
- **`Hierarchy`** - Parent/child tree built on `Item_base`. Supports reparenting, depth tracking, recursive traversal (`for_each`), removal (splice or recursive), and cloning with `adopt_orphan_children()`.
- **`Item_host`** - Abstract host for items, provides a mutex for synchronized access. `Item_host_lock_guard` falls back to a static orphan mutex when no host is available.

## Public API

### Item_base
- `get_id()`, `get_name()`, `set_name()`, `describe(level)`
- `get_flag_bits()`, `set_flag_bits()`, `enable_flag_bits()`, `disable_flag_bits()`
- `is_visible()`, `is_selected()`, `is_hovered()`, `show()`, `hide()`, `set_selected()`
- `add_tag()`, `remove_tag()`, `has_tag()`, `get_tags()`, `clear_tags()`
- `set_source_path()`, `get_source_path()`
- `clone()` - polymorphic deep copy via CRTP
- `get_type()`, `get_type_name()` - virtual, overridden by CRTP `Item<>`

### Hierarchy
- `set_parent(shared_ptr)`, `set_parent(shared_ptr, position)` - reparent with depth update
- `get_parent()`, `get_children()`, `get_depth()`, `get_root()`
- `get_child_count()`, `get_child_count(filter)`, `get_index_in_parent()`, `get_index_of_child()`
- `is_ancestor()`
- `remove()` - splice out node, reparent children to parent
- `recursive_remove()` - remove node and all descendants
- `for_each<T>(callback)`, `for_each_child<T>(callback)` - type-filtered subtree traversal with early termination
- `adopt_orphan_children()` - fix parent back-pointers after copy construction
- `hierarchy_sanity_check()` - validates parent/child consistency and detects cycles

### Free functions
- `erhe::is<T>(item)` - bitmask-based type check (raw pointer and shared_ptr overloads)
- `resolve_item_host()`, `resolve_item_host_mutex()` - find the first non-null host among items

## Dependencies

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

106 unit tests in `test/` using Google Test (CPM-fetched). Run with `ERHE_BUILD_TESTS=ON`.

| File | Tests | Coverage |
|------|-------|----------|
| `test_unique_id.cpp` | 8 | ID generation, move semantics, no-copy, reset, independent counters |
| `test_item_flags.cpp` | 6 | Bit distinctness, label count, `to_string()` |
| `test_item_filter.cpp` | 14 | All four filter criteria, combined conditions, describe |
| `test_item_base.cpp` | 25 | Construction, flags, copy, source path, describe, tags |
| `test_item_crtp.cpp` | 8 | Type/name, clone modes, `is<T>()` |
| `test_hierarchy.cpp` | 35 | Construction, reparent, traversal, removal, copy/assign depth + parent correctness |
| `test_hierarchy_smoke.cpp` | 1 | Randomized stress test (create/reparent/remove/clone/iterate) with deterministic seed |
| `test_item_host.cpp` | 6 | Host resolution, lock guard with/without host |

Test harness (`main.cpp`) bootstraps `erhe::file::log_file` before `erhe::item::initialize_logging()` to break a circular dependency.
