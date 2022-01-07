#pragma once

//#include <imgui.h>

#include <memory>
#include <string_view>

namespace editor
{

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

    virtual void imgui   () = 0;
    virtual void on_begin();
    virtual void on_end  ();

protected:
    bool              m_is_visible{true};
    const std::string m_title;
};

} // namespace editor
