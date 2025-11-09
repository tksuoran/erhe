#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_monitor.hpp"

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace erhe::graphics {


class Device;
class Device_impl final
{
public:
    Device_impl   (Device& device, erhe::window::Context_window& context_window);
    Device_impl   (const Device_impl&) = delete;
    void operator=(const Device_impl&) = delete;
    Device_impl   (Device_impl&&)      = delete;
    void operator=(Device_impl&&)      = delete;
    ~Device_impl  ();

    void start_of_frame        ();
    void end_of_frame          ();
    void memory_barrier        (Memory_barrier_mask barriers);
    void clear_texture         (Texture& texture, std::array<double, 4> clear_value);
    void upload_to_buffer      (Buffer& buffer, size_t offset, const void* data, size_t length);
    void add_completion_handler(std::function<void()> callback);
    void on_thread_enter       ();

    [[nodiscard]] auto get_handle                  (const Texture& texture, const Sampler& sampler) const -> uint64_t;
    [[nodiscard]] auto create_dummy_texture        () -> std::shared_ptr<Texture>;
    [[nodiscard]] auto get_buffer_alignment        (Buffer_target target) -> std::size_t;
    [[nodiscard]] auto get_frame_number            () const -> uint64_t;
    [[nodiscard]] auto allocate_ring_buffer_entry  (Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range;
    [[nodiscard]] auto make_blit_command_encoder   () -> Blit_command_encoder;
    [[nodiscard]] auto make_compute_command_encoder() -> Compute_command_encoder;
    [[nodiscard]] auto make_render_command_encoder (Render_pass& render_pass) -> Render_command_encoder;
    [[nodiscard]] auto get_format_properties       (erhe::dataformat::Format format) const -> Format_properties;
    [[nodiscard]] auto choose_depth_stencil_format (unsigned int flags, int sample_count) const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_shader_monitor          () -> Shader_monitor&;
    [[nodiscard]] auto get_info                    () const -> const Device_info&;

private:
    void frame_completed(uint64_t frame);

    erhe::window::Context_window& m_context_window;
    Device&                       m_device;
    Shader_monitor                m_shader_monitor;
    Device_info                   m_info;
    class Completion_handler
    {
    public:
        uint64_t              frame_number;
        std::function<void()> callback;
    };
    std::vector<std::unique_ptr<Ring_buffer>> m_ring_buffers;
    uint64_t                                  m_frame_number{1};
    std::vector<Completion_handler>           m_completion_handlers;
};


} // namespace erhe::graphics
