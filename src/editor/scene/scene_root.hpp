#pragma once

#include "app_message.hpp"
#include "scene/generated/scene_settings.hpp"

#include "erhe_message_bus/message_bus.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/scene_host.hpp"

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

class btCollisionShape;

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::graphics {
    class Buffer;
    class Buffer_transfer_queue;
    class Vertex_format;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace erhe::physics {
    class IWorld;
    class Trigger_event;
}
namespace erhe::primitive {
    class Material;
    class Buffer_mesh;
    class Primitive;
}
namespace erhe::raytrace {
    class IScene;
}
namespace erhe::scene {
    using Layer_id = uint64_t;
    class Camera;
    class Light;
    class Light_layer;
    class Mesh;
    class Mesh_layer;
    class Mesh_raytrace;
    class Message_bus;
    class Node;
    class Scene;
}

namespace editor {

class Content_library;
class App_context;
class App_message_bus;
class App_scenes;
class App_settings;
class Item_tree_window;
class Node_joint;
class Node_physics;
class Raytrace_primitive;
class Rendertarget_mesh;
class Scene_root;
class Scene_view;
class Viewport_scene_view;

class Mesh_layer_id
{
public:
    static constexpr erhe::scene::Layer_id brush        = 0;
    static constexpr erhe::scene::Layer_id content      = 1;
    static constexpr erhe::scene::Layer_id sky          = 2;
    static constexpr erhe::scene::Layer_id controller   = 3;
    static constexpr erhe::scene::Layer_id tool         = 4;
    static constexpr erhe::scene::Layer_id rendertarget = 5;
};

class Scene_layers
{
public:
    Scene_layers();

    void add_layers_to_scene(erhe::scene::Scene& scene);

    [[nodiscard]] auto brush       () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto content     () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto controller  () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto tool        () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto rendertarget() const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto light       () const -> erhe::scene::Light_layer*;
    [[nodiscard]] auto mesh_layers () const -> std::array<erhe::scene::Mesh_layer*, 5>;

private:
    std::shared_ptr<erhe::scene::Mesh_layer>  m_content;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_controller;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_tool;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_brush;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_rendertarget;
    std::shared_ptr<erhe::scene::Light_layer> m_light;
};

class Scene_root
    : public std::enable_shared_from_this<Scene_root>
    , public erhe::scene::Scene_host
{
public:
    Scene_root(
        App_message_bus*                        app_message_bus,
        const std::shared_ptr<Content_library>& content_library,
        std::string_view                        name,
        bool                                    enable_physics
    );
    ~Scene_root() noexcept override;

    // Implements erhe::Item_host
    auto get_host_name() const -> const char* override;

    // Public API
    auto make_browser_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 context,
        App_settings&                app_settings
    ) -> std::shared_ptr<Item_tree_window>;
    void remove_browser_window();

    void register_to_editor_scenes    (App_scenes& app_scenes);
    void unregister_from_editor_scenes(App_scenes& app_scenes);
    // Clears this scene_root's registration state without touching the
    // App_scenes registry. Called by ~App_scenes while it tears down its
    // own list, so that the later ~Scene_root does not try to unregister
    // from a registry that has already released it.
    void detach_from_editor_scenes    (App_scenes& app_scenes);

    void register_node    (const std::shared_ptr<erhe::scene::Node>&   node)   override;
    void unregister_node  (const std::shared_ptr<erhe::scene::Node>&   node)   override;
    void register_camera  (const std::shared_ptr<erhe::scene::Camera>& camera) override;
    void unregister_camera(const std::shared_ptr<erhe::scene::Camera>& camera) override;
    void register_mesh    (const std::shared_ptr<erhe::scene::Mesh>&   mesh)   override;
    void unregister_mesh  (const std::shared_ptr<erhe::scene::Mesh>&   mesh)   override;
    void register_skin    (const std::shared_ptr<erhe::scene::Skin>&   skin)   override;
    void unregister_skin  (const std::shared_ptr<erhe::scene::Skin>&   skin)   override;
    void register_light   (const std::shared_ptr<erhe::scene::Light>&  light)  override;
    void unregister_light (const std::shared_ptr<erhe::scene::Light>&  light)  override;
    auto get_hosted_scene () -> erhe::scene::Scene* override;

    void begin_mesh_rt_update(const std::shared_ptr<erhe::scene::Mesh>& mesh);
    void end_mesh_rt_update  (const std::shared_ptr<erhe::scene::Mesh>& mesh);

    void register_node_physics  (const std::shared_ptr<Node_physics>& node_physics);
    void unregister_node_physics(const std::shared_ptr<Node_physics>& node_physics);

    // Node_joint bookkeeping. All attached joints stay registered; a joint
    // without a live constraint is pending. register_node_physics() retries
    // pending joints after adding the new rigid body to the world, and
    // unregister_node_physics() tears down constraints referencing the
    // departing body (returning those joints to the pending state).
    void register_node_joint    (const std::shared_ptr<Node_joint>& node_joint);
    void unregister_node_joint  (const std::shared_ptr<Node_joint>& node_joint);

    void before_physics_simulation_steps     ();
    void update_physics_simulation_fixed_step(double dt);
    void after_physics_simulation_steps      ();

    [[nodiscard]] auto layers            () -> Scene_layers&;
    [[nodiscard]] auto layers            () const -> const Scene_layers&;
    [[nodiscard]] auto has_physics_world () const -> bool;
    [[nodiscard]] auto get_physics_world () -> erhe::physics::IWorld&;

    // Bounded log of recent sensor (trigger) overlap events, appended by the
    // physics world trigger callbacks at the end of update_fixed_step() and
    // shown in the Physics window. Lines are preformatted at event time;
    // the counter keeps counting after old lines fall out of the log.
    [[nodiscard]] auto get_trigger_event_log  () const -> const std::deque<std::string>&;
    [[nodiscard]] auto get_trigger_event_count() const -> uint64_t;
    void clear_trigger_event_log();
    [[nodiscard]] auto get_raytrace_scene() -> erhe::raytrace::IScene&;
    [[nodiscard]] auto get_scene         () -> erhe::scene::Scene&;
    [[nodiscard]] auto get_scene         () const -> const erhe::scene::Scene&;
    // Stable shared_ptr to the Scene item, used to make the Scene selectable and
    // to show it as the top row of the Hierarchy window (issue #240).
    [[nodiscard]] auto get_scene_item    () -> std::shared_ptr<erhe::scene::Scene>;
    [[nodiscard]] auto get_name          () const -> const std::string&;

    // Per-scene setting overrides (issue #239). Each field is an optional; a
    // disengaged optional means "use the editor-global default". Effective values
    // are resolved by the helpers in scene/scene_settings_resolve.hpp.
    [[nodiscard]] auto get_scene_settings()       -> Scene_settings&;
    [[nodiscard]] auto get_scene_settings() const -> const Scene_settings&;

    void imgui();

    auto camera_combo(const char* label, erhe::scene::Camera*& camera, bool nullptr_option = false) const -> bool;
    auto camera_combo(const char* label, std::shared_ptr<erhe::scene::Camera>& selected_camera, bool nullptr_option = false) const -> bool;
    auto camera_combo(const char* label, std::weak_ptr<erhe::scene::Camera>& selected_camera, bool nullptr_option = false) const -> bool;
    void sort_lights();

    [[nodiscard]] auto get_content_library() const -> std::shared_ptr<Content_library>;

    void update_pointer_for_rendertarget_meshes(Scene_view* scene_view);
    void sanity_check();

private:
    void add_trigger_event(bool enter, const erhe::physics::Trigger_event& event);

    [[nodiscard]] auto get_node_rt_mask(erhe::scene::Node* node) -> uint32_t;
    // Returns the raytrace IInstance mask for a mesh. Skinned meshes get
    // the Raytrace_node_mask::skinned bit in lieu of the role bits the
    // node would otherwise contribute, so picking-tool rays (which use
    // role bits) skip them and the ID renderer handles them instead. See
    // Raytrace_node_mask::skinned.
    [[nodiscard]] auto get_mesh_rt_mask(erhe::scene::Mesh* mesh) -> uint32_t;

    erhe::message_bus::Subscription<Selection_message> m_selection_subscription;

    static int s_browser_window_count;

    // Live longest
    mutable ERHE_PROFILE_MUTEX(std::mutex, m_mutex);
    ERHE_PROFILE_MUTEX        (std::mutex, m_rendertarget_meshes_mutex);

    App_scenes*                                     m_app_scenes{nullptr};
    std::shared_ptr<Content_library>                m_content_library;
    bool                                            m_is_registered{false};

    // Must live longer than m_scene for example
    bool                                            m_node_physics_sorted{false};
    std::vector<std::shared_ptr<Node_physics>>      m_node_physics;
    std::vector<std::shared_ptr<Node_joint>>        m_node_joints;
    std::vector<std::shared_ptr<Rendertarget_mesh>> m_rendertarget_meshes;

    std::vector<std::shared_ptr<erhe::Item_base>>   m_physics_disabled_nodes;

    std::unique_ptr<erhe::physics::IWorld>          m_physics_world;
    std::unique_ptr<erhe::raytrace::IScene>         m_raytrace_scene;

    static constexpr std::size_t s_max_trigger_event_log_entries = 100;
    std::deque<std::string>                         m_trigger_event_log;
    uint64_t                                        m_trigger_event_counter{0};

    std::shared_ptr<erhe::scene::Scene>             m_scene;
    Scene_layers                                    m_layers;
    Scene_settings                                  m_scene_settings;

    std::shared_ptr<Item_tree_window>               m_node_tree_window;
};

}
