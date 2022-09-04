#include "scene/scene_root.hpp"

#include "editor_log.hpp"
#include "rendertarget_node.hpp"

#include "scene/debug_draw.hpp"
#include "scene/helpers.hpp"
#include "scene/material_library.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/view.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/framebuffer.hpp"
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

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

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

Scene_layers::Scene_layers(erhe::scene::Scene& scene)
{
    using std::make_shared;

    m_content      = make_shared<Mesh_layer>("content",      Node_visibility::content);
    m_controller   = make_shared<Mesh_layer>("controller",   Node_visibility::controller);
    m_tool         = make_shared<Mesh_layer>("tool",         Node_visibility::tool);
    m_rendertarget = make_shared<Mesh_layer>("rendertarget", Node_visibility::rendertarget);
    m_brush        = make_shared<Mesh_layer>("brush",        Node_visibility::brush);
    m_light        = make_shared<Light_layer>("lights");

    scene.mesh_layers .push_back(m_content);
    scene.mesh_layers .push_back(m_controller);
    scene.mesh_layers .push_back(m_tool);
    scene.mesh_layers .push_back(m_brush);
    scene.light_layers.push_back(m_light);
}

auto Scene_layers::brush() const -> erhe::scene::Mesh_layer*
{
    return m_brush.get();
}

auto Scene_layers::content() const -> erhe::scene::Mesh_layer*
{
    return m_content.get();
}

auto Scene_layers::controller() const -> erhe::scene::Mesh_layer*
{
    return m_controller.get();
}

auto Scene_layers::tool() const -> erhe::scene::Mesh_layer*
{
    return m_tool.get();
}

auto Scene_layers::rendertarget() const -> erhe::scene::Mesh_layer*
{
    return m_rendertarget.get();
}

auto Scene_layers::light() const -> erhe::scene::Light_layer*
{
    return m_light.get();
}

Scene_root::Scene_root(const std::string_view& name)
    : m_name  {name}
    , m_scene {std::make_unique<Scene>(this)}
    , m_layers(*m_scene.get())
{
    ERHE_PROFILE_FUNCTION

    // Layer configuration
    using std::make_shared;
    using std::make_unique;
    using erhe::scene::Node;

    m_material_library = std::make_shared<Material_library>();
    m_physics_world    = erhe::physics::IWorld::create_unique();
    m_raytrace_scene   = erhe::raytrace::IScene::create_unique("root");

    ///// TODO Do these where scene is created instead
    /////
    ///// const auto& debug_drawer  = components.get<Debug_draw>();
    ///// const auto& configuration = components.get<erhe::application::Configuration>();
    ///// m_physics_world->set_debug_drawer(debug_drawer.get());
    ///// if (configuration->physics.enabled)
    ///// {
    /////     m_physics_world->enable_physics_updates();
    ///// }
    ///// else
    ///// {
    /////     m_physics_world->disable_physics_updates();
    ///// }
}

Scene_root::~Scene_root()
{
}

//// void Scene_root::attach_to_selection(const std::shared_ptr<erhe::scene::Node>& node)
//// {
////     auto selection_tool = try_get<Selection_tool>();
////     if (selection_tool && !selection_tool->selection().empty())
////     {
////         const auto& entry = selection_tool->selection().front();
////         entry->attach(node);
////         node->set_parent_from_node(erhe::scene::Transform{});
////     }
//// }
////
//// void Scene_root::attach_to_selection(const std::shared_ptr<erhe::scene::Node>& node)
//// {
////     auto selection_tool = try_get<Selection_tool>();
////     if (selection_tool && !selection_tool->selection().empty())
////     {
////         const auto& entry = selection_tool->selection().front();
////         entry->attach(node);
////         node->set_parent_from_node(erhe::scene::Transform{});
////     }
//// }

