#pragma once

#include "renderers/enums.hpp"
#include "renderers/primitive_buffer.hpp"
#include "renderers/render_style.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/scene/item.hpp"

#include <glm/glm.hpp>

#include <string_view>

namespace editor
{


class Viewport_config
{
public:
    // TODO Use Render_style instead of Render_style_data?
    Render_style_data render_style_not_selected;
    Render_style_data render_style_selected;
    glm::vec4         clear_color{0.0f, 0.0f, 0.0, 0.4f};

    class Debug_visualizations
    {
    public:
        Visualization_mode light {Visualization_mode::selected};
        Visualization_mode camera{Visualization_mode::selected};
    };

    Debug_visualizations debug_visualizations;
    bool                 selection_bounding_box;
    bool                 selection_bounding_sphere;
};


} // namespace editor
