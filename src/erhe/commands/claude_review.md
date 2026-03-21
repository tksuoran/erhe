# erhe_commands -- Code Review

## Summary
A comprehensive input command system supporting keyboard, mouse (button, drag, motion, wheel), game controllers, and OpenXR inputs with priority-based dispatch and state machines. The design is functional and handles complex input scenarios well, but the large class hierarchy with many binding types leads to significant boilerplate. The `Input_arguments` union is a potential source of type-safety issues.

## Strengths
- Well-structured priority system: command state (Active > Ready > Inactive > Disabled) combined with host priority ensures correct dispatch order
- Thread-safe command registration and binding via mutex
- The drag binding state machine (Inactive -> Ready -> Active -> Inactive) is well-implemented with proper edge cases handled
- Active mouse command tracking prevents mid-drag preemption
- Good use of `[[nodiscard]]` on query methods
- Comprehensive support for many input device types

## Issues
- **[notable]** `Input_arguments::Variant` (input_arguments.hpp:15-29) is a raw union with no discriminant. Accessing the wrong union member is UB. A `std::variant` would be type-safe, or at minimum a tag enum should accompany the union.
- **[moderate]** `c_state_str[]` in `state.hpp:12` is `static constexpr` in a header -- each TU gets its own copy. Should be `inline constexpr` or moved to a `.cpp` file. Also, `c_str()` in `state.cpp:9` does no bounds check on the `State` enum value before array indexing.
- **[moderate]** `Command_binding::c_type_strings[]` (command_binding.hpp:38-53) has only 13 entries but `Type` enum goes up to value 14 (`Update`). The array is missing entries for `Controller_axis` (8) and `Controller_button` (9) -- the existing entries after "Mouse_wheel" skip to "Xr_boolean" without controller entries.
- **[moderate]** `commands.cpp` has ~200 lines of commented-out `remove_command_binding()` code (lines 191-269). This dead code should be removed or tracked in an issue.
- **[minor]** `command.cpp:76` uses `static_cast<void>(input)` to suppress unused parameter warning. In C++, simply omitting the parameter name in the declaration is cleaner.
- **[minor]** Several `TODO` comments throughout (lines 241, 284, 289, 318 in command.cpp) indicating incomplete understanding of the design. These suggest the state machine could be better documented.
- **[minor]** Inconsistent brace style between functions (some use `if () {` on same line, some don't).

## Suggestions
- Replace the raw `Variant` union in `Input_arguments` with `std::variant<bool, float, Vector2, Pose>` or add a discriminant tag.
- Fix the `c_type_strings` array to match the `Type` enum values exactly.
- Remove the commented-out `remove_command_binding()` code or implement it.
- Add bounds checking in `c_str(State)` before array access.
- Consider reducing the binding type explosion by using a template or a more generic binding class parameterized on input type.
- Document the command state machine (Disabled <-> Inactive <-> Ready <-> Active) transitions formally, as the TODOs suggest the current behavior is not fully understood even by the author.
