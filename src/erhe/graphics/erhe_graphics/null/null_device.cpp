#include "erhe_graphics/null/null_device.hpp"
#include "erhe_graphics/null/null_surface.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/texture.hpp"

#include "erhe_dataformat/dataformat.hpp"

namespace erhe::graphics {

Device_impl::Device_impl(Device& device, const Surface_create_info& surface_create_info, const Graphics_config& graphics_config)
    : m_device        {device}
    , m_shader_monitor{device}
{
    static_cast<void>(graphics_config);

    // Initialize Device_info with reasonable defaults for headless/server mode
    m_info.glsl_version                             = 460;
    m_info.max_texture_size                         = 16384;
    m_info.max_samples                              = 8;
    m_info.max_color_texture_samples                = 8;
    m_info.max_depth_texture_samples                = 8;
    m_info.max_framebuffer_samples                  = 8;
    m_info.max_integer_samples                      = 8;
    m_info.max_vertex_attribs                       = 16;
    m_info.use_persistent_buffers                   = true;
    m_info.use_compute_shader                       = true;
    m_info.use_multi_draw_indirect_core             = true;
    m_info.uniform_buffer_offset_alignment          = 256;
    m_info.shader_storage_buffer_offset_alignment   = 256;
    m_info.max_uniform_buffer_bindings              = 84;
    m_info.max_shader_storage_buffer_bindings       = 16;
    m_info.max_combined_texture_image_units         = 32;
    m_info.max_3d_texture_size                      = 2048;
    m_info.max_cube_map_texture_size                = 16384;
    m_info.max_texture_buffer_size                  = 134217728;
    m_info.max_array_texture_layers                 = 2048;
    m_info.max_uniform_block_size                   = 65536;
    m_info.max_per_stage_descriptor_samplers        = 16;
    m_info.max_compute_workgroup_count[0]           = 65535;
    m_info.max_compute_workgroup_count[1]           = 65535;
    m_info.max_compute_workgroup_count[2]           = 65535;
    m_info.max_compute_workgroup_size[0]            = 1024;
    m_info.max_compute_workgroup_size[1]            = 1024;
    m_info.max_compute_workgroup_size[2]            = 64;
    m_info.max_compute_work_group_invocations       = 1024;
    m_info.max_compute_shared_memory_size           = 49152;
    m_info.max_vertex_shader_storage_blocks         = 16;
    m_info.max_vertex_uniform_blocks                = 14;
    m_info.max_vertex_uniform_vectors               = 4096;
    m_info.max_fragment_shader_storage_blocks       = 16;
    m_info.max_fragment_uniform_blocks              = 14;
    m_info.max_fragment_uniform_vectors             = 4096;
    m_info.max_geometry_shader_storage_blocks       = 16;
    m_info.max_geometry_uniform_blocks              = 14;
    m_info.max_tess_control_shader_storage_blocks   = 16;
    m_info.max_tess_control_uniform_blocks          = 14;
    m_info.max_tess_evaluation_shader_storage_blocks = 16;
    m_info.max_tess_evaluation_uniform_blocks       = 14;
    m_info.max_compute_shader_storage_blocks        = 16;
    m_info.max_compute_uniform_blocks               = 14;
    m_info.max_texture_max_anisotropy               = 16.0f;
    m_info.max_depth_layers                         = 4;
    m_info.max_depth_resolution                     = 4096;

    if (surface_create_info.context_window != nullptr) {
        m_surface = std::make_unique<Surface>(
            std::make_unique<Surface_impl>(*this, surface_create_info)
        );
    }
}

Device_impl::~Device_impl() noexcept = default;

auto Device_impl::wait_frame(Frame_state& out_frame_state) -> bool
{
    out_frame_state.predicted_display_time   = 0;
    out_frame_state.predicted_display_period = 0;
    out_frame_state.should_render            = true;
    return true;
}

auto Device_impl::begin_frame(const Frame_begin_info& frame_begin_info) -> bool
{
    static_cast<void>(frame_begin_info);
    return true;
}

auto Device_impl::end_frame(const Frame_end_info& frame_end_info) -> bool
{
    static_cast<void>(frame_end_info);
    ++m_frame_index;
    return true;
}

auto Device_impl::get_surface() -> Surface*
{
    return m_surface.get();
}

void Device_impl::resize_swapchain_to_window()
{
    // No-op for null backend
}

void Device_impl::memory_barrier(const Memory_barrier_mask barriers)
{
    static_cast<void>(barriers);
    // No-op for null backend
}

void Device_impl::clear_texture(const Texture& texture, const std::array<double, 4> clear_value)
{
    static_cast<void>(texture);
    static_cast<void>(clear_value);
    // No-op for null backend
}

void Device_impl::upload_to_buffer(const Buffer& buffer, const size_t offset, const void* data, const size_t length)
{
    static_cast<void>(buffer);
    static_cast<void>(offset);
    static_cast<void>(data);
    static_cast<void>(length);
    // No-op for null backend
}

void Device_impl::add_completion_handler(std::function<void()> callback)
{
    static_cast<void>(callback);
    // No-op for null backend
}

void Device_impl::on_thread_enter()
{
    // No-op for null backend
}

auto Device_impl::get_handle(const Texture& texture, const Sampler& sampler) const -> uint64_t
{
    static_cast<void>(texture);
    static_cast<void>(sampler);
    return 0;
}

auto Device_impl::create_dummy_texture(const erhe::dataformat::Format format) -> std::shared_ptr<Texture>
{
    const Texture_create_info create_info{
        .device       = m_device,
        .usage_mask   = Image_usage_flag_bit_mask::sampled | Image_usage_flag_bit_mask::transfer_dst,
        .type         = Texture_type::texture_2d,
        .pixelformat  = format,
        .use_mipmaps  = false,
        .sample_count = 0,
        .width        = 2,
        .height       = 2,
        .debug_label  = erhe::utility::Debug_label{"null dummy texture"}
    };
    return std::make_shared<Texture>(m_device, create_info);
}

auto Device_impl::get_buffer_alignment(const Buffer_target target) -> std::size_t
{
    switch (target) {
        case Buffer_target::storage: {
            return m_info.shader_storage_buffer_offset_alignment;
        }
        case Buffer_target::uniform: {
            return m_info.uniform_buffer_offset_alignment;
        }
        case Buffer_target::draw_indirect: {
            return sizeof(Draw_indexed_primitives_indirect_command);
        }
        default: {
            return 64;
        }
    }
}

auto Device_impl::get_frame_index() const -> uint64_t
{
    return m_frame_index;
}

auto Device_impl::allocate_ring_buffer_entry(
    const Buffer_target     buffer_target,
    const Ring_buffer_usage  usage,
    const std::size_t       byte_count
) -> Ring_buffer_range
{
    static_cast<void>(buffer_target);
    static_cast<void>(usage);
    static_cast<void>(byte_count);
    return Ring_buffer_range{};
}

auto Device_impl::make_blit_command_encoder() -> Blit_command_encoder
{
    return Blit_command_encoder{m_device};
}

auto Device_impl::make_compute_command_encoder() -> Compute_command_encoder
{
    return Compute_command_encoder{m_device};
}

auto Device_impl::make_render_command_encoder() -> Render_command_encoder
{
    return Render_command_encoder{m_device};
}

auto Device_impl::get_format_properties(const erhe::dataformat::Format format) const -> Format_properties
{
    static_cast<void>(format);
    Format_properties properties{};
    properties.supported = true;
    return properties;
}

auto Device_impl::get_supported_depth_stencil_formats() const -> std::vector<erhe::dataformat::Format>
{
    return std::vector<erhe::dataformat::Format>{
        erhe::dataformat::Format::format_d32_sfloat
    };
}

void Device_impl::sort_depth_stencil_formats(
    std::vector<erhe::dataformat::Format>& formats,
    const unsigned int                     sort_flags,
    const int                              requested_sample_count
) const
{
    static_cast<void>(formats);
    static_cast<void>(sort_flags);
    static_cast<void>(requested_sample_count);
    // No-op for null backend
}

auto Device_impl::choose_depth_stencil_format(const std::vector<erhe::dataformat::Format>& formats) const -> erhe::dataformat::Format
{
    if (!formats.empty()) {
        return formats.front();
    }
    return erhe::dataformat::Format::format_d32_sfloat;
}

auto Device_impl::choose_depth_stencil_format(const unsigned int sort_flags, const int requested_sample_count) const -> erhe::dataformat::Format
{
    static_cast<void>(sort_flags);
    static_cast<void>(requested_sample_count);
    return erhe::dataformat::Format::format_d32_sfloat;
}

auto Device_impl::get_shader_monitor() -> Shader_monitor&
{
    return m_shader_monitor;
}

auto Device_impl::get_info() const -> const Device_info&
{
    return m_info;
}

void Device_impl::reset_shader_stages_state_tracker()
{
    // No-op for null backend
}

auto Device_impl::get_draw_id_uniform_location() const -> GLint
{
    return -1;
}

} // namespace erhe::graphics
