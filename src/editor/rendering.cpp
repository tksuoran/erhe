#include "rendering.hpp"
#include "application.hpp"
#include "configuration.hpp"
#include "log.hpp"
#include "editor_imgui_windows.hpp"
#include "editor_time.hpp"
#include "editor_tools.hpp"
#include "editor_view.hpp"
#include "window.hpp"
#include "renderers/forward_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_renderer.hpp"
#endif
#include "renderers/id_renderer.hpp"
#include "renderers/line_renderer.hpp"
#include "renderers/render_context.hpp"
#include "renderers/shadow_renderer.hpp"
#include "renderers/text_renderer.hpp"
#include "scene/scene_root.hpp"
#include "windows/debug_view_window.hpp"
#include "windows/log_window.hpp"
#include "windows/viewport_config.hpp"
#include "windows/viewport_window.hpp"

#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"

namespace editor {

using erhe::graphics::Color_blend_state;

Editor_rendering::Editor_rendering()
    : erhe::components::Component{c_name}
{
}

Editor_rendering::~Editor_rendering()
{
}

void Editor_rendering::connect()
{
    m_application            = get<Application         >();
    m_configuration          = get<Configuration       >();
    m_editor_imgui_windows   = get<Editor_imgui_windows>();
    m_editor_view            = get<Editor_view         >();
    m_editor_time            = get<Editor_time         >();
    m_editor_tools           = get<Editor_tools        >();
    m_forward_renderer       = get<Forward_renderer    >();
    m_log_window             = get<Log_window          >();
#if defined(ERHE_XR_LIBRARY_OPENXR)
    m_headset_renderer       = get<Headset_renderer    >();
#endif
    m_id_renderer            = get<Id_renderer         >();
    m_line_renderer_set      = get<Line_renderer_set   >();
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
    m_pointer_context        = get<Pointer_context     >();
    m_scene_root             = get<Scene_root          >();
    m_shadow_renderer        = get<Shadow_renderer     >();
    m_text_renderer          = get<Text_renderer       >();
    m_viewport_windows       = get<Viewport_windows    >();

    require<Programs   >();
    require<Mesh_memory>();
}

void Editor_rendering::initialize_component()
{
    using erhe::graphics::Vertex_input_state;
    using erhe::graphics::Input_assembly_state;
    using erhe::graphics::Rasterization_state;
    using erhe::graphics::Depth_stencil_state;
    using erhe::graphics::Color_blend_state;

    auto& programs     = *get<Programs>().get();
    auto* vertex_input = get<Mesh_memory>()->vertex_input.get();

    m_depth_stencil_tool_set_hidden = Depth_stencil_state{
        true,                     // depth test enabled
        false,                    // depth writes disabled
        m_configuration->depth_function(gl::Depth_function::greater),  // where depth is further
        true,                     // enable stencil operation
        {
            gl::Stencil_op::keep,         // on stencil test fail, do nothing
            gl::Stencil_op::keep,         // on depth test fail, do nothing
            gl::Stencil_op::replace,      // on depth test pass, set stencil to 1
            gl::Stencil_function::always, // stencil test always passes
            1u,
            0xffffu,
            0xffffu
        },
        {
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_op::replace,
            gl::Stencil_function::always,
            1u,
            0xffffu,
            0xffffu
        },
    };

    m_depth_stencil_tool_set_visible = Depth_stencil_state{
        true,                   // depth test enabled
        false,                  // depth writes disabled
        m_configuration->depth_function(gl::Depth_function::lequal), // where depth is closer
        true,                   // enable stencil operation
        {
            gl::Stencil_op::keep,         // on stencil test fail, do nothing
            gl::Stencil_op::keep,         // on depth test fail, do nothing
            gl::Stencil_op::replace,      // on depth test pass, set stencil
            gl::Stencil_function::always, // stencil test always passes
            2u,
            0xffffu,
            0xffffu
        },
        {
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_op::replace,
            gl::Stencil_function::always,
            2u,
            0xffffu,
            0xffffu
        },
    };

    m_depth_stencil_tool_test_for_hidden = Depth_stencil_state{
        true,                     // depth test enabled
        true,                     //
        m_configuration->depth_function(gl::Depth_function::lequal),   //
        true,                     // enable stencil operation
        {
            gl::Stencil_op::keep,        // on stencil test fail, do nothing
            gl::Stencil_op::keep,        // on depth test fail, do nothing
            gl::Stencil_op::keep,        // on depth test pass, do nothing
            gl::Stencil_function::equal, // stencil test requires exact value 1
            1u,
            0xffffu,
            0xffffu
        },
        {
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_op::replace,
            gl::Stencil_function::always,
            1u,
            0xffffu,
            0xffffu
        },
    };

    m_depth_stencil_tool_test_for_visible = Depth_stencil_state{
        true,                     // depth test enabled
        true,                     //
        m_configuration->depth_function(gl::Depth_function::lequal),   //
        true,                     // enable stencil operation
        {
            gl::Stencil_op::keep,        // on stencil test fail, do nothing
            gl::Stencil_op::keep,        // on depth test fail, do nothing
            gl::Stencil_op::keep,        // on depth test pass, do nothing
            gl::Stencil_function::equal, // stencil test requires exact value 2
            2u,
            0xffffu,
            0xffffu
        },
        {
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_function::equal,
            2u,
            0xffffu,
            0xffffu
        },
    };

    m_color_blend_constant_point_six = Color_blend_state {
        true,
        {
            gl::Blend_equation_mode::func_add,
            gl::Blending_factor::constant_alpha,
            gl::Blending_factor::one_minus_constant_alpha
        },
        {
            gl::Blend_equation_mode::func_add,
            gl::Blending_factor::constant_alpha,
            gl::Blending_factor::one_minus_constant_alpha
        },
        glm::vec4{0.0f, 0.0f, 0.0f, 0.6f},
        true,
        true,
        true,
        true
    };

    m_color_blend_constant_point_two = Color_blend_state {
        true,
        {
            gl::Blend_equation_mode::func_add,
            gl::Blending_factor::constant_alpha,
            gl::Blending_factor::one_minus_constant_alpha
        },
        {
            gl::Blend_equation_mode::func_add,
            gl::Blending_factor::constant_alpha,
            gl::Blending_factor::one_minus_constant_alpha
        },
        glm::vec4{0.0f, 0.0f, 0.0f, 0.2f},
        true,
        true,
        true,
        true
    };

    m_depth_hidden = Depth_stencil_state{
        true,                     // depth test enabled
        false,                    // depth writes disabled
        m_configuration->depth_function(gl::Depth_function::greater),  // where depth is further
        false,                    // no stencil test
        {
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_function::always,
            0u,
            0xffffu,
            0xffffu
        },
        {
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_function::always,
            0u,
            0xffffu,
            0xffffu
        },
    };

    m_rp_polygon_fill = Renderpass
    {
        .name = "Polygon fill",
        .pipeline =
        {
            .shader_stages  = programs.standard.get(),
            .vertex_input   = vertex_input,
            .input_assembly = &Input_assembly_state::triangles,
            .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->reverse_depth),
            .color_blend    = &Color_blend_state::color_blend_disabled
        }
    };

