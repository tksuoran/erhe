#include "erhe_scene/projection.hpp"
#include "erhe_scene/transform.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_math/math_util.hpp"

namespace erhe::scene {

auto Projection::clip_from_node_transform(const erhe::math::Viewport viewport) const -> Transform
{
    const auto aspect_ratio = viewport.aspect_ratio();
    const auto m = get_projection_matrix(aspect_ratio, viewport.reverse_depth);
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

auto get_planes_from_clip_from_world(const glm::mat4& m) -> std::array<glm::vec4, 6>
{
    // Reconstruct rows from the column-major matrix
    glm::vec4 row0{m[0][0], m[1][0], m[2][0], m[3][0]};
    glm::vec4 row1{m[0][1], m[1][1], m[2][1], m[3][1]};
    glm::vec4 row2{m[0][2], m[1][2], m[2][2], m[3][2]};
    glm::vec4 row3{m[0][3], m[1][3], m[2][3], m[3][3]};

    return {
        row3 + row0, // Left
        row3 - row0, // Right
        row3 + row1, // Bottom
        row3 - row1, // Top
        row3 + row2, // Near
        row3 - row2  // Far
    };
}

auto get_point_on_plane(const glm::vec4& plane) -> glm::vec3
{
    glm::vec3 normal = glm::vec3(plane);
    float d = plane.w;

    // Avoid division by zero
    float denom = glm::dot(normal, normal);
    if (denom == 0.0f) {
        return glm::vec3{0.0f}; // degenerate plane
    }

    // Return point: -d * n / ||n||^2
    return -d * normal / denom;
}

void get_plane_basis(const glm::vec3& normal, glm::vec3& tangent, glm::vec3& bitangent)
{
    glm::vec3 tangent_ = erhe::math::min_axis<float>(normal);

    bitangent = glm::normalize(glm::cross(normal, tangent_));
    tangent   = glm::normalize(glm::cross(bitangent, normal));
}

} // namespace erhe::scene
