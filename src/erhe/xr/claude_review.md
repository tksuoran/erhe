# erhe_xr -- Code Review

## Summary
A comprehensive OpenXR integration providing VR headset support with hand tracking, action-based input, swapchain management, and multi-view rendering. The code handles the notoriously complex OpenXR lifecycle well, with proper extension discovery, session state management, and fallback types for when OpenXR is not available. The `xr_action.hpp` header cleverly provides OpenXR type stubs when the library is disabled. The main concern is the large amount of public state in `Xr_instance`.

## Strengths
- Full OpenXR lifecycle management: instance creation, system discovery, session management, frame rendering
- Extensive action binding support for multiple controller profiles (Simple, Oculus Touch, Valve Index, HTC Vive)
- Hand tracking integration via `EXT_hand_tracking`
- FB Passthrough extension support
- Clever fallback: `xr_action.hpp` provides complete OpenXR type stubs when `ERHE_XR_LIBRARY_OPENXR` is not defined, allowing the rest of the code to compile without ifdefs
- `etl::vector` for action storage ensuring pointer stability
- Proper extension capability detection before use
- `Frame_timing` struct cleanly encapsulates frame prediction data

## Issues
- **[notable]** `Xr_instance` has an enormous amount of public state: function pointers (`xrCreateDebugUtilsMessengerEXT`, etc.), `Extensions` struct, `Paths`, `actions_left`, `actions_right`, all action vectors via getters. This breaks encapsulation -- callers can freely modify internal XR state.
- **[moderate]** `xr_action.hpp:251` closing namespace comment says `namespace erhe::ui` instead of `namespace erhe::xr`.
- **[moderate]** `headset.hpp:29` has a typo: `predicted_display_pediod` should be `predicted_display_period`.
- **[moderate]** The duplicate declaration of `debug_utils_messenger_callback` in `xr_instance.hpp` -- appears at both line 72-76 (public, const method) and lines 149-153 (non-const). These have different signatures (`const` vs non-`const` return and method constness).
- **[minor]** `Xr_actions` class (xr_action.hpp:212-249) uses raw (non-owning) pointers to actions. If an action is destroyed while `Xr_actions` still holds a pointer, use-after-free occurs.
- **[minor]** `Xr_session` stores `m_context_window` by reference -- if the context window is destroyed before the session, this becomes a dangling reference.

## Suggestions
- Make the function pointers and extension flags in `Xr_instance` private, exposing only the operations that need them
- Fix the namespace comment in `xr_action.hpp:251` (`erhe::ui` -> `erhe::xr`)
- Fix the `predicted_display_pediod` typo in `headset.hpp`
- Resolve the duplicate `debug_utils_messenger_callback` declarations -- likely one should be removed
- Document the lifetime requirements: `Context_window` must outlive `Xr_session`, `Xr_instance` must outlive all actions
