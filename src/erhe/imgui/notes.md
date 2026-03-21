# erhe_imgui

## Purpose
Custom ImGui backend and window management layer for erhe. Provides GPU-accelerated
ImGui rendering via erhe::graphics, a host abstraction that supports multiple
independent ImGui contexts (one per viewport or render target), and a window
management system that tracks, registers, and dispatches input events to ImGui windows.

## Key Types
- `Imgui_renderer` -- GPU rendering backend: manages shaders, buffers, font atlas, texture samplers; renders ImDrawData
- `Imgui_host` -- abstract base (also a `Rendergraph_node` + `Input_event_handler`): hosts an ImGui context; subclassed by `Window_imgui_host` and editor's `Rendertarget_imgui_host`
- `Window_imgui_host` -- concrete host that renders ImGui into the main OS window via swapchain
- `Imgui_window` -- base class for individual ImGui windows; override `imgui()` to draw content
- `Imgui_windows` -- registry/manager: registers windows, dispatches input events, persists visibility state, provides menu entries
- `Imgui_settings` -- font paths, sizes, scale factor configuration
- `Scoped_imgui_context` -- RAII guard for switching the active ImGui context
- `File_dialog_window` -- simple file browser dialog window
- Helper functions in `imgui_helpers.hpp`: `make_button()`, `make_combo()`, `make_scalar_button()`, etc.

## Public API
- Create an `Imgui_renderer`, then create `Imgui_host` subclasses (each gets its own ImGuiContext).
- Derive from `Imgui_window` and register with `Imgui_windows` to add UI panels.
- Per frame: `begin_frame()` -> draw windows -> `render_draw_data()` -> `next_frame()`.
- `image()` / `image_button()` for rendering GPU textures in ImGui.

## Dependencies
- `erhe::graphics` -- shaders, buffers, textures, render pipeline, ring buffers
- `erhe::rendergraph` -- `Rendergraph_node` base class for hosts
- `erhe::window` -- `Input_event_handler`, `Context_window`
- `erhe::math` -- `Viewport`
- `erhe::dataformat` -- vertex format for ImGui geometry
- External: Dear ImGui, glm

## Notes
- Each `Imgui_host` has its own `ImGuiContext`, enabling multiple independent ImGui viewports (e.g., main window + VR render targets).
- The renderer uses indirect draw calls with a ring buffer strategy for vertex/index/draw-parameter data.
- Font atlas is shared across all hosts.
- The `windows/` subdirectory has reusable utility windows (performance, log, pipeline inspector, graph plotter, framebuffer viewer).
