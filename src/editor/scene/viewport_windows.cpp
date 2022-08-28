// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "scene/viewport_windows.hpp"

#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "renderers/shadow_renderer.hpp"
#include "rendergraph/basic_viewport_window.hpp"
#include "rendergraph/post_processing.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "windows/imgui_viewport_window.hpp"

#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_viewport.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/window_imgui_viewport.hpp"
#include "erhe/application/rendergraph/multisample_resolve.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/windows/log_window.hpp"
#include "erhe/application/window.hpp"
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
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

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
    : erhe::components::Component       {c_type_name}
    , m_open_new_viewport_window_command{*this}
{
}

Viewport_windows::~Viewport_windows() noexcept
{
}

void Viewport_windows::declare_required_components()
{
    require<erhe::application::Commands>();
    m_configuration   = require<erhe::application::Configuration>();
    m_imgui_windows   = require<erhe::application::Imgui_windows>();
    m_rendergraph     = require<erhe::application::Rendergraph  >();
    m_window          = require<erhe::application::Window       >();
    m_post_processing = require<Post_processing>();
    m_shadow_renderer = require<Shadow_renderer >();
}

void Viewport_windows::initialize_component()
{
    const auto commands = get<erhe::application::Commands>();
    commands->register_command   (&m_open_new_viewport_window_command);
    commands->bind_command_to_key(&m_open_new_viewport_window_command, erhe::toolkit::Key_f1, true);
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
    erhe::scene::Camera*               camera,
    bool                               enable_post_processing
) -> std::shared_ptr<Viewport_window>
{
    const auto new_viewport_window = std::make_shared<Viewport_window>(
        name,
        *m_components,
        scene_root,
        camera
    );

    std::lock_guard<std::mutex> lock{m_mutex};
    {
        m_viewport_windows.push_back(new_viewport_window);
    }
    m_rendergraph->register_node(new_viewport_window);

    const auto& shadow_render_nodes = m_shadow_renderer->get_nodes();
    if (!shadow_render_nodes.empty())
    {
        auto& shadow_render_node = shadow_render_nodes.front();
        m_rendergraph->connect(
            erhe::application::Rendergraph_node_key::shadow_maps,
            shadow_render_node,
            new_viewport_window
        );
    }

    auto multisample_resolve_node = std::make_shared<erhe::application::Multisample_resolve_node>(
        fmt::format("MSAA for {}", name),
        m_configuration->graphics.msaa_sample_count,
        "viewport",
        erhe::application::Rendergraph_node_key::viewport
    );
    new_viewport_window->link_to(multisample_resolve_node);
    m_rendergraph->register_node(multisample_resolve_node);
    m_rendergraph->connect(
        erhe::application::Rendergraph_node_key::viewport,
        new_viewport_window,
        multisample_resolve_node
    );

    std::shared_ptr<erhe::application::Rendergraph_node> viewport_producer;
    if (enable_post_processing)
    {
        auto post_processing_node = std::make_shared<Post_processing_node>(
            fmt::format("Post processing for {}", name),
            *m_post_processing.get()
        );
        new_viewport_window->link_to(post_processing_node);
        m_rendergraph->register_node(post_processing_node);
        m_rendergraph->connect(
            erhe::application::Rendergraph_node_key::viewport,
            multisample_resolve_node,
            post_processing_node
        );
        new_viewport_window->set_final_output(post_processing_node);
    }
    else
    {
        new_viewport_window->set_final_output(multisample_resolve_node);
    }
    return new_viewport_window;
}

auto Viewport_windows::create_basic_viewport_window(
    const std::shared_ptr<Viewport_window>& viewport_window
) -> std::shared_ptr<Basic_viewport_window>
{
    auto new_basic_viewport_window = std::make_shared<Basic_viewport_window>(
        fmt::format("Basic_viewport_window for '{}'", viewport_window->name()),
        viewport_window
    );
    m_basic_viewport_windows.push_back(new_basic_viewport_window);
    m_rendergraph->register_node(new_basic_viewport_window);
    m_rendergraph->connect(
        erhe::application::Rendergraph_node_key::viewport,
        viewport_window->get_final_output(),
        new_basic_viewport_window
    );

    const int count           = static_cast<int>(m_basic_viewport_windows.size());
    const int a               = std::max<int>(1, static_cast<int>(std::sqrt(count)));
    const int b               = count / a;
    const int window_width    = m_window->get_context_window()->get_width();
    const int window_height   = m_window->get_context_window()->get_height();
    const int x_count         = (window_width >= window_height) ? std::max(a, b) : std::min(a, b);
    const int y_count         = (window_width >= window_height) ? std::min(a, b) : std::max(a, b);
    const int viewport_width  = window_width  / x_count;
    const int viewport_height = window_height / y_count;
    int x = 0;
    int y = 0;
    for (const auto& basic_viewport_window : m_basic_viewport_windows)
    {
        basic_viewport_window->set_viewport(
            erhe::scene::Viewport{
                .x      = x * window_width,
                .y      = y * window_height,
                .width  = viewport_width,
                .height = viewport_height
            }
        );
        ++x;
        if (x == x_count)
        {
            x = 0;
            ++y;
        }
    }
    return new_basic_viewport_window;
}