    // Tool pass one: For hidden tool parts, set stencil to 1.
    // Only reads depth buffer, only writes stencil buffer.
    m_rp_tool1_hidden_stencil = Renderpass
    {
        .name = "Tool pass 1: Tag depth hidden with stencil = 1",
        .pipeline =
        {
            .shader_stages  = programs.tool.get(),
            .vertex_input   = vertex_input,
            .input_assembly = &Input_assembly_state::triangles,
            .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
            .depth_stencil  = &m_depth_stencil_tool_set_hidden,
            .color_blend    = &Color_blend_state::color_writes_disabled
        }
    };

    // Tool pass two: For visible tool parts, set stencil to 2.
    // Only reads depth buffer, only writes stencil buffer.
    m_rp_tool2_visible_stencil = Renderpass
    {
        .name = "Tool pass 2: Tag visible tool parts with stencil = 2",
        .pipeline =
        {
            .shader_stages  = programs.tool.get(),
            .vertex_input   = vertex_input,
            .input_assembly = &Input_assembly_state::triangles,
            .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
            .depth_stencil  = &m_depth_stencil_tool_set_visible,
            .color_blend    = &Color_blend_state::color_writes_disabled
        }
    };

    // Tool pass three: Set depth to fixed value (with depth range)
    // Only writes depth buffer, depth test always.
    m_rp_tool3_depth_clear = Renderpass
    {
        .name = "Tool pass 3: Set depth to fixed value",
        .pipeline =
        {
            .shader_stages  = programs.tool.get(),
            .vertex_input   = vertex_input,
            .input_assembly = &Input_assembly_state::triangles,
            .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
            .depth_stencil  = &Depth_stencil_state::depth_test_always_stencil_test_disabled,
            .color_blend    = &Color_blend_state::color_writes_disabled
        },
        .begin = [](){ gl::depth_range(0.0f, 0.0f); },
        .end   = [](){ gl::depth_range(0.0f, 1.0f); }
    };

