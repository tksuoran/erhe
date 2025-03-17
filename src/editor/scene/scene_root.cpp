#include "scene/scene_root.hpp"

#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_scenes.hpp"
#include "editor_settings.hpp"
#include "rendertarget_mesh.hpp"

#include "scene/content_library.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "tools/clipboard.hpp"
#include "windows/item_tree_window.hpp"

#include "erhe_primitive/material.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/mesh_raytrace.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
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

Scene_layers::Scene_layers()
{
    m_brush        = std::make_shared<Mesh_layer>("brush",        erhe::Item_flags::brush,        Mesh_layer_id::brush);
    m_content      = std::make_shared<Mesh_layer>("content",      erhe::Item_flags::content,      Mesh_layer_id::content);
    m_controller   = std::make_shared<Mesh_layer>("controller",   erhe::Item_flags::controller,   Mesh_layer_id::controller);
    m_rendertarget = std::make_shared<Mesh_layer>("rendertarget", erhe::Item_flags::rendertarget, Mesh_layer_id::rendertarget);
    m_tool         = std::make_shared<Mesh_layer>("tool",         erhe::Item_flags::tool,         Mesh_layer_id::tool);

    m_light        = std::make_shared<Light_layer>("lights", 0);
}

void Scene_layers::add_layers_to_scene(erhe::scene::Scene& scene)
{
    scene.add_mesh_layer(m_brush);
    scene.add_mesh_layer(m_content);
    scene.add_mesh_layer(m_controller);
    scene.add_mesh_layer(m_rendertarget);
    scene.add_mesh_layer(m_tool);

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

auto Scene_layers::mesh_layers() const -> std::vector<erhe::scene::Mesh_layer*>
{
    return std::vector<erhe::scene::Mesh_layer*>{
        m_content.get(),
        m_controller.get(),
        m_tool.get(),
        m_brush.get(),
        m_rendertarget.get()
    };
}

auto Scene_layers::light() const -> erhe::scene::Light_layer*
{
    return m_light.get();
}

Scene_root::Scene_root(
    erhe::imgui::Imgui_renderer*            imgui_renderer,
    erhe::imgui::Imgui_windows*             imgui_windows,
    erhe::scene::Scene_message_bus&         scene_message_bus,
    Editor_context*                         editor_context,
    Editor_message_bus*                     editor_message_bus,
    Editor_scenes*                          editor_scenes,
    const std::shared_ptr<Content_library>& content_library,
    const std::string_view                  name
)
    : m_content_library{content_library}
{
    ERHE_PROFILE_FUNCTION();

    m_scene = std::make_unique<Scene>(scene_message_bus, name, this);
    //m_scene->enable_flag_bits(erhe::Item_flags::invisible_parent);
    m_layers.add_layers_to_scene(*m_scene.get());

    // Layer configuration
    using std::make_shared;
    using std::make_unique;
    using erhe::scene::Node;

    //m_scene->enable_flag_bits(erhe::Item_flags::show_in_ui);
    m_scene->get_root_node()->enable_flag_bits(erhe::Item_flags::invisible_parent);
    m_physics_world  = erhe::physics::IWorld::create_unique();
    m_physics_world->set_on_body_activated(
        [this](erhe::physics::IRigid_body* rigid_body) {
            ERHE_VERIFY(rigid_body != nullptr);
            if (rigid_body->get_motion_mode() != erhe::physics::Motion_mode::e_dynamic) {
                return;
            }
            void* owner = rigid_body->get_owner();
            Node_physics* node_physics = reinterpret_cast<Node_physics*>(owner);
            if (node_physics == nullptr) {
                return;
            }
            erhe::scene::Node* node = node_physics->get_node();
            if (node == nullptr) {
                return;
            }
            node->enable_flag_bits(erhe::Item_flags::no_transform_update);
        }
    );
    m_physics_world->set_on_body_deactivated(
        [this](erhe::physics::IRigid_body* rigid_body) {
            ERHE_VERIFY(rigid_body != nullptr);
            //if (rigid_body->get_motion_mode() != erhe::physics::Motion_mode::e_dynamic) {
            //    return;
            //}
            void* owner = rigid_body->get_owner();
            Node_physics* node_physics = reinterpret_cast<Node_physics*>(owner);
            if (node_physics == nullptr) {
                return;
            }
            erhe::scene::Node* node = node_physics->get_node();
            if (node == nullptr) {
                return;
            }
            node->disable_flag_bits(erhe::Item_flags::no_transform_update);
        }
    );

    m_raytrace_scene = erhe::raytrace::IScene::create_unique("rt_root_scene");

    if (editor_scenes != nullptr) {
        register_to_editor_scenes(*editor_scenes);
    }

    if ((imgui_renderer != nullptr) && (imgui_windows  != nullptr) && (editor_context != nullptr)) {
        m_content_library_tree_window = std::make_shared<Item_tree_window>(
            *imgui_renderer,
            *imgui_windows,
            *editor_context,
            "Content Library",
            "Content_library",
            m_content_library->root
        );
        m_content_library_tree_window->set_item_filter(
            erhe::Item_filter{
                .require_all_bits_set           = 0,
                .require_at_least_one_bit_set   = 0,
                .require_all_bits_clear         = 0,
                .require_at_least_one_bit_clear = 0
            }
        );
    }

    if (editor_message_bus != nullptr) {
        editor_message_bus->add_receiver(
            [this](Editor_message& message) {
                using namespace erhe::bit;
                if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_selection)) {
                    for (const auto& item : message.no_longer_selected) {
                        if (item->get_item_host() != this) {
                            continue;
                        }
                        const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
                        if (!node) {
                            continue;
                        }
                        const auto& node_physics = get_node_physics(node.get());
                        if (!node_physics) {
                            continue;
                        }
                        auto* rigid_body = node_physics->get_rigid_body();
                        if (rigid_body == nullptr) {
                            continue;
                        }
                        log_physics->trace("release physics: {}", node->describe());
                        rigid_body->set_motion_mode(node_physics->physics_motion_mode);
                        rigid_body->end_move       (); // allows sleeping
                        const auto i = std::remove(m_physics_disabled_nodes.begin(), m_physics_disabled_nodes.end(), item);
                        if (i == m_physics_disabled_nodes.end()) {
                            log_physics->error("node {} not in physics disabled nodes", item->get_name());
                        } else {
                            m_physics_disabled_nodes.erase(i, m_physics_disabled_nodes.end());
                        }
                    }

                    for (const auto& item : message.newly_selected) {
                        if (item->get_item_host() != this) {
                            continue;
                        }
                        const auto node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
                        if (!node) {
                            continue;
                        }
                        const auto node_physics = get_node_physics(node.get());
                        if (!node_physics) {
                            continue;
                        }
                        auto* rigid_body = node_physics->get_rigid_body();
                        if (rigid_body == nullptr) {
                            continue;
                        }
                        log_physics->trace("acquire physics: {}", node->describe());
                        node_physics->physics_motion_mode = rigid_body->get_motion_mode();
                        rigid_body->set_motion_mode(erhe::physics::Motion_mode::e_kinematic_physical);
                        rigid_body->begin_move();

#ifndef NDEBUG
                        const auto i = std::find(m_physics_disabled_nodes.begin(), m_physics_disabled_nodes.end(), item);
                        if (i != m_physics_disabled_nodes.end()) {
                            log_physics->error("node {} already in physics disabled nodes", item->get_name());
                        } else
#endif
                            m_physics_disabled_nodes.push_back(item);
                        }
                }
            }
        );
    }
}

