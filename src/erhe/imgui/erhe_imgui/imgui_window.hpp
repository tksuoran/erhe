#pragma once

#include "erhe_imgui/imgui_renderer.hpp"

#include <string>
#include <string_view>

typedef int ImGuiWindowFlags;

namespace erhe::graphics { class Texture; }

namespace erhe::imgui {

class Imgui_renderer;
class Imgui_host;
class Imgui_windows;

// Wrapper for ImGui window.
//
// Each Imgui_window must be hosted in exactly one Imgui_host.
class Imgui_window
{
public:
    Imgui_window(
        Imgui_renderer&  imgui_renderer,
        Imgui_windows&   imgui_windows,
        std::string_view title,
        std::string_view ini_label,
        bool             developer = false
    );
    virtual ~Imgui_window() noexcept;

    [[nodiscard]] auto is_window_visible     () const -> bool;
    [[nodiscard]] auto get_ini_label         () const -> const std::string&;
    [[nodiscard]] auto get_title             () const -> const std::string&;
    [[nodiscard]] auto get_scale_value       () const -> float;
    [[nodiscard]] auto show_in_menu          () const -> bool;
    [[nodiscard]] auto show_in_developer_menu() const -> bool;
    void set_developer           ();
    void set_min_size            (float min_width, float min_height);
    void set_max_size            (float max_width, float max_height);

    // Request an initial placement, applied once on this window's next begin()
    // and then cleared. If dock_target_title names a window that is currently
    // docked, this window is docked (tabbed) into that window's dock node.
    // Otherwise the window is shown floating, centered on the main ImGui
    // viewport, sized to the given fraction of the viewport size. Intended for
    // dynamically created windows that have no persisted ini position.
    void set_initial_placement(
        std::string_view dock_target_title,
        float            fallback_width_ratio,
        float            fallback_height_ratio
    );
    void set_show_in_menu        (bool show);

    // Request ImGui focus for this window, applied once on its next begin()
    // and then cleared. Focusing selects the window's tab when it is docked
    // and brings it to the front. Also shows the window if it was hidden.
    void request_window_focus    ();
    auto begin                   () -> bool;
    void end                     ();
    void set_window_visibility   (bool visible);
    void show_window             ();
    void hide_window             ();
    void toggle_window_visibility();
    void draw_image              (
        const std::shared_ptr<erhe::graphics::Texture_reference>& texture_reference,
        int                                                       width,
        int                                                       height,
        erhe::graphics::Filter                                    filter      = erhe::graphics::Filter::nearest,
        erhe::graphics::Sampler_mipmap_mode                       mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped
    );

    [[nodiscard]] auto get_imgui_host() const -> Imgui_host*;
    virtual void set_imgui_host(Imgui_host* imgui_host);

    virtual void imgui               () = 0;
    virtual void hidden              ();
    virtual void on_begin            ();
    virtual void on_end              ();
    virtual auto flags               () -> ImGuiWindowFlags;
    virtual void toolbar             (bool& hovered);
    [[nodiscard]] virtual auto want_keyboard_events     () const -> bool;
    [[nodiscard]] virtual auto want_mouse_events        () const -> bool;
    [[nodiscard]] virtual auto want_cursor_relative_hold() const -> bool;

protected:
    Imgui_renderer& m_imgui_renderer;
    Imgui_windows&  m_imgui_windows;

    std::string     m_title;
    Imgui_host*     m_imgui_host  {nullptr};
    bool            m_show_in_menu{true};
    bool            m_developer   {false};
    std::string     m_ini_label   {};
    float           m_min_size[2]{200.0f, 100.0f};
    float           m_max_size[2]{99999.0f, 99999.0f};

private:
    void apply_initial_placement();

    bool            m_is_hovered  {false};
    bool            m_is_visible  {true};

    // One-shot focus request (see request_window_focus()).
    bool            m_focus_requested{false};

    // One-shot initial placement request (see set_initial_placement()).
    bool            m_has_initial_placement    {false};
    std::string     m_initial_dock_target_title{};
    float           m_initial_width_ratio      {0.0f};
    float           m_initial_height_ratio     {0.0f};
};

} // namespace erhe::imgui
