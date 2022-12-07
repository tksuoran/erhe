#include "scene/scene_root.hpp"

#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "rendertarget_mesh.hpp"

#include "scene/content_library.hpp"
#include "scene/debug_draw.hpp"
#include "scene/scene_message_bus.hpp"
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
using erhe::scene::Scene;
using erhe::primitive::Material;

Scene_layers::Scene_layers(erhe::scene::Scene& scene)
{
    using std::make_shared;

    m_brush        = std::make_shared<Mesh_layer>("brush",        erhe::scene::Scene_item_flags::brush);
    m_content      = std::make_shared<Mesh_layer>("content",      erhe::scene::Scene_item_flags::content);
    m_controller   = std::make_shared<Mesh_layer>("controller",   erhe::scene::Scene_item_flags::controller);
    m_rendertarget = std::make_shared<Mesh_layer>("rendertarget", erhe::scene::Scene_item_flags::rendertarget);
    m_tool         = std::make_shared<Mesh_layer>("tool",         erhe::scene::Scene_item_flags::tool);

    scene.add_mesh_layer(m_brush);
    scene.add_mesh_layer(m_content);
    scene.add_mesh_layer(m_controller);
    scene.add_mesh_layer(m_rendertarget);
    scene.add_mesh_layer(m_tool);

    m_light = std::make_shared<Light_layer>("lights");
    scene.add_light_layer(m_light);
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

Scene_root::Scene_root(
    const erhe::components::Components&     components,
    const std::shared_ptr<Content_library>& content_library,
    const std::string_view                  name
)
    : m_name              {name}
    , m_scene             {std::make_unique<Scene>(components.get<Scene_message_bus>().get(), this)}
    , m_content_library   {content_library}
    , m_editor_message_bus{components.get<Editor_message_bus>()}
    , m_scene_message_bus {components.get<Scene_message_bus >()}
    , m_layers            (*m_scene.get())
{
    ERHE_PROFILE_FUNCTION

    // Layer configuration
    using std::make_shared;
    using std::make_unique;
    using erhe::scene::Node;

    m_physics_world  = erhe::physics::IWorld::create_unique();
    m_raytrace_scene = erhe::raytrace::IScene::create_unique("root");
}

Scene_root::~Scene_root() noexcept
{
}

[[nodiscard]] auto Scene_root::get_scene() -> Scene*
{
    return m_scene.get();
}

[[nodiscard]] auto Scene_root::get_editor_message_bus() const -> std::shared_ptr<Editor_message_bus>
{
    return m_editor_message_bus;
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

[[nodiscard]] auto Scene_root::get_name() const -> const std::string&
{
    return m_name;
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
    const auto& scene_cameras = scene().get_cameras();
    for (const auto& camera : scene_cameras)
    {
        names.push_back(camera->get_name().c_str());
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

auto Scene_root::camera_combo(
    const char*                           label,
    std::shared_ptr<erhe::scene::Camera>& selected_camera,
    const bool                            nullptr_option
) const -> bool
{
    int selected_camera_index = 0;
    int index = 0;
    std::vector<const char*> names;
    std::vector<std::shared_ptr<erhe::scene::Camera>> cameras;
    if (nullptr_option || (selected_camera == nullptr))
    {
        names.push_back("(none)");
        cameras.push_back(nullptr);
    }
    const auto& scene_cameras = scene().get_cameras();
    for (const auto& camera : scene_cameras)
    {
        names.push_back(camera->get_name().c_str());
        cameras.push_back(camera);
        if (selected_camera == camera)
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

auto Scene_root::camera_combo(
    const char*                         label,
    std::weak_ptr<erhe::scene::Camera>& selected_camera,
    const bool                          nullptr_option
) const -> bool
{
    int selected_camera_index = 0;
    int index = 0;
    std::vector<const char*> names;
    std::vector<std::weak_ptr<erhe::scene::Camera>> cameras;
    if (nullptr_option || selected_camera.expired())
    {
        names.push_back("(none)");
        cameras.push_back({});
    }
    const auto& scene_cameras = scene().get_cameras();
    for (const auto& camera : scene_cameras)
    {
        names.push_back(camera->get_name().c_str());
        cameras.push_back(camera);
        if (selected_camera.lock() == camera)
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
        (selected_camera.lock() != cameras[selected_camera_index].lock());
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

void Scene_root::update_pointer_for_rendertarget_meshes()
{
    std::lock_guard<std::mutex> lock(m_rendertarget_meshes_mutex);

    for (const auto& rendertarget_mesh : m_rendertarget_meshes)
    {
        rendertarget_mesh->update_pointer();
    }
}

auto Scene_root::content_library() const -> std::shared_ptr<Content_library>
{
    return m_content_library;
}

auto Scene_root::create_rendertarget_mesh(
    const erhe::components::Components& components,
    Viewport_window&                    host_viewport_window,
    const int                           width,
    const int                           height,
    const double                        pixels_per_meter
) -> std::shared_ptr<Rendertarget_mesh>
{
    std::lock_guard<std::mutex> lock(m_rendertarget_meshes_mutex);

    auto rendertarget_mesh = std::make_shared<Rendertarget_mesh>(
        *this,
        host_viewport_window,
        components,
        width,
        height,
        pixels_per_meter
    );
    rendertarget_mesh->mesh_data.layer_id = layers().rendertarget()->id.get_id();
    m_rendertarget_meshes.push_back(rendertarget_mesh);
    return rendertarget_mesh;
}

void Scene_root::sanity_check()
{
    m_scene->sanity_check();
}

} // namespace editor
