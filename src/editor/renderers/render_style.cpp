#include "renderers/render_style.hpp"

namespace editor
{

Render_style::Render_style()
    : erhe::Item{erhe::Unique_id<Render_style>{}.get_id()}
{
}

Render_style::Render_style(const std::string_view name)
    : erhe::Item{name, erhe::Unique_id<Render_style>{}.get_id()}
{
}

Render_style::~Render_style() noexcept = default;

auto Render_style_data::is_primitive_mode_enabled(
    erhe::primitive::Primitive_mode primitive_mode
) const -> bool
{
    using namespace erhe::primitive;
    switch (primitive_mode) {
        case Primitive_mode::not_set          : return false;
        case Primitive_mode::polygon_fill     : return polygon_fill;
        case Primitive_mode::edge_lines       : return edge_lines;
        case Primitive_mode::corner_points    : return corner_points;
        case Primitive_mode::corner_normals   : return false; // TODO
        case Primitive_mode::polygon_centroids: return polygon_centroids;
        case Primitive_mode::count            : return false;
        default:                                return false;
    }
}

auto Render_style_data::get_primitive_settings(
    erhe::primitive::Primitive_mode primitive_mode
) const -> erhe::scene_renderer::Primitive_interface_settings
{
    using namespace erhe::primitive;
    using Primitive_size_source = erhe::scene_renderer::Primitive_size_source;
    using Primitive_interface_settings = erhe::scene_renderer::Primitive_interface_settings;
    switch (primitive_mode) {
        case Primitive_mode::not_set          : return Primitive_interface_settings{};
        case Primitive_mode::polygon_fill     : return Primitive_interface_settings{};

        case Primitive_mode::edge_lines:
            return Primitive_interface_settings{
                .color_source   = edge_lines_color_source,
                .constant_color = line_color,
                .size_source    = Primitive_size_source::constant_size,
                .constant_size  = line_width
            };

        case Primitive_mode::corner_points:
            return Primitive_interface_settings{
                .color_source   = corner_points_color_source,
                .constant_color = corner_color,
                .size_source    = Primitive_size_source::constant_size,
                .constant_size  = point_size
            };

        case Primitive_mode::corner_normals   : return Primitive_interface_settings{};

        case Primitive_mode::polygon_centroids:
            return Primitive_interface_settings{
                .color_source   = polygon_centroids_color_source,
                .constant_color = centroid_color,
                .size_source    = Primitive_size_source::constant_size,
                .constant_size  = point_size
            };

        case Primitive_mode::count            : return Primitive_interface_settings{};
        default:                                return Primitive_interface_settings{};
    }
}

} // namespace editor
