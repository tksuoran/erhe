# Command_buffer migration -- progress notes

## Goal

Move `erhe::graphics` off the implicit "single device-frame command buffer"
model and onto an explicit `Command_buffer` API. Every site that records GPU
work now takes a `Command_buffer&`. The `Vulkan_immediate_commands` class is
gone. Frame submit + present go through `Device::submit_command_buffers` with
implicit-present semantics.

## New public API

### `Command_buffer`

```cpp
class Command_buffer final {
    explicit Command_buffer(Device&, erhe::utility::Debug_label = {});

    void begin();
    void end  ();

    // Swapchain-frame lifecycle (replaces removed Device::wait_swapchain_frame
    // / begin_swapchain_frame / end_swapchain_frame).
    [[nodiscard]] auto wait_for_swapchain(Frame_state&) -> bool;
    [[nodiscard]] auto begin_swapchain   (const Frame_begin_info&, Frame_state&) -> bool;
    void               end_swapchain     (const Frame_end_info&);

    // Cross-cb sync. Each cb has an implicit fence + binary semaphore
    // allocated by Device_impl. The four methods register dependency
    // edges; the backend collects them at submit time.
    void wait_for_fence    (Command_buffer& other);  // CPU-side vkWaitForFences
    void wait_for_semaphore(Command_buffer& other);  // GPU->GPU
    void signal_semaphore  (Command_buffer& other);  // GPU->GPU
    void signal_fence      (Command_buffer& other);

    // GPU recording (was on Device).
    void upload_to_buffer         (const Buffer&, size_t offset, const void*, size_t length);
    void upload_to_texture        (const Texture&, int level, int x, int y, int w, int h,
                                   erhe::dataformat::Format, const void*, int row_stride);
    void clear_texture            (const Texture&, std::array<double,4>);
    void transition_texture_layout(const Texture&, Image_layout);
    void cmd_texture_barrier      (uint64_t usage_before, uint64_t usage_after);
    void memory_barrier           (Memory_barrier_mask);
};
```

### `Device` (changes)

```cpp
[[nodiscard]] auto get_command_buffer(unsigned int thread_slot) -> Command_buffer&;

// Submits cbs in order. Drives implicit vkQueuePresentKHR /
// swap_buffers / presentDrawable for any cb that engaged the
// swapchain via Command_buffer::begin_swapchain.
void submit_command_buffers    (std::span<Command_buffer* const>);

// Init-time texture allocation; records the dummy-pixel upload into the
// caller-supplied init cb.
[[nodiscard]] auto create_dummy_texture(Command_buffer& init_command_buffer,
                                        erhe::dataformat::Format) -> std::shared_ptr<Texture>;

// CONTRACT: only advances the frame index. Does not submit, does not
// present. See notes.md "Frame lifecycle".
[[nodiscard]] auto end_frame() -> bool;
```

Removed from `Device`:
`upload_to_buffer`, `upload_to_texture`, `clear_texture`,
`transition_texture_layout`, `cmd_texture_barrier`, `memory_barrier`,
`wait_swapchain_frame`, `begin_swapchain_frame`, `end_swapchain_frame`,
`prime_device_frame_slot`, `is_in_device_frame`. The legacy
`begin_frame(Frame_begin_info)` / `end_frame(Frame_end_info)` overloads remain
as compat shims that ignore the info argument.

## Canonical frame loop (hello_swap)

```cpp
while (running) {
    device.wait_frame();                         // pace timeline + per-thread pool reset

    Command_buffer& cb = device.get_command_buffer(0);

    Frame_state fs{};
    bool wait_ok  = cb.wait_for_swapchain(fs);
    bool can_draw = wait_ok && cb.begin_swapchain(frame_begin_info, fs);
    if (can_draw) {
        cb.begin();
        {
            Render_command_encoder enc = device.make_render_command_encoder(cb);
            Scoped_render_pass     rp{*render_pass, cb};
            // record draws...
        }
        cb.end();
        cb.end_swapchain(frame_end_info);

        Command_buffer* cbs[] = { &cb };
        device.submit_command_buffers(cbs);      // submit + implicit present
    }

    device.end_frame();                          // advance frame index, signal timeline
}
```

## Canonical init phase (example, rendering_test)

