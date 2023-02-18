#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cstdint>

namespace erhe::scene
{

class Transform;
class Viewport;

class Projection
{
public:
    enum class Type : unsigned int
    {
        other = 0,            // Projection is done by shader in unusual way - hemispherical for example
        perspective_horizontal,
        perspective_vertical,
        perspective,          // Uses both horizontal and vertical fov and ignores aspect ratio
        perspective_xr,
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
        "Perspective XR",
        "Orthogonal Horizontal",
        "Orthogonal Vertical",
        "Orthogonal Rectangle",
        "Generic Frustum"
    };

    [[nodiscard]] auto clip_from_node_transform(const Viewport viewport) const -> Transform;

    auto get_projection_matrix(
        const float viewport_aspect_ration,
        const bool  reverse_depth
    ) const -> glm::mat4;

    class Fov_sides
    {
    public:
        Fov_sides(const float left, const float right, const float up, const float down)
            : left {left}
            , right{right}
            , up   {up}
            , down {down}
        {
        }

        float left;
        float right;
        float up;
        float down;
    };

    [[nodiscard]] auto get_fov_sides(const Viewport viewport) const -> Fov_sides;

    Type  projection_type{Type::perspective_vertical};
    float z_near         {  0.03f};
    float z_far          {64.0};
    float fov_x          { glm::pi<float>() / 4.0f};
    float fov_y          { glm::pi<float>() / 4.0f};
    float fov_left       {-glm::pi<float>() / 4.0f};
    float fov_right      { glm::pi<float>() / 4.0f};
    float fov_up         { glm::pi<float>() / 4.0f};
    float fov_down       {-glm::pi<float>() / 4.0f};
    float ortho_left     {-0.5f};
    float ortho_width    { 1.0f};
    float ortho_bottom   {-0.5f};
    float ortho_height   { 1.0f};
    float frustum_left   {-0.5f};
    float frustum_right  { 0.5f};
    float frustum_bottom {-0.5f};
    float frustum_top    { 0.5f};
};

} // namespace erhe::scene
