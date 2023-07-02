#pragma once

#include "erhe/primitive/primitive_geometry.hpp"

#include <glm/glm.hpp>

#include <map>
#include <memory>

namespace erhe::scene {
    class Light;
}

namespace editor
{

class Light_mesh
{
public:
    Light_mesh();

    // Public API
    [[nodiscard]] auto get_light_transform(const erhe::scene::Light& light) -> glm::mat4;
    [[nodiscard]] auto point_in_light     (const glm::vec3 point_in_world, const erhe::scene::Light& light) -> bool;
    [[nodiscard]] auto get_light_mesh     (const erhe::scene::Light& light) -> erhe::primitive::Primitive_geometry*;

private:
    erhe::primitive::Primitive_geometry m_quad_mesh;
    erhe::primitive::Primitive_geometry m_cone_mesh;
    int                                 m_light_cone_sides{0};
};

} // namespace editor
