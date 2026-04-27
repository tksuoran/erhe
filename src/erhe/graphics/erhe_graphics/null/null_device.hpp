#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_monitor.hpp"

#include <memory>

typedef int GLint;

namespace erhe::dataformat {
    enum class Format : unsigned int;
}

namespace erhe::graphics {

class Frame_state;
class Frame_end_info;
class Render_pass_impl;
class Ring_buffer_client;
class Surface;
class Swapchain;

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

    void resize_swapchain_to_window();
    void start_frame_capture       ();
    void end_frame_capture         ();

    // Active render pass tracking
    static Render_pass_impl* s_active_render_pass;
    void set_queue_annotation      (const char* key, bool value);
    void set_queue_annotation      (const char* key, int32_t value);
    void set_queue_annotation      (const char* key, uint32_t value);
    void set_queue_annotation      (const char* key, int64_t value);
    void set_queue_annotation      (const char* key, uint64_t value);
    void set_queue_annotation      (const char* key, float value);
    void set_queue_annotation      (const char* key, double value);
    void set_queue_annotation      (const char* key, const char* value);
    void clear_queue_annotation    (const char* key);
    void set_command_annotation    (const char* key, bool value);
    void set_command_annotation    (const char* key, int32_t value);
    void set_command_annotation    (const char* key, uint32_t value);
    void set_command_annotation    (const char* key, int64_t value);
    void set_command_annotation    (const char* key, uint64_t value);
    void set_command_annotation    (const char* key, float value);
    void set_command_annotation    (const char* key, double value);
    void set_command_annotation    (const char* key, const char* value);
    void clear_command_annotation  (const char* key);
    void set_object_annotation     (uint64_t object_handle, const char* key, bool value);
    void set_object_annotation     (uint64_t object_handle, const char* key, int32_t value);
    void set_object_annotation     (uint64_t object_handle, const char* key, uint32_t value);
    void set_object_annotation     (uint64_t object_handle, const char* key, int64_t value);
    void set_object_annotation     (uint64_t object_handle, const char* key, uint64_t value);
    void set_object_annotation     (uint64_t object_handle, const char* key, float value);
    void set_object_annotation     (uint64_t object_handle, const char* key, double value);
    void set_object_annotation     (uint64_t object_handle, const char* key, const char* value);
    void clear_object_annotation   (uint64_t object_handle, const char* key);
    void memory_barrier            (Memory_barrier_mask barriers);
    void clear_texture             (const Texture& texture, std::array<double, 4> clear_value);
    void transition_texture_layout (const Texture& texture, Image_layout new_layout);
    void cmd_texture_barrier       (uint64_t usage_before, uint64_t usage_after);
    [[nodiscard]] auto get_command_buffer(unsigned int thread_slot) -> Command_buffer&;
    void submit_command_buffers    (std::span<Command_buffer* const> command_buffers);
    void upload_to_buffer          (const Buffer& buffer, size_t offset, const void* data, size_t length);
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

private:
    Device&                  m_device;
    Graphics_config          m_graphics_config;
    std::unique_ptr<Surface> m_surface;
    Shader_monitor           m_shader_monitor;
    Device_info              m_info;
    uint64_t                 m_frame_index{1};
};

} // namespace erhe::graphics
