#pragma once

#include "erhe/components/components.hpp"

namespace erhe::application
{

class Imgui_renderer;
class Imgui_viewport;
class Imgui_window;
class View;
class Window_imgui_viewport;

class Imgui_windows
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"erhe::application::Imgui_windows"};
    static constexpr std::string_view c_title{"ImGui Windows"};
    static constexpr uint32_t         hash{
        compiletime_xxhash::xxh32(
            c_label.data(),
            c_label.size(),
            {}
        )
    };

    Imgui_windows ();
    ~Imgui_windows() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Public API
    void register_imgui_window(Imgui_window* window);
    void imgui_windows        ();
    void render_imgui_frame   ();

    //[[nodiscard]] auto want_capture_mouse() const -> bool;

    // NOTE: Same interface as Imgui_viewport

    //void begin_imgui_frame ();
    //void end_imgui_frame   ();

    [[nodiscard]] auto want_capture_mouse() const -> bool;

    void on_key         (const signed int keycode, const uint32_t modifier_mask, const bool pressed);
    void on_char        (const unsigned int codepoint);
    void on_focus       (int focused);
    void on_cursor_enter(int entered);
    void on_mouse_click (const uint32_t button, const int count);
    void on_mouse_wheel (const double x, const double y);

private:
    // Component dependencies
    std::shared_ptr<View>                        m_view;
    std::shared_ptr<Imgui_renderer>              m_imgui_renderer;

    std::mutex                                   m_mutex;
    std::vector<std::shared_ptr<Imgui_viewport>> m_imgui_viewports;
    std::vector<Imgui_window*>                   m_imgui_windows;
    Imgui_viewport*                              m_current_viewport{nullptr};
    std::shared_ptr<Window_imgui_viewport>       m_window_imgui_viewport;
    bool                                         m_show_style_editor{false};
};

} // namespace erhe::application
