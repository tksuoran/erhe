#include "erhe_scene/camera_snapshot.hpp"
#include "erhe_scene/camera.hpp"

namespace erhe::scene {

Camera_snapshot::Camera_snapshot(const Camera& camera)
{
    capture(camera);
}

void Camera_snapshot::capture(const Camera& camera)
{
    m_projection      = *camera.projection();
    m_world_from_node = camera.world_from_node_transform();
    m_exposure        = camera.get_exposure();
    m_shadow_range    = camera.get_shadow_range();
}

auto Camera_snapshot::projection_transforms(
    const erhe::math::Viewport&               viewport,
    const bool                                reverse_depth,
    const erhe::math::Depth_range             depth_range,
    const erhe::math::Coordinate_conventions& conventions
) const -> Camera_projection_transforms
{
    const Transform clip_from_node = m_projection.clip_from_node_transform(viewport, reverse_depth, depth_range, conventions);
    return Camera_projection_transforms{
        .clip_from_camera = clip_from_node,
        .clip_from_world = Transform{
            clip_from_node.get_matrix()         * m_world_from_node.get_inverse_matrix(),
            m_world_from_node.get_matrix()      * clip_from_node.get_inverse_matrix()
        }
    };
}

} // namespace erhe::scene
