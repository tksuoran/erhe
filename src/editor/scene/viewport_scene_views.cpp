// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "scene/viewport_scene_views.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_settings.hpp"
#include "input_state.hpp"
#include "graphics/icon_set.hpp"
#include "rendergraph/basic_scene_view_node.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "rendergraph/post_processing.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "windows/viewport_config_window.hpp"
#include "windows/imgui_window_scene_view_node.hpp"

#include "erhe_bit/bit_helpers.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/renderbuffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_hash/xxhash.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_host.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/window_imgui_host.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_rendergraph/multisample_resolve.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_window/window.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

namespace editor{

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;
using erhe::graphics::Framebuffer;
using erhe::graphics::Renderbuffer;
using erhe::graphics::Texture;

#pragma region Commands
Open_new_viewport_scene_view_command::Open_new_viewport_scene_view_command(erhe::commands::Commands& commands, Editor_context& editor_context)
    : Command  {commands, "Scene_views.open_new_viewport_scene_view"}
    , m_context{editor_context}
{
}

auto Open_new_viewport_scene_view_command::try_call() -> bool
{
    m_context.scene_views->open_new_viewport_scene_view_node();
    return true;
}
#pragma endregion Commands

Scene_views::Scene_views(
    erhe::commands::Commands& commands,
    Editor_context&           editor_context,
    Editor_message_bus&       editor_message_bus
)
    : m_context                             {editor_context}
    , m_open_new_viewport_scene_view_command{commands, editor_context}
{
    Command_host::set_description("Scene_views");

    commands.register_command   (&m_open_new_viewport_scene_view_command);
    commands.bind_command_to_key(&m_open_new_viewport_scene_view_command, erhe::window::Key_f1, true);

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            on_message(message);
        }
    );

    m_open_new_viewport_scene_view_command.set_host(this);
}

void Scene_views::on_message(Editor_message& message)
{
    using namespace erhe::bit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_graphics_settings)) {
        handle_graphics_settings_changed(message.graphics_preset);
    }
}

void Scene_views::handle_graphics_settings_changed(Graphics_preset* graphics_preset)
{
    std::lock_guard<std::mutex> lock{m_mutex};

    const int msaa_sample_count = graphics_preset != nullptr ? graphics_preset->msaa_sample_count : 1;
    for (const auto& viewport_scene_view : m_viewport_scene_views) {
        viewport_scene_view->reconfigure(msaa_sample_count);
    }
}

void Scene_views::erase(Viewport_scene_view* viewport_scene_view)
{
    const auto i = std::remove_if(
        m_viewport_scene_views.begin(),
        m_viewport_scene_views.end(),
        [viewport_scene_view](const auto& entry) {
            return entry.get() == viewport_scene_view;
        }
    );
    m_viewport_scene_views.erase(i, m_viewport_scene_views.end());
}

void Scene_views::erase(Basic_scene_view_node* basic_viewport_window)
{
    const auto i = std::remove_if(
        m_basic_scene_view_nodes.begin(),
        m_basic_scene_view_nodes.end(),
        [basic_viewport_window](const auto& entry) {
            return entry.get() == basic_viewport_window;
        }
    );
    m_basic_scene_view_nodes.erase(i, m_basic_scene_view_nodes.end());
}

