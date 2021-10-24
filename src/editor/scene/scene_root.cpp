#include "scene/scene_root.hpp"
#include "scene/helpers.hpp"
#include "scene/node_physics.hpp"
#include "log.hpp"

#include "erhe/graphics/buffer.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/tracy_client.hpp"

#include "mango/core/thread.hpp"
#include "glm/gtx/color_space.hpp"

#include <algorithm>

namespace editor
{

using namespace erhe::graphics;
using namespace erhe::geometry;
using namespace erhe::scene;
using namespace erhe::primitive;
using namespace std;
using namespace glm;


Camera_rig::Camera_rig(Scene_root&                          scene_root,
                       std::shared_ptr<erhe::scene::Camera> camera)
    : position{camera}
{
    // position_fps_heading           = make_shared<erhe::scene::Camera>("Camera");
    // position_fps_heading_elevation = make_shared<erhe::scene::Camera>("Camera");
    // position_free                  = make_shared<erhe::scene::Camera>("Camera");
    // *position_fps_heading          ->projection() = *camera->projection();
    // *position_fps_heading_elevation->projection() = *camera->projection();
    // *position_free                 ->projection() = *camera->projection();
    // scene_root.scene().cameras.push_back(position_fps_heading          );
    // scene_root.scene().cameras.push_back(position_fps_heading_elevation);
    // scene_root.scene().cameras.push_back(position_fps_heading);
    //
    // auto position_fps_heading_node = make_shared<erhe::scene::Node>();
    // scene_root.scene().nodes.emplace_back(position_fps_heading_node);
    // const glm::mat4 identity{1.0f};
    // position_fps_heading_node->transforms.parent_from_node.set(identity);
    // position_fps_heading_node->update();
    // position_fps_heading_node->attach(position_fps_heading);
}


Scene_root::Scene_root()
    : Component(c_name)
{
}

Scene_root::~Scene_root() = default;

void Scene_root::connect()
{
}

void Scene_root::initialize_component()
{
    ZoneScoped;

    // Layer configuration
    m_content_layer    = make_shared<Layer>();
    m_controller_layer = make_shared<Layer>();
    m_tool_layer       = make_shared<Layer>();
    m_brush_layer      = make_shared<Layer>();
    m_scene            = std::make_unique<Scene>();
    m_scene->layers      .push_back(m_content_layer);
    m_scene->layers      .push_back(m_controller_layer);
    m_scene->layers      .push_back(m_tool_layer);
    m_scene->layers      .push_back(m_brush_layer);
    m_all_layers         .push_back(m_content_layer);
    m_all_layers         .push_back(m_controller_layer);
    m_all_layers         .push_back(m_tool_layer);
    m_all_layers         .push_back(m_brush_layer);
    m_content_layers     .push_back(m_content_layer);
    m_content_fill_layers.push_back(m_content_layer);
    m_content_fill_layers.push_back(m_controller_layer);
    m_tool_layers        .push_back(m_tool_layer);
    m_brush_layers       .push_back(m_brush_layer);

    m_physics_world = erhe::physics::IWorld::create_unique();
}

auto Scene_root::materials() -> vector<shared_ptr<Material>>&
{
    return m_materials;
}

auto Scene_root::materials() const -> const vector<shared_ptr<Material>>&
{
    return m_materials;
}

auto Scene_root::physics_world() -> erhe::physics::IWorld&
{
    VERIFY(m_physics_world);
    return *m_physics_world.get();
}

auto Scene_root::scene() -> erhe::scene::Scene&
{
    VERIFY(m_scene);
    return *m_scene.get();
}

auto Scene_root::content_layer() -> erhe::scene::Layer&
{
    VERIFY(m_content_layer);
    return *m_content_layer.get();
}

auto Scene_root::add(const shared_ptr<Material>& material)
-> shared_ptr<Material>
{
    VERIFY(material);
    material->index = m_materials.size();
    m_materials.push_back(material);
    log_materials.trace("material {} is {}\n", material->index, material->name);
    return material;
}

auto Scene_root::add(const shared_ptr<Mesh>& mesh)
-> shared_ptr<Mesh>
{
    m_content_layer->meshes.push_back(mesh);
    return mesh;
}

auto Scene_root::add(const shared_ptr<Light>& light)
-> shared_ptr<Light>
{
    m_content_layer->lights.push_back(light);
    return light;
}

auto Scene_root::content_layer() const -> std::shared_ptr<erhe::scene::Layer>
{
    return m_content_layer;
}

auto Scene_root::controller_layer() const -> std::shared_ptr<erhe::scene::Layer>
{
    return m_controller_layer;
}

auto Scene_root::tool_layer() const -> std::shared_ptr<erhe::scene::Layer>
{
    return m_tool_layer;
}

auto Scene_root::make_mesh_node(
    string_view                           name,
    const shared_ptr<Primitive_geometry>& primitive_geometry,
    const shared_ptr<Material>&           material,
    Node*                                 parent,
    const glm::vec3                       position
) -> shared_ptr<Mesh>
{
    return make_mesh_node(name, primitive_geometry, material, content_layer(), parent, position);
}

auto Scene_root::make_mesh_node(
    string_view                           name,
    const shared_ptr<Primitive_geometry>& primitive_geometry,
    const shared_ptr<Material>&           material,
    Layer&                                layer,
    Node*                                 parent,
    const glm::vec3                       position
) -> shared_ptr<Mesh>
{
    const glm::mat4 transform = erhe::toolkit::create_translation(position);

    auto mesh = make_shared<Mesh>(name);
    mesh->primitives.emplace_back(primitive_geometry, material);

    auto node = make_shared<Node>(name);
    node->parent = parent;
    node->transforms.parent_from_node.set(transform);
    node->update();

    attach(
        layer,
        scene(),
        physics_world(),
        node,
        mesh,
        {}
    );

    return mesh;
}

} // namespace editor
