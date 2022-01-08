#include "xr/controller_visualization.hpp"
#include "scene/scene_root.hpp"
#include "renderers/mesh_memory.hpp"

#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/xr/headset.hpp"

namespace editor
{

Controller_visualization::Controller_visualization(
    Mesh_memory&       mesh_memory,
    Scene_root&        scene_root,
    erhe::scene::Node* view_root
)
{
    auto controller_material = scene_root.make_material("Controller", glm::vec4{0.1f, 0.1f, 0.2f, 1.0f});
    //constexpr float length = 0.05f;
    //constexpr float radius = 0.02f;
    auto controller_geometry = erhe::geometry::shapes::make_torus(0.05f, 0.0025f, 22, 8);
    controller_geometry.transform(erhe::toolkit::mat4_swap_yz);
    controller_geometry.reverse_polygons();

    erhe::graphics::Buffer_transfer_queue buffer_transfer_queue;
    auto controller_pg = erhe::primitive::make_primitive(
        controller_geometry,
        mesh_memory.build_info_set.gl
    );

    erhe::primitive::Primitive primitive{
        .material              = controller_material,
        .gl_primitive_geometry = controller_pg
    };
    m_controller_mesh = std::make_shared<erhe::scene::Mesh>("Controller", primitive);
    view_root->attach(m_controller_mesh);
    scene_root.add(m_controller_mesh, scene_root.controller_layer().get());

    //m_controller_mesh = scene_root.make_mesh_node(
    //    "Controller",                           // name
    //    controller_pg,                          // primitive geometry
    //    controller_material,                    // material
    //    *scene_root.controller_layer().get(),   // layer
    //    view_root,                              // parent
    //    glm::vec3{-9999.9f, -9999.9f, -9999.9f} // position
    //);
}

void Controller_visualization::update(const erhe::xr::Pose& pose)
{
    const glm::mat4 orientation = glm::mat4_cast(pose.orientation);
    const glm::mat4 translation = glm::translate(glm::mat4{ 1 }, pose.position);
    const glm::mat4 m           = translation * orientation;
    m_controller_mesh->set_parent_from_node(m);
}

auto Controller_visualization::get_node() const -> erhe::scene::Node*
{
    return m_controller_mesh.get();
}

}