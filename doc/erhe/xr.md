# erhe_xr

Stability: stable

## Purpose
OpenXR integration for VR/AR headset support. Wraps the OpenXR API to provide instance creation, session management, swapchain handling, action-based input (buttons, triggers, thumbsticks, pose tracking), hand tracking, and per-eye stereo rendering. Supports multiple controller interaction profiles (Oculus Touch, Valve Index, HTC Vive, KHR Simple).

## Key Types
- `Headset` -- High-level facade combining `Xr_instance` and `Xr_session`. Frame API: `wait_frame()` (`xrWaitFrame`) returns a `Frame_timing`; `begin_frame(const Frame_timing&)` runs `xrBeginFrame` when the wait succeeded, then refreshes the `XR_FB_performance_metrics` counters, and returns whether the frame began; `render(command_buffer, callback)` / `render_multiview(...)` render the views; `end_frame(rendered)` runs `xrEndFrame`. Also `poll_events()` (`xrPollEvent`), `update_actions()` (`xrSyncActions`, hand tracking, view pose) and `locate_views()`. Exposes left/right `Xr_actions` and hand tracking data.
- `Xr_instance` -- Manages the OpenXR instance, system discovery, view configuration, extension loading, action set creation, and interaction profile binding. Holds all action objects.
- `Xr_session` -- Manages the OpenXR session lifecycle, reference spaces (local/stage/view), swapchains (color + depth), frame timing, hand tracker creation, and per-frame view pose updates. `render_frame()` / `render_frame_multiview()` take their view command buffers from the device thread slot set with `set_command_buffer_thread_slot()` (default 0; the editor sets its render slot, `doc/erhe/graphics.md` "Frame lifecycle"); a program that renders the XR frame on its own thread slot sets it, so no two threads record from one slot.
- `Frame_timing` (`frame_timing.hpp`) -- The result of `xrWaitFrame` (`predicted_display_time`, `predicted_display_pediod`, `should_render`, `wait_ok`) plus `begin_ok`, which the caller sets from `begin_frame()`. Plain data without OpenXR types, available in every `ERHE_XR_LIBRARY` configuration, so it travels from the thread that waits to the thread that begins the frame.
- `Xr_action` / `Xr_action_boolean` / `Xr_action_float` / `Xr_action_vector2f` / `Xr_action_pose` -- Typed OpenXR action wrappers. Each resolves its path from the instance and queries state from the session.
- `Xr_actions` -- Aggregates all left/right hand actions (select, menu, squeeze, trigger, thumbstick, grip/aim pose, etc.).
- `Headset_config` -- Configuration flags (codegen-generated from `definitions/headset_config.py`): openxr enable, openxr_mirror, debug, validation, api_dump, quad view, composition_depth_layer, swapchain_depth_attachment, visibility_mask, hand tracking, passthrough_fb, composition_alpha, CPU/GPU performance level, boost_on_thermal_warning.
- `Render_view` -- Per-eye render target info: pose, FOV, texture handles, dimensions, format.
- `Swapchain` / `Swapchain_image` -- OpenXR swapchain wrappers for acquiring/releasing/waiting on swapchain images.
- `Quad_layer` -- One `XrCompositionLayerQuad` backed by its own color swapchain. Callers `acquire()` the texture, record into it, then `release()` to mark it rendered. `Xr_session` runs `xrWaitSwapchainImage` on acquired quad (and projection) images immediately before each command buffer submit in `render_frame()` / `render_frame_multiview()`, and `xrReleaseSwapchainImage` in `end_frame()`, after every submit of the frame.
- `Pose` / `Hand_tracking_joint` -- GLM-compatible pose and hand joint data.

## Public API
- Construct `Headset(context_window, configuration)`.
- Each frame, in order: `poll_events()`, `wait_frame()`, `update_actions()`, `begin_frame(timing)`, `render()` or `render_multiview()`, `end_frame(rendered)`.
  - `update_actions()` needs a waited frame; it does not need a begun one.
  - Every begun frame is ended in the same frame. A frame that began but cannot be used is ended with `end_frame(false)` (no layers). `Xr_session::begin_frame` treats `XR_FRAME_DISCARDED` (the runtime discarded an earlier begun frame that was not ended) as a begun frame.
  - `begin_frame()`, `render*()` and `end_frame()` run on one thread: `Xr_session::begin_frame`, `render_frame*` and `end_frame` assert the render role in phase `r_render`, and `Xr_session::wait_frame` asserts the main thread in phase `m1_overlap` (`doc/erhe/utility.md`; no-ops until the roles are set, so the editor's init pump drives the whole frame on the main thread).
  - `Xr_session::begin_frame(timing)` keeps the timing's predicted display time for `render_frame*()` (`xrLocateViews` for rendering) and `end_frame()`; `update_actions()`, `locate_views()` and hand tracking use the latest `xrWaitFrame` result, which may belong to the next frame.
- Editor threads (`doc/editor/editor.md` "Frame phases and the render thread"):

  | Phase | Thread | OpenXR calls |
  |---|---|---|
  | M1 | main | `xrWaitFrame` |
  | M2 | main | `xrPollEvent`; `xrSyncActions`, hand tracking, `xrLocateSpace` (view pose) and `xrLocateViews` (headset shadow frustum) |
  | R | render | `xrBeginFrame`; `xrLocateViews` for rendering, `xrAcquireSwapchainImage`, `xrWaitSwapchainImage`, `xrReleaseSwapchainImage`; `xrEndFrame` |

  The OpenXR frame functions permit this split: `xrWaitFrame` may be called on a different thread than `xrBeginFrame` / `xrEndFrame`, and `xrWaitFrame` for frame N+1 returns only once `xrBeginFrame` of frame N has been called, so it overlaps the render thread's frame N bracket. `xrPollEvent` is on the main thread in M2, where the render thread is idle, so a session state change never lands inside a begun frame.
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
- Vulkan headers (conditional, for `ERHE_GRAPHICS_API_VULKAN`)

## Notes
- Profile-specific bindings are collected and suggested for KHR Simple, Oculus Touch, Valve Index, and HTC Vive controllers.
- Hand tracking and visibility mask are optional extensions enabled via `Headset_config`.
- FB passthrough (`XR_FB_passthrough`) is enabled whenever the runtime supports it and starts running at session creation, so the editor's init status screen shows camera passthrough (issue #214). The `passthrough_fb` config knob controls whether passthrough stays on for the whole session; when it is false the editor reconciles passthrough per frame (`Headset::set_passthrough_active()`): paused while the sky covers the scene background, kept running when sky rendering is disabled (the scene clears to transparent, so the room shows around the scene content -- mixed reality).
- The `Xr_action` header includes full OpenXR type stubs when `ERHE_XR_LIBRARY_OPENXR` is not defined, allowing compilation without the OpenXR SDK.
- Max 50 actions per type (boolean, float, vector2f, pose) using `etl::vector`.
