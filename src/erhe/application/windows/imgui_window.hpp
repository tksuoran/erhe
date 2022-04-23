#pragma once

#include <glm/glm.hpp>

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

class Imgui_renderer;
class Pointer_context;

class Imgui_window_context
{
public:
    Pointer_context& pointer_context;
};

class Mouse_input_sink
{
public:
};

class Imgui_window
{
public:
    explicit Imgui_window(const std::string_view title);
    Imgui_window(const std::string_view title, const std::string_view label);
    virtual ~Imgui_window() noexcept;

    [[nodiscard]] auto is_visible          () const -> bool;
    [[nodiscard]] auto title               () const -> const std::string_view;
    [[nodiscard]] auto label               () -> const char*;
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

    virtual void imgui               () = 0;
    virtual auto get_window_type_hash() const -> uint32_t;
    virtual void on_begin            ();
    virtual void on_end              ();
    virtual auto flags               () -> ImGuiWindowFlags;
    virtual auto consumes_mouse_input() const -> bool;

protected:
    // Component dependencies
    std::shared_ptr<Imgui_renderer> m_imgui_renderer;

    bool              m_is_visible{true};
    const std::string m_title;
    const std::string m_label;
    glm::vec2         m_min_size{120.0f, 120.0f};
    glm::vec2         m_max_size{99999.0f, 99999.0f};
};

class Rendertarget_imgui_window
    : public Imgui_window
{
public:
    explicit Rendertarget_imgui_window(const std::string_view title);

    auto flags   () -> ImGuiWindowFlags override;
    void on_begin()                     override;
    void on_end  ()                     override;
};

} // namespace erhe::application
