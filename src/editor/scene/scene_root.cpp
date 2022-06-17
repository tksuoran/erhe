#include "scene/scene_root.hpp"

#include "log.hpp"

#include "scene/helpers.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/view.hpp"
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
using erhe::scene::Node_visibility;
using erhe::scene::Scene;
using erhe::primitive::Material;

Instance::Instance(
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    const std::shared_ptr<Node_physics>&      node_physics,
    const std::shared_ptr<Node_raytrace>&     node_raytrace
)
    : mesh         {mesh}
    , node_physics {node_physics}
    , node_raytrace{node_raytrace}
{
}

Instance::~Instance() noexcept
{
}

auto Create_new_camera_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    static_cast<void>(context);

    return m_scene_root.create_new_camera();
}

auto Create_new_empty_node_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    static_cast<void>(context);

    return m_scene_root.create_new_empty_node();
}

auto Create_new_light_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    static_cast<void>(context);

    return m_scene_root.create_new_light();
}

Scene_root::Scene_root()
    : Component{c_label}
    , m_create_new_camera_command    {*this}
    , m_create_new_empty_node_command{*this}
    , m_create_new_light_command     {*this}

{
}

Scene_root::~Scene_root() noexcept
{
}

void Scene_root::declare_required_components()
{
    require<erhe::application::Configuration>();
    require<erhe::application::View>();
}

void Scene_root::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    // Layer configuration
    using std::make_shared;
    using std::make_unique;
    using erhe::scene::Node;
    m_content_layer    = make_shared<Mesh_layer>("content",    Node_visibility::content);
    m_controller_layer = make_shared<Mesh_layer>("controller", Node_visibility::controller);
    m_tool_layer       = make_shared<Mesh_layer>("tool",       Node_visibility::tool);
    m_gui_layer        = make_shared<Mesh_layer>("gui",        Node_visibility::gui);
    m_brush_layer      = make_shared<Mesh_layer>("brush",      Node_visibility::brush);
    m_light_layer      = make_shared<Light_layer>("lights");

    m_scene            = std::make_unique<Scene>();

    m_scene->mesh_layers .push_back(m_content_layer);
    m_scene->mesh_layers .push_back(m_controller_layer);
    m_scene->mesh_layers .push_back(m_tool_layer);
    m_scene->mesh_layers .push_back(m_brush_layer);
    m_scene->light_layers.push_back(m_light_layer);

    m_physics_world  = erhe::physics::IWorld::create_unique();
    m_raytrace_scene = erhe::raytrace::IScene::create_unique("root");

    if (get<erhe::application::Configuration>()->physics.enabled)
    {
        m_physics_world->enable_physics_updates();
    }
    else
    {
        m_physics_world->disable_physics_updates();
    }

    auto view = get<erhe::application::View>();

    view->register_command   (&m_create_new_camera_command);
    view->register_command   (&m_create_new_empty_node_command);
    view->register_command   (&m_create_new_light_command);
    view->bind_command_to_key(&m_create_new_camera_command,     erhe::toolkit::Key_f2, true);
    view->bind_command_to_key(&m_create_new_empty_node_command, erhe::toolkit::Key_f3, true);
    view->bind_command_to_key(&m_create_new_light_command,      erhe::toolkit::Key_f4, true);
}

void Scene_root::attach_to_selection(const std::shared_ptr<erhe::scene::Node>& node)
{
    auto selection_tool = get<Selection_tool>();
    if (!selection_tool->selection().empty())
    {
        const auto& entry = selection_tool->selection().front();
        entry->attach(node);
        node->set_parent_from_node(erhe::scene::Transform{});
    }
}

auto Scene_root::create_new_camera() -> bool
{
    auto camera = std::make_shared<erhe::scene::Camera>("Camera");
    camera->projection()->fov_y           = glm::radians(35.0f);
    camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
    camera->projection()->z_near          = 0.03f;
    camera->projection()->z_far           = 200.0f;
    scene().add(camera);
    attach_to_selection(camera);
    return true;
}

