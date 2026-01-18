#pragma once

#include "erhe_primitive/buffer_mesh.hpp"

#include <glm/glm.hpp>

namespace erhe::scene {
    class Light;
}

namespace editor {

class Light_mesh
{
public:
    Light_mesh();

    // Public API
    [[nodiscard]] auto get_light_transform(const erhe::scene::Light& light) -> glm::mat4;
    [[nodiscard]] auto point_in_light     (glm::vec3 point_in_world, const erhe::scene::Light& light) -> bool;
    [[nodiscard]] auto get_light_mesh     (const erhe::scene::Light& light) -> erhe::primitive::Buffer_mesh*;

private:
    erhe::primitive::Buffer_mesh m_quad_mesh;
    erhe::primitive::Buffer_mesh m_cone_mesh;
    int                          m_light_cone_sides{0};
};

}
