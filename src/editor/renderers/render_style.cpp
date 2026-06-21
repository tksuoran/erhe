#include "renderers/render_style.hpp"

auto is_primitive_mode_enabled(
    const Render_style_data&        style,
    erhe::primitive::Primitive_mode primitive_mode
) -> bool
{
    using namespace erhe::primitive;
    switch (primitive_mode) {
        case Primitive_mode::not_set          : return false;
        case Primitive_mode::polygon_fill     : return style.polygon_fill;
        case Primitive_mode::edge_lines       : return style.edge_lines;
        case Primitive_mode::corner_points    : return style.corner_points;
        case Primitive_mode::corner_normals   : return false;
        case Primitive_mode::polygon_centroids: return style.polygon_centroids;
        case Primitive_mode::solid_wireframe  : return style.solid_wireframe;
        case Primitive_mode::count            : return false;
        default:                                return false;
    }
}

auto get_primitive_settings(
    const Render_style_appearance&  appearance,
    erhe::primitive::Primitive_mode primitive_mode
) -> erhe::scene_renderer::Primitive_interface_settings
{
    using namespace erhe::primitive;
    using Primitive_size_source = erhe::scene_renderer::Primitive_size_source;
    using Primitive_interface_settings = erhe::scene_renderer::Primitive_interface_settings;
    switch (primitive_mode) {
        case Primitive_mode::not_set          : return Primitive_interface_settings{};
        case Primitive_mode::polygon_fill     : return Primitive_interface_settings{};

        case Primitive_mode::edge_lines:
            return Primitive_interface_settings{
                .color_source    = appearance.edge_lines_color_source,
                .constant_color0 = appearance.line_color,
                .constant_color1 = appearance.line_color,
                .size_source     = Primitive_size_source::constant_size,
                .constant_size   = appearance.line_width
            };

        case Primitive_mode::corner_points:
            return Primitive_interface_settings{
                .color_source    = appearance.corner_points_color_source,
                .constant_color0 = appearance.corner_color,
                .constant_color1 = appearance.corner_color,
                .size_source     = Primitive_size_source::constant_size,
                .constant_size   = appearance.point_size
            };

        case Primitive_mode::corner_normals   : return Primitive_interface_settings{};

        case Primitive_mode::solid_wireframe:
            // Wireframe color / width are read by standard.frag (via the
            // per-draw Primitive block color / size) for the expanded fill draw.
            return Primitive_interface_settings{
                .color_source    = erhe::scene_renderer::Primitive_color_source::constant_color,
                .constant_color0 = appearance.solid_wireframe_color,
                .constant_color1 = appearance.solid_wireframe_color,
                .size_source     = Primitive_size_source::constant_size,
                .constant_size   = appearance.solid_wireframe_width
            };

        case Primitive_mode::polygon_centroids:
            return Primitive_interface_settings{
                .color_source    = appearance.polygon_centroids_color_source,
                .constant_color0 = appearance.centroid_color,
                .constant_color1 = appearance.centroid_color,
                .size_source     = Primitive_size_source::constant_size,
                .constant_size   = appearance.point_size
            };

        case Primitive_mode::count            : return Primitive_interface_settings{};
        default:                                return Primitive_interface_settings{};
    }
}

namespace editor {

Render_style::Render_style() = default;
Render_style::~Render_style() noexcept = default;

Render_style::Render_style(const std::string_view name)
    : Item{name}
{
}

}
