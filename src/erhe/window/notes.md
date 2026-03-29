# erhe_window

## Purpose
Platform windowing abstraction over SDL and GLFW. Provides window creation, OpenGL context management, input event collection (keyboard, mouse, controller, XR), cursor control, and optional RenderDoc frame capture integration. The backend is selected at CMake configure time (`ERHE_WINDOW_LIBRARY`).

## Key Types
- `Context_window` -- Main window class (implemented separately for SDL and GLFW). Creates a window with an OpenGL/Vulkan context, collects input events into a double-buffered queue, supports cursor capture (relative hold mode), joystick/controller handling, and text input.
- `Window_configuration` -- Configuration struct for window creation: color depth, depth/stencil, MSAA, swap interval, fullscreen, size, title, GL version, context sharing.
- `Input_event` -- Tagged union of all input event types with a timestamp.
- `Input_event_handler` -- Abstract base class with virtual `on_*_event()` methods for each event type. `dispatch_input_event()` routes events to the correct handler.
- `Key_event`, `Mouse_move_event`, `Mouse_button_event`, `Mouse_wheel_event`, `Controller_axis_event`, etc. -- Individual event data classes.
- `Xr_boolean_event`, `Xr_float_event`, `Xr_vector2f_event` -- XR controller action events integrated into the input system.
- `Keycode` / `Mouse_button` / `Key_modifier_mask` -- Constants for keys, mouse buttons, and modifier flags.

## Public API
- Construct `Context_window(configuration)` to create a window.
- `poll_events(wait_time)` collects input; `get_input_events()` returns the event queue.
- `make_current()` / `swap_buffers()` for OpenGL context management.
- `set_cursor()` / `set_cursor_relative_hold()` for cursor control.
- `get_device_pointer()` / `get_window_handle()` for native handle access.
- `initialize_frame_capture()` / `start_frame_capture()` / `end_frame_capture()` for RenderDoc.

## Dependencies
- SDL3 (when `ERHE_WINDOW_LIBRARY_SDL`)
- GLFW (when `ERHE_WINDOW_LIBRARY_GLFW`)
- erhe::time
- glm
- Windows API (on Windows, for HWND/HGLRC access)

XR event types (`Xr_action_boolean`, etc.) are forward-declared in `window_event_handler.hpp` but erhe::xr is not a build dependency.

## Notes
- Input events are double-buffered: one queue is written by callbacks while the other is read by the application.
- Both SDL and GLFW backends provide the same `Context_window` API; `window.hpp` includes the appropriate header based on the configured backend.
- Joystick scanning runs on a background thread to avoid blocking the main loop.
- The SDL backend is the default and recommended choice.
