#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/gl/gl_binding_state.hpp"
#include "erhe_graphics/gl/gl_context_provider.hpp"
#include "erhe_graphics/gl/gl_objects.hpp"
#include "erhe_graphics/gl/gl_state_tracker.hpp"
#include "erhe_graphics/shader_monitor.hpp"

#include <array>
#include <chrono>
#include <memory>
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
    [[nodiscard]] auto is_in_swapchain_frame() const -> bool;

    // Transitional: see Vulkan Device_impl. Command_buffer_impl took
    // over the swapchain-frame entry points and still needs to flip
    // m_had_swapchain_frame for the legacy end_frame path.
    friend class Command_buffer_impl;

    void resize_swapchain_to_window();
    void start_frame_capture       ();
    void end_frame_capture         ();

    // Active render pass tracking
    static Render_pass_impl* s_active_render_pass;
    void memory_barrier            (Memory_barrier_mask barriers);
    void clear_texture             (const Texture& texture, std::array<double, 4> clear_value);
    void transition_texture_layout (const Texture& texture, Image_layout new_layout);
    void cmd_texture_barrier       (uint64_t usage_before, uint64_t usage_after);
    [[nodiscard]] auto get_command_buffer(unsigned int thread_slot) -> Command_buffer&;
    void submit_command_buffers    (std::span<Command_buffer* const> command_buffers);
    void upload_to_buffer          (const Buffer& buffer, size_t offset, const void* data, size_t length);
    void upload_to_texture         (const Texture& texture, int level, int x, int y, int width, int height, erhe::dataformat::Format pixelformat, const void* data, int row_stride);
    void add_completion_handler    (std::function<void(Device_impl&)> callback);
    void on_thread_enter           ();

    [[nodiscard]] auto get_surface                        () -> Surface*;
    [[nodiscard]] auto get_native_handles                 () const -> Native_device_handles;
    [[nodiscard]] auto get_handle                         (const Texture& texture, const Sampler& sampler) const -> uint64_t;
    [[nodiscard]] auto create_dummy_texture               (Command_buffer& init_command_buffer, erhe::dataformat::Format format) -> std::shared_ptr<Texture>;
    [[nodiscard]] auto get_buffer_alignment               (Buffer_target target) -> std::size_t;
    [[nodiscard]] auto get_frame_index                    () const -> uint64_t;
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

    void reset_shader_stages_state_tracker();
    [[nodiscard]] auto get_draw_id_uniform_location() const -> GLint;
    [[nodiscard]] auto get_binding_state() -> Gl_binding_state&;

    // GL object creation
    [[nodiscard]] auto create_texture     (gl::Texture_target target) -> Gl_texture;
    [[nodiscard]] auto create_texture_view(gl::Texture_target target) -> Gl_texture;
    [[nodiscard]] auto create_buffer      () -> Gl_buffer;
    [[nodiscard]] auto create_framebuffer () -> Gl_framebuffer;
    [[nodiscard]] auto create_renderbuffer() -> Gl_renderbuffer;
    [[nodiscard]] auto create_sampler     () -> Gl_sampler;
    [[nodiscard]] auto create_vertex_array() -> Gl_vertex_array;
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
    OpenGL_state_tracker          m_gl_state_tracker;
    Gl_binding_state              m_gl_binding_state;
    Gl_context_provider           m_gl_context_provider;
    Device_info                   m_info;
    erhe::window::Context_window* m_context_window{nullptr};

    std::unordered_map<gl::Internal_format, Format_properties> format_properties;

    std::vector<std::unique_ptr<Ring_buffer>> m_ring_buffers;
    std::size_t                               m_min_buffer_size = 2 * 1024 * 1024; // TODO

    std::array<Frame_sync, 16>            m_frame_syncs;
    uint64_t                              m_frame_index{1};
    Device_frame_state                    m_state{Device_frame_state::idle};
    bool                                  m_had_swapchain_frame{false};
    std::chrono::steady_clock::time_point m_last_ok_frame_timestamp;
    std::vector<uint64_t>                 m_pending_frames;
    std::vector<uint64_t>                 m_completed_frames;
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