auto Scene_root::create_new_camera() -> std::shared_ptr<erhe::scene::Camera>
{
    auto camera = std::make_shared<erhe::scene::Camera>("Camera");
    camera->projection()->fov_y           = glm::radians(35.0f);
    camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
    camera->projection()->z_near          = 0.03f;
    camera->projection()->z_far           = 200.0f;
    scene().add(camera);
    //// attach_to_selection(camera);
    return camera;
}

auto Scene_root::create_new_empty_node() -> std::shared_ptr<erhe::scene::Node>
{
    auto node = std::make_shared<erhe::scene::Node>("Empty Node");
    scene().add_node(node);
    //// attach_to_selection(node);
    return node;
}

auto Scene_root::create_new_light() -> std::shared_ptr<erhe::scene::Light>
{
    auto light = std::make_shared<Light>("Light");
    light->type      = Light::Type::directional;
    light->color     = glm::vec3{1.0, 1.0f, 1.0};
    light->intensity =  1.0f;
    light->range     =  0.0f;
    //// attach_to_selection(light);
    scene().add_to_light_layer(
        *m_layers.light(),
        light
    );
    return light;
}

// TODO This is not thread safe
//// auto Scene_root::materials() -> std::vector<std::shared_ptr<Material>>&
//// {
////     return m_materials;
//// }
////
//// // TODO This is not thread safe
//// auto Scene_root::materials() const -> const std::vector<std::shared_ptr<Material>>&
//// {
////     return m_materials;
//// }

auto Scene_root::material_library() const -> const std::shared_ptr<Material_library>&
{
    return m_material_library;
}

[[nodiscard]] auto Scene_root::layers() -> Scene_layers&
{
    return m_layers;
}

[[nodiscard]] auto Scene_root::layers() const -> const Scene_layers&
{
    return m_layers;
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

[[nodiscard]] auto Scene_root::name() const -> const std::string&
{
    return m_name;
}

void Scene_root::add_instance(const Instance& instance)
{
    ERHE_PROFILE_FUNCTION

    scene().add_to_mesh_layer(
        *m_layers.content(),
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
    const std::lock_guard<std::mutex> lock{m_mutex};

    if (layer == nullptr)
    {
        layer = m_layers.content();
    }
    scene().add_to_mesh_layer(*layer, mesh);
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
auto Scene_root::camera_combo(
    const char*           label,
    erhe::scene::Camera*& selected_camera,
    const bool            nullptr_option
) const -> bool
{
    int selected_camera_index = 0;
    int index = 0;
    std::vector<const char*>          names;
    std::vector<erhe::scene::Camera*> cameras;
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
            static_cast<int>(names.size()),
            static_cast<int>(names.size())
        ) &&
        (selected_camera != cameras[selected_camera_index]);
    if (camera_changed)
    {
        selected_camera = cameras[selected_camera_index];
    }
    return camera_changed;
}

#endif

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
        m_layers.light()->lights.begin(),
        m_layers.light()->lights.end(),
        Light_comparator()
    );
}

void Scene_root::update_pointer_for_rendertarget_nodes()
{
    std::lock_guard<std::mutex> lock(m_rendertarget_nodes_mutex);

    for (const auto& rendertarget_node : m_rendertarget_nodes)
    {
        rendertarget_node->update_pointer();
    }
}

[[nodiscard]] auto Scene_root::create_rendertarget_node(
    const erhe::components::Components& components,
    Viewport_window&                    host_viewport_window,
    const int                           width,
    const int                           height,
    const double                        dots_per_meter
) -> std::shared_ptr<Rendertarget_node>
{
    std::lock_guard<std::mutex> lock(m_rendertarget_nodes_mutex);

    auto rendertarget_node = std::make_shared<Rendertarget_node>(
        *this,
        host_viewport_window,
        components,
        width,
        height,
        dots_per_meter
    );
    m_rendertarget_nodes.push_back(rendertarget_node);
    return rendertarget_node;
}

} // namespace editor
