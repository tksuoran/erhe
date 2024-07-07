#pragma once

#include <memory>
#include <string>
#include <string_view>

typedef int ImGuiWindowFlags;

namespace erhe::graphics
{
    class Texture;
}

namespace erhe::imgui
{

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
        Imgui_renderer&        imgui_renderer,
        Imgui_windows&         imgui_windows,
        const std::string_view title,
        const std::string_view ini_label
    );
    virtual ~Imgui_window() noexcept;

    [[nodiscard]] auto is_visible     () const -> bool;
    [[nodiscard]] auto is_hovered     () const -> bool;
    [[nodiscard]] auto get_ini_label  () const -> const std::string&;
    [[nodiscard]] auto get_title      () const -> const std::string&;
    [[nodiscard]] auto get_scale_value() const -> float;
    [[nodiscard]] auto show_in_menu   () const -> bool;
    void set_min_size     (float min_width, float min_height);
    auto begin            () -> bool;
    void end              ();
    void set_visibility   (bool visible);
    void show             ();
    void hide             ();
    void toggle_visibility();
    void image(
        const std::shared_ptr<erhe::graphics::Texture>& texture,
        const int                                       width,
        const int                                       height
    );

    auto get_imgui_host() const -> Imgui_host*;
    virtual void set_imgui_host(Imgui_host* imgui_host);

    virtual void imgui               () = 0;
    virtual void hidden              ();
    virtual void on_begin            ();
    virtual void on_end              ();
    virtual auto flags               () -> ImGuiWindowFlags;
    virtual auto has_toolbar         () const -> bool;
    virtual void toolbar             (bool& hovered);
    virtual auto want_keyboard_events() const -> bool;
    virtual auto want_mouse_events   () const -> bool;

protected:
    Imgui_renderer& m_imgui_renderer;
    Imgui_windows&  m_imgui_windows;

    std::string     m_title;
    Imgui_host*     m_imgui_host  {nullptr};
    bool            m_is_visible  {true};
    bool            m_is_hovered  {false};
    bool            m_show_in_menu{true};
    std::string     m_ini_label   {};
    float           m_min_size[2]{200.0f, 400.0f};
    float           m_max_size[2]{99999.0f, 99999.0f};
};

} // namespace erhe::imgui
