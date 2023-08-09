#pragma once


#include "scene/collision_generator.hpp"
#include "scene/frame_controller.hpp"

#include "erhe/commands/command.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/message_bus/message_bus.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/primitive/format_info.hpp"
#include "erhe/scene/scene_host.hpp"
#include "erhe/scene/scene_message.hpp"
#include "erhe/scene/scene_message_bus.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <map>
#include <memory>
#include <mutex>
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
}
namespace erhe::primitive {
    class Material;
    class Geometry_mesh;
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
    class Message_bus;
    class Node;
    class Scene;
    class Scene_message_bus;
}

namespace editor
{

class Content_library;
class Editor_context;
class Editor_message_bus;
class Editor_scenes;
class Editor_settings;
class Item_tree_window;
class Node_physics;
class Node_raytrace;
class Raytrace_primitive;
class Rendertarget_mesh;
class Scene_root;
class Scene_view;
class Viewport_window;

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
    explicit Scene_layers();

    void add_layers_to_scene(erhe::scene::Scene& scene);

    [[nodiscard]] auto get_layer_by_id(erhe::scene::Layer_id id) const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto brush          () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto content        () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto controller     () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto tool           () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto rendertarget   () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto light          () const -> erhe::scene::Light_layer*;

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
        erhe::scene::Scene_message_bus&         scene_message_bus,
        Editor_message_bus*                     editor_message_bus,
        Editor_scenes*                          editor_scenes,
        const std::shared_ptr<Content_library>& content_library,
        const std::string_view                  name
    );
    ~Scene_root() noexcept override;

    // Implements erhe::Item_host
    [[nodiscard]] auto get_host_name() const -> const char* override;

    // Public API
    auto make_browser_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              context,
        Editor_settings&             editor_settings
    ) -> std::shared_ptr<Item_tree_window>;
    void remove_browser_window();

    void register_to_editor_scenes    (Editor_scenes& editor_scenes);
    void unregister_from_editor_scenes(Editor_scenes& editor_scenes);

    void register_node          (const std::shared_ptr<erhe::scene::Node>&   node)   override;
    void unregister_node        (const std::shared_ptr<erhe::scene::Node>&   node)   override;
    void register_camera        (const std::shared_ptr<erhe::scene::Camera>& camera) override;
    void unregister_camera      (const std::shared_ptr<erhe::scene::Camera>& camera) override;
    void register_mesh          (const std::shared_ptr<erhe::scene::Mesh>&   mesh)   override;
    void unregister_mesh        (const std::shared_ptr<erhe::scene::Mesh>&   mesh)   override;
    void register_skin          (const std::shared_ptr<erhe::scene::Skin>&   skin)   override;
    void unregister_skin        (const std::shared_ptr<erhe::scene::Skin>&   skin)   override;
    void register_light         (const std::shared_ptr<erhe::scene::Light>&  light)  override;
    void unregister_light       (const std::shared_ptr<erhe::scene::Light>&  light)  override;
    auto get_hosted_scene       () -> erhe::scene::Scene* override;

    void register_node_physics  (const std::shared_ptr<Node_physics>& node_physics);
    void unregister_node_physics(const std::shared_ptr<Node_physics>& node_physics);

    void register_node_raytrace  (const std::shared_ptr<Node_raytrace>& node_raytrace);
    void unregister_node_raytrace(const std::shared_ptr<Node_raytrace>& node_raytrace);

    void before_physics_simulation_steps     ();
    void update_physics_simulation_fixed_step(double dt);
    void after_physics_simulation_steps      ();

    [[nodiscard]] auto layers            () -> Scene_layers&;
    [[nodiscard]] auto layers            () const -> const Scene_layers&;
    [[nodiscard]] auto get_physics_world () -> erhe::physics::IWorld&;
    [[nodiscard]] auto get_raytrace_scene() -> erhe::raytrace::IScene&;
    [[nodiscard]] auto get_scene         () -> erhe::scene::Scene&;
    [[nodiscard]] auto get_scene         () const -> const erhe::scene::Scene&;
    [[nodiscard]] auto get_name          () const -> const std::string&;

    void imgui();

    auto camera_combo(
        const char*           label,
        erhe::scene::Camera*& camera,
        bool                  nullptr_option = false
    ) const -> bool;

    auto camera_combo(
        const char*                           label,
        std::shared_ptr<erhe::scene::Camera>& selected_camera,
        bool                                  nullptr_option = false
    ) const -> bool;

    auto camera_combo(
        const char*                         label,
        std::weak_ptr<erhe::scene::Camera>& selected_camera,
        bool                                nullptr_option = false
    ) const -> bool;

    void sort_lights();

    [[nodiscard]] auto content_library() const -> std::shared_ptr<Content_library>;

    void update_pointer_for_rendertarget_meshes(Scene_view* scene_view);

    void sanity_check();

private:
    // Live longest
    mutable std::mutex                              m_mutex;
    std::mutex                                      m_rendertarget_meshes_mutex;

    Editor_scenes*                                  m_editor_scenes{nullptr};
    std::shared_ptr<Content_library>                m_content_library;
    bool                                            m_is_registered{false};

    // Must live longer than m_scene for example
    bool                                            m_node_physics_sorted{false};
    std::vector<std::shared_ptr<Node_physics>>      m_node_physics;
    std::vector<std::shared_ptr<Node_raytrace>>     m_node_raytraces;
    std::vector<std::shared_ptr<Rendertarget_mesh>> m_rendertarget_meshes;

    std::vector<std::shared_ptr<erhe::Item>>        m_physics_disabled_nodes;

    std::unique_ptr<erhe::physics::IWorld>          m_physics_world;
    std::unique_ptr<erhe::raytrace::IScene>         m_raytrace_visualization_scene;

    std::unique_ptr<erhe::scene::Scene>             m_scene;
    Scene_layers                                    m_layers;

    std::shared_ptr<erhe::scene::Camera>            m_camera;
    std::shared_ptr<Frame_controller>               m_camera_controls;

    std::shared_ptr<Item_tree_window>               m_node_tree_window;
};

} // namespace editor
