#pragma once

#include "erhe/components/component.hpp"

#include <imgui.h>

#include <memory>
#include <string_view>

namespace editor
{

class Pointer_context;
class Scene_manager;

class Imgui_window_context
{
public:
    Pointer_context& pointer_context;
};

class Imgui_window
{
public:
    Imgui_window(const std::string_view title);

    void show             ();
    void hide             ();
    void toggle_visibility();
    auto is_visibile      () const -> bool;
    auto title            () const -> const std::string_view;

    virtual void imgui() = 0;

protected:
    bool              m_is_visible{true};
    const std::string m_title;
};

} // namespace editor
