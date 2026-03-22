# Unit Test Plan for erhe::item

## Overview

Add Google Test-based unit tests for the `erhe::item` module. No test infrastructure currently exists in erhe — this establishes the pattern for other modules.

## Files to Create/Modify

### CMake Changes

**`CMakeLists.txt` (root)** — add option and enable testing:
```cmake
set_option(ERHE_BUILD_TESTS "Build erhe unit tests" "OFF" "ON;OFF")
if (ERHE_BUILD_TESTS)
    enable_testing()
endif()
```

**`src/erhe/item/CMakeLists.txt`** — append:
```cmake
if (ERHE_BUILD_TESTS)
    add_subdirectory(test)
endif()
```

**`src/erhe/item/test/CMakeLists.txt`** — new file:
- Fetch Google Test via CPM
- Define `erhe_item_tests` executable
- Link `erhe::item`, `erhe::log`, `GTest::gtest_main`
- Use `gtest_discover_tests()` for CTest integration

### Test Files (`src/erhe/item/test/`)

All instances of `Item_base`/`Hierarchy` must be created via `std::make_shared` (due to `enable_shared_from_this`).

Logging must be initialized before Hierarchy tests run — use a GTest global environment that calls `erhe::item::initialize_logging()`.

#### Test Helpers (defined in test files)

```cpp
// Concrete CRTP subclass for testing Item_base and Item<> template
class Concrete_item : public erhe::Item<erhe::Item_base, erhe::Item_base, Concrete_item> {
    using Item::Item;
    static constexpr std::string_view static_type_name{"Concrete_item"};
    static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::mesh; }
};

// Non-clonable variant
class Concrete_not_clonable : public erhe::Item<
    erhe::Item_base, erhe::Item_base, Concrete_not_clonable, erhe::Item_kind::not_clonable> { ... };

// Concrete Item_host for testing locking
class Concrete_item_host : public erhe::Item_host {
    auto get_host_name() const -> const char* override { return "test_host"; }
};
```

---

## Test Cases

### `test_unique_id.cpp`

| Test | Verifies |
|------|----------|
| DefaultConstruction_NonZero | `get_id() != null_id` |
| SequentialIds | Two IDs are different, second > first |
| NullIdIsZero | `null_id == 0` |
| NoCopy | Not copy-constructible/assignable (static_assert) |
| MoveConstructor | Destination gets ID, source becomes `null_id` |
| MoveAssignment | Same as above |
| Reset | `reset()` sets ID to `null_id` |
| DifferentTypes | `Unique_id<A>` and `Unique_id<B>` have independent counters |

### `test_item_flags.cpp`

| Test | Verifies |
|------|----------|
| NoneIsZero | `Item_flags::none == 0` |
| DistinctBits | Each flag is a distinct power of 2 |
| LabelCount | `c_bit_labels` array has exactly `count` entries |
| ToStringEmpty | `to_string(0)` returns empty |
| ToStringSingle | `to_string(selected)` contains `"Selected"` |
| ToStringMultiple | `to_string(selected \| visible)` contains both labels |
| TypeNoneIsZero | `Item_type::none == 0` |
| TypeDistinctBits | Each type is `1 << index_xxx`, all distinct |
| TypeLabelCount | `c_bit_labels` has `count + 1` entries (index 0 = "none") |

### `test_item_filter.cpp`

| Test | Verifies |
|------|----------|
| DefaultPassesAll | Default filter returns `true` for any input |
| RequireAllSet_Pass | `require_all_bits_set = A\|B`, input has both -> true |
| RequireAllSet_Fail | Input missing one -> false |
| RequireAtLeastOneSet_Pass | Input has one of the required -> true |
| RequireAtLeastOneSet_Fail | Input has none -> false |
| RequireAtLeastOneSet_ZeroMask | Zero mask = vacuously true |
| RequireAllClear_Pass | Required bits are absent -> true |
| RequireAllClear_Fail | One required bit present -> false |
| RequireAtLeastOneClear_Pass | One required bit absent -> true |
| RequireAtLeastOneClear_Fail | All required bits present -> false |
| CombinedConditions | Multiple fields set, verify AND semantics |
| Describe | Non-default filter produces non-empty description |

### `test_item_base.cpp`

| Test | Verifies |
|------|----------|
| DefaultConstruction | Non-zero ID, empty name, zero flags |
| NamedConstruction | `get_name()` matches constructor arg |
| SetName | `set_name()` changes name |
| UniqueIds | Two items get different IDs |
| EnableFlagBits | `enable_flag_bits(visible)` -> `is_visible()` true |
| DisableFlagBits | Disable after enable -> false |
| SetFlagBitsTrue | `set_flag_bits(selected, true)` sets bit |
| SetFlagBitsFalse | `set_flag_bits(selected, false)` clears bit |
| SetSelected | Toggles `is_selected()` |
| ShowHide | `show()` / `hide()` toggle `is_visible()` / `is_hidden()` |
| IsHovered | Either `hovered_in_viewport` or `hovered_in_item_tree` -> `is_hovered()` |
| MultipleFlagBits | Enabling distinct flags don't interfere |
| CopyConstruction | Copy gets new ID, name appended with " Copy", `selected` bit stripped |
| SourcePath | `set_source_path()` / `get_source_path()` roundtrip |
| Describe | Returns string containing name; higher levels include type/ID/flags |