    // Tool pass four: Set depth to proper tool depth
    // Normal depth buffer update with depth test.
    m_rp_tool4_depth = Renderpass
    {
        .name = "Tool pass 4: Set depth to proper tool depth",
        .pipeline =
        {
            .shader_stages  = programs.tool.get(),
            .vertex_input   = vertex_input,
            .input_assembly = &Input_assembly_state::triangles,
            .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->reverse_depth),
            .color_blend    = &Color_blend_state::color_writes_disabled
        }
    };

    // Tool pass five: Render visible tool parts
    // Normal depth test, stencil test require 1, color writes enabled, no blending
    m_rp_tool5_visible_color = Renderpass
    {
        .name = "Tool pass 5: Render visible tool parts",
        .pipeline =
        {
            .shader_stages  = programs.tool.get(),
            .vertex_input   = vertex_input,
            .input_assembly = &Input_assembly_state::triangles,
            .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
            .depth_stencil  = &m_depth_stencil_tool_test_for_visible,
            .color_blend    = &Color_blend_state::color_blend_disabled
        }
    };

    // Tool pass six: Render hidden tool parts
    // Normal depth test, stencil test requires 2, color writes enabled, blending
    m_rp_tool6_hidden_color = Renderpass
    {
        .name = "Tool pass 6: Render hidden tool parts",
        .pipeline =
        {
            .shader_stages  = programs.tool.get(),
            .vertex_input   = vertex_input,
            .input_assembly = &Input_assembly_state::triangles,
            .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
            .depth_stencil  = &m_depth_stencil_tool_test_for_hidden,
            .color_blend    = &m_color_blend_constant_point_six
        }
    };

    m_rp_edge_lines = Renderpass
    {
        .name = "Edge lines",
        .pipeline =
        {
            .shader_stages  = programs.wide_lines.get(),
            .vertex_input   = vertex_input,
            .input_assembly = &Input_assembly_state::lines,
            .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->reverse_depth),
            .color_blend    = &Color_blend_state::color_blend_premultiplied
        },
        .primitive_mode = erhe::primitive::Primitive_mode::edge_lines
    };

    m_rp_corner_points = Renderpass
    {
        .name = "Corner Points",
        .pipeline =
        {
            .shader_stages  = programs.points.get(),
            .vertex_input   = vertex_input,
            .input_assembly = &Input_assembly_state::points,
            .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->reverse_depth),
            .color_blend    = &Color_blend_state::color_blend_disabled
        },
        .primitive_mode = erhe::primitive::Primitive_mode::corner_points
    };

    m_rp_polygon_centroids = Renderpass
    {
        .name = "Polygon Centroids",
        .pipeline =
        {
            .shader_stages  = programs.points.get(),
            .vertex_input   = vertex_input,
            .input_assembly = &Input_assembly_state::points,
            .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->reverse_depth),
            .color_blend    = &Color_blend_state::color_blend_disabled
        },
        .primitive_mode = erhe::primitive::Primitive_mode::polygon_centroids
    };

    m_rp_line_hidden_blend = Renderpass
    {
        .name = "Hidden lines with blending",
        .pipeline =
        {
            .shader_stages  = programs.wide_lines.get(),
            .vertex_input   = vertex_input,
            .input_assembly = &Input_assembly_state::lines,
            .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
            .depth_stencil  = &m_depth_hidden,
            .color_blend    = &m_color_blend_constant_point_two
        },
        .primitive_mode = erhe::primitive::Primitive_mode::edge_lines
    };

    m_rp_brush_back = Renderpass
    {
        .name = "Brush back faces",
        .pipeline =
        {
            .shader_stages  = programs.brush.get(),
            .vertex_input   = vertex_input,
            .input_assembly = &Input_assembly_state::triangles,
            .rasterization  = Rasterization_state::cull_mode_front_cw(m_configuration->reverse_depth),
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->reverse_depth),
            .color_blend    = &Color_blend_state::color_blend_premultiplied
        }
    };

    m_rp_brush_front = Renderpass
    {
        .name = "Brush front faces",
        .pipeline =
        {
            .shader_stages  = programs.brush.get(),
            .vertex_input   = vertex_input,
            .input_assembly = &Input_assembly_state::triangles,
            .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->reverse_depth),
            .color_blend    = &Color_blend_state::color_blend_premultiplied
        }
    };
}

