#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/shader_monitor.hpp"

#include <memory>

typedef int GLint;

namespace MTL { class ArgumentEncoder; }
namespace MTL { class Device; }
namespace MTL { class CommandQueue; }
namespace MTL { class LogState; }
namespace MTL { class SamplerState; }

namespace erhe::dataformat {
    enum class Format : unsigned int;
}

namespace erhe::graphics {

class Frame_state;
class Frame_end_info;
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

    [[nodiscard]] auto wait_frame (Frame_state& out_frame_state) -> bool;
    [[nodiscard]] auto begin_frame(const Frame_begin_info& frame_begin_info) -> bool;
    [[nodiscard]] auto end_frame  (const Frame_end_info& frame_end_info) -> bool;

    void resize_swapchain_to_window();
    void initialize_frame_capture  ();
    void start_frame_capture       ();
    void end_frame_capture         ();
    void memory_barrier            (Memory_barrier_mask barriers);
    void clear_texture             (const Texture& texture, std::array<double, 4> clear_value);
    void transition_texture_layout (const Texture& texture, Image_layout new_layout);
    void upload_to_buffer          (const Buffer& buffer, size_t offset, const void* data, size_t length);
    void upload_to_texture         (const Texture& texture, int level, int x, int y, int width, int height, erhe::dataformat::Format pixelformat, const void* data, int row_stride);
    void add_completion_handler    (std::function<void()> callback);
    void on_thread_enter           ();

    [[nodiscard]] auto get_surface                        () -> Surface*;
    [[nodiscard]] auto get_handle                         (const Texture& texture, const Sampler& sampler) const -> uint64_t;
    [[nodiscard]] auto create_dummy_texture               (erhe::dataformat::Format format) -> std::shared_ptr<Texture>;
    [[nodiscard]] auto get_buffer_alignment               (Buffer_target target) -> std::size_t;
    [[nodiscard]] auto get_frame_index                    () const -> uint64_t;
    [[nodiscard]] auto allocate_ring_buffer_entry         (Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range;
    [[nodiscard]] auto make_blit_command_encoder          () -> Blit_command_encoder;
    [[nodiscard]] auto make_compute_command_encoder       () -> Compute_command_encoder;
    [[nodiscard]] auto make_render_command_encoder        () -> Render_command_encoder;
    [[nodiscard]] auto get_format_properties              (erhe::dataformat::Format format) const -> Format_properties;
    [[nodiscard]] auto get_supported_depth_stencil_formats() const -> std::vector<erhe::dataformat::Format>;
                  void sort_depth_stencil_formats         (std::vector<erhe::dataformat::Format>& formats, unsigned int sort_flags, int requested_sample_count) const;
    [[nodiscard]] auto choose_depth_stencil_format        (const std::vector<erhe::dataformat::Format>& formats) const -> erhe::dataformat::Format;
    [[nodiscard]] auto choose_depth_stencil_format        (unsigned int sort_flags, int requested_sample_count) const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_shader_monitor                 () -> Shader_monitor&;
    [[nodiscard]] auto get_info                           () const -> const Device_info&;

    void reset_shader_stages_state_tracker();
    [[nodiscard]] auto get_draw_id_uniform_location() const -> GLint;

    [[nodiscard]] auto get_mtl_device              () const -> MTL::Device*;
    [[nodiscard]] auto get_mtl_command_queue       () const -> MTL::CommandQueue*;
    [[nodiscard]] auto get_default_sampler_state   () const -> MTL::SamplerState*;

    // Texture argument buffer support
    [[nodiscard]] auto get_texture_argument_encoder() const -> MTL::ArgumentEncoder*;
                  void set_texture_argument_encoder(MTL::ArgumentEncoder* encoder);

    // Argument buffer resource index mapping (GLSL binding -> arg buffer id)
    struct Texture_arg_buffer_layout
    {
        uint32_t shadow_compare_texture_id    {0xFFFFFFFFu};
        uint32_t shadow_compare_sampler_id    {0xFFFFFFFFu};
        uint32_t shadow_no_compare_texture_id {0xFFFFFFFFu};
        uint32_t shadow_no_compare_sampler_id {0xFFFFFFFFu};
        uint32_t material_texture_base_id     {0xFFFFFFFFu};
        uint32_t material_sampler_base_id     {0xFFFFFFFFu};
        uint32_t argument_buffer_index        {2}; // [[buffer(N)]]
        bool     valid{false};
    };
    [[nodiscard]] auto get_texture_arg_buffer_layout() const -> const Texture_arg_buffer_layout&;
                  void set_texture_arg_buffer_layout(const Texture_arg_buffer_layout& layout);

private:
    Device&                  m_device;
    std::unique_ptr<Surface> m_surface;
    Shader_monitor           m_shader_monitor;
    Device_info              m_info;
    uint64_t                 m_frame_index{1};
    MTL::Device*             m_mtl_device          {nullptr};
    MTL::CommandQueue*       m_mtl_command_queue   {nullptr};
    MTL::SamplerState*       m_default_sampler_state{nullptr};
    MTL::LogState*           m_mtl_log_state        {nullptr};
    MTL::ArgumentEncoder*    m_texture_argument_encoder{nullptr};
    Texture_arg_buffer_layout m_texture_arg_buffer_layout;
    std::vector<std::unique_ptr<Ring_buffer>> m_ring_buffers;
    static constexpr std::size_t m_min_buffer_size{4 * 1024 * 1024};
};

void install_metal_error_handler(Device& device);

} // namespace erhe::graphics
