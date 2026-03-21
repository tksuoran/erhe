# erhe_item -- Code Review

## Summary
The foundational entity system for erhe, providing `Item_base` (identity, flags, naming), `Hierarchy` (parent/child tree), `Unique_id` (atomic ID generation), and a CRTP-based type system with compile-time type masks. This is a well-designed core library with comprehensive test coverage (90+ tests). The hierarchy management is robust with extensive sanity checks. The main concerns are around the complexity of the CRTP clone pattern and the shared_ptr-based ownership model.

## Strengths
- Atomic `Unique_id` generation is thread-safe and non-copyable with proper move semantics
- Thorough `hierarchy_sanity_check()` catches parent/child inconsistencies, cycle detection via `sanity_check_root_path()`
- The CRTP `Item` template elegantly handles polymorphic cloning with three modes (copy, custom clone, not clonable)
- `Item_filter` provides a flexible bitmask-based filtering system with four orthogonal criteria
- `Item_host_lock_guard` with fallback to a static orphan mutex ensures consistent locking even for unhosted items
- `for_each` templates use C++20 concepts (`std::invocable<T&>`) for clean callable constraints
- Good test coverage including hierarchy smoke tests and ASAN scripts

## Issues
- **[moderate]** `Hierarchy::set_parent(const std::shared_ptr<Hierarchy>&, std::size_t)` at hierarchy.cpp:160 calls `m_parent.lock().get()` to get `old_parent`, then immediately assigns `m_parent = new_parent_`. The locked shared_ptr from the first `.lock()` is a temporary that goes out of scope, so `old_parent` is a dangling raw pointer if no other shared_ptr keeps the old parent alive. In practice this works because the old parent is typically kept alive by the scene graph, but it is fragile.
- **[moderate]** `Hierarchy` copy constructor (hierarchy.cpp:16-32) clones children but cannot set their `m_parent` because `shared_from_this()` is unavailable during construction. The `adopt_orphan_children()` fix-up happens later in `set_parent()`. This two-phase initialization is error-prone; if `adopt_orphan_children()` is not called, orphan children have expired `m_parent` weak_ptrs.
- **[minor]** `Item_flags` and `Item_type` use `uint64_t` for bit masks but `count` is also `uint64_t` -- semantically it is a count, not a mask. Using a separate type or at least a comment would clarify.
- **[minor]** `c_motion_mode_strings` pattern in `irigid_body.hpp` is unrelated to this library but the same pattern appears here: `c_bit_labels` arrays (item.hpp:46,156) lack bounds checking on `bit_position` when used in `to_string()`.
- **[minor]** `Item_base::m_source_path` is a `unique_ptr<std::filesystem::path>` to optimize the common case (no source path). This is reasonable but `std::optional<std::filesystem::path>` would be more idiomatic in C++17/20.
- **[minor]** `for_each` template (hierarchy.hpp:60-69) constraints say `std::invocable<Hierarchy&>` but it expects the callable to return a `bool` for early termination. A more precise concept like `std::predicate<Hierarchy&>` would be better.

## Suggestions
- In `set_parent()`, lock the old parent into a local `shared_ptr` that lives for the full scope, not just the line where `old_parent` is read.
- Consider using `std::optional<std::filesystem::path>` instead of `unique_ptr` for `m_source_path`.
- Use `std::predicate` instead of `std::invocable` in the `for_each` template constraints.
- Add bounds checking in `Item_flags::to_string()` when indexing `c_bit_labels`.
- Document the two-phase parent initialization pattern in `Hierarchy`'s copy constructor with a warning about the requirement to call `adopt_orphan_children()`.