void Editor_rendering::init_state()
{
    gl::clip_control(gl::Clip_control_origin::lower_left, gl::Clip_control_depth::zero_to_one);
    gl::disable     (gl::Enable_cap::primitive_restart);
    gl::enable      (gl::Enable_cap::primitive_restart_fixed_index);
    gl::enable      (gl::Enable_cap::texture_cube_map_seamless);
    gl::enable      (gl::Enable_cap::framebuffer_srgb);
}

auto Editor_rendering::width() const -> int
{
    return m_editor_view->width();
}

auto Editor_rendering::height() const -> int
{
    return m_editor_view->height();
}

void Editor_rendering::begin_frame()
{
    ERHE_PROFILE_FUNCTION

#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (m_headset_renderer)
    {
        m_headset_renderer->begin_frame();
    }
#endif
}

void Editor_rendering::bind_default_framebuffer()
{
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    gl::viewport        (0, 0, width(), height());
}

void Editor_rendering::clear()
{
    ERHE_PROFILE_FUNCTION

    Expects(m_pipeline_state_tracker != nullptr);

    // Pipeline state required for NVIDIA driver not to complain about texture
    // unit state when doing the clear.
    m_pipeline_state_tracker->shader_stages.reset();
    m_pipeline_state_tracker->color_blend.execute(&Color_blend_state::color_blend_disabled);
    gl::clear_color  (0.0f, 0.0f, 0.2f, 0.1f);
    gl::clear_depth_f(*m_configuration->depth_clear_value_pointer());
    gl::clear        (gl::Clear_buffer_mask::color_buffer_bit | gl::Clear_buffer_mask::depth_buffer_bit);
}