Scene_root::~Scene_root() noexcept
{
    if (m_is_registered) { // && (m_editor_scenes != nullptr)) {
        unregister_from_editor_scenes(*m_editor_scenes);
    }
}

auto custom_isprint(const char c) -> bool
{
    return (c >= 32 && c <= 126);
}

auto custom_isalnum(const char c) -> bool
{
    if (c >= '0' && c <= '9') return true;
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= 'a' && c <= 'z') return true;
    return false;
}

void sanitize(std::string& s)
{
    s.erase(
        std::remove_if(
            s.begin(), s.end(), [](const char c) {
                return !custom_isprint(c);
            }
        ),
        s.end()
    );
    std::replace_if(s.begin(), s.end(), [](const char c){ return !custom_isalnum(c); }, '_');
}

auto Scene_root::make_browser_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              context,
    Editor_settings&             editor_settings
) -> std::shared_ptr<Item_tree_window>
{
    std::string ini_label = m_scene->get_name();
    sanitize(ini_label);
    m_node_tree_window = std::make_shared<Item_tree_window>(
        imgui_renderer,
        imgui_windows,
        context,
        m_scene->get_name(),
        fmt::format("scene_node_tree_{}", ini_label),
        m_scene->get_root_node()
    );
    m_node_tree_window->set_item_filter(
        editor_settings.node_tree_show_all
            ? erhe::Item_filter{
                .require_all_bits_set           = 0,
                .require_at_least_one_bit_set   = 0,
                .require_all_bits_clear         = 0,//erhe::Item_flags::tool | erhe::Item_flags::brush,
                .require_at_least_one_bit_clear = 0
            }
            : erhe::Item_filter{
                .require_all_bits_set           = erhe::Item_flags::show_in_ui,
                .require_at_least_one_bit_set   = 0,
                .require_all_bits_clear         = 0, //erhe::Item_flags::tool | erhe::Item_flags::brush,
                .require_at_least_one_bit_clear = 0
            }
    );
    m_node_tree_window->set_item_callback(
        [this](const std::shared_ptr<erhe::Item_base>& item) {
            return ImGui::IsDragDropActive() && m_node_tree_window->drag_and_drop_target(item);
        }
    );
    m_node_tree_window->set_hover_callback(
        [this, &context]() {
            context.editor_message_bus->send_message(
                Editor_message{
                    .update_flags = Message_flag_bit::c_flag_bit_hover_scene_item_tree,
                    .scene_root   = this
                }
            );
        }
    );
    return m_node_tree_window;
}

