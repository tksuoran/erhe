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

namespace editor {

Controller_visualization::Controller_visualization(erhe::scene::Node* view_root, Mesh_memory& mesh_memory, Scene_root& scene_root)
{
    ERHE_PROFILE_FUNCTION();

    auto& material_library = scene_root.content_library()->materials;
    auto controller_material = material_library->make<erhe::primitive::Material>(
        "Controller",
        glm::vec4{0.1f, 0.1f, 0.2f, 1.0f}
    );

    GEO::Mesh controller_geo_mesh;
    erhe::geometry::shapes::make_torus(controller_geo_mesh, 0.05f, 0.0025f, 40, 14);
    transform_mesh(controller_geo_mesh, to_geo_mat4(erhe::math::mat4_swap_yz));

    erhe::graphics::Buffer_transfer_queue buffer_transfer_queue{};
    erhe::primitive::Element_mappings dummy{};
    erhe::primitive::Buffer_mesh buffer_mesh{};
    const bool buffer_mesh_ok = erhe::primitive::make_buffer_mesh(
        buffer_mesh,
        controller_geo_mesh,
        erhe::primitive::Build_info{
            .primitive_types = {.fill_triangles = true },
            .buffer_info = mesh_memory.buffer_info
        },
        dummy, // TODO make element mappings optional
        erhe::primitive::Normal_style::corner_normals
    );
    ERHE_VERIFY(buffer_mesh_ok); // TODO handle possible error (out of memory)

    erhe::primitive::Primitive primitive(buffer_mesh, controller_material);

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
