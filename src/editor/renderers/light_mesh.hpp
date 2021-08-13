#pragma once

#include "erhe/components/component.hpp"
#include "erhe/primitive/primitive_geometry.hpp"

#include <glm/glm.hpp>
#include <map>
#include <memory>

namespace erhe::primitive
{
    class Primitive_eometry;
}
namespace erhe::scene
{
    class Light;
}

namespace editor
{

class Program_interface;

class Light_mesh
    : public erhe::components::Component
{
public:
    static constexpr const char* c_name = "Light_mesh";
    Light_mesh ();
    ~Light_mesh() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    auto get_light_transform(const erhe::scene::Light& light) -> glm::mat4;
    auto point_in_light     (const glm::vec3 point_in_world, const erhe::scene::Light& light) -> bool;
    auto get_light_mesh     (const erhe::scene::Light& light) -> erhe::primitive::Primitive_geometry*;

private:
    void update_light_model(const erhe::scene::Light& light);

    std::shared_ptr<Program_interface>  m_program_interface;
    erhe::primitive::Primitive_geometry m_quad_mesh;
    erhe::primitive::Primitive_geometry m_cone_mesh;
    int                                 m_light_cone_sides;
};

} // namespace editor
