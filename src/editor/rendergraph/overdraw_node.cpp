#include "rendergraph/overdraw_node.hpp"
#include "app_context.hpp"
#include "scene/viewport_scene_view.hpp"

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

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

Overdraw_node::Overdraw_node(
    erhe::rendergraph::Rendergraph&             rendergraph,
    App_context&                                app_context,
    const std::shared_ptr<Viewport_scene_view>& viewport_scene_view,
    erhe::utility::Debug_label                  debug_label
)
    : erhe::rendergraph::Texture_rendergraph_node{
        erhe::rendergraph::Texture_rendergraph_node_create_info{
            .rendergraph          = rendergraph,
            .debug_label          = debug_label,
            .output_key           = erhe::rendergraph::Rendergraph_node_key::viewport_texture,
            .color_format         = erhe::dataformat::Format::format_16_vec4_float,
            .depth_stencil_format = erhe::dataformat::Format::format_undefined
        }
    }
    , m_app_context        {app_context}
    , m_viewport_scene_view{viewport_scene_view}
    , m_counts_block{
        rendergraph.get_graphics_device(),
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "overdraw_counts",
            .binding_point = static_cast<int>(counts_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_parameters_block{
        rendergraph.get_graphics_device(),
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "overdraw_visualize_parameters",
            .binding_point = static_cast<int>(parameters_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_bind_group_layout{
        rendergraph.get_graphics_device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                {
                    .binding_point   = input_binding_point,
                    .type            = erhe::graphics::Binding_type::combined_image_sampler,
                    .sampler_aspect  = erhe::graphics::Sampler_aspect::color,
                    .name            = "s_input",
                    .glsl_type       = erhe::graphics::Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = erhe::graphics::Shader_stage_flags::fragment
                },
                { .binding_point = counts_binding_point,     .type = erhe::graphics::Binding_type::storage_buffer, .stage_flags = erhe::graphics::Shader_stage_flags::fragment },
                { .binding_point = parameters_binding_point, .type = erhe::graphics::Binding_type::storage_buffer, .stage_flags = erhe::graphics::Shader_stage_flags::fragment }
            },
            .debug_label       = "Overdraw visualization",
            .uses_texture_heap = false
        }
    }
    , m_fragment_outputs{erhe::graphics::Fragment_output{.name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0}}
    , m_shader_stages{
        rendergraph.get_graphics_device(),
        [this, &rendergraph]() -> erhe::graphics::Shader_stages_create_info {
            m_counts_block    .add_uint ("counts",     erhe::graphics::Shader_resource::unsized_array);
            m_parameters_block.add_uvec4("parameters");
            const erhe::graphics::Device& device = rendergraph.get_graphics_device();
            return erhe::graphics::Shader_stages_create_info{
                .name                = "overdraw_visualize",
                .defines             =
                    (device.get_info().coordinate_conventions.framebuffer_origin == erhe::math::Framebuffer_origin::top_left)
                        ? std::vector<std::pair<std::string, std::string>>{ {"FRAMEBUFFER_TOP_LEFT", "1"} }
                        : std::vector<std::pair<std::string, std::string>>{},
                .interface_blocks    = { &m_counts_block, &m_parameters_block },
                .fragment_outputs    = &m_fragment_outputs,
                .shaders = {
                    { erhe::graphics::Shader_type::vertex_shader,   get_shader_path() / std::filesystem::path{"post_processing.vert"} },
                    { erhe::graphics::Shader_type::fragment_shader, get_shader_path() / std::filesystem::path{"overdraw_visualize.frag"} }
                },
                .extra_include_paths = {
                    std::filesystem::path{"res"} / std::filesystem::path{"shaders"},
                    get_shader_path()
                },
                .bind_group_layout   = &m_bind_group_layout
            };
        }()
    }
    , m_pipeline{
        rendergraph.get_graphics_device(),
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Overdraw visualization"},
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled
        }
    }
    , m_sampler_nearest{
        rendergraph.get_graphics_device(),
        erhe::graphics::Sampler_create_info{
            .min_filter   = erhe::graphics::Filter::nearest,
            .mag_filter   = erhe::graphics::Filter::nearest,
            .mipmap_mode  = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode = { erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge },
            .debug_label  = "Overdraw visualization nearest"
        }
    }
    , m_parameters_buffer{rendergraph.get_graphics_device(), erhe::graphics::Buffer_target::storage, "Overdraw visualization parameters", parameters_binding_point}
{
    register_input("viewport", erhe::rendergraph::Rendergraph_node_key::viewport_texture);
    rendergraph.get_graphics_device().get_shader_monitor().add(m_shader_stages);
}

Overdraw_node::~Overdraw_node() noexcept
{
    m_rendergraph.get_graphics_device().get_shader_monitor().remove(m_shader_stages);
}

void Overdraw_node::execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);

    const std::shared_ptr<Viewport_scene_view> viewport_scene_view = m_viewport_scene_view.lock();
    if (!viewport_scene_view) {
        return;
    }
    // The view executed before this node in this frame: its color texture
    // and, when it counted, its count buffer of the frame.
    const std::shared_ptr<erhe::graphics::Texture> input_texture = get_consumer_input_texture(erhe::rendergraph::Rendergraph_node_key::viewport_texture);
    if (!input_texture) {
        return;
    }
    const int width  = input_texture->get_width();
    const int height = input_texture->get_height();
    if ((width < 1) || (height < 1)) {
        return;
    }
    m_render_target.update(width, height, nullptr);
    erhe::graphics::Render_pass* render_pass = m_render_target.get_render_pass();
    ERHE_VERIFY(render_pass != nullptr);

    erhe::graphics::Device&       graphics_device = m_rendergraph.get_graphics_device();
    const uint64_t                frame_index     = graphics_device.get_frame_index();
    const erhe::graphics::Buffer* count_buffer    = viewport_scene_view->get_overdraw_count_buffer(frame_index);
    const uint32_t                max_count       = viewport_scene_view->get_render_state(frame_index).overdraw_max_count;

    erhe::graphics::Scoped_debug_group debug_group{command_buffer, "Overdraw visualization"};
    if (count_buffer != nullptr) {
        // The counting draws' atomics -> this pass's reads.
        command_buffer.buffer_barrier(
            *count_buffer,
            erhe::graphics::Buffer_usage_flag_bit_mask::storage_read | erhe::graphics::Buffer_usage_flag_bit_mask::storage_write | erhe::graphics::Buffer_usage_flag_bit_mask::fragment_shader,
            erhe::graphics::Buffer_usage_flag_bit_mask::storage_read | erhe::graphics::Buffer_usage_flag_bit_mask::fragment_shader,
            erhe::graphics::Barrier_scope::command_buffer
        );
    }

    erhe::graphics::Ring_buffer_range parameters_range = m_parameters_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, sizeof(std::array<uint32_t, 4>));
    {
        const std::array<uint32_t, 4> parameters{max_count, 0u, 0u, 0u};
        std::memcpy(parameters_range.get_span().data(), parameters.data(), sizeof(parameters));
    }
    parameters_range.bytes_written(sizeof(std::array<uint32_t, 4>));
    parameters_range.close();

    {
        // The render pass clears the target; without counts this frame (the
        // view rendered nothing) the cleared target is the image.
        erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
        erhe::graphics::Scoped_render_pass     scoped_render_pass{*render_pass, command_buffer};
        if (count_buffer != nullptr) {
            const erhe::scene_renderer::Vertex_input_entry& empty_vertex_input = m_app_context.mesh_memory->get_empty_vertex_input();
            erhe::graphics::Render_pipeline* pipeline = m_pipeline.get_pipeline_for(
                render_pass->get_descriptor(),
                nullptr,
                &m_shader_stages.shader_stages,
                empty_vertex_input.vertex_input.get(),
                &empty_vertex_input.vertex_format
            );
            if (pipeline != nullptr) {
                encoder.set_bind_group_layout(&m_bind_group_layout);
                encoder.set_render_pipeline(*pipeline);
                encoder.set_sampled_image(input_binding_point, *input_texture, m_sampler_nearest);
                encoder.set_buffer(erhe::graphics::Buffer_target::storage, count_buffer, 0, count_buffer->get_capacity_byte_count(), counts_binding_point);
                m_parameters_buffer.bind(encoder, parameters_range);
                encoder.set_viewport_rect(0, 0, width, height);
                encoder.set_scissor_rect (0, 0, width, height);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
            }
        }
    }
    parameters_range.release();
}

}
