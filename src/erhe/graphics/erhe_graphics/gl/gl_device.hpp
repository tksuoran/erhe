#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/gl/gl_binding_state.hpp"
#include "erhe_graphics/gl/gl_objects.hpp"
#include "erhe_graphics/gl/gl_state_tracker.hpp"
#include "erhe_graphics/gl/gl_context_index.hpp"
#include "erhe_graphics/shader_monitor.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <source_location>
#include <thread>
#include <unordered_map>
#include <vector>

namespace erhe::dataformat {
    enum class Format : unsigned int;
}

namespace erhe::graphics {

class Frame_sync
{
public:
    uint64_t        frame_number{0};
    void*           fence_sync  {nullptr};
    gl::Sync_status result      {gl::Sync_status::timeout_expired};
};

class Command_buffer;
class Frame_state;
class Frame_end_info;
class Render_pass_impl;
class Ring_buffer_client;
class Surface;
class Swapchain;

// Mirrors Vulkan's Device_frame_state. Tracks lifecycle of new-style
// begin_swapchain_frame / end_swapchain_frame API, and backs
// is_in_device_frame / is_in_swapchain_frame.
enum class Device_frame_state : uint8_t
{
    idle,
    waited,
    recording,
    in_swapchain_frame
};

// Shared (share-group-wide) GL object kinds whose deletion must scrub
// every context's binding cache, not only the deleting context's.
// Container objects (vertex arrays, framebuffers) are per-context and go
// through the deferred container-delete queues instead.
enum class Gl_shared_object_kind : int {
    buffer = 0,
    texture,
    sampler,
    renderbuffer,
    program
};

class Device;
class Device_impl final
{
public:
    Device_impl   (Device& device, const Surface_create_info& surface_create_info, const Graphics_config& graphics_config = {});
    Device_impl   (const Device_impl&) = delete;
    void operator=(const Device_impl&) = delete;
    Device_impl   (Device_impl&&)      = delete;
    void operator=(Device_impl&&)      = delete;
    ~Device_impl() noexcept;

    [[nodiscard]] auto wait_frame () -> bool;
    [[nodiscard]] auto begin_frame() -> bool;
    [[nodiscard]] auto end_frame  () -> bool;
    [[nodiscard]] auto begin_frame(const Frame_begin_info& frame_begin_info) -> bool;
    [[nodiscard]] auto end_frame  (const Frame_end_info& frame_end_info) -> bool;

    void               wait_idle            ();
    void               clear_render_pipeline_cache();
    [[nodiscard]] auto recreate_surface_for_new_window() -> bool;
    [[nodiscard]] auto is_in_swapchain_frame() const -> bool;

    // Transitional: see Vulkan Device_impl. Command_buffer_impl took
    // over the swapchain-frame entry points and still needs to flip
    // m_had_swapchain_frame for the legacy end_frame path.
    friend class Command_buffer_impl;

    void resize_swapchain_to_window();
    void start_frame_capture       ();
    void end_frame_capture         ();

    // Active render pass tracking. Per-context, not process-wide: a GL
    // context is current on exactly one thread, so thread_local keys the
    // active pass by the calling thread's current context.
    static thread_local Render_pass_impl* s_active_render_pass;
    void memory_barrier            (Memory_barrier_mask barriers);
    void clear_texture             (const Texture& texture, std::array<double, 4> clear_value);
    void transition_texture_layout (const Texture& texture, Image_layout new_layout);
    void cmd_texture_barrier       (uint64_t usage_before, uint64_t usage_after);
    [[nodiscard]] auto get_command_buffer(unsigned int thread_slot) -> Command_buffer&;
    void submit_command_buffers    (std::span<Command_buffer* const> command_buffers);
    void submit_command_buffer_and_wait(Command_buffer& command_buffer);
    void upload_to_buffer          (const Buffer& buffer, size_t offset, const void* data, size_t length);
    void upload_to_texture         (const Texture& texture, int level, int x, int y, int width, int height, erhe::dataformat::Format pixelformat, const void* data, int row_stride);
    void add_completion_handler    (std::function<void(Device_impl&)> callback);
    void on_thread_enter           ();

    // glDebugMessageCallback is per-context GL state: call this once per
    // context, while that context is current - the Device constructor does
    // it for the main context, worker share contexts do it at creation.
    // Without it every GL error a worker raises is silently discarded.
    void install_gl_debug_callback ();