auto Scene_root::create_new_empty_node() -> bool
{
    auto node = std::make_shared<erhe::scene::Node>("Empty Node");
    scene().add_node(node);
    attach_to_selection(node);
    return true;
}

auto Scene_root::create_new_light() -> bool
{
    auto light = std::make_shared<Light>("Light");
    light->type                          = Light::Type::directional;
    light->color                         = glm::vec3{1.0, 1.0f, 1.0};
    light->intensity                     =  1.0f;
    light->range                         =  0.0f;
    light->projection()->projection_type = erhe::scene::Projection::Type::orthogonal;
    light->projection()->ortho_left      = -10.0f;
    light->projection()->ortho_width     =  20.0f;
    light->projection()->ortho_bottom    = -10.0f;
    light->projection()->ortho_height    =  20.0f;
    light->projection()->z_near          =   5.0f;
    light->projection()->z_far           =  20.0f;
    attach_to_selection(light);
    scene().add_to_light_layer(
        *light_layer(),
        light
    );
    return true;
}

// TODO This is not thread safe
auto Scene_root::materials() -> std::vector<std::shared_ptr<Material>>&
{
    return m_materials;
}

// TODO This is not thread safe
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

void Scene_root::add_instance(const Instance& instance)
{
    ERHE_PROFILE_FUNCTION

    scene().add_to_mesh_layer(
        *content_layer(),
        instance.mesh
    );

    if (instance.node_physics)
    {
        add_to_physics_world(
            physics_world(),
            instance.node_physics
        );
    }

    if (instance.node_raytrace)
    {
        add_to_raytrace_scene(
            raytrace_scene(),
            instance.node_raytrace
        );
    }
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
    scene().add_to_mesh_layer(*layer, mesh);
}

auto Scene_root::brush_layer() const -> erhe::scene::Mesh_layer*
{
    return m_brush_layer.get();
}

auto Scene_root::content_layer() const -> erhe::scene::Mesh_layer*
{
    return m_content_layer.get();
}

auto Scene_root::controller_layer() const -> erhe::scene::Mesh_layer*
{
    return m_controller_layer.get();
}

auto Scene_root::tool_layer() const -> erhe::scene::Mesh_layer*
{
    return m_tool_layer.get();
}

auto Scene_root::gui_layer() const -> erhe::scene::Mesh_layer*
{
    return m_gui_layer.get();
}

auto Scene_root::light_layer() const -> erhe::scene::Light_layer*
{
    return m_light_layer.get();
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
    for (const auto& camera : scene().cameras)
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

auto Scene_root::material_combo(
    const char*                                 label,
    std::shared_ptr<erhe::primitive::Material>& selected_material,
    const bool                                  empty_option
) const -> bool
{
    const std::lock_guard<std::mutex> lock{m_materials_mutex};

    int selection_index = 0;
    int index = 0;
    std::vector<const char*> names;
    std::vector<std::shared_ptr<Material>> materials;
    const bool empty_entry = empty_option || (selected_material == nullptr);
    if (empty_entry)
    {
        names.push_back("(none)");
        materials.push_back({});
        ++index;
    }
    for (const auto& material : m_materials)
    {
        if (!material->visible)
        {
            continue;
        }
        names.push_back(material->name.c_str());
        materials.push_back(material);
        if (selected_material == material)
        {
            selection_index = index;
        }
        ++index;
    }

    const bool selection_changed =
        ImGui::Combo(
            label,
            &selection_index,
            names.data(),
            static_cast<int>(names.size())
        ) &&
        (selected_material != materials.at(selection_index));
    if (selection_changed)
    {
        selected_material = materials.at(selection_index);
    }
    return selection_changed;
}

namespace
{

[[nodiscard]] auto sort_value(const Light::Type light_type) -> int
{
    switch (light_type)
    {
        //using enum erhe::scene::Light_type;
        case erhe::scene::Light_type::directional: return 0;
        case erhe::scene::Light_type::point:       return 1;
        case erhe::scene::Light_type::spot:        return 2;
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
