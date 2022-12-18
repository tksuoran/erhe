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
    class Configuration;
    class Imgui_viewport;
    class Imgui_windows;
    class Rendergraph;
    class View;
    class Window;
}

namespace erhe::graphics
{
    class OpenGL_state_tracker;
}

namespace editor
{

class Basic_viewport_window;
class Editor_message;
class Editor_message_bus;
class Editor_rendering;
class Id_renderer;
class Imgui_viewport_window;
class Post_processing;
class Scene_root;
class Shadow_renderer;
class Tools;
class Viewport_window;

class Viewport_windows;

class Open_new_viewport_window_command
    : public erhe::application::Command
{
public:
    explicit Open_new_viewport_window_command(Viewport_windows& viewport_windows)
        : Command           {"Viewport_windows.open_new_viewport_window"}
        , m_viewport_windows{viewport_windows}
    {
    }

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Viewport_windows& m_viewport_windows;
};

/// <summary>
/// Manages set of Viewport_window instances
/// </summary>
/// All Viewport_window instances should be created using Viewport_windows.
/// Keeps track of the Viewport_window that is currently under pointer
/// (mouse cursor).
class Viewport_windows
    : public erhe::components::Component
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
    void post_initialize            () override;

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

    void update_keyboard(
        bool                   pressed,
        erhe::toolkit::Keycode code,
        uint32_t               modifier_mask
    );
    void update_mouse(erhe::toolkit::Mouse_button button, int count);
    void update_mouse(double x, double y);

    [[nodiscard]] auto shift_key_down       () const -> bool;
    [[nodiscard]] auto control_key_down     () const -> bool;
    [[nodiscard]] auto alt_key_down         () const -> bool;
    [[nodiscard]] auto mouse_button_pressed (erhe::toolkit::Mouse_button button) const -> bool;
    [[nodiscard]] auto mouse_button_released(erhe::toolkit::Mouse_button button) const -> bool;
    [[nodiscard]] auto mouse_x              () const -> double;
    [[nodiscard]] auto mouse_y              () const -> double;

private:
    void on_message                      (Editor_message& message);
    void handle_graphics_settings_changed();

    void update_hover_from_imgui_viewport_windows(erhe::application::Imgui_viewport* imgui_viewport);
    void update_hover_from_basic_viewport_windows();
    void layout_basic_viewport_windows           ();

    // Component dependencies
    std::shared_ptr<erhe::application::Configuration>     m_configuration;
    std::shared_ptr<erhe::application::Imgui_windows>     m_imgui_windows;
    std::shared_ptr<erhe::application::Rendergraph>       m_rendergraph;
    std::shared_ptr<erhe::application::Window>            m_window;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Editor_message_bus>                   m_editor_message_bus;
    std::shared_ptr<Editor_rendering>                     m_editor_rendering;
    std::shared_ptr<Id_renderer>                          m_id_renderer;
    std::shared_ptr<Post_processing>                      m_post_processing;
    std::shared_ptr<Scene_root>                           m_scene_root;
    std::shared_ptr<Shadow_renderer>                      m_shadow_renderer;
    std::shared_ptr<Tools>                                m_tools;
    std::shared_ptr<Viewport_config>                      m_viewport_config;

    // Commands
    Open_new_viewport_window_command m_open_new_viewport_window_command;

    std::mutex                                          m_mutex;
    std::vector<std::shared_ptr<Basic_viewport_window>> m_basic_viewport_windows;
    std::vector<std::shared_ptr<Imgui_viewport_window>> m_imgui_viewport_windows;
    std::vector<std::shared_ptr<Viewport_window>>       m_viewport_windows;
    std::vector<std::weak_ptr<Viewport_window>>         m_hover_stack;
    std::weak_ptr<Viewport_window>                      m_last_window;

    class Mouse_button
    {
    public:
        bool pressed {false};
        bool released{false};
    };

    Mouse_button m_mouse_button[static_cast<int>(erhe::toolkit::Mouse_button_count)];
    double       m_mouse_x{0.0f};
    double       m_mouse_y{0.0f};
    bool         m_shift  {false};
    bool         m_control{false};
    bool         m_alt    {false};
};

} // namespace editor
