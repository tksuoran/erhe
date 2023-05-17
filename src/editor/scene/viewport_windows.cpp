// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "scene/viewport_windows.hpp"

#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "graphics/icon_set.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "renderers/shadow_renderer.hpp"
#include "rendergraph/basic_viewport_window.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "rendergraph/post_processing.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "windows/imgui_viewport_window.hpp"

#include "erhe/application/application_view.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_viewport.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/window_imgui_viewport.hpp"
#include "erhe/application/rendergraph/multisample_resolve.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
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
#include "erhe/toolkit/bit_helpers.hpp"
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

#pragma region Commands
Open_new_viewport_window_command::Open_new_viewport_window_command()
    : Command{"Viewport_windows.open_new_viewport_window"}
{
}

auto Open_new_viewport_window_command::try_call() -> bool
{
    g_viewport_windows->open_new_imgui_viewport_window();
    return true;
}
#pragma endregion Commands

Viewport_windows* g_viewport_windows{nullptr};

Viewport_windows::Viewport_windows()
    : erhe::components::Component{c_type_name}
{
}

Viewport_windows::~Viewport_windows() noexcept
{
    ERHE_VERIFY(g_viewport_windows == nullptr);
}

void Viewport_windows::deinitialize_component()
{
    ERHE_VERIFY(g_viewport_windows == this);
    m_open_new_viewport_window_command.set_host(nullptr);
    m_basic_viewport_windows.clear();
    m_imgui_viewport_windows.clear();
    m_viewport_windows.clear();
    m_hover_stack.clear();
    m_hover_window.reset();
    m_last_window.reset();
    g_viewport_windows = nullptr;
}

void Viewport_windows::declare_required_components()
{
    require<erhe::application::Commands     >();
    require<erhe::application::Configuration>();
    require<erhe::application::Imgui_windows>();
    require<erhe::application::Rendergraph  >();
    require<erhe::application::Window       >();
    require<Editor_message_bus>();
    require<Post_processing   >();
    require<Shadow_renderer   >();
}

void Viewport_windows::initialize_component()
{
    ERHE_VERIFY(g_viewport_windows == nullptr);

    Command_host::set_description("Viewport_windows");

    auto& commands = *erhe::application::g_commands;
    commands.register_command            (&m_open_new_viewport_window_command);
    commands.bind_command_to_key         (&m_open_new_viewport_window_command, erhe::toolkit::Key_f1, true);

    g_editor_message_bus->add_receiver(
        [&](Editor_message& message)
        {
            on_message(message);
        }
    );

    m_open_new_viewport_window_command.set_host(this);

    g_viewport_windows = this;
}

void Viewport_windows::on_message(Editor_message& message)
{
    using namespace erhe::toolkit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_graphics_settings)) {
        handle_graphics_settings_changed();
    }
}

void Viewport_windows::handle_graphics_settings_changed()
{
    std::lock_guard<std::mutex> lock{m_mutex};

    for (const auto& viewport_window : m_viewport_windows) {
        viewport_window->reconfigure(erhe::application::g_configuration->graphics.msaa_sample_count);
    }
}

void Viewport_windows::erase(Viewport_window* viewport_window)
{
    const auto i = std::remove_if(
        m_viewport_windows.begin(),
        m_viewport_windows.end(),
        [viewport_window](const auto& entry)
        {
            return entry.get() == viewport_window;
        }
    );
    m_viewport_windows.erase(i, m_viewport_windows.end());
}

void Viewport_windows::erase(Basic_viewport_window* basic_viewport_window)
{
    const auto i = std::remove_if(
        m_basic_viewport_windows.begin(),
        m_basic_viewport_windows.end(),
        [basic_viewport_window](const auto& entry)
        {
            return entry.get() == basic_viewport_window;
        }
    );
    m_basic_viewport_windows.erase(i, m_basic_viewport_windows.end());
}

