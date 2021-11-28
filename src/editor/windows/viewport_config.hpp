#pragma once

#include "renderers/base_renderer.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/graphics/configuration.hpp"

#include <glm/glm.hpp>

#include <string_view>

namespace editor
{

class Configuration;

class Render_style
{
public:
    Render_style();

    explicit Render_style(const Configuration& configuration);

    bool                                  polygon_fill         {true};
    bool                                  edge_lines           {false};  // for selection
    bool                                  polygon_centroids    {false}; // for selection
    bool                                  corner_points        {false}; // for selection
    bool                                  polygon_offset_enable{false};
    float                                 polygon_offset_factor{-1.0000f};
    float                                 polygon_offset_units {-1.0000f};
    float                                 polygon_offset_clamp {-0.0001f};
    float                                 point_size           {4.0f};
    float                                 line_width           {4.0f};
    Base_renderer::Primitive_color_source edge_lines_color_source       {Base_renderer::Primitive_color_source::constant_color};
    Base_renderer::Primitive_color_source polygon_centroids_color_source{Base_renderer::Primitive_color_source::constant_color};
    Base_renderer::Primitive_color_source corner_points_color_source    {Base_renderer::Primitive_color_source::constant_color};
    glm::vec4                             line_color                    {1.00f, 0.60f, 0.00f, 1.0f};
    glm::vec4                             corner_color                  {0.00f, 0.00f, 1.00f, 1.0f};
    glm::vec4                             centroid_color                {0.00f, 0.00f, 1.00f, 1.0f};
};

class Viewport_config
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name {"Viewport_config"};
    static constexpr std::string_view c_title{"Viewport Properties"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Viewport_config ();
    ~Viewport_config() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui(Pointer_context& pointer_context) override;

    void render_style_ui(Render_style& render_style);

    Render_style render_style_not_selected;
    Render_style render_style_selected;

    glm::vec4 clear_color          {0.18f, 0.41f, 0.58f, 1.0f};
    //glm::vec4 clear_color          {0.02f, 0.02f, 0.02f, 0.0f};
};

} // namespace editor
