#include "rendering.hpp"
#include "application.hpp"
#include "configuration.hpp"
#include "log.hpp"
#include "time.hpp"
#include "tools.hpp"
#include "view.hpp"
#include "window.hpp"
#include "renderers/forward_renderer.hpp"
#include "renderers/headset_renderer.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/line_renderer.hpp"
#include "renderers/shadow_renderer.hpp"
#include "renderers/text_renderer.hpp"
#include "scene/scene_manager.hpp"
#include "scene/scene_root.hpp"
#include "tools/fly_camera_tool.hpp"
#include "windows/frame_log_window.hpp"
#include "windows/viewport_config.hpp"
#include "windows/viewport_window.hpp"

#include "erhe/geometry/shapes/cone.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

#include <glm/trigonometric.hpp>

namespace editor {

using namespace erhe::graphics;

Editor_rendering::Editor_rendering()
    : erhe::components::Component{c_name}
{
}

Editor_rendering::~Editor_rendering()
{
}

void Editor_rendering::connect()
{
    m_application            = get<Application      >();
    m_configuration          = get<Configuration    >();
    m_editor_view            = get<Editor_view      >();
    m_editor_time            = get<Editor_time      >();
    m_editor_tools           = get<Editor_tools     >();
    m_forward_renderer       = get<Forward_renderer >();
    m_frame_log_window       = get<Frame_log_window >();
    m_headset_renderer       = get<Headset_renderer >();
    m_id_renderer            = get<Id_renderer      >();
    m_line_renderer          = get<Line_renderer    >();
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
    m_pointer_context        = get<Pointer_context >();
    m_scene_manager          = get<Scene_manager   >();
    m_scene_root             = get<Scene_root      >();
    m_shadow_renderer        = get<Shadow_renderer >();
    m_text_renderer          = get<Text_renderer   >();
    m_viewport_windows       = get<Viewport_windows>();
}

void Editor_rendering::init_state()
{
    gl::clip_control(gl::Clip_control_origin::lower_left, gl::Clip_control_depth::zero_to_one);
    gl::disable     (gl::Enable_cap::primitive_restart);
    gl::enable      (gl::Enable_cap::primitive_restart_fixed_index);
    gl::enable      (gl::Enable_cap::texture_cube_map_seamless);
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

    if (m_configuration->gui)
    {
        m_editor_tools->menu();
    }

    if (m_headset_renderer)
    {
        m_frame_log_window->log("headset rendering");
        m_headset_renderer->begin_frame();
    }

    m_editor_tools->begin_frame();
}

///// auto Editor_rendering::is_primary_scene_output_framebuffer_ready() -> bool
///// {
/////     if (m_configuration->gui)
/////     {
/////         return m_viewport_window->is_framebuffer_ready();
/////     }
/////     else
/////     {
/////         return true;
/////     }
///// }

void Editor_rendering::gui_render()
{
    ERHE_PROFILE_FUNCTION

    Expects(m_pipeline_state_tracker != nullptr);

    m_viewport_windows->render();

    // Pipeline state required for NVIDIA driver not to complain about texture
    // unit state when doing the clear.
    m_pipeline_state_tracker->shader_stages.reset();
    m_pipeline_state_tracker->color_blend.execute(&Color_blend_state::color_blend_disabled);
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    gl::disable         (gl::Enable_cap::framebuffer_srgb);
    gl::viewport        (0, 0, width(), height());
    gl::clear_color     (0.0f, 0.0f, 0.2f, 0.1f);
    gl::clear           (gl::Clear_buffer_mask::color_buffer_bit);
}

//auto Editor_rendering::is_content_in_focus() const -> bool
//{
//    return m_configuration->gui 
//        ? m_viewport_windows->is_content_in_focus()
//        : true;
//}
//
//auto Editor_rendering::to_scene_content(const glm::vec2 position_in_root) const -> glm::vec2
//{
//    if (m_configuration->gui)
//    {
//        return m_viewport_windows->to_scene_content(position_in_root);
//    }
//    else
//    {
//        return {
//            position_in_root.x,
//            height() - position_in_root.y
//        };
//    }
//}

void Editor_rendering::render()
{
    ERHE_PROFILE_FUNCTION

    Expects(m_application);
    Expects(m_scene_manager);
    Expects(m_editor_view);

    const int w = width();
    const int h = height();
    if ((w == 0) || (h == 0))
    {
        m_frame_log_window->log("width and/or height is zero, skipping rendering");
        return;
    }

    if (m_trigger_capture)
    {
        get<Window>()->begin_renderdoc_capture();
    }

    begin_frame();

    // Render shadow maps
    if (
        (m_scene_manager != nullptr) &&
        (m_shadow_renderer != nullptr)
    )
    {
        m_frame_log_window->log("rendering shadowmaps");
        m_scene_manager->sort_lights();
        m_shadow_renderer->render(
            m_scene_root->content_layers(),
            *m_scene_root->light_layer().get()
        );
    }

    if (m_configuration->gui)
    {
        m_frame_log_window->log("rendering gui");
        m_pointer_context->set_priority_action(m_editor_tools->get_priority_action());
        gui_render();
        m_editor_tools->imgui_windows();
    }
    else
    {
        m_frame_log_window->log("rendering scene directly to window");
        gl::bind_framebuffer(gl::Framebuffer_target::framebuffer, 0);
        gl::viewport(0, 0, width(), height());
        m_pipeline_state_tracker->shader_stages.reset();
        m_pipeline_state_tracker->color_blend.execute(&Color_blend_state::color_blend_disabled);
        gl::clear_color(0.0f, 0.0f, 0.0f, 1.0f);
        gl::clear_depth_f(*m_configuration->depth_clear_value_pointer());
        gl::clear(gl::Clear_buffer_mask::color_buffer_bit | gl::Clear_buffer_mask::depth_buffer_bit);

        const Render_context render_context{
            nullptr,                          // TODO viewport_window
            nullptr,                          // viewport config
            get<Fly_camera_tool>()->camera(), // camera for non-gui
            {                                 // viewport for non-gui
                0,
                0,
                width(),
                height(),
                m_configuration->reverse_depth
            }
        };

        m_pointer_context->update(nullptr); // TODO
        render_viewport(render_context);
    }

    if (m_headset_renderer)
    {
        m_headset_renderer->render();
    }

    if (m_shadow_renderer)  m_shadow_renderer ->next_frame();
    if (m_id_renderer)      m_id_renderer     ->next_frame();
    if (m_forward_renderer) m_forward_renderer->next_frame();
    if (m_text_renderer)    m_text_renderer   ->next_frame();
    if (m_line_renderer)    m_line_renderer   ->next_frame();
}

void Editor_rendering::render_viewport(const Render_context& context)
{
    m_frame_log_window->log("rendering viewport");

    gl::enable(gl::Enable_cap::framebuffer_srgb);

    if (m_scene_manager && m_forward_renderer)
    {
        render_content    (context);
        render_selection  (context);
        render_brush      (context);
        render_tool_meshes(context);
    }

    gl::disable(gl::Enable_cap::framebuffer_srgb);

    m_editor_tools->render_tools(context);

    if (m_line_renderer)
    {
        m_line_renderer->render(context.viewport, *context.camera);
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
        (m_pointer_context->window() == nullptr) ||
        !m_pointer_context->pointer_in_content_area() ||
        (context.camera == nullptr) ||
        !m_pointer_context->position_in_viewport_window().has_value()
    )
    {
        return;
    }

    if (m_pointer_context->window() != context.window)
    {
        return;
    }

    auto pointer = m_pointer_context->position_in_viewport_window().value();
    m_frame_log_window->log("rendering id @ {}", pointer);

    m_id_renderer->render(
        context.viewport,
        m_scene_root->content_layers(),
        m_scene_root->tool_layers(),
        *context.camera,
        m_editor_time->time(),
        static_cast<int>(pointer.x),
        static_cast<int>(pointer.y)
    );
}

void Editor_rendering::render_content(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION

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
        erhe::scene::Node::c_visibility_content,    // all set
        0u,                                         // at least one set
        erhe::scene::Node::c_visibility_selected,   // all clear
        0u                                          // at least one clear
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
            context.viewport,
            *context.camera,
            m_scene_root->content_layers(),
            *m_scene_root->light_layer().get(),
            m_scene_root->materials(),
            {
                Forward_renderer::Pass::polygon_fill
            },
            content_not_selected_filter
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
            context.viewport,
            *context.camera,
            m_scene_root->content_fill_layers(),
            *m_scene_root->light_layer().get(),
            m_scene_root->materials(),
            {
                Forward_renderer::Pass::edge_lines
            },
            content_not_selected_filter
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
            context.viewport,
            *context.camera,
            m_scene_root->content_layers(),
            *m_scene_root->light_layer().get(),
            m_scene_root->materials(),
            {
                Forward_renderer::Pass::polygon_centroids
            },
            content_not_selected_filter
        );
    }
    if (render_style.corner_points)
    {
        m_forward_renderer->primitive_color_source   = render_style.corner_points_color_source; // Base_renderer::Primitive_color_source::constant_color;
        m_forward_renderer->primitive_constant_color = render_style.corner_color;
        m_forward_renderer->primitive_size_source    = Base_renderer::Primitive_size_source::constant_size;
        m_forward_renderer->primitive_constant_size  = render_style.point_size;
        m_forward_renderer->render(
            context.viewport,
            *context.camera,
            m_scene_root->content_layers(),
            *m_scene_root->light_layer().get(),
            m_scene_root->materials(),
            {
                Forward_renderer::Pass::corner_points
            },
            content_not_selected_filter
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
        (
            erhe::scene::Node::c_visibility_content |  // all set
            erhe::scene::Node::c_visibility_selected
        ), 
        0u,                                         // at least one set
        0u,                                         // all clear
        0u                                          // at least one cler
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
            context.viewport,
            *context.camera,
            m_scene_root->content_layers(),
            *m_scene_root->light_layer().get(),
            m_scene_root->materials(),
            {
                Forward_renderer::Pass::polygon_fill
            },
            content_selected_filter
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
            context.viewport,
            *context.camera,
            m_scene_root->content_layers(),
            *m_scene_root->light_layer().get(),
            m_scene_root->materials(),
            {
                Forward_renderer::Pass::edge_lines,
                Forward_renderer::Pass::hidden_line_with_blend
            },
            content_selected_filter
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
            context.viewport,
            *context.camera,
            m_scene_root->content_layers(),
            *m_scene_root->light_layer().get(),
            m_scene_root->materials(),
            { Forward_renderer::Pass::polygon_centroids },
            content_selected_filter
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
            context.viewport,
            *context.camera,
            m_scene_root->content_layers(),
            *m_scene_root->light_layer().get(),
            m_scene_root->materials(),
            {
                Forward_renderer::Pass::corner_points
            },
            content_selected_filter
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

    constexpr erhe::scene::Visibility_filter tool_filter{
        erhe::scene::Node::c_visibility_tool,   // all set
        0u,                                     // at least one set
        0u,                                     // all clear
        0u                                      // at least one cler
    };                                    

    m_forward_renderer->render(
        context.viewport,
        *context.camera,
        m_scene_root->tool_layers(),
        *m_scene_root->light_layer().get(),
        m_scene_root->materials(),
        {
            Forward_renderer::Pass::tag_depth_hidden_with_stencil,
            Forward_renderer::Pass::tag_depth_visible_with_stencil,
            Forward_renderer::Pass::clear_depth,
            Forward_renderer::Pass::depth_only,
            Forward_renderer::Pass::require_stencil_tag_depth_visible,
            Forward_renderer::Pass::require_stencil_tag_depth_hidden_and_blend,
        },
        tool_filter
    );
}

void Editor_rendering::render_brush(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION

    if (context.camera == nullptr)
    {
        return;
    }

    constexpr erhe::scene::Visibility_filter brush_filter{
        erhe::scene::Node::c_visibility_brush,  // all set
        0u,                                     // at least one set
        0u,                                     // all clear
        0u                                      // at least one cler
    };                                    

    m_forward_renderer->render(
        context.viewport,
        *context.camera,
        m_scene_root->brush_layers(),
        *m_scene_root->light_layer().get(),
        m_scene_root->materials(),
        {
            Forward_renderer::Pass::brush_back,
            Forward_renderer::Pass::brush_front
        },
        brush_filter
    );
}

}  // namespace editor
