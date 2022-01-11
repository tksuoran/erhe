#include "erhe/scene/projection.hpp"
#include "erhe/scene/transform.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/toolkit/math_util.hpp"

namespace erhe::scene
{

auto Projection::clip_from_node_transform(const Viewport viewport) const -> Transform
{
    const auto aspect_ratio = viewport.aspect_ratio();
    const auto m = get_projection_matrix(aspect_ratio, viewport.reverse_depth);
    return Transform{
        m,
        glm::inverse(m)
    };
}

auto Projection::get_projection_matrix(
    const float aspect_ratio,
    const bool  reverse_depth
) const -> glm::mat4
{
    const auto clip_range = reverse_depth ? Clip_range{z_far, z_near} : Clip_range{z_near, z_far};

    switch (projection_type)
    {
        using enum Projection::Type;
        case perspective:
        {
            return erhe::toolkit::create_perspective(
                fov_x,
                fov_y,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case perspective_xr:
        {
            return erhe::toolkit::create_perspective_xr(
                fov_left,
                fov_right,
                fov_up,
                fov_down,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case perspective_horizontal:
        {
            return erhe::toolkit::create_perspective_horizontal(
                fov_x,
                aspect_ratio,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case perspective_vertical:
        {
            return erhe::toolkit::create_perspective_vertical(
                fov_y,
                aspect_ratio,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case orthogonal_horizontal:
        {
            return erhe::toolkit::create_orthographic(
                -0.5f * ortho_width,
                 0.5f * ortho_width,
                -0.5f * ortho_width / aspect_ratio,
                 0.5f * ortho_width / aspect_ratio,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case orthogonal_vertical:
        {
            return erhe::toolkit::create_orthographic(
                -0.5f * ortho_height / aspect_ratio,
                 0.5f * ortho_height / aspect_ratio,
                -0.5f * ortho_height,
                 0.5f * ortho_height,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case orthogonal:
        {
            return erhe::toolkit::create_orthographic(
                -0.5f * ortho_width,
                 0.5f * ortho_width,
                -0.5f * ortho_height,
                 0.5f * ortho_height,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case orthogonal_rectangle:
        {
            return erhe::toolkit::create_orthographic(
                ortho_left,
                ortho_left + ortho_width,
                ortho_bottom,
                ortho_bottom + ortho_height,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case generic_frustum:
        {
            return erhe::toolkit::create_frustum(
                frustum_left,
                frustum_right,
                frustum_bottom,
                frustum_top,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case other:
        {
            // TODO(tksuoran@gmail.com): Implement
            return glm::mat4{1.0f};
        }

        default:
        {
            // TODO(tksuoran@gmail.com): Implement
            return glm::mat4{1.0f};
        }
    }
}

auto Projection::get_fov_sides(const Viewport viewport) const -> Fov_sides
{
    switch (projection_type)
    {
        using enum Projection::Type;
        case perspective:
        {
            return Fov_sides{
                -0.5f * fov_x,
                 0.5f * fov_x,
                 0.5f * fov_y,
                -0.5f * fov_y
            };
        }

        case perspective_xr:
        {
            return Fov_sides{
                fov_left,
                fov_right,
                fov_up,
                fov_down
            };
        }

        case perspective_horizontal:
        {
            return Fov_sides{
                -0.5f * fov_x,
                 0.5f * fov_x,
                 0.5f * fov_x / viewport.aspect_ratio(),
                -0.5f * fov_x / viewport.aspect_ratio()
            };
        }

        case perspective_vertical:
        {
            return Fov_sides{
                -0.5f * fov_y * viewport.aspect_ratio(),
                 0.5f * fov_y * viewport.aspect_ratio(),
                 0.5f * fov_y,
                -0.5f * fov_y
            };
        }

        case orthogonal_horizontal:
        {
            return Fov_sides{
                -0.5f * ortho_width,
                 0.5f * ortho_width,
                 0.5f * ortho_width / viewport.aspect_ratio(),
                -0.5f * ortho_width / viewport.aspect_ratio()
            };
        }

        case orthogonal_vertical:
        {
            return Fov_sides{
                -0.5f * ortho_height / viewport.aspect_ratio(),
                 0.5f * ortho_height / viewport.aspect_ratio(),
                 0.5f * ortho_height,
                -0.5f * ortho_height
            };
        }

        case orthogonal:
        {
            return Fov_sides{
                -0.5f * ortho_width,
                 0.5f * ortho_width,
                 0.5f * ortho_height,
                -0.5f * ortho_height
            };
        }

        case orthogonal_rectangle:
        {
            return Fov_sides{
                ortho_left,
                ortho_left   + ortho_width,
                ortho_bottom + ortho_height,
                ortho_bottom
            };
        }

        case generic_frustum:
        {
            return Fov_sides{
                frustum_left,
                frustum_right,
                frustum_top,
                frustum_bottom
            };
        }

        case other:
        {
            // The projection is externally updated - do nothing here.
            break;
        }
    }

    return Fov_sides{ 0.0f, 0.0f, 0.0f, 0.0f };
}

} // namespace erhe::scene
