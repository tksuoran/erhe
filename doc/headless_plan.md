# Plan: Null Window + Null Graphics Backends for Headless Mode

## Goal

Enable running the editor (and other erhe apps) without a display or GPU — headless/server mode for remote testing. Two new backends needed:

1. **Null window** (`ERHE_WINDOW_LIBRARY=none`) — `Context_window` exists but does nothing
2. **Null graphics** (`ERHE_GRAPHICS_LIBRARY=none`) — all graphics APIs implemented as no-ops, no actual GPU calls

## Current State

- Window: CMake already has `none` option and sets `ERHE_WINDOW_LIBRARY_NONE`, but no implementation exists (link error)
- Graphics: CMake only has `opengl`/`vulkan` options, no `none`
- The graphics system uses pimpl: public classes (`Device`, `Buffer`, `Texture`, etc.) delegate to `*_impl` classes that are backend-specific (in `gl/` or `vulkan/` subdirectories)
- `Context_window` is a concrete class (not virtual), selected via conditional includes in `window.hpp`

## Task 1: Null Window Backend

### Files to create
- `src/erhe/window/erhe_window/null_window.hpp`
- `src/erhe/window/erhe_window/null_window.cpp`

### `Context_window` stub methods

All the same public API as SDL/GLFW backends. Key behaviors:
- **Constructor**: stores `Window_configuration`, sets default width/height
- **`open()`**: returns `true`
- **`poll_events()`**: no-op (just swap empty event buffers)
- **`get_input_events()`**: returns empty vector
- **`get_width()`/`get_height()`**: return configured size (default 1920x1080)
- **`make_current()`/`clear_current()`/`swap_buffers()`**: no-ops (guarded by `ERHE_GRAPHICS_LIBRARY_OPENGL` — won't be called with null graphics)
- **`get_cursor_position()`**: outputs 0, 0
- **`set_cursor()`/`set_visible()`/`set_cursor_relative_hold()`**: no-ops
- **`get_device_pointer()`/`get_window_handle()`**: return `nullptr`
- **`get_scale_factor()`**: return `1.0f`
- **`get_modifier_mask()`**: return `0`
- **Platform-specific methods** (`get_hwnd`, `get_hglrc`, `get_wl_display`, Vulkan surface): return null/empty
- **`inject_input_event()`**: push to event queue (for programmatic testing)

### CMake changes
- `src/erhe/window/CMakeLists.txt`: add conditional block for `none` including `null_window.cpp/hpp`
- `src/erhe/window/erhe_window/window.hpp`: add `#if defined(ERHE_WINDOW_LIBRARY_NONE)` include for `null_window.hpp`
- No changes needed in root `CMakeLists.txt` — the `none` option and `ERHE_WINDOW_LIBRARY_NONE` define already exist

## Task 2: Null Graphics Backend

### CMake changes

Root `CMakeLists.txt`:
- Add `none` to `ERHE_GRAPHICS_LIBRARY` options
- Add `elseif (STREQUAL "none")` block setting `ERHE_GRAPHICS_API_NONE` and `-DERHE_GRAPHICS_LIBRARY_NONE`
- No external library dependencies

`src/erhe/graphics/CMakeLists.txt`:
- Add conditional block for `ERHE_GRAPHICS_API_NONE` including all `null/null_*.cpp/hpp` files
- No library linking needed

`src/erhe/CMakeLists.txt`:
- Skip `add_subdirectory(gl)` when `ERHE_GRAPHICS_API_NONE`

### Impl classes to create (in `erhe_graphics/null/`)

Each mirrors the corresponding `gl/gl_*.hpp/.cpp`:

| Impl class | Key behavior |
|---|---|
| `null_device.hpp/.cpp` | `Device_impl` — creates surface, manages frame index, ring buffers backed by CPU memory, returns sensible `Device_info` defaults, `wait_frame`/`begin_frame`/`end_frame` return `true` |
| `null_buffer.hpp/.cpp` | `Buffer_impl` — allocates CPU `std::vector<std::byte>`, map/flush/invalidate operate on CPU memory |
| `null_texture.hpp/.cpp` | `Texture_impl` — stores metadata only (dimensions, format, levels), no pixel storage needed, returns dummy handles |
| `null_sampler.hpp/.cpp` | `Sampler_impl` — stores parameters, no GPU object |
| `null_shader_stages.hpp/.cpp` | `Shader_stages_prototype_impl` (accepts sources, `is_valid()` returns `true`) + `Shader_stages_impl` (stores handle=0, pretends linked) |
| `null_render_command_encoder.hpp/.cpp` | `Render_command_encoder_impl` — all `set_*()` and `draw_*()` are no-ops |
| `null_command_encoder.hpp/.cpp` | `Command_encoder_impl` base — no-ops |
| `null_blit_command_encoder.hpp/.cpp` | `Blit_command_encoder_impl` — no-ops |
| `null_compute_command_encoder.hpp/.cpp` | `Compute_command_encoder_impl` — no-ops |
| `null_render_pass.hpp/.cpp` | `Render_pass_impl` — start/end are no-ops |
| `null_surface.hpp/.cpp` | `Surface_impl` — owns swapchain, no actual surface |
| `null_swapchain.hpp/.cpp` | `Swapchain_impl` — frame lifecycle returns `true`/`should_render=true` |
| `null_vertex_input_state.hpp/.cpp` | `Vertex_input_state_impl` — stores format info, no GPU state |
| `null_gpu_timer.hpp/.cpp` | `Gpu_timer_impl` — no-ops, returns 0 durations |
| `null_texture_heap.hpp/.cpp` | `Texture_heap_impl` — no-ops |

### `Device_info` defaults for null backend

Return reasonable values so the editor doesn't crash on capability checks:
- `glsl_version = 460`, `use_persistent_buffers = true`, `use_bindless_texture = false`
- `use_compute_shader = true`, `use_multi_draw_indirect_core = true`
- `max_texture_size = 16384`, `max_samples = 8`, `max_vertex_attribs = 16`
- `uniform_buffer_offset_alignment = 256`, `shader_storage_buffer_offset_alignment = 256`

### Buffer implementation detail

The null `Buffer_impl` must provide real CPU memory for `map_bytes()`/`map_elements()` since the editor writes vertex/index data through mapped buffers. Use `std::vector<std::byte>` as backing storage. The map returns a span into this vector. Flush/invalidate are no-ops.

### Shader implementation detail

`Shader_stages_prototype_impl` must accept shader source code without compiling it. `is_valid()` returns `true`. `Shader_stages_impl` stores a dummy handle (0) and pretends the program is linked.

## Implementation Order

1. **Null window first** — smallest scope, can test independently
2. **Null graphics** — larger scope, but follows clear pattern from GL backend
3. **Integration test** — configure with both `none`, build editor, verify it starts and runs the main loop without crashing

## Verification

```bash
cmake -DERHE_WINDOW_LIBRARY=none -DERHE_GRAPHICS_LIBRARY=none -B build_headless
cmake --build build_headless --target editor
./build_headless/editor  # Should start, run main loop, exit cleanly
```

## Risks

1. **Hidden GL/Vulkan dependencies**: Code outside `erhe_graphics` may directly reference `ERHE_GRAPHICS_LIBRARY_OPENGL` for conditional compilation. These guards need auditing — any code that's only compiled when OpenGL is active needs a `none` path or needs to be made unconditional.
2. **ImGui backend**: The ImGui renderer likely calls graphics APIs directly. With null graphics, ImGui rendering must degrade gracefully (skip render calls). The existing `ERHE_GUI_LIBRARY=none` option may handle this.
3. **Buffer mapping**: The editor relies on mapping GPU buffers to write geometry data. The null backend must provide real CPU memory for maps or the editor will crash on null pointer dereference.
4. **Scope**: The GL backend has ~22 impl files. Writing null stubs for all of them is mechanical but tedious. Consider generating stubs or copying GL files and stripping the GL calls.
