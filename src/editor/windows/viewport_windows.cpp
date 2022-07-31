// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "windows/viewport_windows.hpp"

#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/post_processing.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_renderer.hpp"
#endif
#include "windows/viewport_window.hpp"

#include "erhe/application/windows/log_window.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui_viewport.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/render_graph.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/window_imgui_viewport.hpp"
#include "erhe/gl/enum_string_functions.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/renderbuffer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{


using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;
using erhe::graphics::Framebuffer;
using erhe::graphics::Renderbuffer;
using erhe::graphics::Texture;

auto Open_new_viewport_window_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    static_cast<void>(context);

    return m_viewport_windows.open_new_viewport_window();
}

Viewport_windows::Viewport_windows()
    : erhe::components::Component       {c_label}
    , m_open_new_viewport_window_command{*this}
{
}

Viewport_windows::~Viewport_windows() noexcept
{
}

void Viewport_windows::declare_required_components()
{
    m_configuration = require<erhe::application::Configuration>();
    m_imgui_windows = require<erhe::application::Imgui_windows>();
    m_render_graph  = require<erhe::application::Render_graph >();
    m_editor_view   = require<erhe::application::View         >();
#if defined(ERHE_XR_LIBRARY_OPENXR)
    m_headset_renderer = require<Headset_renderer>();
#endif
}

void Viewport_windows::initialize_component()
{
//#if defined(ERHE_XR_LIBRARY_OPENXR)
//    {
//        auto* const headset_camera = m_headset_renderer->root_camera().get();
//        create_window("Headset Camera", headset_camera);
//    }
//#else
    m_editor_view->register_command   (&m_open_new_viewport_window_command);
    m_editor_view->bind_command_to_key(&m_open_new_viewport_window_command, erhe::toolkit::Key_f1, true);
}

void Viewport_windows::post_initialize()
{
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
    m_editor_rendering       = get<Editor_rendering>();
    m_id_renderer            = get<Id_renderer     >();
    m_viewport_config        = get<Viewport_config >();
}

auto Viewport_windows::create_window(
    const std::string_view             name,
    const std::shared_ptr<Scene_root>& scene_root,
    erhe::scene::Camera*               camera
) -> std::shared_ptr<Viewport_window>
{
    const auto new_window = std::make_shared<Viewport_window>(
        name,
        *m_components,
        scene_root,
        camera
    );

    m_windows.push_back(new_window);
    m_render_graph->register_node(new_window);

    auto& configuration = *m_configuration.get();
    if (configuration.imgui.enabled)
    {
        m_imgui_windows->register_imgui_window(new_window.get());
    }
    return new_window;
}

auto Viewport_windows::open_new_viewport_window(
    const std::shared_ptr<Scene_root>& scene_root
) -> bool
{
    if (scene_root)
    {
        std::string name = fmt::format("Viewport for {}", scene_root->name());

        auto selection_tool = try_get<Selection_tool>();
        if (selection_tool)
        {
            for (const auto& entry : selection_tool->selection())
            {
                if (is_camera(entry))
                {
                    erhe::scene::Camera* camera = as_camera(entry.get());
                    create_window(name, scene_root, camera);
                    return true;
                }
            }
        }
        // Case for when no camera found in selection
        if (!m_scene_root->scene().cameras.empty())
        {
            const auto& camera = scene_root->scene().cameras.front();
            create_window(name, scene_root, camera.get());
        }
        return true;
    }

    // Case for when no cameras found in scene
    create_window("Viewport", {}, nullptr);
    return true;
}

//void Viewport_windows::update_hover()
//{
//    reset_hover();
//    update_hover(m_window_imgui_viewport.get());
//
//    for (const auto& window : m_windows)
//    {
//        window->reset_pointer_context();
//
//        if (window->get_viewport() != imgui_viewport)
//        {
//            continue;
//        }
//
//        for (auto& viewport : m_rendertarget_nodes)
//        {
//            viewport->update_pointer();
//            m_viewport_windows->update_hover(viewport.get());
//        }
//    }
//}

void Viewport_windows::reset_hover()
{
    m_hover_stack.clear();
}

void Viewport_windows::update_hover(erhe::application::Imgui_viewport* imgui_viewport)
{
    ERHE_PROFILE_FUNCTION

    for (const auto& window : m_windows)
    {
        window->reset_pointer_context();

        if (window->get_viewport() != imgui_viewport)
        {
            continue;
        }

        if (window->is_hovered())
        {
            const glm::vec2 viewport_position = window->to_scene_content(
                glm::vec2{
                    static_cast<float>(m_mouse_x),
                    static_cast<float>(m_mouse_y)
                }
            );
            SPDLOG_LOGGER_TRACE(
                log_pointer,
                "mouse {}, {} hovers viewport {} @ {}",
                m_mouse_x,
                m_mouse_y,
                window->name(),
                viewport_position
            );
            window->update_pointer_context(
                *m_id_renderer.get(),
                viewport_position
            );
            ERHE_VERIFY(m_hover_stack.empty());
            m_last_window = window.get();
            m_hover_stack.push_back(m_last_window);
        }
    }
}

auto Viewport_windows::hover_window() -> Viewport_window*
{
    if (m_hover_stack.empty())
    {
        return nullptr;
    }
    return m_hover_stack.back();
}

auto Viewport_windows::last_window() -> Viewport_window*
{
    return m_last_window;
}

void Viewport_windows::update_keyboard(
    const bool                   pressed,
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask
)
{
    static_cast<void>(pressed);
    static_cast<void>(code);

    m_shift   = (modifier_mask & erhe::toolkit::Key_modifier_bit_shift) == erhe::toolkit::Key_modifier_bit_shift;
    m_alt     = (modifier_mask & erhe::toolkit::Key_modifier_bit_menu ) == erhe::toolkit::Key_modifier_bit_menu;
    m_control = (modifier_mask & erhe::toolkit::Key_modifier_bit_ctrl ) == erhe::toolkit::Key_modifier_bit_ctrl;
}

void Viewport_windows::update_mouse(
    const erhe::toolkit::Mouse_button button,
    const int                         count
)
{
    SPDLOG_LOGGER_TRACE(log_pointer, "mouse {} count = {}", static_cast<int>(button), count);
    m_mouse_button[button].pressed  = (count > 0);
    m_mouse_button[button].released = (count == 0);
}

void Viewport_windows::update_mouse(
    const double x,
    const double y
)
{
    SPDLOG_LOGGER_TRACE(log_pointer, "mouse x = {} y = {}", x, y);
    m_mouse_x = x;
    m_mouse_y = y;
}

auto Viewport_windows::shift_key_down() const -> bool
{
    return m_shift;
}

auto Viewport_windows::control_key_down() const -> bool
{
    return m_control;
}

auto Viewport_windows::alt_key_down() const -> bool
{
    return m_alt;
}

auto Viewport_windows::mouse_button_pressed(const erhe::toolkit::Mouse_button button) const -> bool
{
    Expects(button < erhe::toolkit::Mouse_button_count);
    return m_mouse_button[static_cast<int>(button)].pressed;
}

auto Viewport_windows::mouse_button_released(const erhe::toolkit::Mouse_button button) const -> bool
{
    Expects(button < erhe::toolkit::Mouse_button_count);
    return m_mouse_button[static_cast<int>(button)].released;
}

auto Viewport_windows::mouse_x() const -> double
{
    return m_mouse_x;
}
auto Viewport_windows::mouse_y() const -> double
{
    return m_mouse_y;
}

} // namespace editor