void Editor_rendering::render()
{
    ERHE_PROFILE_FUNCTION

    Expects(m_application);
    Expects(m_editor_view);

    if (m_trigger_capture)
    {
        get<Window>()->begin_renderdoc_capture();
    }

    begin_frame();

    // Render shadow maps
    if (
        m_scene_root &&
        m_shadow_renderer
    )
    {
        m_scene_root->sort_lights();
        m_shadow_renderer->render(
            {
                .mesh_layers = { m_scene_root->content_layer() },
                .light_layer = m_scene_root->light_layer()
            }
        );
        get<Debug_view_window>()->render(*m_pipeline_state_tracker.get());
    }

    m_editor_imgui_windows->rendertarget_imgui_windows();

    m_viewport_windows->render();

    m_editor_imgui_windows->imgui_windows();

#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (m_headset_renderer)
    {
        m_headset_renderer->render();
    }
#endif

    if (m_shadow_renderer)   m_shadow_renderer  ->next_frame();
    if (m_id_renderer)       m_id_renderer      ->next_frame();
    if (m_forward_renderer)  m_forward_renderer ->next_frame();
    if (m_text_renderer)     m_text_renderer    ->next_frame();
    if (m_line_renderer_set) m_line_renderer_set->next_frame();
}

void Editor_rendering::render_viewport(const Render_context& context, const bool has_pointer)
{
    ERHE_PROFILE_FUNCTION

    if (m_forward_renderer)
    {
        render_content  (context);
        render_selection(context);
        render_brush    (context);
        if (has_pointer)
        {
            render_tool_meshes(context);
        }
        render_gui(context);
    }

    m_editor_tools->render_tools(context);

    if (m_line_renderer_set)
    {
        m_line_renderer_set->render(context.viewport, *context.camera);
    }

    if (m_text_renderer)
    {
        m_text_renderer->render(context.viewport);
    }
}

void Editor_rendering::render_id(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION

    if (
        (m_id_renderer == nullptr) ||
        (m_pointer_context->window() != context.window) ||
        !m_pointer_context->pointer_in_content_area() ||
        (context.camera == nullptr) ||
        !m_pointer_context->position_in_viewport_window().has_value()
    )
    {
        return;
    }

    auto pointer = m_pointer_context->position_in_viewport_window().value();

    m_id_renderer->render(
        {
            .viewport            = context.viewport,
            .camera              = *context.camera,
            .content_mesh_layers = { m_scene_root->content_layer() },
            .tool_mesh_layers    = { m_scene_root->tool_layer() },
            .time                = m_editor_time->time(),
            .x                   = static_cast<int>(pointer.x),
            .y                   = static_cast<int>(pointer.y)
        }
    );
}