auto Viewport_windows::create_viewport_window(
    const std::string_view                      name,
    const std::shared_ptr<Scene_root>&          scene_root,
    const std::shared_ptr<erhe::scene::Camera>& camera,
    const int                                   msaa_sample_count,
    const bool                                  enable_post_processing
) -> std::shared_ptr<Viewport_window>
{
    const auto new_viewport_window = std::make_shared<Viewport_window>(
        name,
        scene_root,
        camera
    );

    std::lock_guard<std::mutex> lock{m_mutex};
    {
        m_viewport_windows.push_back(new_viewport_window);
    }
    erhe::application::g_rendergraph->register_node(new_viewport_window);

    if (g_shadow_renderer->config.enabled) {
        //// TODO: Share Shadow_render_node for each unique (scene, camera) pair
        const auto shadow_render_node = g_shadow_renderer->create_node_for_scene_view(
            *new_viewport_window.get()
        );
        erhe::application::g_rendergraph->connect(
            erhe::application::Rendergraph_node_key::shadow_maps,
            shadow_render_node,
            new_viewport_window
        );

        //// const auto& shadow_render_nodes = m_shadow_renderer->get_nodes();
        //// if (!shadow_render_nodes.empty())
        //// {
        ////     auto& shadow_render_node = shadow_render_nodes.front();
        ////     m_rendergraph->connect(
        ////         erhe::application::Rendergraph_node_key::shadow_maps,
        ////         shadow_render_node,
        ////         new_viewport_window
        ////     );
        //// }
    }

    std::shared_ptr<erhe::application::Rendergraph_node> previous_node;
    if (msaa_sample_count > 1) {
        auto multisample_resolve_node = std::make_shared<erhe::application::Multisample_resolve_node>(
            fmt::format("MSAA for {}", name),
            erhe::application::g_configuration->graphics.msaa_sample_count,
            "viewport",
            erhe::application::Rendergraph_node_key::viewport
        );
        new_viewport_window->link_to(multisample_resolve_node);
        erhe::application::g_rendergraph->register_node(multisample_resolve_node);
        erhe::application::g_rendergraph->connect(
            erhe::application::Rendergraph_node_key::viewport,
            new_viewport_window,
            multisample_resolve_node
        );
        previous_node = multisample_resolve_node;
    } else {
        previous_node = new_viewport_window;
    }

    std::shared_ptr<erhe::application::Rendergraph_node> viewport_producer;
    if (enable_post_processing) {
        auto post_processing_node = std::make_shared<Post_processing_node>(
            fmt::format("Post processing for {}", name)
        );
        new_viewport_window->link_to(post_processing_node);
        erhe::application::g_rendergraph->register_node(post_processing_node);
        erhe::application::g_rendergraph->connect(
            erhe::application::Rendergraph_node_key::viewport,
            previous_node,
            post_processing_node
        );
        new_viewport_window->set_final_output(post_processing_node);
    } else {
        new_viewport_window->set_final_output(previous_node);
    }
    return new_viewport_window;
}

auto Viewport_windows::create_basic_viewport_window(
    const std::shared_ptr<Viewport_window>& viewport_window
) -> std::shared_ptr<Basic_viewport_window>
{
    auto new_basic_viewport_window = std::make_shared<Basic_viewport_window>(
        fmt::format("Basic_viewport_window for '{}'", viewport_window->get_name()),
        viewport_window
    );
    m_basic_viewport_windows.push_back(new_basic_viewport_window);
    erhe::application::g_rendergraph->register_node(new_basic_viewport_window);
    erhe::application::g_rendergraph->connect(
        erhe::application::Rendergraph_node_key::viewport,
        viewport_window->get_final_output(),
        new_basic_viewport_window
    );

    layout_basic_viewport_windows();

    return new_basic_viewport_window;
}

