#pragma once

#include "renderers/primitive_buffer.hpp"

#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/graphics/configuration.hpp"

#include <glm/glm.hpp>

#include <string_view>

namespace erhe::application
{
    class Configuration;
}

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
    float     line_width           {2.0f};
    glm::vec4 line_color           {1.00f, 0.60f, 0.00f, 1.0f};
    glm::vec4 corner_color         {0.00f, 0.00f, 1.00f, 1.0f};
    glm::vec4 centroid_color       {0.00f, 0.00f, 1.00f, 1.0f};

    Primitive_color_source edge_lines_color_source       {Primitive_color_source::constant_color};
    Primitive_color_source polygon_centroids_color_source{Primitive_color_source::constant_color};
    Primitive_color_source corner_points_color_source    {Primitive_color_source::constant_color};
};

enum class Visualization_mode : unsigned int
{
    off = 0,
    selected,
    all
};

class Viewport_config
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr const char* c_visualization_mode_strings[] =
    {
        "Off",
        "Selected",
        "All"
    };

    static constexpr std::string_view c_label{"Viewport_config"};
    static constexpr std::string_view c_title{"Viewport config"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Viewport_config();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Imgui_window
    void imgui          () override;

    // Public API
    void render_style_ui(Render_style& render_style);

    Render_style render_style_not_selected;
    Render_style render_style_selected;
    glm::vec4    clear_color{0.18f, 0.41f, 0.58f, 1.0f};
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
    float                rendertarget_node_lod_bias{-0.666f};
};

} // namespace editor
