#pragma once

#include <memory>
#include <string>
#include <string_view>

typedef int ImGuiWindowFlags;

namespace erhe::graphics
{
    class Texture;
}

namespace erhe::components
{
    class Components;
}

namespace erhe::application
{

#if defined(ERHE_GUI_LIBRARY_IMGUI)
class Imgui_renderer;
#endif
class Imgui_viewport;


class Mouse_input_sink
{
public:
};

/// <summary>
/// Wrapper for ImGui window.
/// </summary>
/// Each Imgui_window must be hosted in exactly one Imgui_viewport.
class Imgui_window
{
public:
    explicit Imgui_window(const std::string_view title);
    Imgui_window(const std::string_view title, const std::string_view label);
    virtual ~Imgui_window() noexcept;

    [[nodiscard]] auto is_visible() const -> bool;
    [[nodiscard]] auto title     () const -> const std::string_view;
    [[nodiscard]] auto label     () -> const char*;
    auto begin            () -> bool;
    void end              ();
    void show             ();
    void hide             ();
    void toggle_visibility();
    void initialize       (const erhe::components::Components& components);
    void image(
        const std::shared_ptr<erhe::graphics::Texture>& texture,
        const int                                       width,
        const int                                       height
    );

    auto get_viewport() const -> Imgui_viewport*;
    virtual void set_viewport(Imgui_viewport* imgui_viewport);

    virtual void imgui               () = 0;
    virtual auto get_window_type_hash() const -> uint32_t;
    virtual void on_begin            ();
    virtual void on_end              ();
    virtual auto flags               () -> ImGuiWindowFlags;
    virtual auto consumes_mouse_input() const -> bool;

protected:
    // Component dependencies
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    std::shared_ptr<Imgui_renderer> m_imgui_renderer;
#endif
    Imgui_viewport*   m_imgui_viewport{nullptr};

    bool              m_is_visible{true};

    const std::string m_title;
    const std::string m_label;
    float             m_min_size[2]{120.0f, 120.0f};
    float             m_max_size[2]{99999.0f, 99999.0f};
};

} // namespace erhe::application
