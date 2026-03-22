# erhe_graphics -- Code Review

## Summary
A large, well-architected graphics abstraction library providing a Vulkan-style API over OpenGL (with Vulkan backend in progress). The pimpl pattern is used extensively for ABI stability and backend swapping. The library covers buffers, textures, shaders, pipeline state, ring buffers, command encoders, and render passes. Code quality is generally high, with the main concerns being the library's complexity and some thread safety considerations.

## Strengths
- Excellent use of pimpl idiom throughout (`Device_impl`, `Buffer_impl`, `Texture_impl`, etc.) -- enables clean backend swapping between OpenGL and Vulkan at compile time
- `Ring_buffer` implementation is sophisticated and correct, with proper wrap-around handling, sync entries for frame-based resource tracking, and detailed comments explaining the algorithm
- `Device_info` comprehensively captures GPU capabilities and limits
- `Render_pipeline_data` groups all pipeline state into a single, well-organized structure matching Vulkan's pipeline state concept
- `Shader_stages_create_info` provides a clean builder pattern for shader compilation with automatic source assembly (version, extensions, defines, interface blocks)
- `Buffer` class provides type-safe `map_elements<T>()` using `std::span<T>` -- excellent modern C++ design
- Format property queries (`Format_properties`) enable runtime feature detection and format selection
- `Scoped_debug_group` provides RAII debug group push/pop for GPU debuggers

## Issues
- **[moderate]** `Render_pipeline_state` (render_pipeline_state.hpp:52-56) has public static members `s_mutex` and `s_pipelines` that track all pipeline instances globally. This global mutable state is a potential threading hazard and makes the library harder to reason about.
- **[moderate]** `Buffer::map_elements()` (buffer.hpp:55-65) uses `reinterpret_cast<T*>` on the mapped memory. While necessary for GPU buffer mapping, it has no alignment check -- if the buffer mapping is not aligned to `alignof(T)`, this is UB.
- **[moderate]** `Device` (device.hpp:151-159) is marked `final` and has deleted copy/move operations, which is correct. However, the constructor takes `Graphics_config` by value with a default parameter, which could cause issues if the config struct grows large.
- **[minor]** `invalid_texture_handle` (texture.hpp:105) is defined as `0xffffffffu` which only covers 32 bits. For a `uint64_t` handle, this value could be a valid handle on some implementations. Consider using `~uint64_t{0}` or `std::numeric_limits<uint64_t>::max()`.
- **[minor]** `Ring_buffer` (ring_buffer.hpp:59-71) has many member variables without documentation. The comments in the `.cpp` file explain the algorithm well, but the header could benefit from brief comments on what `m_write_wrap_count` vs `m_read_wrap_count` represent.
- **[minor]** `Texture_create_info` (texture.hpp:17-43) has many default member initializers which is good, but `device` is a reference member without a default -- this means the struct cannot be value-initialized or used in containers.
- **[minor]** `Buffer_usage` (buffer.hpp:22) is typed as a bare integer (`{0}`) -- this should use the strongly-typed enum or bitmask type for consistency with the Vulkan-style design philosophy.

## Suggestions
- Consider making the global pipeline registry (`s_pipelines`) opt-in or using a per-device registry instead of static globals.
- Add an alignment check (at least in debug builds) to `Buffer::map_elements<T>()`.
- Add brief documentation comments to `Ring_buffer` member variables in the header.
- Use `std::numeric_limits<uint64_t>::max()` for `invalid_texture_handle` to be correct for 64-bit handles.
- Consider splitting the very large `device.hpp` include list -- `Device` currently transitively includes buffer, ring_buffer_range, shader_monitor, and surface headers. Forward declarations could reduce compile times.
- The `Reloadable_shader_stages` class is a useful pattern; consider documenting the reload flow (monitor detects file change -> creates prototype -> swaps into active shader stages).
- Consider using `std::expected` (C++23) or a custom result type instead of returning `bool` from `wait_frame`/`begin_frame`/`end_frame` to provide error context.
