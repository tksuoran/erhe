# erhe_graphics

Stability: stable

## Purpose
Vulkan-style abstraction over OpenGL, Vulkan, and Metal. Provides
GPU resource management, render pipeline state, shader compilation, command encoding,
ring buffers, texture/sampler management, render passes, and a device abstraction that
hides the underlying graphics API behind a pimpl pattern.

## Header dependencies

`device.hpp` is included by every consumer of the library, so it carries
only what its declarations need by value: `enums.hpp`, `ring_buffer_range.hpp`,
`erhe_math/coordinate_conventions.hpp` (the `Device_info` conventions, without
`math_util.hpp` and glm) and `erhe_utility/debug_label.hpp`. `Graphics_config`,
`Surface_create_info`, `Shader_monitor`, `Shader_source_cache`, `Spirv_cache`
and `erhe::frame_pacing::Frame_time_recorder` are forward-declared; the
`Device` owns the last three through `unique_ptr` for that reason (the
`Shader_monitor` is a by-value member of each backend's `Device_impl`). A translation
unit that uses one of them includes its header. `texture.hpp` is the one
interface header whose cost is structural: `Texture` is an `erhe::Item`, so
the header brings `erhe_item/item.hpp` and the property system with it; code
that only passes a `Texture` by pointer or `shared_ptr`, or implements
`Texture_reference`, forward-declares it or includes `texture_reference.hpp`.
No header outside `src/erhe/graphics/` includes a backend header
(`erhe_graphics/vulkan/`, `gl/`, `metal/`, `null/`).

## Interface and backend targets

`src/erhe/graphics/CMakeLists.txt` builds the library as two targets:

- `erhe_graphics_interface`, an object library holding every public header
  and each translation unit that includes no backend header (`enums.cpp`,
  `shader_resource.cpp`, the ring buffers, the image loaders and writers,
  the `state/` types, `spirv_cache.cpp`, ...). Its dependencies are the
  neutral ones only: no volk, VMA, `erhe::gl`, metal-cpp or SPIRV-Cross. A
  backend include added to one of its sources fails to compile on the
  OpenGL, Vulkan and Metal backends, because the backend header's own
  includes are not on its path; that is the check that keeps the interface
  neutral.
- `erhe_graphics` (`erhe::graphics`), the backend target: the selected
  backend directory plus the translation units of the interface classes that
  include a backend header, which are the pimpl bridges (`device.cpp`,
  `buffer.cpp`, `texture.cpp`, `render_pass.cpp`, ...), `native_format.cpp`,
  `shader_stages_create_info.cpp` (GL extension preamble),
  `scoped_transient_object_pool.cpp` (Metal autorelease pool),
  `state/vertex_input_state.cpp` and `glsl_to_spirv.cpp` (takes the backend's
  `Shader_stages_prototype_impl`). It archives the interface objects, so
  consumers link `erhe::graphics` only and no target links the interface
  directly.

The placement rule for a new file: a header, or a translation unit that
includes only `erhe_graphics/*.hpp` and neutral dependencies, goes to the
interface target; a translation unit that includes
`erhe_graphics/<backend>/`, `erhe_gl/`, volk, metal-cpp or
`vulkan_external_creators.hpp` goes to the backend target.

The split does not change what a backend-only edit rebuilds: before it,
touching `vulkan/vulkan_device.cpp` recompiled one object and touching
`vulkan/vulkan_device.hpp` the 26 objects inside the library that include it,
then the archive and the executables, and no consumer library recompiled
(measured on the Linux Vulkan Debug tree); the include diet above is what
keeps consumers out of backend headers. The CI `Windows (VS 2026 /
headless)` job builds the editor and the test targets with
`ERHE_GRAPHICS_API=none`, so the interface compiles against the null backend
on every push.

## Key Types
- `Device` -- Central graphics device. Creates command encoders, manages ring buffers, queries capabilities, handles frame lifecycle (`wait_for_frame_start`/`acquire_frame_slot`/`wait_frame`/`end_frame`).
- `Device_info` -- GPU capability queries: GLSL version, limits, feature flags (bindless textures, sparse textures, persistent buffers, compute shaders, multi-draw indirect). `texture_heap_path` selects which of the four sampler-binding strategies the backend uses (see below).
- `Buffer` -- GPU buffer (vertex, index, uniform, storage, etc.) with mapping and flush operations. Uses pimpl for backend. Pure GPU resource wrapper - allocation is handled externally by `Free_list_allocator` (in `erhe::buffer`).
- `Texture` -- GPU texture (1D/2D/3D/cube) with mipmap, MSAA, sparse, and array layer support. It is a typed prim (`erhe::Typed`, `doc/erhe/item.md` "Prim classes") with the fixed `typeName` token `Texture`, so a texture a loader registers as scene content can be parented in a prim tree; a render target, shadow map or other device-internal texture is the same class and is never placed in one.
- `Texture_create_info::make_view(device, source, base_level, base_layer)` -- A create info for a texture view of `source` starting at the given level and layer: extents of the source at `base_level`, level and layer counts of what remains past the bases, so the view's own `get_width()` / `get_level_count()` describe exactly the subresources it covers. The caller sets `usage_mask` (and `type` for a single-layer view of an array).
- `Texture_reference` / `Texture_reference_user` (`texture_reference.hpp`) -- The interface a class resolving to a texture implements (`Texture` itself, a render graph node, a texture graph output) and the interface of a holder that must learn when the resolved texture changes (a material slot). The header forward-declares `Texture`, so a class deriving from either includes it, not `texture.hpp`.
- `Sampler` -- Texture sampler with filtering, addressing, LOD, and anisotropy settings.
- `Shader_stages` -- Compiled and linked shader program. Created from `Shader_stages_prototype`.
- `Shader_stages_create_info` -- Describes shader sources, defines, interface blocks, vertex format, fragment outputs, and the active `Bind_group_layout`. The layout's synthesized default uniform block supplies the sampler declarations that get injected into the shader preamble -- callers do not pass a `Shader_resource` for samplers directly.
- `Shader_resource` -- Describes GLSL resources (uniform/storage blocks, struct types, samplers). Generates GLSL source strings for injection into shaders, eliminating need for reflection. When used for the sampler-declaration block, ownership belongs to `Bind_group_layout` -- application code only sees the type for buffer interface blocks.
- `Bind_group_layout` -- Describes the full set of resources a pipeline reads: buffer bindings (uniform / storage), combined image samplers (dedicated and texture-heap), and the GLSL sampler declarations that ship with them. See "Bind group layout" below.
- `Texture_heap` -- Runtime sampler-array / argument-buffer / descriptor-indexing container for material textures. See "Texture heap" below.
- `Render_pipeline_state` -- Complete render pipeline: shader, vertex input, rasterization, depth/stencil, color blend, multisample, viewport, scissor states.
- `Render_pipeline` / `Base_render_pipeline` -- Compiled pipeline objects. `create_render_pipeline()` builds one up-front; `Base_render_pipeline` defers creation to first use and caches variants keyed by `(shader_stages, vertex_input, vertex_format, render-pass format)`.
- `Render_pass` -- Framebuffer configuration with color/depth/stencil attachments and load/store actions. Attachment descriptors carry `usage_before` / `usage_after` so backends can derive image layout transitions and subpass dependencies without per-texture layout tracking.
- `Render_command_encoder` -- Records draw commands: set pipeline, bind buffers, bind sampled images via `set_sampled_image()`, draw primitives (including multi-draw indirect).
- `Gpu_timer` -- GPU wall-clock timer; `last_result()` returns the latest completed measurement in nanoseconds, one or two frames after it was recorded, and `all_gpu_timers()` lists every live timer (the Performance window plots them). Constructed with a `Render_pass`, it times that pass from `Render_pass::start_render_pass` to `end_render_pass`. Constructed with a `Device`, it times an explicit range of one `Command_buffer` that the caller brackets with `begin()` / `end()` or the RAII `Scoped_gpu_timer` -- the form used around compute dispatches. The timestamps are taken after all previously recorded work completes, so consecutive ranges partition the wall time: a range whose work overlaps its predecessor's and finishes first reads close to 0, and the sum of consecutive ranges is exact. Explicit ranges must not nest (OpenGL runs one `GL_TIME_ELAPSED` query at a time). Vulkan, OpenGL and Metal measure (Metal at encoder boundaries, `doc/erhe/metal_backend.md` "GPU timers"); the null backend reads 0.
- `Ring_buffer` -- Circular GPU buffer for streaming per-frame data with fence-based synchronization.
- `Shader_monitor` -- Watches shader source files and hot-reloads programs when files change.
- `Shader_source_cache` -- Text of shader source files by path, owned by the `Device` (`get_shader_source_cache()`). `Glsl_file_loader` reads every source and include through it, `preload(directory)` fills it for a whole shader directory before the first program is built (`Program_interface` does this for its `shader_paths`), and `Shader_monitor` erases the entry of a changed file before it rebuilds the stages that use it.
- `Fragment_outputs` -- Describes fragment shader output declarations.
- `Surface` / `Swapchain` -- Window surface and swapchain management.

## Public API
- `Device device(surface_create_info)` -- Create the graphics device.
- `device.make_render_command_encoder()` -- Get a command encoder for drawing.
- `device.allocate_ring_buffer_entry(target, usage, size)` -- Allocate from ring buffer.
- `device.create_render_pipeline(Render_pipeline_create_info)` -- Up-front pipeline creation.
- `Buffer(device, create_info)` / `Texture(device, create_info)` -- Create GPU resources.
- `Bind_group_layout(device, create_info)` -- Create a descriptor layout plus synthesized sampler declarations.
- `Shader_stages(device, prototype)` -- Create shader program from compiled prototype.
- `Texture_heap(device, fallback_texture, fallback_sampler, bind_group_layout)` -- Create a material-texture heap bound to a layout.
- `Scoped_render_pass(render_pass, command_buffer, render_pass_before, render_pass_after)` -- RAII render pass begin/end recorded into `command_buffer`; the optional neighbouring passes refine the synchronization between passes.
- `Scoped_debug_group(command_buffer, label)` / `Scoped_queue_debug_group(device, label)` -- RAII debug label region; the label (`std::string_view`, literal or `Debug_label`) is read during construction only and copied onto the stack (`Debug_label_buffer`, truncated at 255 characters), so a caller may format per-draw labels into a reused buffer without heap allocation.
- `Gpu_timer(render_pass, label)` / `Gpu_timer(device, label)` + `Scoped_gpu_timer(timer, command_buffer)` -- GPU timing of a render pass or of an explicit command buffer range. A render pass's timers write their timestamps just outside the pass (before it begins, after it ends): KosmicKrisp returns the same value for every timestamp written inside one render pass.
- `Render_command_encoder::set_render_pipeline()` / `set_bind_group_layout()` / `set_sampled_image(binding_point, texture, sampler)` / `draw_indexed_primitives()` -- Issue draw calls.

## Frame lifecycle

A device frame is bracketed by its frame start and `Device::end_frame()`.
The frame start has two halves, `Device::wait_for_frame_start()` and
`Device::acquire_frame_slot(frame_id)`; `Device::wait_frame()` calls both,
for callers that run the whole frame on one thread (`src/example`,
`src/hello_swap`, `src/hextiles`, the GPU tests, the editor's init pump).
In between, callers obtain one or more
`Command_buffer` instances from `Device::get_command_buffer(thread_slot)`,
optionally engage the window swapchain via
`Command_buffer::wait_for_swapchain` + `Command_buffer::begin_swapchain`,
record into them, and submit through `Device::submit_command_buffers`.

Frames carry ids (starting at 1) and frame F uses the frame-in-flight slot
`F % N`, N = `get_number_of_frames_in_flight()` (2 on Vulkan and the null
backend, 3 on OpenGL and Metal). A frame is open from its
`acquire_frame_slot()` to its `end_frame()`, and up to N frames may be open
at once: frame F+1 may be acquired while F is still recording. The order
rule: frames are acquired and ended strictly in id order, so the open
frames are always a consecutive run from the oldest open frame (the render
frame, the one `end_frame()` ends next) to the newest acquired frame (the
prepare frame). Each slot keeps its own state (idle, waited, recording), so
slot A may be recording on one thread while slot B is waited on another.

`Device::get_frame_index()` returns the frame of the CALLING ROLE
(`erhe::utility::Thread_roles`, `doc/erhe/utility.md`): the render role (a
dedicated render thread) gets the render frame; every other caller - the
main thread, worker threads, programs that never set the roles - gets the
prepare frame while it is open, otherwise the next frame to be acquired.
With one frame open the two are the same frame. Everything keyed on the
frame index - ring-buffer entries, completion handlers, deferred
destruction, the frame time records and command buffers - therefore lands
in the frame the calling role works on: the main thread touches only the
slot of the frame it prepares, the render thread only the slot of the frame
it renders. `get_command_buffer` verifies this at the allocation site.

The display refresh period the frame pacer runs on
(`get_display_refresh_duration_seconds()`) is resolved by the device's
`Display_refresh_resolver` (`display_refresh_resolver.hpp`,
`get_display_refresh_resolver()` / `get_display_refresh_report()`) from the
window's display mode, the backend's swapchain timing query and an init
calibration on a dedicated swapchain; `doc/frame_pacing/inputs.md` section
3.1 states the sources and the decision, and the graphics config keys
`display_refresh_source` / `display_refresh_override_hz` choose the deciding
source or an explicit rate. The resolver applies its decision to the frame
time recorder, from which the pacing observer reads it.

The contract for each entry point:

- `wait_for_frame_start() -> uint64_t` -- the pacing half: advance the
  device's started-frame counter and return the id of the frame it starts,
  open that frame's time record (CPU slot begin, `doc/erhe/frame_pacing.md`),
  and on Vulkan refresh the GPU clock calibration and harvest completed GPU
  frame brackets. It reads no state `end_frame()` writes, so it may run while
  another thread is still submitting and ending the previous frame. Only the
  thread that starts frames advances the counter.
- `acquire_frame_slot(frame_id)` -- the resource half. Verifies that
  `frame_id` was started, that it follows the newest acquired frame, and
  that at most N frames are then open (its slot is idle). It then paces
  the per-(frame_in_flight, thread_slot) command pools of the slot on the
  device's timeline semaphore: for a frame that re-enters a slot
  (`frame_id > N`) it waits until the timeline reports value
  `frame_id - N`, then `vkResetCommandPool`s every per-thread pool for that
  slot. Each freed `Command_buffer` recycles its implicit fence + binary
  semaphore back into `Device_sync_pool`. Only that slot is touched: the
  command buffers of an older frame that is still open stay valid.
- `wait_frame()` -- `wait_for_frame_start()` then `acquire_frame_slot()`.
- `get_command_buffer(thread_slot)` -- allocate a fresh primary cb
  from the per-thread pool of the calling role's frame slot. Vulkan
  verifies (fatal otherwise) that the calling role's frame is open:
  its slot is waited or recording and held by that frame. The cb carries
  the frame id (`Command_buffer::get_frame_id()`); its timestamps, GPU
  timer queries and submit are keyed on it. The wrapper carries an
  implicit fence + binary semaphore, never exposed, used by the four
  synchronization methods. A GPU-to-GPU edge is
  `producer.signal_gpu_completion_to(consumer)` (when the producer's
  GPU work completes, signal the consumer's semaphore) paired with
  `consumer.wait_for_gpu_completion_signal()` (before the consumer's
  GPU work starts, wait on its own semaphore); the primitive is the
  consumer's, so one producer fans out to N consumers. A GPU-to-CPU
  edge is `producer.signal_cpu_completion()` (the producer's submit
  signals its own fence) paired with
  `consumer.wait_for_cpu_completion_of(producer)` (block the CPU
  before the consumer's submit until the producer's fence is
  signaled). Lifetime is tied to the next pool reset for this slot.
  It first calls `erhe::utility::Thread_roles::assert_command_recording_allowed()`,
  which is fatal when the main thread obtains a command buffer while its
  frame phase is `m1_overlap` (`doc/erhe/utility.md`).
- `Command_buffer::wait_for_swapchain(out_frame_state)` /
  `begin_swapchain(info, out_frame_state)` -- wait on the next
  swapchain image to be acquirable, then acquire it and bind the
  per-slot acquire / present semaphores to this cb. The swapchain's
  per-slot state is the slot of this cb's frame id. The cb tracks
  the swapchain it engaged so the submit can attach the right
  semaphores and drive the implicit present. The swapchain has one
  state machine and one acquired image, and `VkSwapchainKHR` is
  host-synchronized, so one thread drives it: the swapchain wait, the
  acquire, the submit that presents and the present wait
  (`Device::wait_for_displayed_frame`).
- `Command_buffer::begin()` / `end()` -- bracket recording
  (`vkBeginCommandBuffer` / `vkEndCommandBuffer` on Vulkan). One cb
  per render pass is the recommended pattern.
- `submit_command_buffers(span)` -- submit. Every cb of one submit belongs
  to the same frame (verified on Vulkan); that frame keys the submit's
  frame time record and present target. Walks each cb,
  performs CPU-side `vkWaitForFences` for any
  `wait_for_cpu_completion_of(producer)` registered, splices the wait/signal
  semaphores into a single `VkSubmitInfo2`, runs `vkQueueSubmit2`,
  and finally drives `vkQueuePresentKHR` / `swap_buffers` /
  `presentDrawable` on every swapchain whose cb engaged it via
  `begin_swapchain`. Implicit present is the rule: there is no
  separate `Device::present` call.
- `end_frame(frame_id)` -- **end the oldest open frame. That is the entire
  contract.** Verifies that `frame_id` is the oldest open frame. Does not
  submit, does not present. Mechanically it signals the device timeline
  semaphore at `frame_id` in a `vkQueueSubmit2` that carries at most the
  device-owned frame-end command buffer (the GPU frame bracket end, when the
  frame's presenting submit did not already carry it;
  `doc/erhe/vulkan_backend.md` "Frame
  lifecycle and command buffers"), so `acquire_frame_slot` of frame
  `frame_id + N` can consume that value, and makes the next frame the
  oldest open one. `end_frame()` without an argument ends the oldest open
  frame, for callers that run the whole frame on one thread. The legacy
  `end_frame(Frame_end_info)` overload is identical to `end_frame()` -- the
  argument is ignored, kept only for migration.

There is no `Device::present()`. Present is implicit in
`submit_command_buffers`; if you didn't run `begin_swapchain` on a
cb, no swapchain was engaged and nothing is presented.

A frame's bracket may span two threads: the frame start and the first
command buffers on one thread (the main role), the remaining recording, the
submits and `end_frame()` on another (the render role), and with two frames
open the main role prepares frame N+1 while the render role renders frame N.
The device entry points both roles reach are safe under that overlap; the
next section lists them. `set_present_target_time()` is called by the thread
that submits the frame. Each thread records from its own thread slot, so no
per-thread command pool is used from two threads.

The editor (`doc/editor/editor.md` "Frame phases and the render thread")
calls `wait_for_frame_start()` on the main thread at the start of M1, while
the render thread may still run R of the previous frame,
`acquire_frame_slot(frame_id)` on the main thread in M2, and
`end_frame(frame_id)` of the packet's frame on the render thread in R; it
opens one frame at a time. The swapchain belongs to the render thread: the
present wait, `wait_for_swapchain`, `begin_swapchain` and the presenting
submit all run in R. It uses the thread slots this way:

| Slot | Thread and use |
|---|---|
| 0 | Main thread: the init pump and loading screen, `erhe::gltf` image transfers (`Image_transfer`) |
| 1 | Main thread, M2: the upload command buffer |
| 2 | Render thread, R: the frame command buffer (allocated, swapchain-engaged, recorded and submitted in R) and the XR view command buffers |
| 6 | Lightmap bake and lightmap streamer helper command buffers |
| 7 | Synchronous readback and export command buffers (ray trace readback, mesh component readback, texture graph export) |

`src/example`, `src/hello_swap` and `src/hextiles` run the whole bracket on
one thread.

## Thread safety

With frame pipelining (`doc/editor/render_thread.md` section 2.1) the main role
runs M2 of frame N+1 - uploads into the upload command buffer, texture and
buffer creation and destruction, ring buffer ranges, standalone submits with
`wait_idle()` - while the render role records, submits, presents and ends
frame N. Every Vulkan `Device_impl` member both roles reach has one of three
guards: a mutex, held only for the bookkeeping (the queue mutex is the one
lock held across a Vulkan call, because Vulkan requires the queue to be
externally synchronized for the whole submit, present or wait idle); a split
per frame-in-flight slot, where each role touches only the slot of its own
frame and a slot changes hands only through `acquire_frame_slot()` after the
frame that held it ended (ordered by the caller's handoff); or one owning
role. The roles are `erhe::utility::Thread_roles` (`doc/erhe/utility.md`);
the role asserts are no-ops while the roles are unset (init, tests,
examples). The table is the Vulkan backend's; the OpenGL and null backends
render inline (one thread); the Metal backend's table is in
`doc/erhe/metal_backend.md` "Thread safety".

| Entry point | Members touched | Guard |
|---|---|---|
| `wait_for_frame_start` (main, M1) | frame time records; GPU clock calibration; frame bracket query pools and frame ids | recorder lock; `m_time_calibration_mutex`; main role only |
| `acquire_frame_slot` (main, M2) | `m_prepare_frame_id` (write), `m_render_frame_id` (read); the slot's state, command pools, frame-end pool, frame bracket flag, fallback descriptor pool; GPU timer fired flags, reset flags, results | `m_frame_id_mutex`; per slot; `m_descriptor_pool_mutex` for the pool reset; `m_gpu_timer_mutex` (results stored with it held). Asserts the main thread |
| `begin_frame`, `end_frame` (render, R) | the render frame's slot state, frame-end command buffer, frame bracket flag; `m_render_frame_id` (write); the graphics queue | asserts the render thread; per slot; `m_frame_id_mutex` for the write (the render role reads its own id unlocked); `m_queue_mutex` for the timeline submit |
| frame completion (`end_frame`, `wait_idle`) | `m_latest_completed_frame`; the ring buffer pool; the completion handler list | `m_completion_run_mutex` (one completing thread at a time); `m_frame_id_mutex`; the pool's mutex; `m_completion_handler_mutex`, released while the handlers run |
| `get_frame_index` (any) | render role: `m_render_frame_id`; others: both frame ids | render role: its own member, unlocked; others: `m_frame_id_mutex` |
| `is_frame_completed` (any) | `m_latest_completed_frame` | `m_frame_id_mutex` |
| `add_completion_handler` (any: texture, buffer, sampler, pipeline, ring buffer destruction) | `m_completion_handlers` | `m_completion_handler_mutex`; the handler runs when the calling role's current frame completes |
| `get_command_buffer` (both) | the calling role's slot state (read) and command pool of the thread slot; the implicit semaphore and fence | per slot and per thread slot (one recording thread per thread slot of a frame: the caller's contract); `Device_sync_pool::m_mutex` |
| command buffer destruction (the slot's pool reset in `acquire_frame_slot`; the render role's swapchain) | `Device_sync_pool` free lists | `Device_sync_pool::m_mutex` |
| `submit_command_buffers`, `submit_command_buffer_and_wait` (both) | the graphics queue; the present target and holdback (presenting submits only); frame time records | `m_queue_mutex` around `vkQueueSubmit2`, not across the fence wait; render role only for the presenting branch; recorder lock |
| `wait_idle` (main in M2 for standalone readbacks, shutdown) | every queue; the ended frames' completion | `m_queue_mutex` across `vkDeviceWaitIdle`; completes only frames that ended before the wait; drains the remaining handlers only when no frame is open |
| `allocate_ring_buffer_entry`, `Ring_buffer_client::acquire` (any) | `Ring_buffer_pool` buffer lists; each `Ring_buffer`'s allocation state | the pool's mutex and a buffer list per role (`doc/erhe/ring_buffer_memory.md` "Roles and threads"); `Ring_buffer::m_mutex` |
| `Ring_buffer_range` release (any, including completion handlers) | the buffer's sync entries | `Ring_buffer::m_mutex` |
| texture, buffer, sampler creation | VMA allocation; debug label of the new handle | VMA is internally synchronized; the handle is not shared yet |
| `make_render_command_encoder` and recording | the active render pass; descriptor counters | per command buffer (`Command_buffer::get_active_render_pass`, `Command_buffer_impl`); one thread records a command buffer |
| fallback descriptor sets (no push descriptors) | the descriptor pool of the command buffer's frame | per slot; `m_descriptor_pool_mutex` |
| pipeline and compatible render pass caches | `m_pipeline_map`, `m_compatible_render_pass_map` | `m_pipeline_map_mutex`, `m_compatible_render_pass_mutex` |
| `Gpu_timer` create, record, read, destroy | slot free list, timer pointers, fired and reset flags; the timer's result | `m_gpu_timer_mutex`; the query slice is the recording command buffer's frame slot; `Gpu_timer_impl::m_result_mutex`; the destructor unhooks the timer at once and releases the slot when the frame current at the destruction completes |
| swapchain (`wait_for_swapchain`, `begin_swapchain`, present, present wait, recreation) and `m_device_submit_history` | `Swapchain_impl` state; the slot's submit fence | render role only ("Frame lifecycle"); `m_queue_mutex` around `vkQueuePresentKHR` and the recreation's `vkDeviceWaitIdle` |
| `set_present_target_time` | `m_present_target_*` | render role only (asserted) |
| `capture_last_frame` (MCP, main) | the emulated swapchain image and its readback submit | `m_queue_mutex` for the submit and the device wait; the image content is ordered only by the caller's handoff |
| queue debug labels (`Scoped_queue_debug_group`) | the graphics queue | `m_queue_mutex` per call |
| shader monitor, shader source cache, SPIR-V cache | watch list; source cache; cache files | `Shader_monitor::m_mutex`; `Shader_source_cache::m_mutex`; the SPIR-V cache keeps no in-memory state |
| `get_info`, `get_buffer_alignment`, format queries, capabilities | device properties | written during construction only |
| `get_memory_budget` | VMA heap budgets | VMA is internally synchronized |

Lock order, outermost first: `m_completion_run_mutex`, the ring buffer pool's
mutex, a ring buffer's mutex, then the leaves (`m_completion_handler_mutex`,
`m_frame_id_mutex`, `Device_sync_pool::m_mutex`, `m_descriptor_pool_mutex`,
`m_gpu_timer_mutex` before `Gpu_timer_impl::m_result_mutex`). No bookkeeping
mutex is held across a GPU wait or a queue call.

## Bind group layout

`Bind_group_layout` is the single source of truth for the descriptor / binding
layout of a shader program. It is constructed from a `Bind_group_layout_create_info`
that lists every resource the shaders reference:

```cpp
Bind_group_layout_binding {
    uint32_t       binding_point;
    Binding_type   type;              // uniform_buffer | storage_buffer | combined_image_sampler |
                                      // storage_image | acceleration_structure
    uint32_t       descriptor_count;

    // combined_image_sampler / storage_image
    Sampler_aspect   sampler_aspect;    // color / depth / stencil
    std::string_view name;              // e.g. "s_shadow_compare"
    Glsl_type        glsl_type;         // e.g. sampler_2d_array_shadow
    std::string_view image_format;      // storage_image only, e.g. "rgba16f"
    bool             is_texture_heap;   // true for bindless / sampler-array / argument-buffer
    uint32_t         array_size;        // array length when is_texture_heap, else 0
    const Sampler*   immutable_sampler; // optional; Vulkan immutable sampler (MoltenVK comparison samplers)

    uint32_t         stage_flags;       // Shader_stage_flags; mandatory on Vulkan (none is an error)
};
```

The backend-specific `Bind_group_layout_impl` constructor consumes this list and
does two things:

1. Builds the host-side descriptor layout (VkDescriptorSetLayout + VkPipelineLayout
   on Vulkan, argument-encoder slot plan on Metal, texture-unit offset tracking on
   GL). The `sampler_binding_offset` is computed from the buffer bindings so the
   sampler namespace does not collide with buffers (Vulkan / Metal).
2. Synthesizes a private `Shader_resource` of type `samplers` -- the "default
   uniform block" -- by mirroring every `combined_image_sampler` binding into
   `Shader_resource::add_sampler()`. The block is exposed via
   `Bind_group_layout::get_default_uniform_block()` and consumed by:
   - the shader source emitter in `shader_stages_create_info.cpp`, which injects
     `uniform sampler2D s_foo;` declarations into the GLSL preamble;
   - the Metal shader-stages prototype, which classifies each sampler as
     texture-heap or dedicated via `Shader_resource::get_is_texture_heap()` and
     routes it to the argument buffer or discrete set 0 accordingly;
   - the OpenGL `<4.30` fallback, which uses the `Shader_resource` member list
     to call `glUniform1i()` for each sampler when `layout(binding = N)` is not
     available.

Application code does not construct or mutate this block directly. Adding a
`combined_image_sampler` binding is the only way to declare a sampler in a
shader; the name / type / is_texture_heap information lives on the binding.

On the GL sampler-array and Metal argument-buffer paths, if no binding is
marked `is_texture_heap=true` and no binding is already named `s_texture`,
the impl appends an implicit `s_texture` array sized to the leftover texture
units. This is a convenience for callers (scene renderer, hextiles) that
just want "all leftover units available for material textures". Callers that
use a custom name or size (imgui's `s_textures`, rendering_test cells)
declare an explicit texture-heap binding and the implicit add is skipped.

## Texture heap

`Texture_heap` is the runtime allocation pool for material textures. It has one
of four backend strategies, selected at device init via `Device_info::texture_heap_path`:

| Path | How the shader samples | Where the handle comes from |
|---|---|---|
| `opengl_bindless_textures` | `sampler2D(handle)` inline | 64-bit bindless handle returned by `allocate()` and made resident by `bind()` |
| `opengl_sampler_array` | `s_texture[index]` array | Index returned by `allocate()` is an offset into the uniform sampler array |
| `vulkan_descriptor_indexing` | `erhe_texture_heap[index]` | Index into a descriptor-indexing array at set 1, binding 0 |
| `metal_argument_buffer` | `s_texture[index]` array (in MSL argument buffer) | Index into the argument buffer at `[[buffer(14)]]` |

The public API is backend-neutral:

```cpp
Texture_heap heap{device, fallback_texture, fallback_sampler, &bind_group_layout};
heap.reset_heap(command_buffer);
uint64_t handle = heap.allocate(texture, sampler);
heap.bind(render_encoder);    // or a Compute_command_encoder; makes handles resident /
                              // updates descriptors / encodes arg buffer
heap.unbind(command_buffer);  // releases residency / clears bindings
```

`reset_heap()` starts an independent per-pass snapshot: several passes recorded
into the same frame (multiple scene viewports, shadow, imgui, ...) each get
slot state of their own, because recorded draws consume the heap contents only
at submit/execution time. Metal achieves this by encoding each pass's argument
buffer into a fresh ring-buffer range; Vulkan by acquiring a per-pass
`VkDescriptorSet` (recycled via `Device_impl::is_frame_completed()`, so a set
is only rewritten once the GPU has retired the frame that used it). GL needs
neither: bindless handles are stable per texture, and the sampler-array path
binds units at record time in an in-order API. Regressing this to a single
mutable table makes the last-recorded pass's textures win for every pass of
the frame (wrong textures whenever two viewports show different scenes).

The `Bind_group_layout*` parameter is how each backend derives its runtime
state without the caller having to know the layout details:

- **GL sampler-array** reads `get_texture_heap_base_unit()` / `get_texture_heap_unit_count()`
  from the layout impl to know where the heap's texture-unit range begins. When
  a caller declares an explicit texture-heap `combined_image_sampler` binding,
  the base unit and count come from *that binding's* `binding_point` and
  `array_size`. Otherwise they come from "one past the last dedicated sampler"
  and "whatever is left in `max_per_stage_descriptor_samplers`".
- **GL bindless** ignores the offset; handles are opaque 64-bit values.
- **Vulkan descriptor indexing** writes into its own descriptor pool; the layout
  parameter is used to locate the pipeline layout for set 1.
- **Metal argument buffer** reads `bind_group_layout->get_default_uniform_block()`
  to find the texture-heap sampler declaration and compute argument-buffer slot
  IDs. Dedicated samplers in the same layout go to discrete set 0 instead.

Handles returned from `allocate()` are written into the material UBO and
sampled in the shader via the helper macros in `erhe_texture.glsl`, which
wrap the four backend-specific expressions.

## Set_sampled_image vs. texture heap

There are two ways a shader can end up reading a texture, and the choice is
made per `combined_image_sampler` binding:

- **Dedicated sampler** (`is_texture_heap = false`). The caller binds the
  texture per draw via `encoder.set_sampled_image(binding_point, texture, sampler)`.
  On Vulkan it becomes `vkCmdPushDescriptorSetKHR`; on Metal it becomes
  `setFragmentTexture`/`setFragmentSamplerState`; on GL it becomes
  `glBindTextureUnit`/`glBindSampler`. Used for resources that change per-draw
  and are not in a heap: shadow maps (`s_shadow_compare`, `s_shadow_no_compare`),
  post-processing chains (`s_input`, `s_downsample`, `s_upsample`),
  text-renderer font (`s_texture`), etc.
- **Texture-heap sampler** (`is_texture_heap = true`). The caller drives the
  backing store through a `Texture_heap`. The shader indexes into the
  heap-visible sampler array; the handle/index is stored in the material UBO.
  Used for material texture arrays that change per-object rather than per-draw.

`Render_command_encoder::set_sampled_image()` reads the `Sampler_aspect` of
the matching binding to pick the correct image view aspect (`color` /
`depth` / `stencil`) -- a sampling view must commit to a single aspect per
VUID-VkDescriptorImageInfo-imageView-01976.

## Draw records: ERHE_DRAW_ID and ERHE_PRIMITIVE_INDEX

A multi-draw's per-draw data lives in a record array the shader indexes
(`erhe::scene_renderer`'s `primitive.primitives[]`, the ImGui renderer's
`draw.draw_parameters[]`). The GLSL preamble (`Shader_stages_create_info::
final_source()`) defines two names for the index:

- `ERHE_DRAW_ID` is the command's position in its multi-draw: `gl_DrawID`
  (`gl_DrawIDARB` with `GL_ARB_shader_draw_parameters`) where multi-draw
  indirect is native; a per-draw push constant on Vulkan without
  `multiDrawIndirect` + `shaderDrawParameters`, on MoltenVK and on Metal
  (vertex and compute stages only); a `uniform int` on GL with emulated
  multi-draw.
- `ERHE_PRIMITIVE_INDEX` is the record index, chosen once at device init as
  `Device_info::record_indexing`:
  - `Record_indexing::first_instance`: the instance index that carries the
    command's `first_instance` (instance count is 1): `gl_InstanceIndex` on
    Vulkan, native and emulated multi-draw alike (the emulation still issues
    one `vkCmdDrawIndexedIndirect` per command; MoltenVK lowers it to Metal's
    `instance_id`, which includes the base instance);
    `gl_BaseInstance + gl_InstanceID` on GL with GLSL 4.60, else
    `gl_BaseInstanceARB + gl_InstanceID`. Vulkan takes it when
    `drawIndirectFirstInstance` is enabled (`use_base_instance`), GL when it
    has base instance (4.2 / `ARB_base_instance`) and the shader built-in
    (GLSL 4.60 / `ARB_shader_draw_parameters`). Metal takes it always: each
    command is an indirect `drawIndexedPrimitives` that reads its base
    instance, and SPIRV-Cross lowers `gl_InstanceIndex` to `[[instance_id]]`,
    which includes it.
  - `Record_indexing::draw_position`: `ERHE_DRAW_ID`, and every command's
    `first_instance` stays 0. The null device, and Vulkan / GL devices
    without the capabilities above.

A command can then name any record of the bound range, so passes that draw
different subsets of the same records can share one upload
(`doc/erhe/draw_list_renderer.md` "Record upload"). The device logs its choice
at startup (`Record indexing (ERHE_PRIMITIVE_INDEX): ...`).

## Draw count from the GPU

`Render_command_encoder::multi_draw_indexed_primitives_indirect_count(
primitive_type, index_type, indirect_offset, count_buffer, count_offset,
max_draw_count, stride)` is the multi-draw of
`multi_draw_indexed_primitives_indirect()` (the commands at
`indirect_offset` of the bound `Buffer_target::draw_indirect` buffer) whose
draw count the GPU reads from `count_buffer` at `count_offset` (one
`uint32_t`, 4-byte aligned), clamped to `max_draw_count`, so a compute pass
can write both the commands and their count (`doc/plans/culling.md` phase
6). It needs `Device_info::use_draw_indirect_count`: Vulkan enables
`VK_KHR_draw_indirect_count` when the device has it (enabling the extension
enables the `drawIndirectCount` feature it was promoted to in 1.2; the 1.2
feature struct cannot sit in the create chain next to the promoted
per-extension structs the device enables) and reports the capability on the
native multi-draw path only (the emulation issues one draw per command and
cannot read a GPU count); GL reports it from 4.6 (`glMultiDrawElementsIndirectCount`,
the count read from the `GL_PARAMETER_BUFFER` binding; the `erhe::gl`
loader takes the core entry point only); Metal reports it when the encoding
kernel below compiles at device init, and fails fatally in this overload;
the null device reports false and fails fatally if called. A count written by
a shader reaches the
draw after a `Memory_barrier_mask::command_barrier_bit` barrier (Vulkan
draw-indirect stage, indirect-command-read access; GL
`GL_COMMAND_BARRIER_BIT`, which covers the parameter buffer). The device
logs the capability at startup (`Draw indirect count: ...`).

### Indirect count draws

The form every backend with `use_draw_indirect_count` draws (Metal has no
other): a producer writes, per job, up to `max_draw_count`
`Draw_indexed_primitives_indirect_command`s at the job's `command_offset` in
a command buffer and their number into a count buffer, then declares a set of
them with `Compute_command_encoder::
prepare_indexed_indirect_count_draws(draws, prepare_info)` and draws job `j`
of the set with `Render_command_encoder::
multi_draw_indexed_primitives_indirect_count(set, j)` inside a later render
pass, after the pass has set the pipeline, vertex buffers and bindings. A
frame may prepare any number of sets, typically one per render pass just
before it (no render encoder open).

- `Indirect_count_draws(device, debug_label)` is persistent (one per
  producer): it keeps one slot per frame in flight, selected by
  `prepare_info.frame_index % get_number_of_frames_in_flight()` (frame
  indices do not decrease). Each prepare replaces the slot's state and gets
  the next sequence number; the returned `Indirect_count_draw_set` names the
  slot and the sequence number, and the draw verifies the set is the most
  recent prepare, so a set is drawn before the next prepare on the same
  `Indirect_count_draws`.
- `Indirect_count_draws_prepare_info`: the primitive type; the index buffers
  (`Indirect_count_index_buffer`: buffer, 16- or 32-bit index type; a job
  names one by index, so jobs of one prepare may draw from different
  buffers); the command buffer and the byte offset of command 0 (stride 20
  bytes); the count buffer, its byte offset and the byte stride between jobs'
  counts (one `uint32_t` read per job, clamped to `max_draw_count`); the jobs
  (`Indirect_count_draw_job`: `command_offset` in commands, `max_draw_count`,
  `index_buffer`), whose command ranges do not overlap.
- The producer's writes are ordered before the prepare's encoder as before any
  later pass: a separate compute encoder after a memory barrier.
- Vulkan and OpenGL: the prepare records the parameters; the draw binds the
  job's index buffer and the command buffer and issues the count draw above.
- Metal: the prepare dispatches a kernel in the given encoder (which leaves
  no erhe compute pipeline bound) that encodes each job's commands into an
  indirect command buffer and writes the job's execution range; the draw
  executes that range (`doc/erhe/metal_backend.md` "Multi-Draw Indirect").
  `ERHE_DRAW_ID` reads 0 in every command, so shaders name their records by
  `first_instance` (`Record_indexing::first_instance`, "Draw records" above).
  The pipeline must be created with
  `Base_render_pipeline_create_info::indirect_count_draws =
  Indirect_count_draw_support::enabled`; Metal rejects that for a fragment
  shader with directly bound textures or samplers (texture heap samplers are
  fine). The other backends ignore the field.

## Dependencies
- **erhe libraries (interface target, public):** `erhe::circular_ring_buffer`, `erhe::dataformat`, `erhe::frame_pacing`, `erhe::item`, `erhe::math`
- **erhe libraries (backend target):** `erhe::window` (public; the surface and swapchain backends), `erhe::gl` (private, OpenGL backend only)
- **erhe libraries (both targets, private):** `erhe::defer`, `erhe::file`, `erhe::hash`, `erhe::log`, `erhe::profile`, `erhe::time`, `erhe::utility`, `erhe::verify`
- **External:** glm, fmt, mango, concurrentqueue and glslang (with `ERHE_SPIRV`) on the interface target; volk and VMA (Vulkan), metal-cpp and SPIRV-Cross (Metal) on the backend target, selected at CMake time

## Notes
- All major types use the pimpl pattern (`*_impl` classes) to isolate backend-specific code. Backend implementations live in `gl/` (OpenGL), `vulkan/` (Vulkan), `metal/` (Metal), and `null/` (headless).
- `ERHE_WINDOW_LIBRARY=none` is a separate axis from the graphics API: the Vulkan and Metal backends both run surfaceless, rendering into an emulated swapchain of offscreen images (`vulkan/vulkan_emulated_swapchain.*`, the ring inside `metal/metal_swapchain.*`) that `Device::capture_last_frame` reads back synchronously.
- The OpenGL backend requires OpenGL 4.5 with direct state access (DSA) as a hard minimum; device creation fails on older contexts, and there are no non-DSA or GL 4.1 fallback paths.
- `Shader_resource` is used to programmatically build GLSL interface declarations from C++, keeping shader sources and C++ code in sync without reflection. For sampler declarations it is an implementation detail of `Bind_group_layout`.
- `Reloadable_shader_stages` combines `Shader_stages_create_info` with a live `Shader_stages` for hot-reload via `Shader_monitor`.
- Enums in `enums.hpp` mirror Vulkan concepts (Buffer_target, Texture_type, Memory_usage, Texture_heap_path, Resolve_mode, etc.) to keep the API backend-neutral.
- The `Graphics_config` type is generated (see `generated/graphics_config.hpp`).
- See `doc/erhe/vulkan_backend.md` and `doc/erhe/metal_backend.md` for backend-specific design notes.
