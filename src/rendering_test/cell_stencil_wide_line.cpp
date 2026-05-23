#include "rendering_test_app.hpp"
#include "rendering_test_log.hpp"

#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_scene_renderer/content_wide_line_renderer.hpp"

namespace rendering_test {

// Pipeline that draws wide-line triangles (compute output) with a
// stencil test that should mask them against the red cube's stamp
// written earlier in the same render pass. This mirrors the editor
// outline pipeline: stencil test `not_equal ref` with read-only
// stencil mask.
void Rendering_test::make_stencil_wide_line_pipeline()
{
    if (!m_content_wide_line_renderer) {
        return; // compute path not available on this backend
    }
    if (m_content_wide_line_renderer->get_graphics_shader_stages() == nullptr) {
        return;
    }

    const erhe::graphics::Stencil_op_state test_ne_1_op{
        .stencil_fail_op = erhe::graphics::Stencil_op::keep,
        .z_fail_op       = erhe::graphics::Stencil_op::keep,
        .z_pass_op       = erhe::graphics::Stencil_op::keep,
        .function        = erhe::graphics::Compare_operation::not_equal,
        .reference       = 0x01u,
        .test_mask       = 0xFFu,
        .write_mask      = 0x00u
    };

    m_compute_edge_lines_stencil_pipeline = std::make_unique<erhe::graphics::Base_render_pipeline>(
        m_graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Compute Edge Lines + Stencil Test != 1"},
            //.shader_stages  = m_content_wide_line_renderer->get_graphics_shader_stages(),
            //.vertex_input   = m_content_wide_line_renderer->get_vertex_input(),
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = {
                .depth_test_enable   = false,
                .depth_write_enable  = false,
                .depth_compare_op    = erhe::graphics::Compare_operation::always,
                .stencil_test_enable = true,
                .stencil_front       = test_ne_1_op,
                .stencil_back        = test_ne_1_op
            },
            .color_blend    = erhe::graphics::Color_blend_state::color_blend_premultiplied
        }
    );
}

// Mirrors the editor outline flow inside a single swapchain render pass:
//   1. Red cube draw with `stencil always replace 1` (stamps stencil=0x01
//      where the cube rasterizes).
//   2. Wide-line triangle draw (compute-expanded edges of the sphere) with
//      `stencil test != 0x01` (should appear only outside the cube's
//      footprint).
//
// The compute dispatch that produces the sphere's edge-line vertices MUST
// have already run (and its memory_barrier MUST have been emitted) before
// this function is called, because compute() cannot be issued inside an
// active render pass. See rendering_test.cpp tick() for the dispatch site.
void Rendering_test::render_stencil_wide_line_cell(
    erhe::graphics::Render_command_encoder&                 render_encoder,
    const erhe::math::Viewport&                             viewport,
    const std::vector<std::shared_ptr<erhe::scene::Light>>& lights,
    erhe::graphics::Render_pass*                            active_render_pass
)
{
    if (!m_content_wide_line_renderer || !m_content_wide_line_renderer->is_enabled()) {
        return;
    }
    if (!m_compute_edge_lines_stencil_pipeline) {
        return;
    }
    if (!m_stencil_cube || !m_stencil_sphere) {
        return;
    }

    const auto& conventions = m_graphics_device.get_info().coordinate_conventions;

    render_encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
    render_encoder.set_scissor_rect (viewport.x, viewport.y, viewport.width, viewport.height);

    // Pass 1: red cube stamps stencil = 1 (reuse cell 1,3 pipeline).
    const std::vector<std::shared_ptr<erhe::scene::Mesh>> cube_meshes{m_stencil_cube};
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
                .debug_label       = "wide-line stencil cube (write 1, cyan)"
            },
            .mesh_spans            = { cube_meshes },
            .base_render_pipelines = m_stencil_write_1_pipelines,
            .primitive_mode        = erhe::primitive::Primitive_mode::polygon_fill,
            .primitive_settings    = erhe::scene_renderer::Primitive_interface_settings{},
            .filter                = erhe::Item_filter{},
            //.override_shader_stages = m_stencil_cyan_shader_stages.get(),
        }
    );

    // Pass 2: compute-expanded wide-line edges of the sphere drawn with
    // stencil test != 1. Uses group 1 so it only consumes the dispatches
    // that were populated with the sphere (see tick()).
    //
    // Note: Content_wide_line_renderer requires the compute viewport to
    // match the draw viewport. The compute call in tick() uses cell_00,
    // so this draw will produce fragments only where cell_14's screen
    // pixels overlap the cell_00 line endpoints (i.e. nothing visible).
    // This cell stays as scaffolding; the real test would need a
    // dedicated renderer instance per viewport.
    m_content_wide_line_renderer->render(
        render_encoder,
        *m_compute_edge_lines_stencil_pipeline,
        *active_render_pass,
        /*group=*/1
    );
}

} // namespace rendering_test