void Scene_root::remove_browser_window()
{
    m_node_tree_window.reset();
}

void Scene_root::register_to_editor_scenes(Editor_scenes& editor_scenes)
{
    ERHE_VERIFY(m_is_registered == false);
    ERHE_VERIFY(m_editor_scenes == nullptr);
    m_editor_scenes = &editor_scenes;
    editor_scenes.register_scene_root(this);
    m_is_registered = true;
}

void Scene_root::unregister_from_editor_scenes(Editor_scenes& editor_scenes)
{
    ERHE_VERIFY(m_is_registered == true);
    ERHE_VERIFY(m_editor_scenes == &editor_scenes);
    m_editor_scenes = nullptr;
    editor_scenes.unregister_scene_root(this);
    m_is_registered = false;
}

auto Scene_root::get_host_name() const -> const char*
{
    return "Scene_root";
}

auto Scene_root::get_hosted_scene() -> Scene*
{
    return m_scene.get();
}

void Scene_root::register_node(const std::shared_ptr<erhe::scene::Node>& node)
{
    if (m_scene) {
        m_scene->register_node(node);
    }
}

void Scene_root::unregister_node(const std::shared_ptr<erhe::scene::Node>& node)
{
    if (m_scene) {
        m_scene->unregister_node(node);
    }
}

void Scene_root::register_camera(const std::shared_ptr<erhe::scene::Camera>& camera)
{
    if (m_scene) {
        m_scene->register_camera(camera);
    }
}
void Scene_root::unregister_camera(const std::shared_ptr<erhe::scene::Camera>& camera)
{
    if (m_scene) {
        m_scene->unregister_camera(camera);
    }
}

auto Scene_root::get_node_rt_mask(erhe::scene::Node* node) -> uint32_t
{
    uint32_t mask = 0;
    if (node != nullptr) {
        for (const auto& node_attachment : node->get_attachments()) {
            mask = mask | raytrace_node_mask(*node_attachment.get());
        }
        log_raytrace->info("RT node attach to {}, mask = {}", node->get_name(), m_scene->get_name(), mask);
    }

    return mask;
}

void Scene_root::register_mesh(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    ERHE_VERIFY(mesh);

    log_scene->info("Registering Mesh '{}' into scene", mesh->get_name());

    mesh->attach_rt_to_scene(m_raytrace_scene.get());
    mesh->set_rt_mask(get_node_rt_mask(mesh->get_node())); // TODO If scene changes, the mesh/node masks need to be updated somehow

    if (m_scene) {
        m_scene->register_mesh(mesh);
    }

    if (mesh->skin) {
        register_skin(mesh->skin);
    }

    if (is_rendertarget(mesh)) {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_rendertarget_meshes_mutex};
        m_rendertarget_meshes.push_back(std::dynamic_pointer_cast<Rendertarget_mesh>(mesh));
    }

    // Make sure materials are in the material library
    auto& material_library = content_library()->materials;
    for (const auto& primitive : mesh->get_primitives()) {
        if (!primitive.material) {
            continue;
        }
        material_library->add(primitive.material);
    }
}

