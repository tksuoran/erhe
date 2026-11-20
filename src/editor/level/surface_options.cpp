#include "level/surface_options.hpp"

#include <fmt/format.h>

#include <cmath>

namespace editor {

auto any_surface_option(const Surface_options& options) -> bool
{
    return (options.snap == Surface_snap::on) || (options.orient == Surface_orient::on);
}

auto make_surface_options(const Surface_snap snap, const Surface_orient orient, const glm::vec3& direction, const float max_distance) -> Surface_options
{
    return Surface_options{.snap = snap, .orient = orient, .direction = direction, .forward_distance = max_distance, .backward_distance = max_distance};
}

auto check_surface_max_distance(const float distance, const std::string_view name) -> std::string
{
    if (!std::isfinite(distance) || (distance < 0.0f)) {
        return fmt::format("the {} must be a finite number >= 0, not {}", name, distance);
    }
    return {};
}

auto check_surface_direction(const glm::vec3& direction) -> std::string
{
    const float length = glm::length(direction);
    if (!std::isfinite(length) || !(length > 1.0e-6f)) {
        return fmt::format("the surface search direction must be a finite vector that is not zero, not ({}, {}, {})", direction.x, direction.y, direction.z);
    }
    return {};
}

auto get_camera_ray_direction(
    const Camera_ray_projection projection,
    const glm::vec3&            camera_position,
    const glm::vec3&            view_direction,
    const glm::vec3&            anchor,
    glm::vec3&                  out_direction
) -> bool
{
    const glm::vec3 ray    = (projection == Camera_ray_projection::orthographic) ? view_direction : (anchor - camera_position);
    const float     length = glm::length(ray);
    if (!std::isfinite(length) || !(length > 1.0e-6f)) {
        return false;
    }
    out_direction = ray / length;
    return true;
}

auto is_axis_along_direction(const glm::vec3& axis, const glm::vec3& direction) -> bool
{
    constexpr float c_parallel_cos = 0.999f;
    const float     length         = glm::length(axis) * glm::length(direction);
    if (!(length > 0.0f)) {
        return false;
    }
    return (std::abs(glm::dot(axis, direction)) / length) > c_parallel_cos;
}

} // namespace editor