auto Viewport_windows::create_imgui_viewport_window(
    const std::shared_ptr<Viewport_window>& viewport_window
) -> std::shared_ptr<Imgui_viewport_window>
{
    const auto& window_imgui_viewport = m_imgui_windows->get_window_viewport();

    auto imgui_viewport_window = std::make_shared<Imgui_viewport_window>(
        fmt::format("Imgui_viewport_window for '{}'", viewport_window->name()),
        viewport_window
    );
    m_imgui_viewport_windows.push_back(imgui_viewport_window);
    m_imgui_windows->register_imgui_window(imgui_viewport_window.get());
    m_rendergraph->register_node(imgui_viewport_window);
    m_rendergraph->connect(
        erhe::application::Rendergraph_node_key::viewport,
        viewport_window->get_final_output(),
        imgui_viewport_window
    );
    m_rendergraph->connect(
        erhe::application::Rendergraph_node_key::window,
        imgui_viewport_window,
        window_imgui_viewport
    );
    return imgui_viewport_window;
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
//    for (const auto& window : m_viewport_windows)
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

    if (imgui_viewport != nullptr)
    {
        update_hover_from_imgui_viewport_windows(imgui_viewport);
    }
    if (!m_basic_viewport_windows.empty())
    {
        update_hover_from_basic_viewport_windows();
    }
}

void Viewport_windows::update_hover_from_imgui_viewport_windows(
    erhe::application::Imgui_viewport* imgui_viewport
)
{
    for (const auto& imgui_viewport_window : m_imgui_viewport_windows)
    {
        if (imgui_viewport_window->get_viewport() != imgui_viewport)
        {
            continue;
        }

        const auto viewport_window = imgui_viewport_window->viewport_window();
        if (!viewport_window)
        {
            continue;
        }

        viewport_window->reset_pointer_context();

        if (imgui_viewport_window->is_hovered())
        {
            const glm::vec2 viewport_position = viewport_window->to_scene_content(
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
                viewport_window->name(),
                viewport_position
            );
            viewport_window->update_pointer_context(
                *m_id_renderer.get(),
                viewport_position
            );
            ERHE_VERIFY(m_hover_stack.empty());
            m_last_window = viewport_window;
            m_hover_stack.push_back(m_last_window);
        }
    }
}

void Viewport_windows::update_hover_from_basic_viewport_windows()
{
    for (const auto& basic_viewport_window : m_basic_viewport_windows)
    {
        const auto viewport_window = basic_viewport_window->viewport_window();
        if (!viewport_window)
        {
            continue;
        }

        viewport_window->reset_pointer_context();

        const erhe::scene::Viewport& viewport = basic_viewport_window->get_viewport();
        const bool is_hoverered = viewport.hit_test(
            static_cast<int>(m_mouse_x),
            static_cast<int>(m_mouse_y)
        );

        viewport_window->set_is_hovered(is_hoverered);

        if (is_hoverered)
        {
            const glm::vec2 viewport_position = viewport_window->to_scene_content(
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
                viewport_window->name(),
                viewport_position
            );

            viewport_window->update_pointer_context(
                *m_id_renderer.get(),
                viewport_position
            );
            ERHE_VERIFY(m_hover_stack.empty());
            m_last_window = viewport_window;
            m_hover_stack.push_back(m_last_window);
        }
    }
}

auto Viewport_windows::hover_window() -> std::shared_ptr<Viewport_window>
{
    if (m_hover_stack.empty())
    {
        return {};
    }
    return m_hover_stack.back().lock();
}

auto Viewport_windows::last_window() -> std::shared_ptr<Viewport_window>
{
    return m_last_window.lock();
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