void Editor_rendering::render_content(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION

    if (!m_forward_renderer)
    {
        return;
    }

    if (context.camera == nullptr)
    {
        return;
    }
    if (context.viewport_config == nullptr)
    {
        return;
    }

    auto& render_style = context.viewport_config->render_style_not_selected;

    constexpr erhe::scene::Visibility_filter content_not_selected_filter{
        .require_all_bits_set   = erhe::scene::Node::c_visibility_content,
        .require_all_bits_clear = erhe::scene::Node::c_visibility_selected
    };

    if (render_style.polygon_fill)
    {
        //if (render_style.polygon_offset_enable)
        //{
        //    gl::enable(gl::Enable_cap::polygon_offset_fill);
        //    gl::polygon_offset_clamp(render_style.polygon_offset_factor,
        //                             render_style.polygon_offset_units,
        //                             render_style.polygon_offset_clamp);
        //}
        m_forward_renderer->render(
            {
                .viewport          = context.viewport,
                .camera            = *context.camera,
                .mesh_layers       = { m_scene_root->content_layer(), m_scene_root->controller_layer() },
                .light_layer       = m_scene_root->light_layer(),
                .materials         = m_scene_root->materials(),
                .passes            = { &m_rp_polygon_fill },
                .visibility_filter = content_not_selected_filter
            }
        );
        //gl::disable(gl::Enable_cap::polygon_offset_line);
    }

    if (render_style.edge_lines)
    {
        gl::enable(gl::Enable_cap::sample_alpha_to_coverage);
        m_forward_renderer->primitive_color_source   = render_style.edge_lines_color_source;// Base_renderer::Primitive_color_source::constant_color;
        m_forward_renderer->primitive_constant_color = render_style.line_color;
        m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::constant_size;
        m_forward_renderer->primitive_constant_size  = render_style.line_width;
        m_forward_renderer->render(
            {
                .viewport          = context.viewport,
                .camera            = *context.camera,
                .mesh_layers       = { m_scene_root->content_layer() },
                .light_layer       = m_scene_root->light_layer(),
                .materials         = m_scene_root->materials(),
                .passes            = { &m_rp_edge_lines },
                .visibility_filter = content_not_selected_filter
            }
        );
        gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
    }

    if (render_style.polygon_centroids)
    {
        m_forward_renderer->primitive_color_source   = render_style.polygon_centroids_color_source; // Base_renderer::Primitive_color_source::constant_color;
        m_forward_renderer->primitive_constant_color = render_style.centroid_color;
        m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::constant_size;
        m_forward_renderer->primitive_constant_size  = render_style.point_size;
        m_forward_renderer->render(
            {
                .viewport          = context.viewport,
                .camera            = *context.camera,
                .mesh_layers       = { m_scene_root->content_layer() },
                .light_layer       = m_scene_root->light_layer(),
                .materials         = m_scene_root->materials(),
                .passes            = { &m_rp_polygon_centroids },
                .visibility_filter = content_not_selected_filter
            }
        );
    }

    if (render_style.corner_points)
    {
        m_forward_renderer->primitive_color_source   = render_style.corner_points_color_source; // Base_renderer::Primitive_color_source::constant_color;
        m_forward_renderer->primitive_constant_color = render_style.corner_color;
        m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::constant_size;
        m_forward_renderer->primitive_constant_size  = render_style.point_size;
        m_forward_renderer->render(
            {
                .viewport          = context.viewport,
                .camera            = *context.camera,
                .mesh_layers       = { m_scene_root->content_layer() },
                .light_layer       = m_scene_root->light_layer(),
                .materials         = m_scene_root->materials(),
                .passes            = { &m_rp_corner_points },
                .visibility_filter = content_not_selected_filter
            }
        );
    }
}

