#pragma once

#include "erhe_math/math_util.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_property/enum_info.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace erhe::scene {

class Transform;

// Enumerator table for Projection::Type properties (labels match Projection::c_type_strings).
extern const erhe::property::Enum_info c_projection_type_enum_info;

class Projection
{
public:
    enum class Type : unsigned int {
        other = 0,            // Projection is done by shader in unusual way - hemispherical for example
        perspective_horizontal,
        perspective_vertical,
        perspective,          // Uses both horizontal and vertical fov and ignores aspect ratio
        perspective_xr,
        orthographic_horizontal,
        orthographic_vertical,
        orthographic,           // Uses both horizontal and vertical size and ignores aspect ratio, O-centered
        orthographic_rectangle, // Like above, not O-centered, uses X and Y as corner
        generic_frustum         // Generic frustum - uses perspective z near and far
    };

    static constexpr const char* c_type_strings[] = {
        "Other",
        "Perspective Horizontal",
        "Perspective Vertical",
        "Perspective",
        "Perspective XR",
        "Orthographic Horizontal",
        "Orthographic Vertical",
        "Orthographic",
        "Orthographic Rectangle",
        "Generic Frustum"
    };

    [[nodiscard]] auto clip_from_node_transform(
        erhe::math::Viewport                      viewport,
        bool                                      reverse_depth,
        erhe::math::Depth_range                   depth_range,
        const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}
    ) const -> Transform;

    [[nodiscard]] auto get_projection_matrix(
        float                                     viewport_aspect_ratio,
        bool                                      reverse_depth,
        erhe::math::Depth_range                   depth_range,
        const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}
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

    [[nodiscard]] auto get_fov_sides(erhe::math::Viewport viewport) const -> Fov_sides;
    [[nodiscard]] auto get_scale() const -> float;
    [[nodiscard]] auto is_orthographic() const -> bool;

    // The clip range of the projection type: the orthographic types use the
    // orthographic pair, every other type (generic_frustum included) the
    // perspective pair.
    [[nodiscard]] auto get_z_near() const -> float { return is_orthographic() ? orthographic_z_near : perspective_z_near; }
    [[nodiscard]] auto get_z_far () const -> float { return is_orthographic() ? orthographic_z_far  : perspective_z_far;  }
    void set_z_near(const float z_near) { if (is_orthographic()) { orthographic_z_near = z_near; } else { perspective_z_near = z_near; } }
    void set_z_far (const float z_far ) { if (is_orthographic()) { orthographic_z_far  = z_far;  } else { perspective_z_far  = z_far;  } }

    Type  projection_type    {Type::perspective_vertical};
    float perspective_z_near {   0.03f};
    float perspective_z_far  {  64.0f};
    // An orthographic projection has no eye point, so its near plane may lie
    // behind the camera (negative z_near).
    float orthographic_z_near{-256.0f};
    float orthographic_z_far { 256.0f};

    // Far plane at infinity, for the perspective projection types only (glTF
    // makes camera.perspective.zfar optional and the reference implementation
    // treats an absent zfar as Infinity). perspective_z_far stays a finite, meaningful
    // number while this is set: it is the depth hint the rest of the editor
    // works from (shadow range fitting, the transform tool's gizmo distance,
    // the properties slider), and only the projection matrix goes to infinity.
    bool  infinite_z_far {false};
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
