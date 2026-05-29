#include "rendering_test_app.hpp"
#include "rendering_test_log.hpp"

#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include <filesystem>

namespace rendering_test {

void Rendering_test::make_render_pipeline_states()
{
    const auto& c = m_graphics_device.get_info().coordinate_conventions;
    const bool  y_flip = (c.clip_space_y_flip == erhe::math::Clip_space_y_flip::enabled);

    m_standard_render_pipeline = std::make_unique<erhe::graphics::Base_render_pipeline>(
        m_graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Standard"},
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(y_flip),
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled()
        }
    );
    m_render_pipeline_states.push_back(m_standard_render_pipeline.get());
}

void Rendering_test::render_scene(
    erhe::graphics::Render_command_encoder&                 render_encoder,
    const erhe::math::Viewport&                             viewport,
    const std::vector<std::shared_ptr<erhe::scene::Light>>& lights,
    const std::vector<std::shared_ptr<erhe::scene::Mesh>>&  meshes,
    erhe::graphics::Render_pass*                            active_render_pass
)
{
    render_encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
    render_encoder.set_scissor_rect (viewport.x, viewport.y, viewport.width, viewport.height);

    const auto& conventions = m_graphics_device.get_info().coordinate_conventions;
    m_forward_renderer->render(
        erhe::scene_renderer::Forward_renderer::Render_parameters{
            .base = erhe::scene_renderer::Forward_renderer::Base_render_parameters{
                .render_encoder    = render_encoder,
                .render_pass       = active_render_pass,
                .viewport          = viewport,
                .camera            = m_camera.get(),
                .ambient_light     = glm::vec3{0.3f, 0.3f, 0.3f},
                .light_projections = &m_light_projections,
                .lights            = lights,
                .skins             = {},
                .materials         = m_materials,
                .reverse_depth     = true,
                .depth_range       = conventions.native_depth_range,
                .conventions       = conventions,
                .debug_label       = "rendering test render",
            },
            .mesh_spans            = { meshes },
            .base_render_pipelines = m_render_pipeline_states,
            .primitive_mode        = erhe::primitive::Primitive_mode::polygon_fill,
            .primitive_settings    = erhe::scene_renderer::Primitive_interface_settings{},
            .filter                = erhe::Item_filter{}
        }
    );
}

void Rendering_test::make_content_wide_line_renderer()
{
    if (!m_graphics_device.get_info().use_compute_shader) {
        return;
    }

    // Build the shader interface first so we have the SSBO structs / UBO
    // layout / bind group layouts ready to feed into shader compilation.
    // Rendering test passes nullptr for joint_block (skinned variant disabled).
    m_content_wide_line_interface = std::make_unique<erhe::scene_renderer::Content_wide_line_interface>(
        m_graphics_device,
        nullptr,
        1 // view_count
    );

    // Build compute shader using the interface's resource definitions.
    {
        using namespace erhe::graphics;
        const std::filesystem::path shader_path = std::filesystem::path{"res"} / std::filesystem::path{"rendering_test"} / std::filesystem::path{"shaders"};
        Shader_stages_create_info create_info{
            .name             = "compute_before_content_line",
            .struct_types     = {
                &m_content_wide_line_interface->edge_line_vertex_struct,
                &m_content_wide_line_interface->triangle_vertex_struct,
                &m_content_wide_line_interface->view_camera_struct
            },
            .interface_blocks = {
                &m_content_wide_line_interface->edge_line_vertex_buffer_block,
                &m_content_wide_line_interface->triangle_vertex_buffer_block,
                &m_content_wide_line_interface->view_block
            },
            .shaders = { { Shader_type::compute_shader, shader_path / "compute_before_content_line.comp" } },
            .bind_group_layout = &m_content_wide_line_interface->bind_group_layout,
        };
        Shader_stages_prototype prototype = build_shader_stages(m_graphics_device, create_info);
        if (prototype.is_valid()) {
            m_compute_shader_stages = std::make_unique<Shader_stages>(m_graphics_device, std::move(prototype));
        }
    }

    // Build graphics shader (renders triangle output from compute).
    //
    // Uses the SHARED program_interface bind_group_layout (the same one
    // forward_renderer/debug_renderer use) so the descriptor set bound
    // by surrounding renderers (camera buffer at camera_buffer_binding_point,
    // etc.) remains compatible. Wide-line render() therefore doesn't need
    // to bind anything for the graphics path -- it relies on the
    // camera_buffer being already bound by an earlier forward_renderer
    // call in the same render pass, and the fragment shader reads
    // camera.cameras[0].viewport.xy to convert gl_FragCoord into
    // viewport-relative pixel coords.
    {
        using namespace erhe::graphics;
        const std::filesystem::path shader_path = std::filesystem::path{"res"} / std::filesystem::path{"rendering_test"} / std::filesystem::path{"shaders"};
        Shader_stages_create_info create_info{
            .name             = "content_line_after_compute",
            .struct_types     = { &m_program_interface.camera_interface.camera_struct },
            .interface_blocks = { &m_program_interface.camera_interface.camera_block  },
            .fragment_outputs = &m_content_wide_line_interface->fragment_outputs,
            .vertex_format    = &m_content_wide_line_interface->triangle_vertex_format,
            .shaders = {
                { Shader_type::vertex_shader,   shader_path / "line_after_compute.vert"        },
                { Shader_type::fragment_shader, shader_path / "content_line_after_compute.frag" }
            },
            .bind_group_layout = m_program_interface.bind_group_layout.get(),
        };
        Shader_stages_prototype prototype = build_shader_stages(m_graphics_device, create_info);
        if (prototype.is_valid()) {
            m_graphics_shader_stages = std::make_unique<Shader_stages>(m_graphics_device, std::move(prototype));
        }
    }

    if (m_compute_shader_stages && m_graphics_shader_stages) {
        // Rendering test does not exercise skinning or multiview, so the
        // skinned compute variant and multiview graphics variant stay null.
        m_content_wide_line_renderer = std::make_unique<erhe::scene_renderer::Content_wide_line_renderer>(
            m_graphics_device,
            *m_content_wide_line_interface,
            m_compute_shader_stages.get(),
            nullptr,
            m_graphics_shader_stages.get(),
            nullptr
        );

        // Create pipeline state for rendering compute output.
        m_compute_edge_lines_pipeline = std::make_unique<erhe::graphics::Base_render_pipeline>(
            m_graphics_device,
            erhe::graphics::Base_render_pipeline_create_info{
                .debug_label    = erhe::utility::Debug_label{"Compute Edge Lines Pipeline"},
                //.shader_stages  = m_content_wide_line_renderer->get_graphics_shader_stages(),
                //.vertex_input   = m_content_wide_line_renderer->get_vertex_input(),
                .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
                //.color_blend    = erhe::graphics::Color_blend_state::color_blend_premultiplied
            }
        );
        log_test->info("Content wide line renderer enabled (compute path)");
    } else {
        m_content_wide_line_interface.reset();
        log_test->warn("Failed to create compute/graphics shaders for content wide line renderer");
    }
}

} // namespace rendering_test