void Viewport_windows::layout_basic_viewport_windows()
{
    const int count           = static_cast<int>(m_basic_viewport_windows.size());
    const int a               = std::max<int>(1, static_cast<int>(std::sqrt(count)));
    const int b               = count / a;
    const int window_width    = erhe::application::g_window->get_context_window()->get_width();
    const int window_height   = erhe::application::g_window->get_context_window()->get_height();
    const int x_count         = (window_width >= window_height) ? std::max(a, b) : std::min(a, b);
    const int y_count         = (window_width >= window_height) ? std::min(a, b) : std::max(a, b);
    const int viewport_width  = window_width  / x_count;
    const int viewport_height = window_height / y_count;
    int x = 0;
    int y = 0;
    for (const auto& basic_viewport_window : m_basic_viewport_windows) {
        basic_viewport_window->set_viewport(
            erhe::scene::Viewport{
                .x      = x * window_width,
                .y      = y * window_height,
                .width  = viewport_width,
                .height = viewport_height
            }
        );
        ++x;
        if (x == x_count) {
            x = 0;
            ++y;
        }
    }
}

auto Viewport_windows::create_imgui_viewport_window(
    const std::shared_ptr<Viewport_window>& viewport_window
) -> std::shared_ptr<Imgui_viewport_window>
{
    const auto& window_imgui_viewport = erhe::application::g_imgui_windows->get_window_viewport();

    auto imgui_viewport_window = std::make_shared<Imgui_viewport_window>(
        fmt::format("Viewport {}", m_imgui_viewport_windows.size()),
        viewport_window
    );
    m_imgui_viewport_windows.push_back(imgui_viewport_window);
    erhe::application::g_imgui_windows->register_imgui_window(
        imgui_viewport_window.get(),
        true
    );
    erhe::application::g_rendergraph->register_node(imgui_viewport_window);
    erhe::application::g_rendergraph->connect(
        erhe::application::Rendergraph_node_key::viewport,
        viewport_window->get_final_output(),
        imgui_viewport_window
    );
    erhe::application::g_rendergraph->connect(
        erhe::application::Rendergraph_node_key::window,
        imgui_viewport_window,
        window_imgui_viewport
    );
    return imgui_viewport_window;
}

auto Viewport_windows::open_new_viewport_window(
    const std::shared_ptr<Scene_root>& scene_root
) -> std::shared_ptr<Viewport_window>
{
    const std::string name = fmt::format("Viewport {}", m_viewport_windows.size());

    if (scene_root) {
        if (g_selection_tool != nullptr) {
            for (const auto& entry : g_selection_tool->get_selection()) {
                if (is_camera(entry)) {
                    const auto camera = as_camera(entry);
                    return create_viewport_window(
                        name,
                        scene_root,
                        camera,
                        erhe::application::g_configuration->graphics.msaa_sample_count
                    );
                }
            }
        }
        // Case for when no camera found in selection
        if (!scene_root->scene().get_cameras().empty()) {
            const auto& camera = scene_root->scene().get_cameras().front();
            return create_viewport_window(
                name,
                scene_root,
                camera,
                erhe::application::g_configuration->graphics.msaa_sample_count
            );
        }
    }

    // Case for when no cameras found in scene
    return create_viewport_window(
        name,
        {},
        nullptr,
        erhe::application::g_configuration->graphics.msaa_sample_count
    );
}

