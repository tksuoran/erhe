#pragma once

#include <glm/gtc/constants.hpp>

#include <cstdint>

namespace erhe::scene
{

class Transform;
struct Viewport;

class Projection
{
public:
    enum class Type : unsigned int
    {
        other = 0,            // Projection is done by shader in unusual way - hemispherical for example
        perspective_horizontal,
        perspective_vertical,
        perspective,          // Uses both horizontal and vertical fov and ignores aspect ratio
        orthogonal_horizontal,
        orthogonal_vertical,
        orthogonal,           // Uses both horizontal and vertical size and ignores aspect ratio, O-centered
        orthogonal_rectangle, // Like above, not O-centered, uses X and Y as corner
        generic_frustum       // Generic frustum
    };

    static constexpr const char* c_type_strings[] =
    {
        "Other",
        "Perspective Horizontal",
        "Perspective Vertical",
        "Perspective",
        "Orthogonal Horizontal",
        "Orthogonal Vertical",
        "Orthogonal Rectangle",
        "Generic Frustum"
    };

    void update(Transform& transform, Viewport viewport) const;

    Type  projection_type{Type::perspective_vertical};
    float z_near         {  1.0f};
    float z_far          {100.0};
    float fov_x          {glm::half_pi<float>()};
    float fov_y          {glm::half_pi<float>()};
    float ortho_left     {-0.5f};
    float ortho_width    { 1.0f};
    float ortho_bottom   {-0.5f};
    float ortho_height   { 1.0f};
    float frustum_left   {-0.5f};
    float frustum_right  { 0.5f};
    float frustum_bottom {-0.05f};
    float frustum_top    { 0.5f};
};

} // namespace erhe::scene
