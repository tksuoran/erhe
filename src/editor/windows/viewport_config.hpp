#pragma once

#include "windows/window.hpp"
#include "erhe/graphics/configuration.hpp"

#include <glm/glm.hpp>

namespace editor
{

class Viewport_config
    : public Window
{
public:
    Viewport_config();

    virtual ~Viewport_config() = default;

    void window(Pointer_context& pointer_context) override;

    bool      polygon_fill         {true};
    bool      edge_lines           {true};  // for selection
    bool      polygon_centroids    {false}; // for selection
    bool      corner_points        {false}; // for selection
    bool      polygon_offset_enable{false};
    float     polygon_offset_factor{erhe::graphics::Configuration::reverse_depth ? -1.0000f : 1.0000f};
    float     polygon_offset_units {erhe::graphics::Configuration::reverse_depth ? -1.0000f : 1.0000f};
    float     polygon_offset_clamp {erhe::graphics::Configuration::reverse_depth ? -0.0001f : 0.0001f};
    float     point_size           {4.0f};
    float     line_width           {4.0f};
    glm::vec4 line_color           {1.00f, 0.60f, 0.00f, 1.0f};
    glm::vec4 corner_color         {0.00f, 0.00f, 1.00f, 1.0f};
    glm::vec4 centroid_color       {0.00f, 0.00f, 1.00f, 1.0f};
    //glm::vec4 clear_color          {0.18f, 0.41f, 0.58f, 1.0f};
    glm::vec4 clear_color          {0.02f, 0.02f, 0.02f, 1.0f};
};

} // namespace editor
