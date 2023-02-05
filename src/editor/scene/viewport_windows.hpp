#pragma once

#include "editor_message.hpp"
#include "windows/viewport_config.hpp"

#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/message_bus/message_bus.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/toolkit/view.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <mutex>

namespace erhe::application
{
    class Imgui_viewport;
    class View;
}

namespace editor
{

class Basic_viewport_window;
class Editor_message;
class Imgui_viewport_window;
class Scene_root;
class Viewport_window;
class Viewport_windows;

class Open_new_viewport_window_command
    : public erhe::application::Command
{
public:
    Open_new_viewport_window_command();
    auto try_call() -> bool override;
};

/// <summary>
/// Manages set of Viewport_window instances
/// </summary>
/// All Viewport_window instances should be created using Viewport_windows.
/// Keeps track of the Viewport_window that is currently under pointer
/// (mouse cursor).
class Viewport_windows
    : public erhe::components::Component
    , erhe::application::Command_host
{
public:
    static constexpr std::string_view c_type_name{"Viewport_windows"};
    static constexpr std::string_view c_title{"View"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Viewport_windows ();
    ~Viewport_windows() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

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
        const std::string_view                      name,
        const std::shared_ptr<Scene_root>&          scene_root,
        const std::shared_ptr<erhe::scene::Camera>& camera,
        int                                         msaa_sample_count,
        bool                                        enable_post_processing = true
    ) -> std::shared_ptr<Viewport_window>;

    auto create_basic_viewport_window(
        const std::shared_ptr<Viewport_window>& viewport_window
    ) -> std::shared_ptr<Basic_viewport_window>;

    auto create_imgui_viewport_window(
        const std::shared_ptr<Viewport_window>& viewport_window
    ) -> std::shared_ptr<Imgui_viewport_window>;

    void erase(Viewport_window* viewport_window);
    void erase(Basic_viewport_window* basic_viewport_window);

    auto open_new_viewport_window(
        const std::shared_ptr<Scene_root>& scene_root = {}
    ) -> std::shared_ptr<Viewport_window>;

    void open_new_imgui_viewport_window();

    //void update_hover();
    void reset_hover ();
    void update_hover(erhe::application::Imgui_viewport* imgui_viewport);

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

    void update_hover_from_imgui_viewport_windows(erhe::application::Imgui_viewport* imgui_viewport);
    void update_hover_from_basic_viewport_windows();
    void layout_basic_viewport_windows           ();

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

extern Viewport_windows* g_viewport_windows;

} // namespace editor
