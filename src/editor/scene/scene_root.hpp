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
class Viewport_window;

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
        const erhe::components::Components&     components,
        const std::shared_ptr<Content_library>& content_library,
        const std::string_view                  name
    );
    ~Scene_root() noexcept override;

    // Implements Scene_host
    [[nodiscard]] auto get_scene() -> erhe::scene::Scene* override;

    [[nodiscard]] auto get_editor_message_bus() const -> std::shared_ptr<Editor_message_bus>;
    [[nodiscard]] auto layers                () -> Scene_layers&;
    [[nodiscard]] auto layers                () const -> const Scene_layers&;
    [[nodiscard]] auto physics_world         () -> erhe::physics::IWorld&;
    [[nodiscard]] auto raytrace_scene        () -> erhe::raytrace::IScene&;
    [[nodiscard]] auto scene                 () -> erhe::scene::Scene&;
    [[nodiscard]] auto scene                 () const -> const erhe::scene::Scene&;
    [[nodiscard]] auto get_name              () const -> const std::string&;

    auto camera_combo(
        const char*           label,
        erhe::scene::Camera*& camera,
        const bool            nullptr_option = false
    ) const -> bool;

    auto camera_combo(
        const char*                           label,
        std::shared_ptr<erhe::scene::Camera>& selected_camera,
        const bool                            nullptr_option = false
    ) const -> bool;

    auto camera_combo(
        const char*                         label,
        std::weak_ptr<erhe::scene::Camera>& selected_camera,
        const bool                          nullptr_option = false
    ) const -> bool;

    void sort_lights();

    [[nodiscard]] auto content_library() const -> std::shared_ptr<Content_library>;

    [[nodiscard]] auto create_rendertarget_mesh(
        const erhe::components::Components& components,
        Viewport_window&                    host_viewport_window,
        const int                           width,
        const int                           height,
        const double                        pixels_per_meter
    ) -> std::shared_ptr<Rendertarget_mesh>;

    void update_pointer_for_rendertarget_meshes();

    void sanity_check();

private:
    std::string                                     m_name;
    mutable std::mutex                              m_mutex;
    std::unique_ptr<erhe::physics::IWorld>          m_physics_world;
    std::unique_ptr<erhe::raytrace::IScene>         m_raytrace_scene;
    std::unique_ptr<erhe::scene::Scene>             m_scene;
    std::shared_ptr<erhe::scene::Camera>            m_camera;
    std::shared_ptr<Content_library>                m_content_library;
    std::shared_ptr<Editor_message_bus>             m_editor_message_bus;
    std::shared_ptr<Frame_controller>               m_camera_controls;
    std::shared_ptr<Scene_message_bus>              m_scene_message_bus;
    std::mutex                                      m_rendertarget_meshes_mutex;
    std::vector<std::shared_ptr<Rendertarget_mesh>> m_rendertarget_meshes;
    Scene_layers                                    m_layers;
};

} // namespace editor