void Scene_root::unregister_mesh(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    ERHE_VERIFY(mesh);

    log_scene->info("Unregistering Mesh '{}' from scene", mesh->get_name());

    mesh->detach_rt_from_scene(); //m_raytrace_scene.get());

    if (is_rendertarget(mesh)) {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_rendertarget_meshes_mutex};
        const auto rendertarget = std::dynamic_pointer_cast<Rendertarget_mesh>(mesh);
        const auto i = std::remove(m_rendertarget_meshes.begin(), m_rendertarget_meshes.end(), rendertarget);
        if (i == m_rendertarget_meshes.end()) {
            log_scene->error("rendertarget mesh {} not in scene root", rendertarget->get_name());
        } else {
            m_rendertarget_meshes.erase(i, m_rendertarget_meshes.end());
        }
    }

    if (mesh->skin) {
        unregister_skin(mesh->skin);
    }

    if (m_scene) {
        m_scene->unregister_mesh(mesh);
    }

    // TODO reference count? Remove materials from material library
    // auto& material_library = content_library()->materials;
    // material_library.remove(m_material);
}

void Scene_root::register_skin(const std::shared_ptr<erhe::scene::Skin>& skin)
{
    if (m_scene) {
        m_scene->register_skin(skin);
    }
}

void Scene_root::unregister_skin(const std::shared_ptr<erhe::scene::Skin>& skin)
{
    if (m_scene) {
        m_scene->unregister_skin(skin);
    }
}

void Scene_root::register_light(const std::shared_ptr<erhe::scene::Light>& light)
{
    if (m_scene) {
        m_scene->register_light(light);
    }
}

void Scene_root::unregister_light(const std::shared_ptr<erhe::scene::Light>& light)
{
    if (m_scene) {
        m_scene->unregister_light(light);
    }
}

void Scene_root::register_node_physics(const std::shared_ptr<Node_physics>& node_physics)
{
    if (!m_physics_world) {
        return;
    }

#ifndef NDEBUG
    const auto i = std::find(m_node_physics.begin(), m_node_physics.end(), node_physics);
    if (i != m_node_physics.end()) {
        auto* node = node_physics->get_node();
        log_physics->error("Node_physics for '{}' already in Scene_root", (node != nullptr) ? node->get_name().c_str() : "");
    } else
#endif
    {
        m_node_physics.push_back(node_physics);
        m_node_physics_sorted = false;
    }

    node_physics->set_physics_world(m_physics_world.get());
    erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
    if (rigid_body != nullptr) {
        m_physics_world->add_rigid_body(node_physics->get_rigid_body());
    }
}

void Scene_root::unregister_node_physics(const std::shared_ptr<Node_physics>& node_physics)
{
    if (!m_physics_world) {
        return;
    }

    const auto i = std::remove(
        m_node_physics.begin(),
        m_node_physics.end(),
        node_physics
    );
    if (i == m_node_physics.end()) {
        auto* node = node_physics->get_node();
        log_physics->error("Node_physics for '{}' not in Scene_root", (node != nullptr) ? node->get_name().c_str() : "");
    } else {
        m_node_physics.erase(i, m_node_physics.end());
        m_node_physics_sorted = false;
    }

    erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
    if (rigid_body != nullptr) {
        m_physics_world->remove_rigid_body(node_physics->get_rigid_body());
    }
    node_physics->set_physics_world(nullptr);
}

void Scene_root::before_physics_simulation_steps()
{
    for (const auto& node_physics : m_node_physics) {
        auto* rigid_body = node_physics->get_rigid_body();
        if (rigid_body == nullptr) {
            continue;
        }
        node_physics->before_physics_simulation();
    }
}

void Scene_root::update_physics_simulation_fixed_step(const double dt)
{
    if (m_physics_world) {
        m_physics_world->update_fixed_step(dt);
    }
}