void Viewport_windows::open_new_imgui_viewport_window()
{
    auto        viewport_window = open_new_viewport_window();
    const auto& configuration   = *erhe::application::g_configuration;
    if (configuration.imgui.window_viewport) {
        create_imgui_viewport_window(viewport_window);
    } else {
        create_basic_viewport_window(viewport_window);
    }
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

void Viewport_windows::update_hover(erhe::application::Imgui_viewport* imgui_viewport)
{
    ERHE_PROFILE_FUNCTION();

    std::shared_ptr<Viewport_window> old_window = m_hover_window;
    m_hover_stack.clear();

    // Pull mouse position
    {
        const auto mouse_position = erhe::application::g_view->mouse_position();
        for (auto& imgui_viewport_window : m_imgui_viewport_windows) {
            if (imgui_viewport_window->is_hovered()) {
                imgui_viewport_window->on_mouse_move(mouse_position);
            }
        }
    }

    if (imgui_viewport != nullptr) {
        update_hover_from_imgui_viewport_windows(imgui_viewport);
    }
    if (!m_basic_viewport_windows.empty()) {
        layout_basic_viewport_windows();
        update_hover_from_basic_viewport_windows();
    }

    m_hover_window = m_hover_stack.empty()
        ? std::shared_ptr<Viewport_window>{}
        : m_hover_stack.back().lock();

    if (old_window != m_hover_window) {
        g_editor_message_bus->send_message(
            Editor_message{
                .update_flags = Message_flag_bit::c_flag_bit_hover_viewport | Message_flag_bit::c_flag_bit_hover_scene_view,
                .scene_view   = m_hover_window.get()
            }
        );
    }
}

void Viewport_windows::update_hover_from_imgui_viewport_windows(
    erhe::application::Imgui_viewport* imgui_viewport
)
{
    for (const auto& imgui_viewport_window : m_imgui_viewport_windows) {
        if (imgui_viewport_window->get_viewport() != imgui_viewport) {
            continue;
        }

        const auto viewport_window = imgui_viewport_window->viewport_window();
        if (!viewport_window) {
            continue;
        }

        if (imgui_viewport_window->is_hovered())
        {
            m_last_window = viewport_window;
            m_hover_stack.push_back(m_last_window);
        }
    }
}

void Viewport_windows::update_hover_from_basic_viewport_windows()
{
    glm::vec2 pointer_window_position{0.0f, 0.0f};
    erhe::application::g_window->get_context_window()->get_cursor_position(
        pointer_window_position.x,
        pointer_window_position.y
    );

    m_hover_stack.clear();
    for (const auto& basic_viewport_window : m_basic_viewport_windows) {
        const auto viewport_window = basic_viewport_window->get_viewport_window();
        if (!viewport_window) {
            continue;
        }

        const erhe::scene::Viewport& viewport = basic_viewport_window->get_viewport();
        const bool is_hoverered = viewport.hit_test(
            static_cast<int>(std::round(pointer_window_position.x)),
            static_cast<int>(std::round(pointer_window_position.y))
        );

        viewport_window->set_is_hovered(is_hoverered);

        if (is_hoverered) {
            const glm::vec2 viewport_position = viewport_window->viewport_from_window(
                pointer_window_position
            );
            SPDLOG_LOGGER_TRACE(
                log_pointer,
                "mouse {}, {} hovers viewport {} @ {}",
                m_mouse_x,
                m_mouse_y,
                viewport_window->get_name(),
                viewport_position
            );

            viewport_window->update_pointer_2d_position(viewport_position);
            ERHE_VERIFY(m_hover_stack.empty());
            m_last_window = viewport_window;
            m_hover_stack.push_back(m_last_window);
        } else {
            viewport_window->reset_control_transform();
            viewport_window->reset_hover_slots();
        }
    }
}

auto Viewport_windows::hover_window() -> std::shared_ptr<Viewport_window>
{
    if (m_hover_stack.empty()) {
        return {};
    }
    return m_hover_stack.back().lock();
}

auto Viewport_windows::last_window() -> std::shared_ptr<Viewport_window>
{
    return m_last_window.lock();
}

void Viewport_windows::viewport_toolbar(
    Viewport_window& viewport_window,
    bool&            hovered
)
{
    ImGui::PushID("Viewport_windows::viewport_toolbar");
    const auto& rasterization = g_icon_set->get_small_rasterization();

    static constexpr std::string_view open_config{"open_config"};
    static constexpr uint32_t viewport_open_config_id{
        compiletime_xxhash::xxh32(open_config.data(), open_config.size(), {})
    };

    const bool button_pressed = rasterization.icon_button(
        ERHE_HASH("open_config"),
        g_icon_set->icons.three_dots
    );
    if (ImGui::IsItemHovered()) {
        hovered = true;
    }
    if (button_pressed) {
        g_viewport_config_window->show();
        g_viewport_config_window->edit_data = viewport_window.get_config();
    }
    ImGui::PopID();
}

} // namespace editor