```cpp
class App {
    Device& m_graphics_device;
    Command_buffer& m_init_command_buffer;
    Image_transfer  m_image_transfer;
    Forward_renderer m_forward_renderer;

    App()
        : m_init_command_buffer{[this]() -> Command_buffer& {
              device.wait_frame();
              Command_buffer& cb = device.get_command_buffer(0);
              cb.begin();
              return cb;
          }()}
        , m_image_transfer  {device, m_init_command_buffer}
        , m_forward_renderer{device, m_init_command_buffer, program_interface}
    {
        // ... gltf parse, mesh buffer transfer, dummy textures ...
        mesh_memory.buffer_transfer_queue.flush(m_init_command_buffer);

        m_init_command_buffer.end();
        Command_buffer* cbs[] = { &m_init_command_buffer };
        device.submit_command_buffers(cbs);
        device.wait_idle();
        device.end_frame();
    }
};
```

The pattern: caller owns the init cb, threads it through every constructor /
helper that records init-time GPU work, then submits + waits at the bottom.
See `example.cpp` and `rendering_test.cpp` for working examples.

## Constructors that take `Command_buffer& init_command_buffer`

- `erhe::ui::Font` (and its private `render` / `post_process`)
- `erhe::gltf::Image_transfer`
- `erhe::scene_renderer::Light_buffer`
- `erhe::scene_renderer::Forward_renderer`
- `erhe::scene_renderer::Shadow_renderer`
- `erhe::scene_renderer::Texel_renderer`
- `erhe::scene_renderer::Cube_renderer`
- `erhe::renderer::Text_renderer`
- `erhe::imgui::Imgui_renderer`
- `erhe::graphics::Device::create_dummy_texture`
- `erhe::graphics::Buffer_transfer_queue::flush(Command_buffer&)` (was `flush()`)

## Encoders / render passes

- `Render_command_encoder`, `Compute_command_encoder`, `Blit_command_encoder`
  ctors all take `Command_buffer&`.
- `Render_command_encoder::get_command_buffer() -> Command_buffer&` exposes
  the cb so callers like `Texture_heap::bind(encoder)` can record into it.
- `Scoped_render_pass{render_pass, command_buffer}` -- second arg is the
  recording cb.
- `Render_pass_impl::start_render_pass(Command_buffer&, ...)` -- the swapchain
  branch records `vkCmdBeginRenderPass2` directly into the user's cb (it used
  to silently route into the legacy single-cb -- the cause of the original
  hello_swap black-screen bug). `Swapchain_impl::acquire_swapchain_framebuffer`
  / `get_swapchain_extent` / `mark_render_pass_recorded` are the new
  accessors used to fill in framebuffer + render area without touching the
  legacy cb path.

## Texture_heap

`Texture_heap::bind() -> Texture_heap::bind(Render_command_encoder&)`. The
Vulkan impl now correctly emits `vkCmdBindDescriptorSets(set=1)` against the
encoder's cb. The earlier no-op stub (during the immediate-commands cleanup)
was the cause of `vkCmdDrawIndexedIndirect` validation errors about
descriptor set 1 not being bound.

## Vulkan internals

### Per-(frame_in_flight, thread_slot) command pools

`Device_impl` owns `m_command_pools[s_number_of_frames_in_flight][s_number_of_thread_slots]`
(currently 2 x 8 = 16 `VkCommandPool`s, all `TRANSIENT_BIT`). Each
`get_command_buffer(thread_slot)` call:

1. Allocates a fresh `VkCommandBuffer` from the slot's pool.
2. Allocates an implicit `VkSemaphore` + `VkFence` from `Device_sync_pool`.
3. Wraps both in a `Command_buffer` and stores the wrapper in the pool's
   `allocated_command_buffers` vector.
4. Returns a reference.

### Pool reset gating (timeline-based)

`Device_impl::wait_frame()`:

1. Wait for `m_vulkan_frame_end_semaphore` (timeline) to reach value
   `m_frame_index - N` (skipped for the first N frames).
2. `vkResetCommandPool` every per-thread pool of the slot we're about to
   enter; clear `allocated_command_buffers`. Each `Command_buffer_impl` dtor
   recycles its implicit fence + semaphore back into `Device_sync_pool`.

`Device_impl::end_frame()` calls `update_frame_completion()` which submits
an empty `vkQueueSubmit2` signalling the timeline at value `m_frame_index`
and then increments `m_frame_index`. That's its whole job -- no submit, no
present.

