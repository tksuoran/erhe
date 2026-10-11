#pragma once

#include "erhe_scene/projection.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

namespace erhe::scene {

class Camera;
class Camera_projection_transforms;

// The view parameters of a Camera, copied for a render phase: the renderers
// read the copy, so the camera node and its properties may be edited on
// another thread while a frame is rendered. capture() is called by the
// thread that owns the camera (the editor's main thread in its prepare
// phase); the accessors mirror the Camera ones they replace.
class Camera_snapshot
{
public:
    Camera_snapshot() = default;
    explicit Camera_snapshot(const Camera& camera);

    void capture(const Camera& camera);

    [[nodiscard]] auto projection_transforms(
        const erhe::math::Viewport&               viewport,
        bool                                      reverse_depth,
        erhe::math::Depth_range                   depth_range,
        const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}
    ) const -> Camera_projection_transforms;

    [[nodiscard]] auto projection               () const -> const Projection&    { return m_projection; }
    [[nodiscard]] auto world_from_node_transform() const -> const Trs_transform& { return m_world_from_node; }
    [[nodiscard]] auto world_from_node          () const -> const glm::mat4&     { return m_world_from_node.get_matrix(); }
    [[nodiscard]] auto node_from_world          () const -> const glm::mat4&     { return m_world_from_node.get_inverse_matrix(); }
    [[nodiscard]] auto position_in_world        () const -> glm::vec4            { return m_world_from_node.get_matrix()[3]; }
    [[nodiscard]] auto get_exposure             () const -> float                { return m_exposure; }
    [[nodiscard]] auto get_shadow_range         () const -> float                { return m_shadow_range; }
    [[nodiscard]] auto get_projection_scale     () const -> float                { return m_projection.get_scale(); }

private:
    Projection    m_projection     {};
    Trs_transform m_world_from_node{};
    float         m_exposure       {1.0f};
    float         m_shadow_range   {22.0f};
};

} // namespace erhe::scene
