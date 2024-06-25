#include "xr/controller_visualization.hpp"

#include "scene/content_library.hpp"
#include "scene/scene_root.hpp"
#include "renderers/mesh_memory.hpp"

#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_geometry/shapes/torus.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_xr/xr_action.hpp"

namespace editor
{

Controller_visualization::Controller_visualization(
    erhe::scene::Node* view_root,
    Mesh_memory&       mesh_memory,
    Scene_root&        scene_root
)
{
    ERHE_PROFILE_FUNCTION();

    auto& material_library = scene_root.content_library()->materials;
    auto controller_material = material_library->make<erhe::primitive::Material>(
        "Controller",
        glm::vec4{0.1f, 0.1f, 0.2f, 1.0f}
    );
    //constexpr float length = 0.05f;
    //constexpr float radius = 0.02f;
    auto controller_geometry = erhe::geometry::shapes::make_torus(0.05f, 0.0025f, 40, 14);
    controller_geometry.transform(erhe::math::mat4_swap_yz);
    //// controller_geometry.reverse_polygons();

    erhe::graphics::Buffer_transfer_queue buffer_transfer_queue;
    auto geometry_primitive = std::make_shared<erhe::primitive::Geometry_primitive>(
        erhe::primitive::make_renderable_mesh(
            controller_geometry,
            erhe::primitive::Build_info{
                .primitive_types = {.fill_triangles = true },
                .buffer_info = mesh_memory.buffer_info
            },
            erhe::primitive::Normal_style::corner_normals
        )
    );

    erhe::primitive::Primitive primitive{
        .material           = controller_material,
        .geometry_primitive = geometry_primitive
    };

    m_controller_node = std::make_shared<erhe::scene::Node>("Controller node");
    m_controller_mesh = std::make_shared<erhe::scene::Mesh>("Controller", primitive);
    m_controller_node->enable_flag_bits(erhe::Item_flags::visible);
    m_controller_mesh->enable_flag_bits(erhe::Item_flags::controller | erhe::Item_flags::opaque);
    m_controller_mesh->layer_id = scene_root.layers().content()->id;
    m_controller_node->attach(m_controller_mesh);
    m_controller_node->set_parent(view_root);
}

void Controller_visualization::update(const erhe::xr::Xr_action_pose* pose)
{
    const glm::mat4 orientation = glm::mat4_cast(pose->orientation);
    const glm::mat4 translation = glm::translate(glm::mat4{1}, pose->position);
    const glm::mat4 m           = translation * orientation;
    m_controller_node->set_parent_from_node(m);
}

auto Controller_visualization::get_node() const -> erhe::scene::Node*
{
    return m_controller_node.get();
}

}