### `submit_command_buffers` flow

1. CPU-side wait: walk every cb and run `pre_submit_wait()` (consumes
   `wait_for_fence(other)` registrations as `vkWaitForFences`).
2. Build `Vulkan_submit_info`: each cb's `collect_synchronization` appends
   its `VkCommandBufferSubmitInfo`, wait/signal semaphores (including the
   per-slot acquire/present semaphore when `begin_swapchain` engaged the
   swapchain), and pins the fence (one `signal_fence` allowed per submit).
   It also pushes `Swapchain_impl*` into `swapchains_to_present` whenever a
   cb engaged the swapchain.
3. Bridge: when a cb engaged the swapchain and no user fence was pinned,
   attach the slot's legacy `submit_fence` so `Swapchain_impl::setup_frame`'s
   acquire-semaphore recycle / swapchain-garbage cleanup still drains. Goes
   away with the rest of the legacy slot-fence path.
4. `vkQueueSubmit2`.
5. For every `Swapchain_impl*` in `swapchains_to_present`:
   `Swapchain_impl::present()` -> `vkQueuePresentKHR`.

### Implicit present

`Command_buffer::begin_swapchain` latches `m_swapchain_used` on the cb's impl
and (on Vulkan) flips `Device_impl::m_had_swapchain_frame` for the legacy
breadcrumb. `collect_synchronization` then:

- Adds the per-slot acquire-semaphore to `Vulkan_submit_info::wait_semaphores`
  (stage `COLOR_ATTACHMENT_OUTPUT`).
- Adds the per-slot present-semaphore to `Vulkan_submit_info::signal_semaphores`.
- Pushes `m_swapchain_used` into `Vulkan_submit_info::swapchains_to_present`.

After the submit succeeds, `submit_command_buffers` walks
`swapchains_to_present` and calls `Swapchain_impl::present()` on each. There
is no public `Device::present` -- present is implicit.

### `Vulkan_immediate_commands` -- gone

Class deleted, files removed (`vulkan_immediate_commands.cpp/.hpp`,
`vulkan_submit_handle.hpp`, `vulkan_submit_handle.cpp`). Removed from
`CMakeLists.txt`. `Device_impl::m_immediate_commands`, `get_immediate_commands`,
`acquire_shared_command_buffer` -- all removed. The `Recording_scope` fallback
in `vulkan_blit_command_encoder.cpp` collapsed to just an
`extract VkCommandBuffer from the encoder's cb` (no fallback path).

### Render_pass_impl pre-transitions

Was: construction-time immediate-commands submit that ran an
`UNDEFINED -> layout_before` transition on each attachment.

Now: deferred to `start_render_pass` -- the same logic runs at the top of
each call against the caller's cb.

## Rendergraph (signature change)

`Rendergraph_node::execute_rendergraph_node()` -> `execute_rendergraph_node(Command_buffer&)`.
`Rendergraph::execute()` -> `execute(Command_buffer&)`. The cb is passed
straight through to every enabled node. All overrides updated:

- `erhe::imgui::Window_imgui_host`
- `editor::Brdf_slice_rendergraph_node`
- `editor::Depth_to_color_rendergraph_node`
- `editor::Post_processing_node` (plus `Post_processing::post_process` takes cb)
- `editor::Shadow_render_node` (cb forwarded into
  `Shadow_renderer::Render_parameters.command_buffer`)
- `editor::Viewport_scene_view` (cb threaded into `Render_context`; compute
  encoders, memory barriers, encoder + scoped pass all use cb)
- `editor::Headset_view_node` / `Headset_view::render_headset(Command_buffer&)`
- `editor::Rendertarget_imgui_host` (cb to `update_draw_data_textures`,
  encoder, scoped pass, `Rendertarget_mesh::render_done(cb, ...)`)

