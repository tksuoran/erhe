#pragma once

#include <memory>
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

namespace editor
{

class Imgui_renderer;
class Pointer_context;

class Imgui_window_context
{
public:
    Pointer_context& pointer_context;
};

class Imgui_window
{
public:
    Imgui_window(const std::string_view title);
    virtual ~Imgui_window();

    [[nodiscard]] auto is_visibile() const -> bool;
    [[nodiscard]] auto title      () const -> const std::string_view;
    auto begin            () -> bool;
    void end              ();
    void show             ();
    void hide             ();
    void toggle_visibility();
    void initialize(const erhe::components::Components& components);
    void image(
        const std::shared_ptr<erhe::graphics::Texture>& texture,
        const int                                       width,
        const int                                       height
    );

    virtual void imgui   () = 0;
    virtual void on_begin();
    virtual void on_end  ();
    virtual auto flags   () -> ImGuiWindowFlags;

protected:
    // Component dependencies
    std::shared_ptr<Imgui_renderer> m_imgui_renderer;

    bool              m_is_visible{true};
    const std::string m_title;
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

} // namespace editor
