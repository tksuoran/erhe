#include "renderers/viewport_config.hpp"
#include "erhe_configuration/configuration.hpp"

namespace editor {

auto Viewport_config::default_config() -> Viewport_config
{
    Viewport_config config;
    config.render_style_not_selected.line_color = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    config.render_style_not_selected.edge_lines = false;
    config.render_style_selected.edge_lines     = false;
    config.selection_highlight_low              = glm::vec4{2.0f, 1.0f, 0.0f, 0.8f};
    config.selection_highlight_high             = glm::vec4{2.0f, 1.0f, 0.0f, 0.8f};
    config.selection_highlight_width_low        = -1.0f;
    config.selection_highlight_width_high       = -1.0f;
    config.selection_highlight_frequency        =  1.0f;

    auto& erhe_ini = erhe::configuration::get_ini_file(erhe::c_erhe_config_file_path);
    float     gizmo_scale              {3.0f};
    bool      polygon_fill             {true};
    bool      edge_lines               {false};
    bool      selection_polygon_fill   {true};
    bool      selection_edge_lines     {false};
    bool      corner_points            {false};
    bool      polygon_centroids        {false};
    bool      selection_bounding_sphere{true};
    bool      selection_bounding_box   {true};
    glm::vec4 edge_color               {0.0f, 0.0f, 0.0f, 0.5f};
    glm::vec4 selection_edge_color     {0.0f, 0.0f, 0.0f, 0.5f};
    glm::vec4 clear_color              {0.1f, 0.2f, 0.4f, 1.0f};
    const auto& viewport_section = erhe_ini.get_section("viewport");
    viewport_section.get("gizmo_scale",               gizmo_scale);
    viewport_section.get("polygon_fill",              polygon_fill);
    viewport_section.get("edge_lines",                edge_lines);
    viewport_section.get("edge_color",                edge_color);
    viewport_section.get("selection_polygon_fill",    selection_polygon_fill);
    viewport_section.get("selection_edge_lines",      selection_edge_lines);
    viewport_section.get("corner_points",             corner_points);
    viewport_section.get("polygon_centroids",         polygon_centroids);
    viewport_section.get("selection_bounding_box",    selection_bounding_box);
    viewport_section.get("selection_bounding_sphere", selection_bounding_sphere);
    viewport_section.get("selection_edge_color",      selection_edge_color);
    viewport_section.get("clear_color",               clear_color);

    Render_style_data& render_style_not_selected = config.render_style_not_selected;
    render_style_not_selected.polygon_fill      = polygon_fill;
    render_style_not_selected.edge_lines        = edge_lines;
    render_style_not_selected.corner_points     = corner_points;
    render_style_not_selected.polygon_centroids = polygon_centroids;
    render_style_not_selected.line_color        = edge_color;
    render_style_not_selected.corner_color      = glm::vec4{1.0f, 0.5f, 0.0f, 1.0f};
    render_style_not_selected.centroid_color    = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};

    Render_style_data& render_style_selected = config.render_style_selected;
    render_style_selected.polygon_fill      = selection_polygon_fill;
    render_style_selected.edge_lines        = selection_edge_lines;
    render_style_selected.corner_points     = corner_points;
    render_style_selected.polygon_centroids = polygon_centroids;
    render_style_selected.line_color        = selection_edge_color;
    render_style_selected.corner_color      = glm::vec4{1.0f, 0.5f, 0.0f, 1.0f};
    render_style_selected.centroid_color    = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};

    config.gizmo_scale               = gizmo_scale;
    config.selection_bounding_box    = selection_bounding_box;
    config.selection_bounding_sphere = selection_bounding_sphere;
    config.clear_color               = clear_color;
    return config;
}

}
