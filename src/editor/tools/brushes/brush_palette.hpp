#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace editor
{

class Brushes;

class Brush_palette
    : public erhe::imgui::Imgui_window
{
public:
    static constexpr std::string_view c_title{"Brush Palette"};

    Brush_palette();

    // Implements Imgui_window
    void imgui() override;

private:
    std::shared_ptr<Brushes> m_brushes;
    int                      m_selected_brush_index{0};
};

} // namespace editor
