#pragma once

#include "windows/viewport_config.hpp"
#include "renderers/programs.hpp"

#include "erhe/application/render_graph_node.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/toolkit/view.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::application
{
    class Configuration;
    class Imgui_viewport;
    class Imgui_windows;
    class Render_graph;
    class View;
}

namespace erhe::graphics
{
    class Framebuffer;
    class Texture;
    class Renderbuffer;
    class OpenGL_state_tracker;
}

namespace editor
{

class Editor_rendering;
class Id_renderer;
class Post_processing;
class Render_context;
class Scene_root;
#if defined(ERHE_XR_LIBRARY_OPENXR)
class Headset_renderer;
#endif
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

class Viewport_windows
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Viewport_windows"};
    static constexpr std::string_view c_title{"View"};
    static constexpr uint32_t hash{
        compiletime_xxhash::xxh32(
            c_label.data(),
            c_label.size(),
            {}
        )
    };

    Viewport_windows ();
    ~Viewport_windows() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Public API
    auto create_window(
        const std::string_view             name,
        const std::shared_ptr<Scene_root>& scene_root,
        erhe::scene::Camera*               camera
    ) -> std::shared_ptr<Viewport_window>;

    auto open_new_viewport_window(
        const std::shared_ptr<Scene_root>& scene_root = {}
    ) -> bool;

    //void update_hover();
    void reset_hover ();
    void update_hover(erhe::application::Imgui_viewport* imgui_viewport);

    auto hover_window() -> Viewport_window*;
    auto last_window () -> Viewport_window*;

    // Pointer_context(s) API
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
    // Component dependencies
    std::shared_ptr<erhe::application::Configuration>     m_configuration;
    std::shared_ptr<erhe::application::Imgui_windows>     m_imgui_windows;
    std::shared_ptr<erhe::application::Render_graph>      m_render_graph;
    std::shared_ptr<erhe::application::View>              m_editor_view;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Editor_rendering>                     m_editor_rendering;
    std::shared_ptr<Id_renderer>                          m_id_renderer;
    std::shared_ptr<Scene_root>                           m_scene_root;
    std::shared_ptr<Viewport_config>                      m_viewport_config;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    std::shared_ptr<Headset_renderer>                     m_headset_renderer;
#endif

    // Commands
    Open_new_viewport_window_command              m_open_new_viewport_window_command;
    std::vector<std::shared_ptr<Viewport_window>> m_windows;
    std::vector<Viewport_window*>                 m_hover_stack;
    Viewport_window*                              m_last_window {nullptr};

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