auto Scene_views::create_viewport_scene_view(
    erhe::graphics::Instance&                   graphics_instance,
    erhe::rendergraph::Rendergraph&             rendergraph,
    Editor_rendering&                           editor_rendering,
    Editor_settings&                            editor_settings,
    Post_processing&                            post_processing,
    Tools&                                      tools,
    const std::string_view                      name,
    const std::shared_ptr<Scene_root>&          scene_root,
    const std::shared_ptr<erhe::scene::Camera>& camera,
    const int                                   msaa_sample_count,
    bool                                        enable_post_processing
) -> std::shared_ptr<Viewport_scene_view>
{
    const auto new_viewport_window = std::make_shared<Viewport_scene_view>(
        m_context,
        rendergraph,
        tools,
        name,
        nullptr, // no ini for viewport windows for now
        scene_root,
        camera
    );

    std::lock_guard<std::mutex> lock{m_mutex};
    {
        m_viewport_scene_views.push_back(new_viewport_window);
    }

    if (editor_settings.graphics.current_graphics_preset.shadow_enable) {
        //// TODO: Share Shadow_render_node for each unique (scene, camera) pair
        const auto shadow_render_node = editor_rendering.create_shadow_node_for_scene_view(
            graphics_instance,
            rendergraph,
            editor_settings,
            *new_viewport_window.get()
        );
        rendergraph.connect(
            erhe::rendergraph::Rendergraph_node_key::shadow_maps,
            shadow_render_node.get(),
            new_viewport_window.get()
        );

        //// const auto& shadow_render_nodes = Shadow_render_node::get_all_nodes();
        //// if (!shadow_render_nodes.empty())
        //// {
        ////     auto& shadow_render_node = shadow_render_nodes.front();
        ////     g_rendergraph->connect(
        ////         erhe::rendergraph::Rendergraph_node_key::shadow_maps,
        ////         shadow_render_node,
        ////         new_viewport_window
        ////     );
        //// }
    }

    std::shared_ptr<erhe::rendergraph::Rendergraph_node> previous_node;
    log_post_processing->trace("Scene_views::create_viewport_scene_view(): msaa_sample_count = {}", msaa_sample_count);
    if (msaa_sample_count > 1) {
        log_post_processing->trace("Adding Multisample_resolve_node to rendergraph");
        auto multisample_resolve_node = std::make_shared<erhe::rendergraph::Multisample_resolve_node>(
            rendergraph,
            fmt::format("MSAA for {}", name),
            msaa_sample_count,
            fmt::format("MSAA resolve node for Viewport_scene_view {}", name),
            erhe::rendergraph::Rendergraph_node_key::viewport
        );
        new_viewport_window->link_to(multisample_resolve_node);
        rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::viewport, new_viewport_window.get(), multisample_resolve_node.get());
        previous_node = multisample_resolve_node;
    } else {
        log_post_processing->trace("Multisample is disabled (not added to rendergraph)");
        previous_node = new_viewport_window;
    }

    std::shared_ptr<erhe::rendergraph::Rendergraph_node> viewport_producer;
    if (enable_post_processing) {
        log_post_processing->trace("Adding post processing node to rendergraph");
        auto post_processing_node = std::make_shared<Post_processing_node>(
            graphics_instance,
            rendergraph,
            post_processing,
            fmt::format("Post processing for {}", name)
        );
        new_viewport_window->link_to(post_processing_node);
        rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::viewport, previous_node.get(), post_processing_node.get());
        new_viewport_window->set_final_output(post_processing_node);
    } else {
        log_post_processing->trace("Post processing is disabled (not added to rendergraph)");
        new_viewport_window->set_final_output(previous_node);
    }
    return new_viewport_window;
}

auto Scene_views::create_basic_viewport_scene_view_node(
    erhe::rendergraph::Rendergraph&             rendergraph,
    const std::shared_ptr<Viewport_scene_view>& viewport_scene_view,
    std::string_view                            name
) -> std::shared_ptr<Basic_scene_view_node>
{
    auto node = std::make_shared<Basic_scene_view_node>(rendergraph, name, viewport_scene_view);
    m_basic_scene_view_nodes.push_back(node);
    rendergraph.connect( erhe::rendergraph::Rendergraph_node_key::viewport, viewport_scene_view->get_final_output(), node.get());
    layout_basic_viewport_windows();
    return node;
}