    [[nodiscard]] auto get_surface                        () -> Surface*;
    // Present-wait clamp (frame pacing FR5): no present-wait path on GL.
    [[nodiscard]] auto wait_for_displayed_frame           (std::int64_t frame_id, uint64_t timeout_ns) -> Present_wait_result;
    [[nodiscard]] auto get_frame_pacing_tier              () const -> Frame_pacing_tier;
    // Target present time (frame pacing FR3): no present-timing path on GL.
    void               set_present_target_time            (std::int64_t frame_id, double target_time_seconds, double hold_until_seconds);
    [[nodiscard]] auto get_native_handles                 () const -> Native_device_handles;
    [[nodiscard]] auto get_handle                         (const Texture& texture, const Sampler& sampler) const -> uint64_t;
    [[nodiscard]] auto create_dummy_texture               (Command_buffer& init_command_buffer, erhe::dataformat::Format format) -> std::shared_ptr<Texture>;
    [[nodiscard]] auto get_buffer_alignment               (Buffer_target target) -> std::size_t;
    [[nodiscard]] auto get_frame_index                    () const -> uint64_t;
    [[nodiscard]] auto get_number_of_frames_in_flight     () const -> std::size_t;
    // Conservative: see Device::is_frame_completed().
    [[nodiscard]] auto is_frame_completed                 (uint64_t frame) const -> bool;
    [[nodiscard]] auto allocate_ring_buffer_entry         (Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range;
    [[nodiscard]] auto make_blit_command_encoder          (Command_buffer& command_buffer) -> Blit_command_encoder;
    [[nodiscard]] auto make_compute_command_encoder       (Command_buffer& command_buffer) -> Compute_command_encoder;
    [[nodiscard]] auto make_render_command_encoder        (Command_buffer& command_buffer) -> Render_command_encoder;
    [[nodiscard]] auto get_format_properties              (erhe::dataformat::Format format) const -> Format_properties;
    [[nodiscard]] auto probe_image_format_support         (erhe::dataformat::Format format, uint64_t usage_mask) const -> bool;
    [[nodiscard]] auto get_supported_depth_stencil_formats() const -> std::vector<erhe::dataformat::Format>;
                  void sort_depth_stencil_formats         (std::vector<erhe::dataformat::Format>& formats, unsigned int sort_flags, int requested_sample_count) const;
    [[nodiscard]] auto choose_depth_stencil_format        (const std::vector<erhe::dataformat::Format>& formats) const -> erhe::dataformat::Format;
    [[nodiscard]] auto choose_depth_stencil_format        (unsigned int sort_flags, int requested_sample_count) const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_shader_monitor                 () -> Shader_monitor&;
    [[nodiscard]] auto get_info                           () const -> const Device_info&;
    [[nodiscard]] auto get_graphics_config                () const -> const Graphics_config&;
    // GL has no portable VRAM budget query; zeros = unknown.
    [[nodiscard]] auto get_memory_budget                  () const -> Memory_budget { return {}; }

    void reset_shader_stages_state_tracker();
    [[nodiscard]] auto get_draw_id_uniform_location() const -> GLint;

    // Temporarily binds `program` and returns an RAII guard that
    // restores the previously-bound program (and updates the cache that
    // backs Render_command_encoder::set_render_pipeline) on destruction.
    // Intended for setup code that must glUseProgram a program for
    // non-render purposes -- e.g. setting sampler uniforms after
    // glLinkProgram on GLSL < 4.30 -- without leaving
    // Shader_stages_tracker desynchronized from real GL state.
    [[nodiscard]] auto push_program(unsigned int program) -> Program_binding_guard;

    // The CURRENT context's software caches, resolved through the
    // thread-local context index. Hot paths (command encoders, render
    // passes) resolve once at construction / pass start and hold the
    // reference rather than paying the TLS lookup per call.
    [[nodiscard]] auto get_binding_state() -> Gl_binding_state&;
    [[nodiscard]] auto get_state_tracker() -> OpenGL_state_tracker&;

    // Persistent empty VAO bound by the vertex-input tracker for draws whose
    // pipeline declares no vertex input. Created eagerly per context - the
    // main context's instance by create_per_context_resources() - so the
    // const per-draw substitution path only ever reads an already-populated
    // own-context slot.
    [[nodiscard]] auto get_default_vertex_input_state() -> const Vertex_input_state*;

    // Called from Device::Device's BODY, after m_impl is wired: creates the
    // per-context resources whose lifetime reaches back through the public
    // Device (Vertex_input_state_impl's destructor goes through
    // Device::get_impl(), which is a null dereference while Device_impl's
    // constructor runs, so the object must not be constructed before m_impl
    // is assigned).
    void create_per_context_resources();

    // Per-context deferred container-object deletion. A per-context GL
    // object (VAO, framebuffer) can only be deleted on its own context, so
    // a destructor running on some other context queues the name here for
    // the owning context to delete. Each context drains its own queue:
    // future worker contexts at acquire, and the MAIN context in
    // wait_frame() - it never becomes current again, so "drain on next
    // make-current" would never fire for it.
    void queue_vertex_array_delete_on_context(int context_index, unsigned int name);
    void queue_framebuffer_delete_on_context (int context_index, unsigned int name);
    void drain_container_object_deletes_for_current_context();
    // TEST HOOK (erhe_graphics_gpu_tests): container-object deletes queued
    // for a context and not yet drained on it. Observes the deferred-delete
    // mechanism; not for production use.
    [[nodiscard]] auto get_pending_container_delete_count(int context_index) -> std::size_t;

    // Shared-object deletion, called from Gl_* destructors right before the
    // glDelete* call. Scrubs the CURRENT context's binding cache directly
    // (GL auto-unbinds the object in the deleting context, so this mirrors
    // that cache-only), and enqueues a scrub entry - with the target
    // context's bind-epoch snapshot, the name-recycling guard - for every
    // OTHER live context. Each context drains its own queue at the same
    // points as the container-delete queues: the MAIN context in
    // wait_frame(), future worker contexts at acquire.
    void on_shared_object_deleted(Gl_shared_object_kind kind, unsigned int name);
    void drain_shared_object_scrubs_for_current_context();

    // Worker share-context pool (behind Scoped_worker_context - call sites
    // never touch these directly). The pool is created eagerly, on the main
    // thread, in create_per_context_resources(); it is empty when the
    // device has no window to share from (headless / null window), and
    // supports_worker_contexts() is then false.
    [[nodiscard]] auto supports_worker_contexts() const -> bool;
    // Blocks until a pool context is free, makes it current on the calling
    // thread, sets the thread's context index, and drains this context's
    // deferred container-delete and shared-scrub queues. Returns the
    // context slot index (1..pool size). The calling thread must have no
    // context current (context index -1). The location is the construction
    // site of the acquiring Scoped_worker_context, recorded per slot so the
    // acquire watchdog can name the holders of a wedged pool.
    [[nodiscard]] auto acquire_worker_context_slot(const std::source_location& location) -> int;
    // Clears the context from the calling thread, resets the context index
    // to -1, and returns the slot to the pool.
    void release_worker_context_slot(int slot);

    // GL object creation
    [[nodiscard]] auto create_texture     (gl::Texture_target target) -> Gl_texture;
    [[nodiscard]] auto create_texture_view(gl::Texture_target target) -> Gl_texture;
    [[nodiscard]] auto create_buffer      () -> Gl_buffer;
    [[nodiscard]] auto create_renderbuffer() -> Gl_renderbuffer;
    [[nodiscard]] auto create_sampler     () -> Gl_sampler;
    [[nodiscard]] auto create_query       (gl::Query_target target) -> Gl_query;
    [[nodiscard]] auto create_program     () -> Gl_program;
    [[nodiscard]] auto create_shader      (gl::Shader_type type) -> Gl_shader;

private:
    void frame_completed(uint64_t frame);

    using PFN_generic          = void (*) ();
    using PFN_get_proc_address = PFN_generic (*) (const char*);

    friend class Blit_command_encoder_impl;
    friend class Compute_command_encoder_impl;
    friend class Render_command_encoder_impl;
    friend class Render_pass_impl;

    Device&                       m_device;
    Graphics_config               m_graphics_config;
    std::unique_ptr<Surface>      m_surface{};
    Shader_monitor                m_shader_monitor;
    // Per-context software caches: one wired {tracker, binding state} pair
    // per context slot (main = 0, worker pool contexts 1..), indexed by the
    // thread-local context index. The constructor wires each tracker to its
    // own binding state; the pairs are never mixed - each context's tracker
    // describes only that context's GL state.
    std::array<OpenGL_state_tracker, gl_context_slot_count> m_gl_state_trackers;
    std::array<Gl_binding_state, gl_context_slot_count>     m_gl_binding_states;
    Device_info                   m_info;
    erhe::window::Context_window* m_context_window{nullptr};

    // Worker share-context pool: entry i backs context slot i + 1 (slot 0
    // is the main context). Populated in create_per_context_resources()
    // (Device::Device's body - share-context creation make-currents the
    // main context, so it is main-thread and must not run while a frame is
    // in flight); empty when there is no window to share from. Declared
    // BEFORE m_default_vertex_input_state: destroying the per-context
    // default VAOs queues deferred deletes on pool slots, and the pool
    // contexts must still exist at that point (the queued names then die
    // with their contexts). Teardown requires no worker holding a pool
    // context current (SDL_GL_DestroyContext un-currents only the calling
    // thread); the application quiesces its executor before destroying the
    // Device.
    std::vector<std::unique_ptr<erhe::window::Context_window>> m_worker_context_windows;
    std::mutex              m_worker_context_pool_mutex;
    std::condition_variable m_worker_context_pool_condition;
    std::vector<int>        m_free_worker_context_slots;
    // Holder of each pool slot (index = slot - 1), for the acquire
    // watchdog's report; guarded by m_worker_context_pool_mutex. Proposal E
    // of doc/gl_worker_context_enforcement.md: a wedged pool looks BUSY
    // (parked parents spin in _corun_until), so without a report naming the
    // holders nothing points at the context pool at all.
    class Worker_context_slot_holder
    {
    public:
        std::thread::id      thread_id   {};
        std::source_location acquire_site{};
    };
    std::array<Worker_context_slot_holder, gl_worker_context_pool_size> m_worker_context_slot_holders;

    // Persistent empty VAO bound for draws whose pipeline declares no vertex
    // input (core-profile GL rejects glDraw* with VAO 0). Created eagerly by
    // create_per_context_resources() - from Device::Device's BODY, because
    // Vertex_input_state construction goes through Device::get_impl() and
    // Device::m_impl is still null while Device_impl's constructor runs.
    // Declared after the worker context pool so it is destroyed first,
    // while the pool contexts (and the main context) still exist.
    std::unique_ptr<Vertex_input_state> m_default_vertex_input_state;

    // See queue_*_delete_on_context() above. One queue per context; the
    // mutex serializes producers (destructors on other contexts) against
    // the owning context's drain.
    class Deferred_container_deletes
    {
    public:
        std::mutex                mutex;
        std::vector<unsigned int> vertex_arrays;
        std::vector<unsigned int> framebuffers;
    };
    std::array<Deferred_container_deletes, gl_context_slot_count> m_deferred_container_deletes;

    // See on_shared_object_deleted() above. One queue per context; the
    // mutex serializes producers (deleting threads on other contexts)
    // against the owning context's drain.
    class Deferred_shared_object_scrubs
    {
    public:
        class Entry
        {
        public:
            Gl_shared_object_kind kind;
            unsigned int          name;
            // The target context's bind epoch at enqueue time; a slot
            // bound after this epoch holds a recycled name and is not
            // scrubbed.
            uint64_t              enqueue_epoch;
        };
        std::mutex         mutex;
        std::vector<Entry> entries;
    };
    std::array<Deferred_shared_object_scrubs, gl_context_slot_count> m_deferred_shared_object_scrubs;

    // Which context slots have a live GL context (and so will eventually
    // drain their queues). Producers skip dead slots - enqueueing to a
    // context that never exists (headless, or before the worker pool is
    // created) would grow its queue without bound. Slot 0 (main) is set in
    // the constructor; worker slots are set when the pool context is
    // created (commit 12).
    std::array<std::atomic<bool>, gl_context_slot_count> m_context_slot_live{};

    std::unordered_map<gl::Internal_format, Format_properties> format_properties;

    std::vector<std::unique_ptr<Ring_buffer>> m_ring_buffers;
    std::size_t                               m_min_buffer_size = 2 * 1024 * 1024; // TODO

    std::array<Frame_sync, 16>            m_frame_syncs;
    // Sizing hint only; see get_number_of_frames_in_flight().
    static constexpr std::size_t          s_number_of_frames_in_flight = 3;
    uint64_t                              m_frame_index{1};
    Device_frame_state                    m_state{Device_frame_state::idle};
    bool                                  m_had_swapchain_frame{false};
    std::chrono::steady_clock::time_point m_last_ok_frame_timestamp;
    std::vector<uint64_t>                 m_pending_frames;
    std::vector<uint64_t>                 m_completed_frames;
    // One past the highest frame the GPU has retired, i.e. the same meaning
    // the Vulkan backend's watermark has. GL fences are in-order and a
    // signaled fence for frame N proves every command submitted before it is
    // done, so frames that never got a fence (end_frame() only creates one
    // when something asked for a sync) are covered by the next fenced frame
    // rather than needing one of their own. Only ever moves forward.
    uint64_t                              m_latest_completed_frame{0};
    bool                                  m_need_sync{false};

    std::unique_ptr<Ring_buffer_client>   m_staging_buffer;

    // GL has no native command buffer object. Each call to
    // get_command_buffer() allocates a fresh wrapper here; the
    // wrappers are kept alive until the next wait_frame() so anything
    // submit_command_buffers / start_render_pass / encoders captured
    // by reference stays valid for the duration of a frame's recording.
    // wait_frame() clears the vector at the start of each frame.
    std::vector<std::unique_ptr<Command_buffer>> m_command_buffers;

    // RenderDoc
    //erhe::window::Context_window* m_context_window           {nullptr};

    class Completion_handler
    {
    public:
        uint64_t                          frame_number;
        std::function<void(Device_impl&)> callback;
    };
    std::vector<Completion_handler> m_completion_handlers;
};

} // namespace erhe::graphics
