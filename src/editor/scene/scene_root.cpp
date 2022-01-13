#include "scene/scene_root.hpp"
#include "scene/helpers.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
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
#include "erhe/toolkit/profile.hpp"

#include <glm/gtx/color_space.hpp>

#include <imgui.h>

#include <algorithm>

namespace editor
{

using erhe::scene::Light;
using erhe::scene::Light_layer;
using erhe::scene::Mesh;
using erhe::scene::Mesh_layer;
using erhe::scene::Scene;
using erhe::primitive::Material;

Scene_root::Scene_root()
    : Component{c_name}
{
}

Scene_root::~Scene_root() = default;

void Scene_root::connect()
{
}

void Scene_root::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    // Layer configuration
    using std::make_shared;
    using std::make_unique;
    m_content_layer    = make_shared<Mesh_layer>("content");
    m_controller_layer = make_shared<Mesh_layer>("controller");
    m_tool_layer       = make_shared<Mesh_layer>("tool");
    m_gui_layer        = make_shared<Mesh_layer>("gui");
    m_brush_layer      = make_shared<Mesh_layer>("brush");
    m_light_layer      = make_shared<Light_layer>("lights");

    m_scene            = std::make_unique<Scene>();

    m_scene->mesh_layers .push_back(m_content_layer);
    m_scene->mesh_layers .push_back(m_controller_layer);
    m_scene->mesh_layers .push_back(m_tool_layer);
    m_scene->mesh_layers .push_back(m_brush_layer);
    m_scene->light_layers.push_back(m_light_layer);

    m_content_layers     .push_back(m_content_layer.get());
    m_content_fill_layers.push_back(m_content_layer.get());
    m_content_fill_layers.push_back(m_controller_layer.get());
    m_tool_layers        .push_back(m_tool_layer.get());
    m_brush_layers       .push_back(m_brush_layer.get());

    m_physics_world = erhe::physics::IWorld::create_unique();

    m_raytrace_scene = erhe::raytrace::IScene::create_unique("root");
}

auto Scene_root::materials() -> std::vector<std::shared_ptr<Material>>&
{
    return m_materials;
}

auto Scene_root::materials() const -> const std::vector<std::shared_ptr<Material>>&
{
    return m_materials;
}

auto Scene_root::physics_world() -> erhe::physics::IWorld&
{
    ERHE_VERIFY(m_physics_world);
    return *m_physics_world.get();
}

auto Scene_root::raytrace_scene() -> erhe::raytrace::IScene&
{
    ERHE_VERIFY(m_raytrace_scene);
    return *m_raytrace_scene.get();
}

auto Scene_root::scene() -> erhe::scene::Scene&
{
    ERHE_VERIFY(m_scene);
    return *m_scene.get();
}

auto Scene_root::scene() const -> const erhe::scene::Scene&
{
    ERHE_VERIFY(m_scene);
    return *m_scene.get();
}

auto Scene_root::content_layer() -> erhe::scene::Mesh_layer&
{
    ERHE_VERIFY(m_content_layer);
    return *m_content_layer.get();
}

void Scene_root::add(
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    Mesh_layer*                               layer
)
{
    const std::lock_guard<std::mutex> lock{m_scene_mutex};

    if (layer == nullptr)
    {
        layer = m_content_layer.get();
    }
    add_to_scene_layer(scene(), *layer, mesh);
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

auto Scene_root::camera_combo(
    const char*            label,
    erhe::scene::ICamera*& selected_camera,
    const bool             nullptr_option
) const -> bool
{
    int selected_camera_index = 0;
    int index = 0;
    std::vector<const char*>           names;
    std::vector<erhe::scene::ICamera*> cameras;
    if (nullptr_option || (selected_camera == nullptr))
    {
        names.push_back("(none)");
        cameras.push_back(nullptr);
    }
    for (auto camera : scene().cameras)
    {
        names.push_back(camera->name().c_str());
        cameras.push_back(camera.get());
        if (selected_camera == camera.get())
        {
            selected_camera_index = index;
        }
        ++index;
    }

    const bool camera_changed =
        ImGui::Combo(
            label,
            &selected_camera_index,
            names.data(),
            static_cast<int>(names.size())
        ) &&
        (selected_camera != cameras[selected_camera_index]);
    if (camera_changed)
    {
        selected_camera = cameras[selected_camera_index];
    }
    return camera_changed;
}

namespace
{

[[nodiscard]] auto sort_value(const Light::Type light_type) -> int
{
    switch (light_type)
    {
        using enum Light::Type;
        case directional: return 0;
        case point:       return 1;
        case spot:        return 2;
        default: return 3;
    }
}

class Light_comparator
{
public:
    [[nodiscard]] inline auto operator()(
        const std::shared_ptr<Light>& lhs,
        const std::shared_ptr<Light>& rhs
    ) -> bool
    {
        return sort_value(lhs->type) < sort_value(rhs->type);
    }
};

}

void Scene_root::sort_lights()
{
    std::sort(
        light_layer()->lights.begin(),
        light_layer()->lights.end(),
        Light_comparator()
    );
}


} // namespace editor
