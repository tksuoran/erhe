#pragma once

#include "renderers/viewport_config.hpp"

#include "erhe_imgui/imgui_window.hpp"

#include <glm/glm.hpp>

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor
{

class Editor_context;

class Viewport_config_window
    : public erhe::imgui::Imgui_window
{
public:
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

    static constexpr const char* c_visualization_mode_strings[] =
    {
        "Off",
        "Selected",
        "All"
    };

    Viewport_config_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context
    );

    // Implements Imgui_window                     `
    void imgui() override;

    // Public API
    void render_style_ui(Render_style_data& render_style);

    Editor_context& m_context;

    Viewport_config  config;
    float            rendertarget_mesh_lod_bias{-0.666f};
    Viewport_config* edit_data{nullptr};
};

} // namespace editor
