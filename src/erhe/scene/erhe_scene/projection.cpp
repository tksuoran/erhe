#include "erhe_scene/projection.hpp"
#include "erhe_scene/transform.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene {

auto Projection::clip_from_node_transform(const erhe::math::Viewport viewport) const -> Transform
{
    const auto aspect_ratio = viewport.aspect_ratio();
    const auto m = get_projection_matrix(aspect_ratio);
    return Transform{
        m,
        glm::inverse(m)
    };
}

auto Projection::get_projection_matrix(const float aspect_ratio, const bool reverse_depth) const -> glm::mat4
{
    const auto clip_range = reverse_depth ? Clip_range{z_far, z_near} : Clip_range{z_near, z_far};

    switch (projection_type) {
        //using enum Projection::Type;
        case Projection::Type::perspective: {
            return erhe::math::create_perspective(
                fov_x,
                fov_y,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case Projection::Type::perspective_xr: {
            return erhe::math::create_perspective_xr(
                fov_left,
                fov_right,
                fov_up,
                fov_down,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case Projection::Type::perspective_horizontal: {
            return erhe::math::create_perspective_horizontal(
                fov_x,
                aspect_ratio,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case Projection::Type::perspective_vertical: {
            return erhe::math::create_perspective_vertical(
                fov_y,
                aspect_ratio,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case Projection::Type::orthogonal_horizontal: {
            if (aspect_ratio == 0.0f) {
                return glm::mat4{1.0f};
            }
            return erhe::math::create_orthographic(
                -0.5f * ortho_width,
                 0.5f * ortho_width,
                -0.5f * ortho_width / aspect_ratio,
                 0.5f * ortho_width / aspect_ratio,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case Projection::Type::orthogonal_vertical: {
            if (aspect_ratio == 0.0f) {
                return glm::mat4{1.0f};
            }
            return erhe::math::create_orthographic(
                -0.5f * ortho_height / aspect_ratio,
                 0.5f * ortho_height / aspect_ratio,
                -0.5f * ortho_height,
                 0.5f * ortho_height,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case Projection::Type::orthogonal: {
            return erhe::math::create_orthographic(
                -0.5f * ortho_width,
                 0.5f * ortho_width,
                -0.5f * ortho_height,
                 0.5f * ortho_height,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case Projection::Type::orthogonal_rectangle: {
            return erhe::math::create_orthographic(
                ortho_left,
                ortho_left + ortho_width,
                ortho_bottom,
                ortho_bottom + ortho_height,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case Projection::Type::generic_frustum: {
            return erhe::math::create_frustum(
                frustum_left,
                frustum_right,
                frustum_bottom,
                frustum_top,
                clip_range.z_near,
                clip_range.z_far
            );
        }

        case Projection::Type::other: {
            // TODO(tksuoran@gmail.com): Implement
            return glm::mat4{1.0f};
        }

        default: {
            // TODO(tksuoran@gmail.com): Implement
            return glm::mat4{1.0f};
        }
    }
}

auto Projection::get_fov_sides(const erhe::math::Viewport viewport) const -> Fov_sides
{
    switch (projection_type) {
        //using enum Projection::Type;
        case Projection::Type::perspective: {
            return Fov_sides{
                -0.5f * fov_x,
                 0.5f * fov_x,
                 0.5f * fov_y,
                -0.5f * fov_y
            };
        }

        case Projection::Type::perspective_xr: {
            return Fov_sides{
                fov_left,
                fov_right,
                fov_up,
                fov_down
            };
        }

        case Projection::Type::perspective_horizontal: {
            return Fov_sides{
                -0.5f * fov_x,
                 0.5f * fov_x,
                 0.5f * fov_x / viewport.aspect_ratio(),
                -0.5f * fov_x / viewport.aspect_ratio()
            };
        }

        case Projection::Type::perspective_vertical: {
            return Fov_sides{
                -0.5f * fov_y * viewport.aspect_ratio(),
                 0.5f * fov_y * viewport.aspect_ratio(),
                 0.5f * fov_y,
                -0.5f * fov_y
            };
        }

        case Projection::Type::orthogonal_horizontal: {
            return Fov_sides{
                -0.5f * ortho_width,
                 0.5f * ortho_width,
                 0.5f * ortho_width / viewport.aspect_ratio(),
                -0.5f * ortho_width / viewport.aspect_ratio()
            };
        }

        case Projection::Type::orthogonal_vertical: {
            return Fov_sides{
                -0.5f * ortho_height / viewport.aspect_ratio(),
                 0.5f * ortho_height / viewport.aspect_ratio(),
                 0.5f * ortho_height,
                -0.5f * ortho_height
            };
        }

        case Projection::Type::orthogonal: {
            return Fov_sides{
                -0.5f * ortho_width,
                 0.5f * ortho_width,
                 0.5f * ortho_height,
                -0.5f * ortho_height
            };
        }

        case Projection::Type::orthogonal_rectangle: {
            return Fov_sides{
                ortho_left,
                ortho_left   + ortho_width,
                ortho_bottom + ortho_height,
                ortho_bottom
            };
        }

        case Projection::Type::generic_frustum: {
            return Fov_sides{
                frustum_left,
                frustum_right,
                frustum_top,
                frustum_bottom
            };
        }

        case Projection::Type::other: {
            // The projection is externally updated - do nothing here.
            break;
        }
    }

    return Fov_sides{ 0.0f, 0.0f, 0.0f, 0.0f };
}

auto Projection::get_scale() const -> float
{
    switch (projection_type) {
        //using enum Projection::Type;
        case Projection::Type::perspective: {
            return 0.5f * std::min(fov_x, fov_y);
        }

        case Projection::Type::perspective_xr: {
            return std::min(fov_right - fov_left, fov_up - fov_down);
        }

        case Projection::Type::perspective_horizontal: {
            return 0.5f * fov_x;
        }

        case Projection::Type::perspective_vertical: {
            return 0.5f * fov_y;
        }

        case Projection::Type::orthogonal_horizontal: {
            return 0.5f * ortho_width;
        }

        case Projection::Type::orthogonal_vertical: {
            return 0.5f * ortho_height;
        }

        case Projection::Type::orthogonal: {
            return 0.5f * std::min(ortho_width, ortho_height);
        }

        case Projection::Type::orthogonal_rectangle: {
            return 0.5f * std::min(ortho_width, ortho_height);
        }

        case Projection::Type::generic_frustum: {
            return 0.5f * std::min(frustum_right - frustum_left, frustum_top -frustum_bottom);
        }

        case Projection::Type::other: {
            return 1.0f;
        }
    }

    return 1.0f;
}

} // namespace erhe::scene
