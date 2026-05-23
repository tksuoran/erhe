#include "rendering_test_app.hpp"

#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

namespace rendering_test {

// TODO Wiring shader stages selection to forward renderer is not implemented

void Rendering_test::make_stencil_pipelines()
{
    // Build the editor's selection-fill / outline stencil pattern using the
    // standard shader so the only thing that varies between cells is the
    // depth-stencil state. cull_mode_none + identical front/back stencil
    // ops avoid any winding/face-culling complications.
    const auto& c = m_graphics_device.get_info().coordinate_conventions;
    const bool  y_flip = (c.clip_space_y_flip == erhe::math::Clip_space_y_flip::enabled);
    const auto& depth_test_enabled = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled();
    static_cast<void>(depth_test_enabled);

    m_stencil_disabled_pipeline = std::make_unique<erhe::graphics::Base_render_pipeline>(
        m_graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Stencil Disabled (baseline)"},
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none.with_winding_flip_if(y_flip),
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
        }
    );
    m_stencil_disabled_pipelines.push_back(m_stencil_disabled_pipeline.get());

    // Selected-fill pattern from editor: function=always, write 0x80 into bit 7,
    // compareMask=0 (intentionally; matches editor).
    const erhe::graphics::Stencil_op_state write_0x80_op{
        .stencil_fail_op = erhe::graphics::Stencil_op::replace,
        .z_fail_op       = erhe::graphics::Stencil_op::replace,
        .z_pass_op       = erhe::graphics::Stencil_op::replace,
        .function        = erhe::graphics::Compare_operation::always,
        .reference       = 0b10000000u,
        .test_mask       = 0b00000000u,
        .write_mask      = 0b10000000u
    };
    m_stencil_write_0x80_pipeline = std::make_unique<erhe::graphics::Base_render_pipeline>(
        m_graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Stencil Write 0x80"},
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none.with_winding_flip_if(y_flip),
            .depth_stencil  = {
                .depth_test_enable   = true,
                .depth_write_enable  = true,
                .depth_compare_op    = erhe::graphics::get_depth_function(erhe::graphics::Compare_operation::less, true),
                .stencil_test_enable = true,
                .stencil_front       = write_0x80_op,
                .stencil_back        = write_0x80_op
            }
        }
    );
    m_stencil_write_0x80_pipelines.push_back(m_stencil_write_0x80_pipeline.get());

    // Outline pattern from editor: function=not_equal 0x80 (bit 7 NOT set),
    // depth disabled, ops keep/keep/replace.
    const erhe::graphics::Stencil_op_state test_ne_0x80_op{
        .stencil_fail_op = erhe::graphics::Stencil_op::keep,
        .z_fail_op       = erhe::graphics::Stencil_op::keep,
        .z_pass_op       = erhe::graphics::Stencil_op::replace,
        .function        = erhe::graphics::Compare_operation::not_equal,
        .reference       = 0b10000000u,
        .test_mask       = 0b10000000u,
        .write_mask      = 0b10000000u
    };
    m_stencil_test_ne_0x80_pipeline = std::make_unique<erhe::graphics::Base_render_pipeline>(
        m_graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Stencil Test != 0x80"},
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none.with_winding_flip_if(y_flip),
            .depth_stencil  = {
                .depth_test_enable   = false,
                .depth_write_enable  = false,
                .depth_compare_op    = erhe::graphics::Compare_operation::always,
                .stencil_test_enable = true,
                .stencil_front       = test_ne_0x80_op,
                .stencil_back        = test_ne_0x80_op
            }
        }
    );
    m_stencil_test_ne_0x80_pipelines.push_back(m_stencil_test_ne_0x80_pipeline.get());

    // Stencil cell (1,3) pipelines: clear stencil 0, render the red cube
    // with "always replace 1", then render the green sphere with
    // "test != 1, keep". The sphere ends up visible only where the cube
    // did not write a pixel, which is the pure-stencil version of an
    // outline / mask test.
    //
    // The cube and sphere use a dedicated "stencil_color" shader (a
    // trivial position-only vert + constant-color frag) compiled twice
    // with different STENCIL_COLOR defines, one variant per pipeline.
    // The rendering_test "standard" shader visualizes encoded normals
    // and would not produce flat red/green output.
    auto make_stencil_color_shader = [this](const char* color_glsl) {
        erhe::graphics::Shader_stages_create_info create_info{
            .name    = "stencil_color",
            .defines = {{"STENCIL_COLOR", color_glsl}}
        };
        return std::make_unique<erhe::graphics::Shader_stages>(
            m_graphics_device,
            erhe::graphics::build_shader_stages(
                m_program_interface.make_prototype(std::move(create_info))
            )
        );
    };
    m_stencil_red_shader_stages   = make_stencil_color_shader("vec4(1.0, 0.0, 0.0, 1.0)");
    m_stencil_green_shader_stages = make_stencil_color_shader("vec4(0.0, 1.0, 0.0, 1.0)");
    m_stencil_cyan_shader_stages  = make_stencil_color_shader("vec4(0.333, 0.333, 0.333, 1.0)");

    const erhe::graphics::Stencil_op_state stencil_write_1_op{
        .stencil_fail_op = erhe::graphics::Stencil_op::replace,
        .z_fail_op       = erhe::graphics::Stencil_op::replace,
        .z_pass_op       = erhe::graphics::Stencil_op::replace,
        .function        = erhe::graphics::Compare_operation::always,
        .reference       = 0x01u,
        .test_mask       = 0xFFu,
        .write_mask      = 0xFFu
    };
    m_stencil_write_1_pipeline = std::make_unique<erhe::graphics::Base_render_pipeline>(
        m_graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Stencil Write 1 (red cube)"},
            //.shader_stages  = m_stencil_red_shader_stages.get(),
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(y_flip),
            .depth_stencil  = {
                .depth_test_enable   = true,
                .depth_write_enable  = true,
                .depth_compare_op    = erhe::graphics::get_depth_function(erhe::graphics::Compare_operation::less, true),
                .stencil_test_enable = true,
                .stencil_front       = stencil_write_1_op,
                .stencil_back        = stencil_write_1_op
            }
        }
    );
    m_stencil_write_1_pipelines.push_back(m_stencil_write_1_pipeline.get());

    const erhe::graphics::Stencil_op_state stencil_test_ne_1_op{
        .stencil_fail_op = erhe::graphics::Stencil_op::keep,
        .z_fail_op       = erhe::graphics::Stencil_op::keep,
        .z_pass_op       = erhe::graphics::Stencil_op::keep,
        .function        = erhe::graphics::Compare_operation::not_equal,
        .reference       = 0x01u,
        .test_mask       = 0xFFu,
        .write_mask      = 0x00u
    };
    m_stencil_test_ne_1_pipeline = std::make_unique<erhe::graphics::Base_render_pipeline>(
        m_graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Stencil Test != 1 (green sphere)"},
            //.shader_stages  = m_stencil_green_shader_stages.get(),
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(y_flip),
            .depth_stencil  = {
                .depth_test_enable   = true,
                .depth_write_enable  = true,
                .depth_compare_op    = erhe::graphics::get_depth_function(erhe::graphics::Compare_operation::less, true),
                .stencil_test_enable = true,
                .stencil_front       = stencil_test_ne_1_op,
                .stencil_back        = stencil_test_ne_1_op
            }
        }
    );
    m_stencil_test_ne_1_pipelines.push_back(m_stencil_test_ne_1_pipeline.get());

    // --- Minimal VkPipeline-switch reproducer (no wide-line renderer) ---
    //
    // Two pipelines that are IDENTICAL except for depth_stencil.stencil_test
    // configuration. Both draw the stencil cube via forward_renderer with
    // their respective shader_stages. If a Mac/Vulkan backend fails to
    // re-issue MTLDepthStencilState on switching from one VkPipeline to the
    // other within a single render pass, the second draw behaves according
    // to the first pipeline's depth-stencil state:
    //
    //   Order A then B:
    //     - Pipeline A: stencil_test=false -> red cube drawn.
    //     - Pipeline B: stencil_test=true, not_equal 0 against cleared
    //       stencil=0 -> draw SHOULD be masked out.
    //       BUG: keeps A's stencil_test=false -> green cube also drawn.
    //
    //   Order B then A:
    //     - Pipeline B: first-bound -> stencil_test correctly applied,
    //       draw masked, no green.
    //     - Pipeline A: should draw red cube.
    //       BUG: keeps B's stencil_test=true + not_equal 0 -> red cube
    //       also masked out (nothing drawn).
    //
    // Either ordering gives a clear visual signal: "too many cubes" vs
    // "too few cubes" vs the correct "one cube exactly".
    m_no_stencil_red_pipeline = std::make_unique<erhe::graphics::Base_render_pipeline>(
        m_graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Pipeline A: stencil_test=false (red)"},
            //.shader_stages  = m_stencil_red_shader_stages.get(),
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(y_flip),
            .depth_stencil  = {
                .depth_test_enable   = true,
                .depth_write_enable  = true,
                .depth_compare_op    = erhe::graphics::get_depth_function(erhe::graphics::Compare_operation::less, true),
                .stencil_test_enable = false
            }
        }
    );
    m_no_stencil_red_pipelines.push_back(m_no_stencil_red_pipeline.get());

    const erhe::graphics::Stencil_op_state stencil_test_ne_0_op{
        .stencil_fail_op = erhe::graphics::Stencil_op::keep,
        .z_fail_op       = erhe::graphics::Stencil_op::keep,
        .z_pass_op       = erhe::graphics::Stencil_op::keep,
        .function        = erhe::graphics::Compare_operation::not_equal,
        .reference       = 0x00u,
        .test_mask       = 0xFFu,
        .write_mask      = 0x00u
    };
    m_stencil_test_ne_0_green_pipeline = std::make_unique<erhe::graphics::Base_render_pipeline>(
        m_graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Pipeline B: stencil_test=ne_0 (green)"},
            //.shader_stages  = m_stencil_green_shader_stages.get(),
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(y_flip),
            // depth_stencil here matches Pipeline A exactly except for
            // stencil_test_enable (off vs on). That keeps the depth-stencil
            // delta as small as the Vulkan preset allows.
            .depth_stencil  = {
                .depth_test_enable   = true,
                .depth_write_enable  = true,
                .depth_compare_op    = erhe::graphics::get_depth_function(erhe::graphics::Compare_operation::less, true),
                .stencil_test_enable = true,
                .stencil_front       = stencil_test_ne_0_op,
                .stencil_back        = stencil_test_ne_0_op
            }
        }
    );
    m_stencil_test_ne_0_green_pipelines.push_back(m_stencil_test_ne_0_green_pipeline.get());

    // Pipeline C: mirrors the depth_stencil state of
    // m_compute_edge_lines_stencil_pipeline_state used by the wide-line
    // renderer (depth_test=false, depth_write=false, compare=always,
    // stencil_test=true, not_equal 0). Pair with Pipeline A to test
    // whether forward_renderer can correctly switch between pipelines
    // with THE SAME depth-stencil delta that the wide-line path uses.
    m_stencil_test_ne_0_no_depth_green_pipeline = std::make_unique<erhe::graphics::Base_render_pipeline>(
        m_graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Pipeline C: no_depth + stencil_test=ne_0 (green)"},
            //.shader_stages  = m_stencil_green_shader_stages.get(),
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(y_flip),
            .depth_stencil  = {
                .depth_test_enable   = false,
                .depth_write_enable  = false,
                .depth_compare_op    = erhe::graphics::Compare_operation::always,
                .stencil_test_enable = true,
                .stencil_front       = stencil_test_ne_0_op,
                .stencil_back        = stencil_test_ne_0_op
            }
        }
    );
    m_stencil_test_ne_0_no_depth_green_pipelines.push_back(m_stencil_test_ne_0_no_depth_green_pipeline.get());
}

