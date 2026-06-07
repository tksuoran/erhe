// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "scene/viewport_scene_views.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_rendering.hpp"
#include "app_settings.hpp"
#include "erhe_graphics/device.hpp"
#include "input_state.hpp"
#include "graphics/icon_set.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "rendergraph/post_processing.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "windows/viewport_config_window.hpp"
#include "windows/viewport_window.hpp"

#include "erhe_utility/bit_helpers.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_hash/xxhash.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_host.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/window_imgui_host.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>

#include <cfloat>

namespace editor{

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;
using erhe::graphics::Render_pass;
using erhe::graphics::Renderbuffer;
using erhe::graphics::Texture;

#pragma region Commands
Open_new_viewport_scene_view_command::Open_new_viewport_scene_view_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Scene_views.open_new_viewport_scene_view"}
    , m_context{context}
{
}

auto Open_new_viewport_scene_view_command::try_call() -> bool
{
    m_context.scene_views->open_new_viewport_scene_view_node();
    return true;
}
#pragma endregion Commands

Scene_views::Scene_views(
    const Viewport_config_data& viewport_config_data,
    erhe::commands::Commands&   commands,
    App_context&                app_context,
    App_message_bus&            app_message_bus
)
    : m_app_context                         {app_context}
    , m_viewport_config_data                {viewport_config_data}
    , m_open_new_viewport_scene_view_command{commands, app_context}
{
    ERHE_PROFILE_FUNCTION();

    Command_host::set_description("Scene_views");

    commands.register_command   (&m_open_new_viewport_scene_view_command);
    commands.bind_command_to_key(&m_open_new_viewport_scene_view_command, erhe::window::Key_f1, true);

    m_graphics_settings_subscription = app_message_bus.graphics_settings.subscribe(
        [&](Graphics_settings_message& message) {
            handle_graphics_settings_changed(message.graphics_preset);
        }
    );

    m_open_new_viewport_scene_view_command.set_host(this);
}

Scene_views::~Scene_views() noexcept
{
    // Reset all references to Viewport_scene_view before destroying vectors.
    m_hover_scene_view.reset();
    m_last_scene_view.reset();
    m_pointer_capture_scene_view.reset();
    m_hover_stack.clear();

    // ~Viewport_scene_view calls erase() which modifies m_viewport_scene_views.
    // Swap to locals so the member is empty before any element destructors run;
    // erase() on the empty member is a no-op. Using clear() would corrupt the
    // vector because element destructors modify it mid-iteration.
    {
        std::vector<std::shared_ptr<Viewport_scene_view>> views;
        views.swap(m_viewport_scene_views);
    }
    {
        std::vector<std::shared_ptr<Viewport_window>> windows;
        windows.swap(m_viewport_windows);
    }
    m_post_processing_nodes.clear();
}


void Scene_views::handle_graphics_settings_changed(Graphics_preset_entry* graphics_preset)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    const int msaa_sample_count = (graphics_preset != nullptr) ? graphics_preset->msaa_sample_count : 0;
    // Reverse-Z is static (Device::get_reverse_depth()); the render target reads
    // it directly when (re)building its render pass, so only MSAA is applied here.
    for (const std::shared_ptr<Viewport_scene_view>& viewport_scene_view : m_viewport_scene_views) {
        viewport_scene_view->get_render_target().reconfigure(msaa_sample_count);
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

void Scene_views::destroy_viewport_scene_view(
    const std::shared_ptr<Viewport_scene_view>&  viewport_scene_view,
    const std::shared_ptr<Post_processing_node>& post_processing_node
)
{
    if (!viewport_scene_view) {
        return;
    }
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    // Drop any cached references that point at this viewport. The
    // ~Viewport_scene_view dtor only removes from m_viewport_scene_views;
    // hover bookkeeping is our responsibility.
    if (m_hover_scene_view == viewport_scene_view) {
        m_hover_scene_view.reset();
    }
    if (m_last_scene_view.lock() == viewport_scene_view) {
        m_last_scene_view.reset();
    }
    if (m_pointer_capture_scene_view.lock() == viewport_scene_view) {
        m_pointer_capture_scene_view.reset();
    }
    const auto hover_end = std::remove_if(
        m_hover_stack.begin(),
        m_hover_stack.end(),
        [&viewport_scene_view](const std::weak_ptr<Viewport_scene_view>& entry) {
            return entry.lock() == viewport_scene_view;
        }
    );
    m_hover_stack.erase(hover_end, m_hover_stack.end());

    if (post_processing_node) {
        const auto pp = std::find(m_post_processing_nodes.begin(), m_post_processing_nodes.end(), post_processing_node);
        if (pp != m_post_processing_nodes.end()) {
            m_post_processing_nodes.erase(pp);
        }
    }

    // Erase from m_viewport_scene_views explicitly so the only remaining
    // strong reference is the caller's. When the caller releases it, the
    // Viewport_scene_view dtor's erase(this) call becomes a no-op (the
    // entry is already gone), then the Rendergraph_node dtor unregisters
    // the node and disconnects its links.
    const auto vsv = std::find(m_viewport_scene_views.begin(), m_viewport_scene_views.end(), viewport_scene_view);
    if (vsv != m_viewport_scene_views.end()) {
        m_viewport_scene_views.erase(vsv);
    }
}

void Scene_views::destroy_viewport_window(const std::shared_ptr<Viewport_window>& viewport_window)
{
    if (!viewport_window) {
        return;
    }
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const auto i = std::find(m_viewport_windows.begin(), m_viewport_windows.end(), viewport_window);
    if (i != m_viewport_windows.end()) {
        m_viewport_windows.erase(i);
    }
}

auto Scene_views::create_viewport_scene_view(
    const Viewport_config_data&                           viewport_config_data,
    erhe::graphics::Device&                               graphics_device,
    erhe::rendergraph::Rendergraph&                       rendergraph,
    erhe::imgui::Imgui_windows&                           imgui_windows,
    App_rendering&                                        app_rendering,
    App_settings&                                         app_settings,
    Post_processing&                                      post_processing,
    Tools&                                                tools,
    const std::string_view                                name,
    const std::shared_ptr<Scene_root>&                    scene_root,
    const std::shared_ptr<erhe::scene::Camera>&           camera,
    const int                                             msaa_sample_count,
    std::shared_ptr<erhe::rendergraph::Rendergraph_node>& out_rendergraph_output_node,
    bool                                                  enable_post_processing
) -> std::shared_ptr<Viewport_scene_view>
{
    const auto new_viewport = std::make_shared<Viewport_scene_view>(
        &app_settings.settings_store(),
        viewport_config_data,
        m_app_context,
        rendergraph,
        tools,
        name,
        nullptr, // no ini for viewport windows for now
        scene_root,
        camera,
        msaa_sample_count
    );

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    {
        m_viewport_scene_views.push_back(new_viewport);
    }

    if (app_settings.graphics.current_graphics_preset.shadow_enable) {
        const auto shadow_render_node = app_rendering.create_shadow_node_for_scene_view(
            graphics_device,
            rendergraph,
            app_settings,
            *new_viewport.get()
        );
        rendergraph.connect(
            erhe::rendergraph::Rendergraph_node_key::shadow_maps,
            shadow_render_node.get(),
            new_viewport.get()
        );
    }

    std::shared_ptr<erhe::rendergraph::Rendergraph_node> viewport_producer;
    if (enable_post_processing) {
        auto post_processing_node = std::make_shared<Post_processing_node>(
            graphics_device,
            rendergraph,
            post_processing,
            erhe::utility::Debug_label{fmt::format("Post processing for {}", name)}
        );
        out_rendergraph_output_node = post_processing_node;
        m_post_processing_nodes.push_back(post_processing_node);
        rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::viewport_texture, new_viewport.get(), post_processing_node.get());
    } else {
        out_rendergraph_output_node = new_viewport;
    }

    rendergraph.connect(
        erhe::rendergraph::Rendergraph_node_key::viewport_texture,
        out_rendergraph_output_node.get(),
        imgui_windows.get_window_imgui_host().get()
    );

    return new_viewport;
}

auto Scene_views::create_viewport_window(
    erhe::imgui::Imgui_renderer&                                imgui_renderer,
    erhe::imgui::Imgui_windows&                                 imgui_windows,
    App_message_bus&                                            app_message_bus,
    const std::shared_ptr<Viewport_scene_view>&                 viewport_scene_view,
    const std::shared_ptr<erhe::rendergraph::Rendergraph_node>& rendergraph_output_node,
    std::string_view                                            name,
    std::string_view                                            ini_name_in
) -> std::shared_ptr<Viewport_window>
{
    std::string window_name = fmt::format("{}##{}", name, m_viewport_windows.size());
    std::string ini_name = ini_name_in.empty() ? std::string{} : fmt::format("{}##{}", ini_name, m_viewport_windows.size());
    auto viewport_window = std::make_shared<Viewport_window>(
        imgui_renderer,
        imgui_windows,
        rendergraph_output_node,
        m_app_context,
        app_message_bus,
        window_name,
        ini_name,
        viewport_scene_view
    );
    const auto& window_imgui_host = imgui_windows.get_window_imgui_host();
    viewport_window->set_imgui_host(window_imgui_host.get());
    m_viewport_windows.push_back(viewport_window);
    return viewport_window;
}

auto Scene_views::open_new_viewport_scene_view(
    std::shared_ptr<erhe::rendergraph::Rendergraph_node>& out_rendergraph_output_node,
    const std::shared_ptr<Scene_root>&                    scene_root
) -> std::shared_ptr<Viewport_scene_view>
{
    const std::string name = fmt::format("Viewport_scene_view {}", m_viewport_scene_views.size());

    const int msaa_sample_count = m_app_context.app_settings->graphics.current_graphics_preset.msaa_sample_count;
    if (scene_root) {
        const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = m_app_context.selection->get_selected_items();
        for (const auto& item : selected_items) {
            const auto camera = std::dynamic_pointer_cast<erhe::scene::Camera>(item);
            if (camera) {
                return create_viewport_scene_view(
                    m_viewport_config_data,
                    *m_app_context.graphics_device,
                    *m_app_context.rendergraph,
                    *m_app_context.imgui_windows,
                    *m_app_context.app_rendering,
                    *m_app_context.app_settings,
                    *m_app_context.post_processing,
                    *m_app_context.tools,
                    name,
                    scene_root,
                    camera,
                    msaa_sample_count,
                    out_rendergraph_output_node
                );
            }
        }
        // Case for when no camera found in selection
        if (!scene_root->get_scene().get_cameras().empty()) {
            const auto& camera = scene_root->get_scene().get_cameras().front();
            return create_viewport_scene_view(
                m_viewport_config_data,
                *m_app_context.graphics_device,
                *m_app_context.rendergraph,
                *m_app_context.imgui_windows,
                *m_app_context.app_rendering,
                *m_app_context.app_settings,
                *m_app_context.post_processing,
                *m_app_context.tools,
                name,
                scene_root,
                camera,
                msaa_sample_count,
                out_rendergraph_output_node
            );
        }
    }

    // Case for when no cameras found in scene
    return create_viewport_scene_view(
        m_viewport_config_data,
        *m_app_context.graphics_device,
        *m_app_context.rendergraph,
        *m_app_context.imgui_windows,
        *m_app_context.app_rendering,
        *m_app_context.app_settings,
        *m_app_context.post_processing,
        *m_app_context.tools,
        name,
        {},
        nullptr,
        msaa_sample_count,
        out_rendergraph_output_node
    );
}

void Scene_views::open_new_viewport_scene_view_node()
{
    std::shared_ptr<erhe::rendergraph::Rendergraph_node> rendergraph_output_node{};
    auto viewport_scene_view = open_new_viewport_scene_view(rendergraph_output_node);
    create_viewport_window(
        *m_app_context.imgui_renderer,
        *m_app_context.imgui_windows,
        *m_app_context.app_message_bus,
        viewport_scene_view,
        rendergraph_output_node,
        "scene view",
        ""
    );
}

void Scene_views::debug_imgui()
{
    if (m_hover_scene_view) {
        m_hover_scene_view->viewport_toolbar();
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

    if (ImGui::TreeNodeEx("ImGui window scene view nodes", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const std::shared_ptr<Viewport_window>& viewport_window : m_viewport_windows) {
            const std::shared_ptr<Viewport_scene_view> scene_view = viewport_window->viewport_scene_view();
            ImGui::BulletText(
                "%s hosting %s",
                viewport_window->get_title().c_str(),
                scene_view ? scene_view->get_name().c_str() : ""
            );
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
                const erhe::math::Viewport& viewport = viewport_scene_view->get_window_viewport();
                ImGui::Text("Viewport: %d, %d, %d, %d", viewport.x, viewport.y, viewport.width, viewport.height);
                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
    }
}

void Scene_views::update_pointer(erhe::imgui::Imgui_host* imgui_host)
{
    ERHE_PROFILE_FUNCTION();

    // log_scene_view->info("Scene_views::update_pointer(host = {})", imgui_host->get_name());

    ERHE_VERIFY(imgui_host != nullptr);

    std::shared_ptr<Viewport_scene_view> old_scene_view = m_hover_scene_view;
    m_hover_stack.clear();

    const glm::vec2 mouse_position = imgui_host->get_mouse_position();

    // While a mouse-drag command is active, the viewport that started the drag keeps the
    // pointer even after the cursor leaves its rect (e.g. moves over another panel), so the
    // drag is never interrupted and keeps tracking the cursor until the button is released.
    const bool mouse_drag_active =
        (m_app_context.commands != nullptr) &&
        (m_app_context.commands->get_active_mouse_command() != nullptr);

    std::shared_ptr<Viewport_scene_view> capture_target;
    if (mouse_drag_active) {
        capture_target = m_pointer_capture_scene_view.lock();
        if (!capture_target) {
            // Drag just became active: lock onto the viewport that was hovered at drag start.
            // m_last_scene_view is null-guarded - if no viewport was hovered we do not capture.
            capture_target = m_last_scene_view.lock();
            m_pointer_capture_scene_view = capture_target;
        }
    } else {
        m_pointer_capture_scene_view.reset();
    }

    if (capture_target) {
        // Pointer-capture path: route only to the capturing viewport, bypassing
        // is_viewport_hovered(). Skip the normal hover scan so a second viewport cannot steal
        // the pointer mid-drag. io.MousePos is the (-FLT_MAX, -FLT_MAX) sentinel only while the
        // cursor is outside the OS window (see Imgui_host::on_cursor_enter_event); feed any real
        // in-window position so the gizmo keeps tracking over other panels, and skip the
        // sentinel so it freezes (rather than jumping) until the cursor returns. Do NOT gate on
        // Imgui_host::has_cursor() here - it is driven only by cursor-enter events, which are
        // not reliably delivered, so it can read false while the cursor is genuinely inside the
        // window (which would wrongly freeze the drag).
        const bool mouse_position_valid = (mouse_position.x > -FLT_MAX) && (mouse_position.y > -FLT_MAX);
        for (const std::shared_ptr<Viewport_window>& window : m_viewport_windows) {
            if ((window->get_imgui_host() == imgui_host) && (window->viewport_scene_view() == capture_target)) {
                if (mouse_position_valid) {
                    window->on_mouse_move(mouse_position);
                }
                m_hover_stack.push_back(capture_target);
                break;
            }
        }
    } else {
        // Normal hover path.
        for (const std::shared_ptr<Viewport_window>& window : m_viewport_windows) {
            if (window->get_imgui_host() == imgui_host) {
                if (window->is_viewport_hovered()) {
                    window->on_mouse_move(mouse_position);
                }
            }
        }
        update_pointer_from_imgui_viewport_windows(imgui_host);
    }

    m_hover_scene_view = m_hover_stack.empty()
        ? std::shared_ptr<Viewport_scene_view>{}
        : m_hover_stack.back().lock();

    if (old_scene_view != m_hover_scene_view) {
        // log_scene_view->info("Changing hover scene view to: {}", m_hover_scene_view ? m_hover_scene_view->get_name().c_str() : "");
        m_app_context.app_message_bus->hover_scene_view.send_message(
            Hover_scene_view_message{
                .scene_view = m_hover_scene_view.get()
            }
        );
    } else {
        // log_scene_view->info("m_hover_scene_view {}", m_hover_scene_view ? m_hover_scene_view->get_name().c_str() : "");
    }
}

void Scene_views::update_hover_info(erhe::imgui::Imgui_host* imgui_host)
{
    ERHE_PROFILE_FUNCTION();

    //SPDLOG_LOGGER_TRACE(log_scene_view, "Scene_views::update_hover(host = {})", imgui_host->get_name());

    ERHE_VERIFY(imgui_host != nullptr);

    for (auto& window : m_viewport_windows) {
        if (window->get_imgui_host() == imgui_host) {
            if (window->is_viewport_hovered()) {
                window->update_hover_info();
            }
        }
    }
}

void Scene_views::update_pointer_from_imgui_viewport_windows(erhe::imgui::Imgui_host* imgui_host)
{
    // log_scene_view->info("update_pointer_from_imgui_viewport_windows({})", imgui_host->get_name());
    for (const auto& window : m_viewport_windows) {
        if (window->get_imgui_host() != imgui_host) {
            continue;
        }

        const auto viewport_scene_view = window->viewport_scene_view();
        if (!viewport_scene_view) {
            continue;
        }

        if (window->is_viewport_hovered()) {
            // log_scene_view->info("pushing {} to hover stack", viewport_scene_view->get_name());
            m_last_scene_view = viewport_scene_view;
            m_hover_stack.push_back(m_last_scene_view);
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

auto Scene_views::owns_pointer_capture(const Viewport_scene_view* scene_view) const -> bool
{
    if (scene_view == nullptr) {
        return false;
    }
    // m_pointer_capture_scene_view is non-empty only while a mouse-drag is in progress.
    return m_pointer_capture_scene_view.lock().get() == scene_view;
}

auto Scene_views::get_post_processing_nodes() const -> const std::vector<std::shared_ptr<Post_processing_node>>&
{
    return m_post_processing_nodes;
}

void Scene_views::update_transforms()
{
    for (auto& view : m_viewport_scene_views) {
        view->update_transforms();
    }
}

}
