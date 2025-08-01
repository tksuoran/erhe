#pragma once

#include "renderers/viewport_config.hpp"

#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/camera.hpp"

#include <memory>
#include <mutex>

namespace erhe::imgui {
    class Imgui_host;
    class Imgui_windows;
    class Imgui_renderer;
}
namespace erhe::scene_renderer {
    class Shadow_renderer;
}

namespace editor {

class App_context;
class App_message;
class App_message_bus;
class App_rendering;
class App_settings;
class Graphics_preset;
class Post_processing;
class Post_processing_node;
class Scene_root;
class Scene_views;
class Tools;
class Viewport_config_window;
class Viewport_scene_view;
class Viewport_window;

class Open_new_viewport_scene_view_command : public erhe::commands::Command
{
public:
    Open_new_viewport_scene_view_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};


// Manages set of Viewport_scene_view instances
//
// All Viewport_scene_view instances should be created using Scene_views.
// Keeps track of the Viewport_scene_view that is currently under pointer
// (mouse cursor).
class Scene_views : erhe::commands::Command_host
{
public:
    Scene_views(erhe::commands::Commands& commands, App_context& context, App_message_bus& app_message_bus);

    // Public API

    auto create_viewport_scene_view(
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
        int                                                   msaa_sample_count,
        std::shared_ptr<erhe::rendergraph::Rendergraph_node>& out_rendergraph_output_node,
        bool                                                  enable_post_processing = true
    ) -> std::shared_ptr<Viewport_scene_view>;

    auto create_viewport_window(
        erhe::imgui::Imgui_renderer&                                imgui_renderer,
        erhe::imgui::Imgui_windows&                                 imgui_windows,
        const std::shared_ptr<Viewport_scene_view>&                 viewport_scene_view,
        const std::shared_ptr<erhe::rendergraph::Rendergraph_node>& rendergraph_output_node,
        std::string_view                                            name,
        std::string_view                                            ini_name
    ) -> std::shared_ptr<Viewport_window>;

    void open_new_viewport_scene_view_node();

    void erase(Viewport_scene_view* viewport_scene_view);

    auto open_new_viewport_scene_view(
        std::shared_ptr<erhe::rendergraph::Rendergraph_node>& out_rendergraph_output_node,
        const std::shared_ptr<Scene_root>&                    scene_root = {}
    ) -> std::shared_ptr<Viewport_scene_view>;

    void update_pointer(erhe::imgui::Imgui_host* imgui_host);

    void update_hover_info(erhe::imgui::Imgui_host* imgui_host);

    void update_transforms();

    void debug_imgui();

    // Returns the Viewport_scene_view instance which is currently under pointer
    // (mouse cursor).
    [[nodiscard]] auto hover_scene_view() -> std::shared_ptr<Viewport_scene_view>;
    [[nodiscard]] auto last_scene_view () -> std::shared_ptr<Viewport_scene_view>;

    void viewport_toolbar(Viewport_scene_view& viewport_scene_view, bool& hovered);

    [[nodiscard]] auto get_post_processing_nodes() const -> const std::vector<std::shared_ptr<Post_processing_node>>&;

private:
    void on_message                      (App_message& message);
    void handle_graphics_settings_changed(Graphics_preset* graphics_preset);

    void update_pointer_from_imgui_viewport_windows(erhe::imgui::Imgui_host* imgui_host);

    App_context& m_app_context;

    // Commands
    Open_new_viewport_scene_view_command m_open_new_viewport_scene_view_command;

    ERHE_PROFILE_MUTEX(std::mutex,                      m_mutex);
    std::vector<std::shared_ptr<Viewport_window>>       m_viewport_windows;
    std::vector<std::shared_ptr<Viewport_scene_view>>   m_viewport_scene_views;
    std::vector<std::shared_ptr<Post_processing_node>>  m_post_processing_nodes;
    std::vector<std::weak_ptr<Viewport_scene_view>>     m_hover_stack;
    std::shared_ptr<Viewport_scene_view>                m_hover_scene_view;
    std::weak_ptr<Viewport_scene_view>                  m_last_scene_view;
};

}
