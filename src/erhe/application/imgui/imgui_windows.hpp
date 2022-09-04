#pragma once

#include "erhe/components/components.hpp"

namespace erhe::application
{

class Commands;
class Imgui_renderer;
class Imgui_viewport;
class Imgui_window;
class Imgui_windows;
class Rendergraph;
class Window_imgui_viewport;

/// <summary>
/// Maintains collection of Imgui_windows and Imgui_viewports
/// </summary>
/// Maintains current Imgui_viewport that ha active ImGui context.
/// All Imgui_window instances must be registered to Imgui_windows.
/// Imgui_windows delegates input events to Imgui_viewport instances.
class Imgui_windows
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"erhe::application::Imgui_windows"};
    static constexpr std::string_view c_title{"ImGui Windows"};
    static constexpr uint32_t         c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Imgui_windows ();
    ~Imgui_windows() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Public API
    [[nodiscard]] auto get_mutex                         () -> std::mutex&;
    [[nodiscard]] auto get_window_viewport               () -> std::shared_ptr<Window_imgui_viewport>;
    [[nodiscard]] auto get_show_imgui_demo_window        () -> bool&;
    [[nodiscard]] auto get_show_imgui_style_editor_window() -> bool&;
    void register_imgui_viewport          (const std::shared_ptr<Imgui_viewport>& viewport);
    void register_imgui_window            (Imgui_window* window);
    void make_current                     (const Imgui_viewport* imgui_viewport);
    void imgui_windows                    ();
    void window_menu                      ();

    // NOTE: Same interface as Imgui_viewport
    [[nodiscard]] auto want_capture_mouse() const -> bool;

    void on_key         (signed int keycode, uint32_t modifier_mask, bool pressed);
    void on_char        (unsigned int codepoint);
    void on_focus       (int focused);
    void on_cursor_enter(int entered);
    void on_mouse_move  (double x, double y);
    void on_mouse_click (uint32_t button, int count);
    void on_mouse_wheel (double x, double y);

private:
    // Component dependencies
    std::shared_ptr<Commands>                    m_commands;
    std::shared_ptr<Imgui_renderer>              m_imgui_renderer;
    std::shared_ptr<Rendergraph>                 m_render_graph;

    std::mutex                                   m_mutex;
    std::vector<std::shared_ptr<Imgui_viewport>> m_imgui_viewports;
    std::vector<Imgui_window*>                   m_imgui_windows;
    const Imgui_viewport*                        m_current_viewport{nullptr}; // current context

    std::shared_ptr<Window_imgui_viewport>       m_window_imgui_viewport;
    bool                                         m_show_imgui_demo_window        {false};
    bool                                         m_show_imgui_style_editor_window{false};
};

} // namespace erhe::application