// Pure-stencil masking test. The swapchain pass clears stencil to 0
// before this is called. The cube renders first with "stencil always
// replace 1", stamping its silhouette into the stencil attachment;
// then the sphere renders with "stencil test != 1", which keeps it
// visible only where the cube did not write a pixel.
void Rendering_test::render_stencil_cell(
    erhe::graphics::Render_command_encoder&                 render_encoder,
    const erhe::math::Viewport&                             viewport,
    const std::vector<std::shared_ptr<erhe::scene::Light>>& lights,
    erhe::graphics::Render_pass*                            active_render_pass
)
{
    const auto& conventions = m_graphics_device.get_info().coordinate_conventions;

    const auto run_pass = [&](
        const std::vector<std::shared_ptr<erhe::scene::Mesh>>& meshes,
        std::vector<erhe::graphics::Base_render_pipeline*>&    pipeline_states,
        const char*                                            debug_label
    ) {
        render_encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
        render_encoder.set_scissor_rect(viewport.x, viewport.y, viewport.width, viewport.height);
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
                    .debug_label       = debug_label
                },
                .mesh_spans            = { meshes },
                .base_render_pipelines = pipeline_states,
                .primitive_mode        = erhe::primitive::Primitive_mode::polygon_fill,
                .primitive_settings    = erhe::scene_renderer::Primitive_interface_settings{},
                .filter                = erhe::Item_filter{}
            }
        );
    };

    run_pass({m_stencil_cube},   m_stencil_write_1_pipelines,   "stencil cube (write 1)");
    run_pass({m_stencil_sphere}, m_stencil_test_ne_1_pipelines, "stencil sphere (test != 1)");
}

} // namespace rendering_test
