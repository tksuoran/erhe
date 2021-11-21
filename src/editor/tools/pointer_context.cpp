#include "tools/pointer_context.hpp"

#include "erhe/scene/camera.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <gsl/gsl>

namespace editor
{

auto Pointer_context::position_in_world() const -> glm::vec3
{
    return position_in_world(pointer_z);
}

auto Pointer_context::position_in_world(const float z) const -> glm::vec3
{
    Expects(camera != nullptr);

    const float     depth_range_near  = 0.0f;
    const float     depth_range_far   = 1.0f;
    const glm::vec3 pointer_in_window = glm::vec3{static_cast<float>(pointer_x), static_cast<float>(pointer_y), z};
    return erhe::toolkit::unproject(
        camera->world_from_clip(),
        pointer_in_window,
        depth_range_near,
        depth_range_far,
        static_cast<float>(viewport.x),
        static_cast<float>(viewport.y),
        static_cast<float>(viewport.width),
        static_cast<float>(viewport.height)
    );
}

auto Pointer_context::position_in_window(const glm::vec3 position_in_world) const -> glm::vec3
{
    Expects(camera != nullptr);

    return erhe::toolkit::project_to_screen_space(
        camera->clip_from_world(),
        position_in_world, 0.0f, 1.0f,
        static_cast<float>(viewport.x),
        static_cast<float>(viewport.y),
        static_cast<float>(viewport.width),
        static_cast<float>(viewport.height)
    );
}

auto Pointer_context::position_in_world(const glm::vec3 position_in_window) const -> glm::vec3
{
    Expects(camera != nullptr);

    const float depth_range_near = 0.0f;
    const float depth_range_far  = 1.0f;
    return erhe::toolkit::unproject(
        camera->world_from_clip(),
        position_in_window,
        depth_range_near,
        depth_range_far,
        static_cast<float>(viewport.x),
        static_cast<float>(viewport.y),
        static_cast<float>(viewport.width),
        static_cast<float>(viewport.height)
    );
}

} // namespace editor
