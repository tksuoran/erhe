#pragma once

#include "erhe/renderer/enums.hpp"
#include "erhe/renderer/primitive_buffer.hpp"

#include "erhe/primitive/enums.hpp"
#include "erhe/scene/item.hpp"

#include <string_view>


namespace editor
{

class Render_style_data
{
public:
    auto is_primitive_mode_enabled(erhe::primitive::Primitive_mode primitive_mode) const -> bool;
    auto get_primitive_settings   (erhe::primitive::Primitive_mode primitive_mode) const -> erhe::renderer::Primitive_interface_settings;

    bool      polygon_fill     {true};
    bool      edge_lines       {false};
    bool      polygon_centroids{false};
    bool      corner_points    {false};
    float     point_size       {4.0f};
    float     line_width       {1.0f};
    glm::vec4 line_color       {0.00f, 0.00f, 0.00f, 0.5f};
    glm::vec4 corner_color     {0.00f, 0.00f, 1.00f, 1.0f};
    glm::vec4 centroid_color   {0.00f, 0.00f, 1.00f, 1.0f};

    erhe::renderer::Primitive_color_source edge_lines_color_source       {erhe::renderer::Primitive_color_source::constant_color};
    erhe::renderer::Primitive_color_source polygon_centroids_color_source{erhe::renderer::Primitive_color_source::mesh_wireframe_color};
    erhe::renderer::Primitive_color_source corner_points_color_source    {erhe::renderer::Primitive_color_source::mesh_wireframe_color};
};

class Render_style
    : public erhe::scene::Item
{
public:
    Render_style();
    Render_style(const std::string_view name);
    ~Render_style() noexcept override;

    Render_style_data data;
};

} // namespace editor

// bool  polygon_offset_enable{false};
// float polygon_offset_factor{1.0000f};
// float polygon_offset_units {1.0000f};
// float polygon_offset_clamp {0.0001f};
