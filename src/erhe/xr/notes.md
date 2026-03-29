# erhe_xr

## Purpose
OpenXR integration for VR/AR headset support. Wraps the OpenXR API to provide instance creation, session management, swapchain handling, action-based input (buttons, triggers, thumbsticks, pose tracking), hand tracking, and per-eye stereo rendering. Supports multiple controller interaction profiles (Oculus Touch, Valve Index, HTC Vive, KHR Simple).

## Key Types
- `Headset` -- High-level facade combining `Xr_instance` and `Xr_session`. Provides `begin_frame_()`, `render(callback)`, and `end_frame()` for the frame loop. Exposes left/right `Xr_actions` and hand tracking data.
- `Xr_instance` -- Manages the OpenXR instance, system discovery, view configuration, extension loading, action set creation, and interaction profile binding. Holds all action objects.
- `Xr_session` -- Manages the OpenXR session lifecycle, reference spaces (local/stage/view), swapchains (color + depth), frame timing, hand tracker creation, and per-frame view pose updates.
- `Xr_action` / `Xr_action_boolean` / `Xr_action_float` / `Xr_action_vector2f` / `Xr_action_pose` -- Typed OpenXR action wrappers. Each resolves its path from the instance and queries state from the session.
- `Xr_actions` -- Aggregates all left/right hand actions (select, menu, squeeze, trigger, thumbstick, grip/aim pose, etc.).
- `Xr_configuration` -- Configuration flags: debug, validation, quad view, depth, hand tracking, passthrough_fb, visibility_mask, api_dump, mirror mode.
- `Render_view` -- Per-eye render target info: pose, FOV, texture handles, dimensions, format.
- `Swapchain` / `Swapchain_image` -- OpenXR swapchain wrappers for acquiring/releasing/waiting on swapchain images.
- `Pose` / `Hand_tracking_joint` -- GLM-compatible pose and hand joint data.

## Public API
- Construct `Headset(context_window, configuration)`.
- Each frame: `poll_events()`, `update_actions()`, `begin_frame_()`, `render(callback)`, `end_frame()`.
- The render callback receives a `Render_view&` per eye with texture handles and projection FOV.
- Query controller state via `get_actions_left()` / `get_actions_right()`.
- Query hand tracking via `get_hand_tracking_joint(hand, joint)`.

## Dependencies
- OpenXR SDK (`openxr/openxr.h`, `openxr_platform.h`)
- erhe::window (Context_window for graphics context integration)
- erhe::dataformat (Format for swapchain pixel formats)
- glm
- spdlog
- etl (fixed-capacity vectors for action storage)
- Vulkan headers (conditional, for `ERHE_GRAPHICS_LIBRARY_VULKAN`)

## Notes
- Profile-specific bindings are collected and suggested for KHR Simple, Oculus Touch, Valve Index, and HTC Vive controllers.
- Hand tracking, FB passthrough, and visibility mask are optional extensions enabled via `Xr_configuration`.
- The `Xr_action` header includes full OpenXR type stubs when `ERHE_XR_LIBRARY_OPENXR` is not defined, allowing compilation without the OpenXR SDK.
- Max 50 actions per type (boolean, float, vector2f, pose) using `etl::vector`.
