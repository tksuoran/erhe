#pragma once

#include "renderers/primitive_buffer.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/graphics/instance.hpp"

#include <glm/glm.hpp>

#include <string_view>

namespace editor
{

class Render_style
{
public:
    bool      polygon_fill         {true};
    bool      edge_lines           {false}; // for selection
    bool      polygon_centroids    {false}; // for selection
    bool      corner_points        {false}; // for selection
    bool      polygon_offset_enable{false};
    float     polygon_offset_factor{1.0000f};
    float     polygon_offset_units {1.0000f};
    float     polygon_offset_clamp {0.0001f};
    float     point_size           {4.0f};
    float     line_width           {1.0f};
    glm::vec4 line_color           {0.00f, 0.00f, 0.00f, 0.5f};
    glm::vec4 corner_color         {0.00f, 0.00f, 1.00f, 1.0f};
    glm::vec4 centroid_color       {0.00f, 0.00f, 1.00f, 1.0f};

    Primitive_color_source edge_lines_color_source       {Primitive_color_source::constant_color};
    Primitive_color_source polygon_centroids_color_source{Primitive_color_source::mesh_wireframe_color};
    Primitive_color_source corner_points_color_source    {Primitive_color_source::mesh_wireframe_color};
};

enum class Visualization_mode : unsigned int
{
    off = 0,
    selected,
    all
};

class Viewport_config
{
public:
    Render_style render_style_not_selected;
    Render_style render_style_selected;
    //glm::vec4    clear_color{0.18f, 0.41f, 0.58f, 0.4f};
    glm::vec4    clear_color{0.0f, 0.0f, 0.0, 0.4f};
    bool         post_processing_enable{true};
    //glm::vec4    clear_color{0.02f, 0.02f, 0.02f, 1.0f};

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
