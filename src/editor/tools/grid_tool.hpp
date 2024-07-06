#pragma once

#include "tools/tool.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor {

class Grid;
class Icon_set;
class Tools;

class Grid_hover_position
{
public:
    glm::vec3   position{0.0f};
    const Grid* grid    {nullptr};
};

class Grid_tool
    : public erhe::imgui::Imgui_window
    , public Tool
{
public:
    class Config
    {
    public:
        bool      enabled   {false};
        glm::vec4 major_color{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 minor_color{0.5f, 0.5f, 0.5f, 0.5f};
        float     major_width{4.0f};
        float     minor_width{2.0f};
        float     cell_size {1.0f};
        int       cell_div  {10};
        int       cell_count{2};
    };
    Config config;

    Grid_tool(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context,
        Icon_set&                    icon_set,
        Tools&                       tools
    );

    // Implements Tool
    void tool_render(const Render_context& context)  override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    void viewport_toolbar(bool& hovered);

    auto update_hover(
        const glm::vec3 ray_origin,
        const glm::vec3 ray_direction
    ) const -> Grid_hover_position;

private:
    bool                               m_enable{true};
    std::vector<std::shared_ptr<Grid>> m_grids;
    int                                m_grid_index{0};
};

} // namespace editor