`Render_context` (editor's per-render-pass blackboard) gained a
`Command_buffer*` field so non-rendergraph render code (id_renderer,
material_preview, brush_preview, app_rendering::render_id) can find the cb
without a deep ctor change.

## App_context::current_command_buffer

`App_context` (editor) gained a `Command_buffer* current_command_buffer`
member. It is set by `Editor::tick()` and the init-phase opener, cleared at
end-of-tick / end-of-init, and read by runtime-triggered code paths whose
calling convention does not carry a cb:

- `Scene_commands::create_new_rendertarget`
- `Material_preview::render_preview` / `Brush_preview::render_preview`
- `Hud` / `Hotbar` rendertarget-mesh construction
- gltf parsing (`Image_transfer{device, *current_command_buffer}` in
  `editor::parsers::gltf` and `editor::scene::scene_serialization`)
- shadow node (re)configuration via `App_rendering`

This is also the **single source of truth** for the editor's init cb during
the constructor: `Init_status_display` takes a `Command_buffer*&` reference
to `m_app_context.current_command_buffer` so when its `render_present` ends
the init cb, drives a swapchain frame for the loading screen, and opens a
fresh init cb, every reader sees the new pointer. (Previously the editor
held a separate `m_init_command_buffer` member that became stale after
`render_present`; on GL, `wait_frame()` cleared `m_command_buffers` and the
stale pointer dangled into freed memory -- crash on `m_staging_buffer`
deref through `Device_impl`.)

## Migrated apps / libraries

- `hello_swap`: full migration (Vulkan + GL).
- `example`: full migration; runs on Vulkan.
- `rendering_test`: full migration; runs on Vulkan with the
  `Texture_heap::bind` and `memory_barrier` fixes.
- `hextiles`: full migration (Vulkan + GL). Constructor uses the
  `m_init_command_buffer` member-init-list pattern; `tick()` follows the
  hello_swap shape.
- `editor`: full migration (Vulkan + OpenGL); runs on Vulkan after the
  texture-layout fixes below. `Init_status_display::render_present` carries
  the loading-screen-during-init pattern: end + submit init cb, drive a
  fresh swapchain frame, open a fresh init cb, reseat the App_context
  pointer.

## Texture initial-layout transitions

Several places relied on the removed `Device::clear_texture` to put a
texture into a known layout before first sample. With clear_texture deleted
the textures stayed in `VK_IMAGE_LAYOUT_UNDEFINED`, and the first descriptor
binding tripped `VUID-vkCmdDraw-None-09600` ("expected
DEPTH_STENCIL_READ_ONLY_OPTIMAL / SHADER_READ_ONLY_OPTIMAL, current is
UNDEFINED"). Fix: explicit `command_buffer.transition_texture_layout(...)`
right after the texture is created.

Sites:
- `editor::Shadow_render_node::reconfigure(cb, ...)` -- shadowmap array
  layers that no light renders into stay in UNDEFINED unless the whole
  array is transitioned. ctor + `App_rendering::handle_graphics_settings_changed`
  pass `m_context.current_command_buffer`.
- `editor::Scene_preview` ctor -- the dummy 1x1 shadowmap is sampled by
  Forward_renderer's standard shaders even though Scene_preview itself
  never renders into it. Threaded `Command_buffer&` through `Material_preview`
  and `Brush_preview` constructors.
- `editor::Rendertarget_mesh::resize_rendertarget(cb, ...)` -- imgui can
  sample the rendertarget texture before the imgui-host render pass writes
  into it; transition to `shader_read_only_optimal` matches the render
  pass's `layout_before`.
- `editor::Thumbnails` ctor -- the array texture is referenced as a
  sampled descriptor before per-thumbnail render passes populate slots.

## Render_pass UNDEFINED-target guard

`Render_pass_impl::start_render_pass`'s deferred pre-transition predicate
now skips emitting a barrier when the target layout is
`VK_IMAGE_LAYOUT_UNDEFINED`. Post-processing render passes set
`layout_before = Image_layout::undefined` to declare "I do not care about
the previous contents" -- the render pass's own initialLayout handles
that fine, but the pre-transition path was issuing
`vkCmdPipelineBarrier2(... newLayout = UNDEFINED)`, which Vulkan rejects per
`VUID-VkImageMemoryBarrier2-newLayout-01198`.

## Known broken / pending

- **Metal**: `Command_buffer_impl` is still a stub; doesn't own a real
  `MTL::CommandBuffer`. The legacy `m_active_command_buffer` on Metal
  `Device_impl` still drives commit + presentDrawable but is unreachable
  through the new public API. Will need a real Metal `Command_buffer_impl`
  before the Metal backend works again.
- **`Vulkan Swapchain_impl::submit_command_buffer` / `end_frame`**: dead code,
  no callers, can be removed in a follow-up cleanup.
- **Slot fence (`Device_frame_in_flight::submit_fence`)**: still allocated by
  `Swapchain_impl::setup_frame` and bridged by `submit_command_buffers` for
  each swapchain submit. Should move off this entirely once the swapchain
  acquire-semaphore recycle is gated on the device timeline like the
  per-thread pools already are.
- **OpenXR + Vulkan submit timing**: `Editor::tick()` now does the
  `submit_command_buffers` after all rendergraph execution, so on OpenXR
  the submit currently lands AFTER `xrEndFrame` (driven from
  `Headset_view::render_headset` -> `xr_session.render_frame`). This is
  the same jitter/latency root cause described in
  `plans/editor-openxr-opengl-works-now-good-jolly-avalanche.md` -- move
  the submit into `Xr_session::render_frame` so it lands before
  `xrReleaseSwapchainImage`.

## Bugs found and fixed during migration

1. **hello_swap black screen** (Vulkan). `Render_pass_impl::start_render_pass`'s
   swapchain branch was recording `vkCmdBeginRenderPass2` into the legacy
   single-cb (returned from `Swapchain_impl::begin_render_pass`) while the
   user's cb was empty. The swapchain image was presented in UNDEFINED
   layout. Fixed by recording into the user-supplied cb directly.
2. **example descriptor-set-1 not bound**. `Texture_heap_impl::bind` was a
   no-op stub I'd left during the immediate-commands cleanup. Restored the
   `vkCmdBindDescriptorSets(set=1)` against the encoder's cb.
3. **rendering_test compute-->vertex sync hazard**. `Device_impl::memory_barrier`
   was gutted (`cb = VK_NULL_HANDLE; return;`). Lifted body into
   `Command_buffer_impl::memory_barrier`.
4. **GL hello_swap null-deref in `Command_buffer::wait_for_swapchain`**.
   `Device_impl::get_command_buffer` was returning `*nullptr`. Now allocates
   a real `Command_buffer` per call and clears the vector in `wait_frame`.
5. **`~Device_impl` order-of-destruction**. `m_sync_pool.reset()` was running
   before the per-thread pool wrapper clear, so the wrapper destructors
   recycled into a freed sync pool. Reordered: drop the wrappers first, then
   reset the sync pool.
6. **Editor GL crash on init: `m_staging_buffer` deref through dangling
   Command_buffer.** Editor held a separate `m_init_command_buffer` member
   that `Init_status_display::render_present` reseated after each loading-
   screen frame, but `App_context::current_command_buffer` was set once and
   never updated. On GL, `wait_frame()` clears `m_command_buffers` and the
   pointer in `App_context` dangled into freed memory; the next
   `Buffer_transfer_queue::flush(*current_command_buffer)` chased the
   freed `Command_buffer_impl` -> stale `m_device` -> faulted on
   `m_staging_buffer`. Fix: removed the duplicate `m_init_command_buffer`
   member; `Init_status_display` now reseats `App_context::current_command_buffer`
   directly so every reader (including parallel-init worker tasks) sees the
   fresh pointer.
7. **Vulkan validation: shadowmap unused layers in UNDEFINED layout.**
   `Shadow_render_node` allocates its shadowmap array sized for
   `light_count`, but per-light render passes only target layers
   `0..(active_light_count-1)`. The unused layers stayed in UNDEFINED and
   tripped `VUID-vkCmdDraw-None-09600` when the whole array was bound as a
   sampled descriptor. Fix: explicit `transition_texture_layout(...)` of
   the entire array right after creation. Same pattern applied to
   `Scene_preview::m_shadow_texture`, `Rendertarget_mesh::m_texture`, and
   `Thumbnails::m_color_texture`.
8. **Vulkan validation: pre-transition newLayout = UNDEFINED.**
   `Render_pass_impl::start_render_pass`'s deferred pre-transition was
   issuing `vkCmdPipelineBarrier2(... newLayout = UNDEFINED)` for render
   passes whose `layout_before` was `Image_layout::undefined` (post-
   processing's downsample/upsample passes use this to mean "don't care
   about previous contents"). Fix: skip emitting the barrier when the
   target layout is UNDEFINED -- the render pass's own initialLayout
   handles UNDEFINED correctly.