void Scene_views::layout_basic_viewport_windows()
{
    const int window_width    = m_context.context_window->get_width();
    const int window_height   = m_context.context_window->get_height();
    const int count           = static_cast<int>(m_basic_scene_view_nodes.size());
    const int a               = std::max<int>(1, static_cast<int>(std::sqrt(count)));
    const int b               = count / a;
    const int x_count         = (window_width >= window_height) ? std::max(a, b) : std::min(a, b);
    const int y_count         = (window_width >= window_height) ? std::min(a, b) : std::max(a, b);
    const int viewport_width  = window_width  / x_count;
    const int viewport_height = window_height / y_count;
    int x = 0;
    int y = 0;
    for (const auto& basic_viewport_window : m_basic_scene_view_nodes) {
        basic_viewport_window->set_viewport(
            erhe::math::Viewport{
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

auto Scene_views::create_imgui_window_scene_view_node(
    erhe::imgui::Imgui_renderer&                imgui_renderer,
    erhe::imgui::Imgui_windows&                 imgui_windows,
    erhe::rendergraph::Rendergraph&             rendergraph,
    const std::shared_ptr<Viewport_scene_view>& viewport_scene_view,
    std::string_view                            name,
    std::string_view                            ini_name
) -> std::shared_ptr<Imgui_window_scene_view_node>
{
    const auto& window_imgui_host = imgui_windows.get_window_imgui_host();

    auto node = std::make_shared<Imgui_window_scene_view_node>(
        imgui_renderer,
        imgui_windows,
        rendergraph,
        m_context,
        name, //fmt::format("Imgui_window_scene_view_node {}", m_imgui_window_scene_view_nodes.size()),
        ini_name,
        viewport_scene_view
    );
    m_imgui_window_scene_view_nodes.push_back(node);
    rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::viewport, viewport_scene_view->get_final_output(), node.get());
    rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::window, node.get(), window_imgui_host.get());
    return node;
}

auto Scene_views::open_new_viewport_scene_view(const std::shared_ptr<Scene_root>& scene_root) -> std::shared_ptr<Viewport_scene_view>
{
    const std::string name = fmt::format("Viewport_scene_view {}", m_viewport_scene_views.size());

    const int msaa_sample_count = m_context.editor_settings->graphics.current_graphics_preset.msaa_sample_count;
    if (scene_root) {
        for (const auto& item : m_context.selection->get_selection()) {
            const auto camera = std::dynamic_pointer_cast<erhe::scene::Camera>(item);
            if (camera) {
                return create_viewport_scene_view(
                    *m_context.graphics_instance,
                    *m_context.rendergraph,
                    *m_context.editor_rendering,
                    *m_context.editor_settings,
                    *m_context.post_processing,
                    *m_context.tools,
                    name,
                    scene_root,
                    camera,
                    msaa_sample_count
                );
            }
        }
        // Case for when no camera found in selection
        if (!scene_root->get_scene().get_cameras().empty()) {
            const auto& camera = scene_root->get_scene().get_cameras().front();
            return create_viewport_scene_view(
                *m_context.graphics_instance,
                *m_context.rendergraph,
                *m_context.editor_rendering,
                *m_context.editor_settings,
                *m_context.post_processing,
                *m_context.tools,
                name,
                scene_root,
                camera,
                msaa_sample_count
            );
        }
    }

    // Case for when no cameras found in scene
    return create_viewport_scene_view(
        *m_context.graphics_instance,
        *m_context.rendergraph,
        *m_context.editor_rendering,
        *m_context.editor_settings,
        *m_context.post_processing,
        *m_context.tools,
        name,
        {},
        nullptr,
        msaa_sample_count
    );
}

void Scene_views::open_new_viewport_scene_view_node()
{
    bool window_viewport{true};
    auto ini = erhe::configuration::get_ini("erhe.ini", "imgui");
    ini->get("window_viewport", window_viewport);

    auto viewport_scene_view = open_new_viewport_scene_view();
    if (window_viewport) {
        create_imgui_window_scene_view_node(
            *m_context.imgui_renderer,
            *m_context.imgui_windows,
            *m_context.rendergraph,
            viewport_scene_view,
            "w scene view",
            ""
        );
    } else {
        create_basic_viewport_scene_view_node(*m_context.rendergraph, viewport_scene_view, "b scene view");
    }
}

void Scene_views::debug_imgui()
{
    if (m_hover_scene_view) {
        const bool hovered = m_hover_scene_view->viewport_toolbar();
        static_cast<void>(hovered);
    }
    ImGui::TextUnformatted("Window hover stack:");
    for (auto& i : m_hover_stack) {
        auto window = i.lock();
        if (!window) {
            continue;
        }
        ImGui::BulletText("%s", window->get_name().c_str());
    }
    ImGui::Text("Hover scene view: %s", m_hover_scene_view ? m_hover_scene_view->get_name().c_str() : "");
    const auto last_scene_view = m_last_scene_view.lock();
    ImGui::Text("Last scene view: %s", last_scene_view ? last_scene_view->get_name().c_str() : "");

    if (ImGui::TreeNodeEx("Basic scene view nodes", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& node : m_basic_scene_view_nodes) {
            ImGui::BulletText("%s", node->get_name().c_str());
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("ImGui window scene view nodes", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& node : m_imgui_window_scene_view_nodes) {
            const std::shared_ptr<Viewport_scene_view> scene_view = node->viewport_scene_view();
            ImGui::BulletText("%s hosting %s", node->get_name().c_str(), scene_view ? scene_view->get_name().c_str() : "");
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Viewport scene views", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& viewport_scene_view : m_viewport_scene_views) {
            if (ImGui::TreeNodeEx(viewport_scene_view->get_name().c_str(), ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen)) {
                const std::shared_ptr<Scene_root> root = viewport_scene_view->get_scene_root();
                if (root) {
                    ImGui::Text("Scene: %s", root->get_name().c_str());
                }
                const std::shared_ptr<erhe::scene::Camera> camera = viewport_scene_view->get_camera();
                if (camera) {
                    ImGui::Text("Camera: %s", camera->get_name().c_str());
                }
                const erhe::math::Viewport& viewport = viewport_scene_view->window_viewport();
                ImGui::Text("Viewport: %d, %d, %d, %d", viewport.x, viewport.y, viewport.width, viewport.height);
                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
    }
}

void Scene_views::update_hover(erhe::imgui::Imgui_host* imgui_host)
{
    ERHE_PROFILE_FUNCTION();

    //SPDLOG_LOGGER_TRACE(log_scene_view, "Scene_views::update_hover(host = {})", imgui_host->get_name());

    ERHE_VERIFY(imgui_host != nullptr);

    std::shared_ptr<Viewport_scene_view> old_scene_view = m_hover_scene_view;
    m_hover_stack.clear();

    // Pull mouse position
    {
        //const auto mouse_position = m_context.input_state->mouse_position;
        const glm::vec2 mouse_position = imgui_host->get_mouse_position();
        if (mouse_position.x >= 0.0f && mouse_position.y >= 0.0f) {
            SPDLOG_LOGGER_TRACE(log_scene_view, "mouse_position: {}, {}", mouse_position.x, mouse_position.y);
        }
        for (auto& window : m_imgui_window_scene_view_nodes) {
            if (window->get_imgui_host() == imgui_host) {
                if (window->is_window_hovered()) {
                    window->on_mouse_move(mouse_position);
                }
            }
        }
    }

    update_hover_from_imgui_viewport_windows(imgui_host);

    if (!m_basic_scene_view_nodes.empty()) {
        layout_basic_viewport_windows();
        update_hover_from_basic_viewport_windows();
    }

    m_hover_scene_view = m_hover_stack.empty()
        ? std::shared_ptr<Viewport_scene_view>{}
        : m_hover_stack.back().lock();

    if (old_scene_view != m_hover_scene_view) {
        SPDLOG_LOGGER_TRACE(log_scene_view, "Changing hover scene view to: {}", m_hover_scene_view ? m_hover_scene_view->get_name().c_str() : "");
        m_context.editor_message_bus->send_message(
            Editor_message{
                .update_flags = Message_flag_bit::c_flag_bit_hover_viewport | Message_flag_bit::c_flag_bit_hover_scene_view,
                .scene_view   = m_hover_scene_view.get()
            }
        );
    } else {
        SPDLOG_LOGGER_TRACE(log_scene_view, "m_hover_scene_view {}", m_hover_scene_view ? m_hover_scene_view->get_name().c_str() : "");
    }
}

void Scene_views::update_hover_from_imgui_viewport_windows(erhe::imgui::Imgui_host* imgui_host)
{
    SPDLOG_LOGGER_TRACE(log_scene_view, "update_hover_from_imgui_viewport_windows({})", imgui_host->get_name());
    for (const auto& window : m_imgui_window_scene_view_nodes) {
        if (window->get_imgui_host() != imgui_host) {
            continue;
        }

        const auto viewport_scene_view = window->viewport_scene_view();
        if (!viewport_scene_view) {
            continue;
        }

        if (window->is_window_hovered()) {
            SPDLOG_LOGGER_TRACE(log_scene_view, "pushing {} to hover stack", viewport_scene_view->get_name());
            m_last_scene_view = viewport_scene_view;
            m_hover_stack.push_back(m_last_scene_view);
        }
    }
}

void Scene_views::update_hover_from_basic_viewport_windows()
{
    glm::vec2 pointer_window_position = m_context.input_state->mouse_position;

    m_hover_stack.clear();
    for (const auto& node : m_basic_scene_view_nodes) {
        const auto viewport_scene_view = node->get_viewport_scene_view();
        if (!viewport_scene_view) {
            continue;
        }

        const erhe::math::Viewport& viewport = node->get_viewport();
        const bool is_hoverered = viewport.hit_test(
            static_cast<int>(std::round(pointer_window_position.x)),
            static_cast<int>(std::round(pointer_window_position.y))
        );

        viewport_scene_view->set_is_scene_view_hovered(is_hoverered);

        if (is_hoverered) {
            const glm::vec2 viewport_position = viewport_scene_view->viewport_from_window(
                pointer_window_position
            );
            SPDLOG_LOGGER_TRACE(
                log_pointer,
                "window position {} hovers Viewport_scene_view {} @ {}",
                pointer_window_position,
                viewport_scene_view->get_name(),
                viewport_position
            );

            viewport_scene_view->update_pointer_2d_position(viewport_position);
            ERHE_VERIFY(m_hover_stack.empty());
            m_last_scene_view = viewport_scene_view;
            m_hover_stack.push_back(m_last_scene_view);
        } else {
            viewport_scene_view->reset_control_transform();
            viewport_scene_view->reset_hover_slots();
        }
    }
}

auto Scene_views::hover_scene_view() -> std::shared_ptr<Viewport_scene_view>
{
    if (m_hover_stack.empty()) {
        return {};
    }
    return m_hover_stack.back().lock();
}

auto Scene_views::last_scene_view() -> std::shared_ptr<Viewport_scene_view>
{
    return m_last_scene_view.lock();
}

void Scene_views::viewport_toolbar(Viewport_scene_view& viewport_scene_view, bool& hovered)
{
    ImGui::PushID("Scene_views::viewport_toolbar");
    const auto& rasterization = m_context.icon_set->get_small_rasterization();

    static constexpr std::string_view open_config{"open_config"};
    static constexpr uint32_t viewport_open_config_id{
        compiletime_xxhash::xxh32(open_config.data(), open_config.size(), {})
    };

    const bool button_pressed = rasterization.icon_button(ERHE_HASH("open_config"), m_context.icon_set->icons.three_dots);
    if (ImGui::IsItemHovered()) {
        hovered = true;
    }
    if (button_pressed) {
        m_context.viewport_config_window->show_window();
        m_context.viewport_config_window->set_edit_data(&viewport_scene_view.get_config());
    }
    ImGui::PopID();
}

} // namespace editor
