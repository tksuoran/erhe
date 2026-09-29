# erhe_renderer -- Code Review

## Summary
A substantial rendering utility library providing debug line/primitive rendering (via compute shader expansion), text rendering, draw indirect buffer management, and Jolt physics debug visualization. The code is well-architected with clean separation between the compute-based line expansion pipeline and the rendering pipeline. The main concerns are around `reinterpret_cast` usage for GPU buffer writes and some dead/unused code.

## Strengths
- Clever compute-shader-based line-to-triangle expansion for thick line rendering
- Well-designed bucket system (`Debug_renderer_bucket`) that groups draw calls by configuration
- `etl::vector` used for `m_buckets` to guarantee pointer stability -- this is explicitly documented with a comment
- Good use of ring buffers for GPU memory management with proper acquire/close/release lifecycle
- `Primitive_renderer` has a rich geometric drawing API (sphere, cone, torus, bone visualization)
- Move semantics properly handled with `std::exchange` in `Primitive_renderer`
- RAII-based debug groups for GPU debugging

## Issues
- **[moderate]** `renderer_message.hpp` defines `App_message`, `Message_flag_bit`, and `Scene_view` forward declaration -- these are editor-level concepts that do not belong in a low-level renderer library. This creates an upward dependency concern.
- **[moderate]** `texture_renderer.cpp` and `renderer_message_bus.cpp` are empty (1-byte files). These likely represent abandoned features; the corresponding headers still exist.
- **[minor]** `debug_renderer.cpp:211` calls `set_compute_pipeline_state` twice with the same pipeline in `compute()`.
- **[minor]** `renderer_config.hpp` is an empty namespace stub with no content.
- **[minor]** `debug_renderer_bucket.cpp:128-129` declares `gpu_float_data` and `gpu_uint32_data` spans but they are never used (only `view_gpu_data` is used via `write()`).

## Suggestions
- Move `renderer_message.hpp` and related types to the editor layer where they belong, or at minimum rename to reflect they're editor-specific
- Remove or properly integrate `texture_renderer.cpp`, `renderer_message_bus.cpp`, and `renderer_config.hpp` -- they appear to be dead code
- Remove the duplicate `set_compute_pipeline_state` call in `Debug_renderer::compute()`
- Remove the unused `gpu_float_data`/`gpu_uint32_data` spans in `update_view_buffer()`