void Editor_rendering::render_selection(const Render_context& context)
{
    if (context.camera == nullptr)
    {
        return;
    }
    if (context.viewport_config == nullptr)
    {
        return;
    }

    const auto& render_style = context.viewport_config->render_style_selected;

    constexpr erhe::scene::Visibility_filter content_selected_filter{
        .require_all_bits_set =
            erhe::scene::Node::c_visibility_content |
            erhe::scene::Node::c_visibility_selected
    };

    if (render_style.polygon_fill)
    {
        //if (render_style.polygon_offset_enable)
        //{
        //    gl::enable(gl::Enable_cap::polygon_offset_fill);
        //    gl::polygon_offset_clamp(render_style.polygon_offset_factor,
        //                             render_style.polygon_offset_units,
        //                             render_style.polygon_offset_clamp);
        //}
        //m_forward_renderer->primitive_color_source   = Base_renderer::Primitive_color_source::constant_color;
        //m_forward_renderer->primitive_constant_color = render_style.line_color;
        m_forward_renderer->render(
            {
                .viewport          = context.viewport,
                .camera            = *context.camera,
                .mesh_layers       = { m_scene_root->content_layer() },
                .light_layer       = m_scene_root->light_layer(),
                .materials         = m_scene_root->materials(),
                .passes            = { &m_rp_polygon_fill },
                .visibility_filter = content_selected_filter
            }
        );
        //gl::disable(gl::Enable_cap::polygon_offset_line);
    }

    if (render_style.edge_lines)
    {
        ERHE_PROFILE_SCOPE("selection edge lines");

        gl::enable(gl::Enable_cap::sample_alpha_to_coverage);
        m_forward_renderer->primitive_color_source   = Base_renderer::Primitive_color_source::constant_color;
        m_forward_renderer->primitive_constant_color = render_style.line_color;
        m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::constant_size;
        m_forward_renderer->primitive_constant_size  = render_style.line_width;
        m_forward_renderer->render(
            {
                .viewport          = context.viewport,
                .camera            = *context.camera,
                .mesh_layers       = { m_scene_root->content_layer() },
                .light_layer       = m_scene_root->light_layer(),
                .materials         = m_scene_root->materials(),
                .passes            = { &m_rp_edge_lines, &m_rp_line_hidden_blend },
                .visibility_filter = content_selected_filter
            }
        );
        gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
    }

    gl::enable(gl::Enable_cap::program_point_size);
    if (render_style.polygon_centroids)
    {
        ERHE_PROFILE_SCOPE("selection polygon centroids");

        m_forward_renderer->primitive_color_source   = Base_renderer::Primitive_color_source::constant_color;
        m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::constant_size;
        m_forward_renderer->primitive_constant_color = render_style.centroid_color;
        m_forward_renderer->primitive_constant_size  = render_style.point_size;
        m_forward_renderer->render(
            {
                .viewport          = context.viewport,
                .camera            = *context.camera,
                .mesh_layers       = { m_scene_root->content_layer() },
                .light_layer       = m_scene_root->light_layer(),
                .materials         = m_scene_root->materials(),
                .passes            = { &m_rp_polygon_centroids },
                .visibility_filter = content_selected_filter
            }
        );
    }
    if (render_style.corner_points)
    {
        ERHE_PROFILE_SCOPE("selection corner points");

        m_forward_renderer->primitive_color_source   = Base_renderer::Primitive_color_source::constant_color;
        m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::constant_size;
        m_forward_renderer->primitive_constant_color = render_style.corner_color;
        m_forward_renderer->primitive_constant_size  = render_style.point_size;
        m_forward_renderer->render(
            {
                .viewport          = context.viewport,
                .camera            = *context.camera,
                .mesh_layers       = { m_scene_root->content_layer() },
                .light_layer       = m_scene_root->light_layer(),
                .materials         = m_scene_root->materials(),
                .passes            = { &m_rp_corner_points },
                .visibility_filter = content_selected_filter
            }
        );
    }
    gl::disable(gl::Enable_cap::program_point_size);
}

void Editor_rendering::render_tool_meshes(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION

    if (context.camera == nullptr)
    {
        return;
    }

    m_forward_renderer->render(
        {
            .viewport    = context.viewport,
            .camera      = *context.camera,
            .mesh_layers = { m_scene_root->tool_layer() },
            .light_layer = m_scene_root->light_layer(),
            .materials   = m_scene_root->materials(),
            .passes      =
            {
                &m_rp_tool1_hidden_stencil,   // tag_depth_hidden_with_stencil
                &m_rp_tool2_visible_stencil,  // tag_depth_visible_with_stencil
                &m_rp_tool3_depth_clear,      // clear_depth
                &m_rp_tool4_depth,            // depth_only
                &m_rp_tool5_visible_color,    // require_stencil_tag_depth_visible
                &m_rp_tool6_hidden_color      // require_stencil_tag_depth_hidden_and_blend,
            },
            .visibility_filter =
            {
                .require_all_bits_set = erhe::scene::Node::c_visibility_tool
            }
        }
    );
}

void Editor_rendering::render_gui(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION

    if (context.camera == nullptr)
    {
        return;
    }

    m_editor_imgui_windows->render_rendertarget_gui_meshes(context);
}

void Editor_rendering::render_brush(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION

    if (context.camera == nullptr)
    {
        return;
    }

    m_forward_renderer->render(
        {
            .viewport          = context.viewport,
            .camera            = *context.camera,
            .mesh_layers       = { m_scene_root->brush_layer() },
            .light_layer       = m_scene_root->light_layer(),
            .materials         = m_scene_root->materials(),
            .passes            = { &m_rp_brush_back, &m_rp_brush_front },
            .visibility_filter =
            {
                .require_all_bits_set = erhe::scene::Node::c_visibility_brush
            }
        }
    );
}

}  // namespace editor
