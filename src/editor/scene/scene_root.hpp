#pragma once


#include "scene/collision_generator.hpp"
#include "scene/frame_controller.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/components/components.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/message_bus/message_bus.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/primitive/format_info.hpp"
#include "erhe/scene/scene_host.hpp"
#include "erhe/scene/scene_message.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

class btCollisionShape;

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::graphics
{
    class Buffer;
    class Buffer_transfer_queue;
    class Vertex_format;
}

namespace erhe::raytrace
{
    class IScene;
}

namespace erhe::scene
{
    using Layer_id = uint64_t;
}

namespace erhe::primitive
{
    class Material;
    class Primitive_geometry;
    class Primitive;
}

namespace erhe::scene
{
    class Camera;
    class Light;
    class Light_layer;
    class Mesh;
    class Mesh_layer;
    class Message_bus;
    class Node;
    class Scene;
}

namespace erhe::physics
{
    class IWorld;
}

namespace editor
{

class Content_library;
class Editor_message_bus;
class Node_physics;
class Node_raytrace;
class Raytrace_primitive;
class Rendertarget_mesh;
class Scene_message_bus;
class Scene_root;
class Scene_view;
class Viewport_window;

class Mesh_layer_id
{
public:
    static constexpr erhe::scene::Layer_id brush        = 0;
    static constexpr erhe::scene::Layer_id content      = 1;
    static constexpr erhe::scene::Layer_id controller   = 2;
    static constexpr erhe::scene::Layer_id tool         = 3;
    static constexpr erhe::scene::Layer_id rendertarget = 4;
};

class Scene_layers
{
public:
    explicit Scene_layers(erhe::scene::Scene& scene);

    [[nodiscard]] auto brush       () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto content     () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto controller  () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto tool        () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto rendertarget() const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto light       () const -> erhe::scene::Light_layer*;

private:
    std::shared_ptr<erhe::scene::Mesh_layer>  m_content;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_controller;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_tool;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_brush;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_rendertarget;
    std::shared_ptr<erhe::scene::Light_layer> m_light;
};

class Scene_root
    : public erhe::scene::Scene_host
{
public:
    Scene_root(
        const std::shared_ptr<Content_library>& content_library,
        const std::string_view                  name
    );
    ~Scene_root() noexcept override;

    // Implements Scene_host
    [[nodiscard]] auto get_hosted_scene() -> erhe::scene::Scene*  override;
    void register_node    (const std::shared_ptr<erhe::scene::Node>&   node)   override;
    void unregister_node  (const std::shared_ptr<erhe::scene::Node>&   node)   override;
    void register_camera  (const std::shared_ptr<erhe::scene::Camera>& camera) override;
    void unregister_camera(const std::shared_ptr<erhe::scene::Camera>& camera) override;
    void register_mesh    (const std::shared_ptr<erhe::scene::Mesh>&   mesh)   override;
    void unregister_mesh  (const std::shared_ptr<erhe::scene::Mesh>&   mesh)   override;
    void register_light   (const std::shared_ptr<erhe::scene::Light>&  light)  override;
    void unregister_light (const std::shared_ptr<erhe::scene::Light>&  light)  override;

    [[nodiscard]] auto get_shared_scene() -> std::shared_ptr<erhe::scene::Scene>;
    [[nodiscard]] auto layers          () -> Scene_layers&;
    [[nodiscard]] auto layers          () const -> const Scene_layers&;
    [[nodiscard]] auto physics_world   () -> erhe::physics::IWorld&;
    [[nodiscard]] auto raytrace_scene  () -> erhe::raytrace::IScene&;
    [[nodiscard]] auto scene           () -> erhe::scene::Scene&;
    [[nodiscard]] auto scene           () const -> const erhe::scene::Scene&;
    [[nodiscard]] auto get_name        () const -> const std::string&;

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
    mutable std::mutex                              m_mutex;
    std::unique_ptr<erhe::physics::IWorld>          m_physics_world;
    std::unique_ptr<erhe::raytrace::IScene>         m_raytrace_scene;
    std::shared_ptr<erhe::scene::Scene>             m_scene;
    std::shared_ptr<erhe::scene::Camera>            m_camera;
    std::shared_ptr<Content_library>                m_content_library;
    std::shared_ptr<Frame_controller>               m_camera_controls;
    std::mutex                                      m_rendertarget_meshes_mutex;
    std::vector<std::shared_ptr<Rendertarget_mesh>> m_rendertarget_meshes;
    Scene_layers                                    m_layers;
};

} // namespace editor