### `test_item_crtp.cpp`

| Test | Verifies |
|------|----------|
| GetType | Returns `Concrete_item::get_static_type()` |
| GetTypeName | Returns `"Concrete_item"` |
| CloneCopyConstructor | `clone()` returns valid ptr with copied name, new ID |
| CloneNotClonable | Returns `nullptr` |
| IsTemplateFunction | `erhe::is<Concrete_item>(ptr)` true for match, false for mismatch |
| IsWithNullptr | `erhe::is<T>(nullptr)` returns false |
| IsWithSharedPtr | Shared_ptr overload works |

### `test_hierarchy.cpp`

**Tree construction:**

| Test | Verifies |
|------|----------|
| DefaultConstruction | No parent, no children, depth 0 |
| SetParent | Child's parent set, parent's children contains child |
| ChildDepth | Child depth = parent depth + 1 |
| MultipleChildren | 3 children in insertion order, `get_child_count() == 3` |
| SetParentWithPosition | Insert at position 0 -> child is first |
| SetParentNull | Orphan has no parent, depth 0 |

**Re-parenting:**

| Test | Verifies |
|------|----------|
| Reparent | Move child from A to B: removed from A, added to B |
| ReparentUpdateDepth | Depth updated recursively |

**Traversal:**

| Test | Verifies |
|------|----------|
| GetRoot | Chain root->A->B: B's root is root |
| GetRootOrphan | Orphan's root is itself |
| IsAncestor | root->A->B: B.is_ancestor(root) true, A.is_ancestor(B) false |
| GetIndexInParent | 3 children return indices 0, 1, 2 |
| GetIndexOfChild | Returns correct optional; non-child returns empty |
| GetChildCountWithFilter | Filtered count matches items with matching flags |

**Removal:**

| Test | Verifies |
|------|----------|
| Remove | Reparents children to parent, detaches node |
| RemoveLeaf | Detaches from parent |
| RecursiveRemove | Removes node and all descendants |
| RemoveAllChildrenRecursively | Node has 0 children after call |

**Iteration:**

| Test | Verifies |
|------|----------|
| ForEach | Visits all nodes in subtree |
| ForEachChild | Visits children only, not self |
| ForEachReturnFalse | Returning false stops iteration |

**Copy:**

| Test | Verifies |
|------|----------|
| CopyConstructor | Clones children recursively, independent shared_ptrs |
| CopyDoesNotCopyParent | Clone has no parent |

### `test_item_host.cpp`

| Test | Verifies |
|------|----------|
| ResolveItemHost | Returns first item with non-null host |
| ResolveAllNull | Returns nullptr when all hosts null |
| NullItemsSkipped | nullptr item pointers don't crash |
| LockGuardAcquires | `Item_host_lock_guard` acquires the mutex |

## Implementation Order

1. Root CMake + item CMake changes (option, enable_testing, add_subdirectory)
2. `test/CMakeLists.txt` (GTest via CPM, target definition)
3. `test_unique_id.cpp` — pure template, no dependencies
4. `test_item_flags.cpp` — static constants
5. `test_item_filter.cpp` — simple predicates
6. `test_item_base.cpp` — needs shared_ptr, test helpers
7. `test_item_crtp.cpp` — needs CRTP subclasses
8. `test_hierarchy.cpp` — most complex, needs logging init
9. `test_item_host.cpp` — needs concrete host

## Key Constraints

- **`enable_shared_from_this`**: All Item_base/Hierarchy instances must be `make_shared`. Stack allocation will throw `bad_weak_ptr` on `set_parent()` etc.
- **Static `Unique_id` counter**: Persists across tests. Use relative comparisons (id2 > id1), not absolute values.
- **Logging init**: Hierarchy methods call `log->trace()`. Must call `erhe::item::initialize_logging()` via GTest global environment before any Hierarchy test runs.
- **`ERHE_VERIFY` aborts**: Death tests for self-parenting etc. may be tricky on Windows (DebugBreak before abort). Consider skipping or guarding.

## Verification

```bash
cmake -B build -DERHE_BUILD_TESTS=ON -DERHE_GRAPHICS_LIBRARY=opengl ...
cmake --build build --target erhe_item_tests
cd build && ctest --test-dir . -R erhe_item
```
