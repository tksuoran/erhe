#pragma once

#include "renderers/render_style.hpp"

#include "erhe/renderer/enums.hpp"

#include <glm/glm.hpp>

namespace editor
{


class Viewport_config
{
public:
    // TODO Use Render_style instead of Render_style_data?
    Render_style_data render_style_not_selected;
    Render_style_data render_style_selected;
    glm::vec4         selection_highlight_low       {  1.0f,  1.0f, 1.0f, 1.0f};
    glm::vec4         selection_highlight_high      {200.0f, 10.0f, 1.0f, 0.5f};
    float             selection_highlight_width_low { 0.0f};
    float             selection_highlight_width_high{-3.0f};
    float             selection_highlight_frequency {1.0f};
    glm::vec4         clear_color{0.0f, 0.0f, 0.0, 0.4f};
    float             gizmo_scale{4.5f};

    class Debug_visualizations
    {
    public:
        erhe::renderer::Visualization_mode light {erhe::renderer::Visualization_mode::selected};
        erhe::renderer::Visualization_mode camera{erhe::renderer::Visualization_mode::selected};
    };

    Debug_visualizations debug_visualizations     {};
    bool                 selection_bounding_box   {false};
    bool                 selection_bounding_sphere{false};
};


} // namespace editor
