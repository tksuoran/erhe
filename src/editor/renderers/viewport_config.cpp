#include "renderers/viewport_config.hpp"
#include "config/generated/viewport_config_data.hpp"
#include "config/generated/viewport_config_serialization.hpp"
#include "erhe_codegen/config_io.hpp"
#include "erhe_file/file.hpp"

namespace editor {

auto make_viewport_config(const Viewport_config_data& viewport_config_data) -> Viewport_config
{
    // Try loading from default_viewport_config.json first
    std::optional<std::string> contents = erhe::file::read("viewport_config", "default_viewport_config.json");
    if (contents.has_value()) {
        return erhe::codegen::load_config<Viewport_config>("default_viewport_config.json");
    }

    // Fallback: build from Viewport_config_data (legacy editor_settings.json)
    Viewport_config config;
    config.render_style_not_selected.line_color = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    config.render_style_not_selected.edge_lines = false;
    config.render_style_selected.edge_lines     = false;
    config.selection_highlight_low              = glm::vec4{2.0f, 1.0f, 0.0f, 0.8f};
    config.selection_highlight_high             = glm::vec4{2.0f, 1.0f, 0.0f, 0.8f};
    config.selection_highlight_width_low        = -3.0f;
    config.selection_highlight_width_high       = -3.0f;
    config.selection_highlight_frequency        =  1.0f;

    Render_style_data& render_style_not_selected = config.render_style_not_selected;
    render_style_not_selected.polygon_fill      = viewport_config_data.polygon_fill;
    render_style_not_selected.edge_lines        = viewport_config_data.edge_lines;
    render_style_not_selected.corner_points     = viewport_config_data.corner_points;
    render_style_not_selected.polygon_centroids = viewport_config_data.polygon_centroids;
    render_style_not_selected.line_color        = viewport_config_data.edge_color;
    render_style_not_selected.corner_color      = glm::vec4{1.0f, 0.5f, 0.0f, 1.0f};
    render_style_not_selected.centroid_color    = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};

    Render_style_data& render_style_selected = config.render_style_selected;
    render_style_selected.polygon_fill      = viewport_config_data.selection_polygon_fill;
    render_style_selected.edge_lines        = viewport_config_data.selection_edge_lines;
    render_style_selected.corner_points     = viewport_config_data.corner_points;
    render_style_selected.polygon_centroids = viewport_config_data.polygon_centroids;
    render_style_selected.line_color        = viewport_config_data.selection_edge_color;
    render_style_selected.corner_color      = glm::vec4{1.0f, 0.5f, 0.0f, 1.0f};
    render_style_selected.centroid_color    = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f};

    config.gizmo_scale               = viewport_config_data.gizmo_scale;
    config.selection_bounding_box    = viewport_config_data.selection_bounding_box;
    config.selection_bounding_sphere = viewport_config_data.selection_bounding_sphere;
    config.clear_color               = viewport_config_data.clear_color;
    return config;
}

}
