#pragma once

#include "erhe_renderer/enums.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include "erhe_primitive/enums.hpp"
#include "erhe_item/item.hpp"

#include <string_view>

namespace editor {

class Render_style_data
{
public:
    auto is_primitive_mode_enabled(erhe::primitive::Primitive_mode primitive_mode) const -> bool;
    auto get_primitive_settings   (erhe::primitive::Primitive_mode primitive_mode) const -> erhe::scene_renderer::Primitive_interface_settings;

    bool      polygon_fill     {true};
    bool      edge_lines       {false};
    bool      polygon_centroids{false};
    bool      corner_points    {false};
    float     point_size       {4.0f};
    float     line_width       {1.0f};
    glm::vec4 line_color       {0.00f, 0.00f, 0.00f, 0.5f};
    glm::vec4 corner_color     {0.00f, 0.00f, 1.00f, 1.0f};
    glm::vec4 centroid_color   {0.00f, 0.00f, 1.00f, 1.0f};

    erhe::scene_renderer::Primitive_color_source edge_lines_color_source       {erhe::scene_renderer::Primitive_color_source::constant_color};
    erhe::scene_renderer::Primitive_color_source polygon_centroids_color_source{erhe::scene_renderer::Primitive_color_source::mesh_wireframe_color};
    erhe::scene_renderer::Primitive_color_source corner_points_color_source    {erhe::scene_renderer::Primitive_color_source::mesh_wireframe_color};
};

class Render_style
    : public erhe::Item<erhe::Item_base, erhe::Item_base, Render_style, erhe::Item_kind::not_clonable>
{
public:
    Render_style();
    ~Render_style() noexcept override;

    explicit Render_style(const std::string_view name);

    static constexpr std::string_view static_type_name{"Render_style"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    Render_style_data data;
};

} // namespace editor

// bool  polygon_offset_enable{false};
// float polygon_offset_factor{1.0000f};
// float polygon_offset_units {1.0000f};
// float polygon_offset_clamp {0.0001f};
