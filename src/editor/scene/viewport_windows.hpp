#pragma once

#include "editor_message.hpp"
#include "renderers/viewport_config.hpp"

#include "erhe/rendergraph/rendergraph_node.hpp"
#include "erhe/commands/command.hpp"
#include "erhe/imgui/imgui_window.hpp"
#include "erhe/message_bus/message_bus.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/toolkit/viewport.hpp"
#include "erhe/toolkit/window_event_handler.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <mutex>

namespace erhe::imgui {
    class Imgui_viewport;
    class Imgui_windows;
}
namespace erhe::scene_renderer {
    class Shadow_renderer;
}

namespace editor
{

class Basic_viewport_window;
class Editor_context;
class Editor_message;
class Editor_message_bus;
class Editor_rendering;
class Imgui_viewport_window;
class Post_processing;
class Scene_root;
class Tools;
class Viewport_config_window;
class Viewport_window;
class Viewport_windows;

class Open_new_viewport_window_command
    : public erhe::commands::Command
{
public:
    Open_new_viewport_window_command(
        erhe::commands::Commands& commands,
        Editor_context&           editor_context
    );
    auto try_call() -> bool override;

private:
    Editor_context& m_context;
};


// Manages set of Viewport_window instances
//
// All Viewport_window instances should be created using Viewport_windows.
// Keeps track of the Viewport_window that is currently under pointer
// (mouse cursor).
class Viewport_windows
    : erhe::commands::Command_host
{
public:
    Viewport_windows(
        erhe::commands::Commands& commands,
        Editor_context&           editor_context,
        Editor_message_bus&       editor_message_bus
    );

    // Public API

    /// <summary>
    /// Creates a new Viewport_window and necessary rendergraph nodes.
    /// </summary>
    /// Creates a new Viewport_window and necessary rendergraph nodes.
    /// The following rendergraph nodes are created:
    /// - Viewport_window - Used as producer node
    /// - Multisample_resolve_node - Consumes Viewport_window output and performs multisample resolve
    /// - Post_processing_node - Consumes Multisample_resolve_node output and performs post processing (bloom and tonemap)
    /// - Imgui_viewport_window - Consumes Post_processing_node output, shows content using ImGui
    /// <param name="name">Name for the new Viewport_window</param>
    /// <param name="scene_root">Scene to be shown</param>
    /// <param name="camera">Initial camera to be used</param>
    /// <returns>The newly created Viewport_window</returns>
    auto create_viewport_window(
        erhe::graphics::Instance&                   graphics_instance,
        erhe::rendergraph::Rendergraph&             rendergraph,
        erhe::scene_renderer::Shadow_renderer&      shadow_renderer,
        Editor_rendering&                           editor_rendering,
        Tools&                                      tools,
        Viewport_config_window&                     viewport_config_window,

        const std::string_view                      name,
        const std::shared_ptr<Scene_root>&          scene_root,
        const std::shared_ptr<erhe::scene::Camera>& camera,
        int                                         msaa_sample_count,
        bool                                        enable_post_processing = true
    ) -> std::shared_ptr<Viewport_window>;

    auto create_basic_viewport_window(
        erhe::rendergraph::Rendergraph&         rendergraph,
        const std::shared_ptr<Viewport_window>& viewport_window
    ) -> std::shared_ptr<Basic_viewport_window>;

    auto create_imgui_viewport_window(
        erhe::imgui::Imgui_renderer&            imgui_renderer,
        erhe::imgui::Imgui_windows&             imgui_windows,
        erhe::rendergraph::Rendergraph&         rendergraph,
        const std::shared_ptr<Viewport_window>& viewport_window
    ) -> std::shared_ptr<Imgui_viewport_window>;

    void erase(Viewport_window* viewport_window);
    void erase(Basic_viewport_window* basic_viewport_window);

    auto open_new_viewport_window(
        const std::shared_ptr<Scene_root>& scene_root = {}
    ) -> std::shared_ptr<Viewport_window>;

    void open_new_imgui_viewport_window();

    //void update_hover();
    void update_hover(erhe::imgui::Imgui_viewport* imgui_viewport);

    void debug_imgui();

    /// <summary>
    /// Returns the Viewport_window instance which is currently under pointer
    /// (mouse cursor).
    /// </summary>
    /// <returns>Pointer to the Viewport_window instance which is currently
    /// under pointer (mouse cursor), or nullptr if pointer (mouse cursor)
    /// is not currently over any Viewport_window</returns>
    auto hover_window() -> std::shared_ptr<Viewport_window>;
    auto last_window () -> std::shared_ptr<Viewport_window>;

    void viewport_toolbar(Viewport_window& viewport_window, bool& hovered);

private:
    void on_message                      (Editor_message& message);
    void handle_graphics_settings_changed();

    void update_hover_from_imgui_viewport_windows(erhe::imgui::Imgui_viewport* imgui_viewport);
    void update_hover_from_basic_viewport_windows();
    void layout_basic_viewport_windows           ();

    Editor_context& m_context;

    // Commands
    Open_new_viewport_window_command m_open_new_viewport_window_command;

    std::mutex                                          m_mutex;
    std::vector<std::shared_ptr<Basic_viewport_window>> m_basic_viewport_windows;
    std::vector<std::shared_ptr<Imgui_viewport_window>> m_imgui_viewport_windows;
    std::vector<std::shared_ptr<Viewport_window>>       m_viewport_windows;
    std::vector<std::weak_ptr<Viewport_window>>         m_hover_stack;
    std::shared_ptr<Viewport_window>                    m_hover_window;
    std::weak_ptr<Viewport_window>                      m_last_window;
};

} // namespace editor
