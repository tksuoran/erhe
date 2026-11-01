#include "xr/headset_overdraw_visualization.hpp"

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <array>
#include <cstring>
#include <filesystem>

namespace editor {

namespace {

[[nodiscard]] auto get_shader_path() -> std::filesystem::path
{
    return std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"};
}

} // anonymous namespace

Headset_overdraw_visualization::Headset_overdraw_visualization(erhe::graphics::Device& graphics_device, const uint32_t view_count)
    : m_graphics_device{graphics_device}
    , m_counts_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "overdraw_counts",
            .binding_point = static_cast<int>(counts_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_parameters_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "overdraw_visualize_parameters",
            .binding_point = static_cast<int>(parameters_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_bind_group_layout{
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                { .binding_point = counts_binding_point,     .type = erhe::graphics::Binding_type::storage_buffer, .stage_flags = erhe::graphics::Shader_stage_flags::fragment },
                { .binding_point = parameters_binding_point, .type = erhe::graphics::Binding_type::storage_buffer, .stage_flags = erhe::graphics::Shader_stage_flags::fragment }
            },
            .debug_label       = "Headset overdraw visualization",
            .uses_texture_heap = false
        }
    }
    , m_fragment_outputs{erhe::graphics::Fragment_output{.name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0}}
    , m_shader_stages{
        graphics_device,
        [this, view_count]() -> erhe::graphics::Shader_stages_create_info {
            m_counts_block    .add_uint ("counts",     erhe::graphics::Shader_resource::unsized_array);
            m_parameters_block.add_uvec4("parameters");
            return erhe::graphics::Shader_stages_create_info{
                .name                = "overdraw_visualize_multiview",
                .interface_blocks    = { &m_counts_block, &m_parameters_block },
                .fragment_outputs    = &m_fragment_outputs,
                .shaders = {
                    { erhe::graphics::Shader_type::vertex_shader,   get_shader_path() / std::filesystem::path{"post_processing.vert"} },
                    { erhe::graphics::Shader_type::fragment_shader, get_shader_path() / std::filesystem::path{"overdraw_visualize_multiview.frag"} }
                },
                .extra_include_paths = {
                    std::filesystem::path{"res"} / std::filesystem::path{"shaders"},
                    get_shader_path()
                },
                .bind_group_layout   = &m_bind_group_layout,
                .view_count          = view_count
            };
        }()
    }
    , m_pipeline{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Headset overdraw visualization"},
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
            .color_blend    = &erhe::graphics::Color_blend_state::color_blend_premultiplied
        }
    }
    , m_parameters_buffer{graphics_device, erhe::graphics::Buffer_target::storage, "Headset overdraw visualization parameters", parameters_binding_point}
{
    graphics_device.get_shader_monitor().add(m_shader_stages);
}

Headset_overdraw_visualization::~Headset_overdraw_visualization() noexcept
{
    m_graphics_device.get_shader_monitor().remove(m_shader_stages);
}

void Headset_overdraw_visualization::draw(
    erhe::graphics::Render_command_encoder& encoder,
    const erhe::graphics::Render_pass&      render_pass,
    erhe::scene_renderer::Mesh_memory&      mesh_memory,
    const erhe::graphics::Buffer&           count_buffer,
    const uint32_t                          width,
    const uint32_t                          height,
    const uint32_t                          max_count
)
{
    ERHE_PROFILE_FUNCTION();

    const erhe::scene_renderer::Vertex_input_entry& empty_vertex_input = mesh_memory.get_empty_vertex_input();
    erhe::graphics::Render_pipeline* pipeline = m_pipeline.get_pipeline_for(
        render_pass.get_descriptor(),
        &erhe::graphics::Color_blend_state::color_blend_premultiplied,
        &m_shader_stages.shader_stages,
        empty_vertex_input.vertex_input.get(),
        &empty_vertex_input.vertex_format
    );
    if (pipeline == nullptr) {
        return;
    }

    constexpr std::size_t parameters_size = sizeof(std::array<uint32_t, 4>);
    erhe::graphics::Ring_buffer_range parameters_range = m_parameters_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, parameters_size);
    {
        const std::array<uint32_t, 4> parameters{max_count, width, height, 0u};
        std::memcpy(parameters_range.get_span().data(), parameters.data(), parameters_size);
    }
    parameters_range.bytes_written(parameters_size);
    parameters_range.close();

    encoder.set_bind_group_layout(&m_bind_group_layout);
    encoder.set_render_pipeline(*pipeline);
    encoder.set_buffer(erhe::graphics::Buffer_target::storage, &count_buffer, 0, count_buffer.get_capacity_byte_count(), counts_binding_point);
    m_parameters_buffer.bind(encoder, parameters_range);
    encoder.set_viewport_rect(0, 0, static_cast<int>(width), static_cast<int>(height));
    encoder.set_scissor_rect (0, 0, static_cast<int>(width), static_cast<int>(height));
    encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
    parameters_range.release();
}

}