void Scene_root::after_physics_simulation_steps()
{
    if (!m_physics_world) {
        return;
    }

    // Sort nodes, so that parent transforms are updated before child nodes
    if (!m_node_physics_sorted) {
        std::sort(
            m_node_physics.begin(),
            m_node_physics.end(),
            [](const auto& lhs, const auto& rhs) -> bool {
                erhe::scene::Node* lhs_node = lhs->get_node();
                erhe::scene::Node* rhs_node = rhs->get_node();
                if ((lhs_node == nullptr) || (rhs_node == nullptr)) {
                    return true;
                }
                return lhs_node->get_depth() < rhs_node->get_depth();
            }
        );
        m_node_physics_sorted = true;
    }

    for (const auto& node_physics : m_node_physics) {
        auto* rigid_body = node_physics->get_rigid_body();
        if (rigid_body) {
            if (rigid_body->is_active()) {
                node_physics->after_physics_simulation();
            }
        }
    }
}

auto Scene_root::layers() -> Scene_layers&
{
    return m_layers;
}

auto Scene_root::layers() const -> const Scene_layers&
{
    return m_layers;
}

auto Scene_root::get_physics_world() -> erhe::physics::IWorld&
{
    ERHE_VERIFY(m_physics_world);
    return *m_physics_world.get();
}

auto Scene_root::get_raytrace_scene() -> erhe::raytrace::IScene&
{
    ERHE_VERIFY(m_raytrace_scene);
    return *m_raytrace_scene.get();
}

auto Scene_root::get_scene() -> erhe::scene::Scene&
{
    ERHE_VERIFY(m_scene);
    return *m_scene.get();
}

auto Scene_root::get_scene() const -> const erhe::scene::Scene&
{
    ERHE_VERIFY(m_scene);
    return *m_scene.get();
}

auto Scene_root::get_name() const -> const std::string&
{
    ERHE_VERIFY(m_scene);
    return m_scene->get_name();
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
    if (nullptr_option || (selected_camera == nullptr)) {
        names.push_back("(none)");
        cameras.push_back(nullptr);
    }
    const auto& scene_cameras = get_scene().get_cameras();
    for (const auto& camera : scene_cameras) {
        names.push_back(camera->get_name().c_str());
        cameras.push_back(camera.get());
        if (selected_camera == camera.get()) {
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
    if (camera_changed) {
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
    if (nullptr_option || (selected_camera == nullptr)) {
        names.push_back("(none)");
        cameras.push_back(nullptr);
    }
    const auto& scene_cameras = get_scene().get_cameras();
    for (const auto& camera : scene_cameras) {
        names.push_back(camera->get_name().c_str());
        cameras.push_back(camera);
        if (selected_camera == camera) {
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
    if (camera_changed) {
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
    if (nullptr_option || selected_camera.expired()) {
        names.push_back("(none)");
        cameras.push_back({});
    }
    const auto& scene_cameras = get_scene().get_cameras();
    for (const auto& camera : scene_cameras) {
        names.push_back(camera->get_name().c_str());
        cameras.push_back(camera);
        if (selected_camera.lock() == camera) {
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
    if (camera_changed) {
        selected_camera = cameras[selected_camera_index];
    }
    return camera_changed;
}

#endif

namespace
{

[[nodiscard]] auto sort_value(const Light::Type light_type) -> int
{
    switch (light_type) {
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
    [[nodiscard]] inline auto operator()(const std::shared_ptr<Light>& lhs, const std::shared_ptr<Light>& rhs) -> bool
    {
        return sort_value(lhs->type) < sort_value(rhs->type);
    }
};

}

void Scene_root::sort_lights()
{
    std::sort(m_layers.light()->lights.begin(), m_layers.light()->lights.end(), Light_comparator());
}

void Scene_root::update_pointer_for_rendertarget_meshes(Scene_view* scene_view)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock(m_rendertarget_meshes_mutex);

    for (const auto& rendertarget_mesh : m_rendertarget_meshes) {
        rendertarget_mesh->update_pointer(scene_view);
    }
}

auto Scene_root::content_library() const -> std::shared_ptr<Content_library>
{
    return m_content_library;
}

void Scene_root::sanity_check()
{
    m_scene->sanity_check();
}

void Scene_root::imgui()
{
    ImGui::Text("Scene_root %s disabled items:", get_name().c_str());
    for (const auto& item : m_physics_disabled_nodes) {
        ImGui::BulletText("%s", item->describe().c_str());
    }
}

} // namespace editor
