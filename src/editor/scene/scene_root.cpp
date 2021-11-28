#include "scene/scene_root.hpp"
#include "scene/helpers.hpp"
#include "scene/node_physics.hpp"
#include "log.hpp"

#include "erhe/graphics/buffer.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/tracy_client.hpp"

#include <mango/core/thread.hpp>
#include <glm/gtx/color_space.hpp>

#include <algorithm>

namespace editor
{

using namespace erhe::graphics;
using namespace erhe::geometry;
using namespace erhe::scene;
using namespace erhe::primitive;
using namespace std;
using namespace glm;


Camera_rig::Camera_rig(
    Scene_root&                          /*scene_root*/,
    std::shared_ptr<erhe::scene::Camera> camera
)
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
    m_content_layer    = make_shared<Mesh_layer>("content");
    m_controller_layer = make_shared<Mesh_layer>("controller");
    m_tool_layer       = make_shared<Mesh_layer>("tool");
    m_brush_layer      = make_shared<Mesh_layer>("brush");
    m_light_layer      = make_shared<Light_layer>("lights");

    m_scene            = std::make_unique<Scene>();

    m_scene->mesh_layers .push_back(m_content_layer);
    m_scene->mesh_layers .push_back(m_controller_layer);
    m_scene->mesh_layers .push_back(m_tool_layer);
    m_scene->mesh_layers .push_back(m_brush_layer);
    m_scene->light_layers.push_back(m_light_layer);

    m_all_layers         .push_back(m_content_layer.get());
    m_all_layers         .push_back(m_controller_layer.get());
    m_all_layers         .push_back(m_tool_layer.get());
    m_all_layers         .push_back(m_brush_layer.get());
    m_content_layers     .push_back(m_content_layer.get());
    m_content_fill_layers.push_back(m_content_layer.get());
    m_content_fill_layers.push_back(m_controller_layer.get());
    m_tool_layers        .push_back(m_tool_layer.get());
    m_brush_layers       .push_back(m_brush_layer.get());

    m_physics_world = erhe::physics::IWorld::create_unique();

    m_raytrace_scene = erhe::raytrace::IScene::create_unique();
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

auto Scene_root::content_layer() -> erhe::scene::Mesh_layer&
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
    m_light_layer->lights.push_back(light);
    return light;
}

auto Scene_root::brush_layer() const -> std::shared_ptr<erhe::scene::Mesh_layer>
{
    return m_brush_layer;
}

auto Scene_root::content_layer() const -> std::shared_ptr<erhe::scene::Mesh_layer>
{
    return m_content_layer;
}

auto Scene_root::controller_layer() const -> std::shared_ptr<erhe::scene::Mesh_layer>
{
    return m_controller_layer;
}

auto Scene_root::tool_layer() const -> std::shared_ptr<erhe::scene::Mesh_layer>
{
    return m_tool_layer;
}

auto Scene_root::light_layer() const -> std::shared_ptr<erhe::scene::Light_layer>
{
    return m_light_layer;
}

auto Scene_root::make_mesh_node(
    const string_view                     name,
    const shared_ptr<Primitive_geometry>& primitive_geometry,
    const shared_ptr<Material>&           material,
    Node*                                 parent,
    const glm::vec3                       position
) -> shared_ptr<Mesh>
{
    return make_mesh_node(name, primitive_geometry, material, content_layer(), parent, position);
}

auto Scene_root::make_mesh_node(
    const string_view                     name,
    const shared_ptr<Primitive_geometry>& primitive_geometry,
    const shared_ptr<Material>&           material,
    Mesh_layer&                           layer,
    Node*                                 parent,
    const glm::vec3                       position
) -> shared_ptr<Mesh>
{
    auto mesh = make_shared<Mesh>(name);
    mesh->data.primitives.emplace_back(primitive_geometry, material);

    mesh->set_parent_from_node(Transform::create_translation(position));

    std::lock_guard<std::mutex> lock{m_scene_mutex};

    add_to_scene_layer(scene(), layer, mesh);
    if (parent != nullptr)
    {
        parent->attach(mesh);
    }

    return mesh;
}

} // namespace editor
