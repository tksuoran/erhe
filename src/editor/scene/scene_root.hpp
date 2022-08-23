#pragma once


#include "scene/collision_generator.hpp"
#include "scene/frame_controller.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/components/components.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/primitive/format_info.hpp"
#include "erhe/gl/wrapper_enums.hpp"
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
    class Scene;
}

namespace erhe::physics
{
    class IWorld;
}

namespace editor
{

class Material_library;
class Node_physics;
class Node_raytrace;
class Raytrace_primitive;
class Rendertarget_node;
class Scene_root;
class Viewport_window;

class Instance
{
public:
    Instance(
        const std::shared_ptr<erhe::scene::Mesh>& mesh,
        const std::shared_ptr<Node_physics>&      node_physics,
        const std::shared_ptr<Node_raytrace>&     node_raytrace
    );
    ~Instance() noexcept;

    std::shared_ptr<erhe::scene::Mesh> mesh;
    std::shared_ptr<Node_physics>      node_physics;
    std::shared_ptr<Node_raytrace>     node_raytrace;
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
{
public:
    explicit Scene_root(const std::string_view& name);
    ~Scene_root() noexcept;

    // Public API
    auto create_new_camera    () -> std::shared_ptr<erhe::scene::Camera>;
    auto create_new_empty_node() -> std::shared_ptr<erhe::scene::Node>;
    auto create_new_light     () -> std::shared_ptr<erhe::scene::Light>;

    //void attach_to_selection(const std::shared_ptr<erhe::scene::Node>& node);

    void add(
        const std::shared_ptr<erhe::scene::Mesh>& mesh,
        erhe::scene::Mesh_layer*                  layer = nullptr
    );

    [[nodiscard]] auto material_library() const -> const std::shared_ptr<Material_library>&;
    [[nodiscard]] auto layers          () -> Scene_layers&;
    [[nodiscard]] auto layers          () const -> const Scene_layers&;
    [[nodiscard]] auto physics_world   () -> erhe::physics::IWorld&;
    [[nodiscard]] auto raytrace_scene  () -> erhe::raytrace::IScene&;
    [[nodiscard]] auto scene           () -> erhe::scene::Scene&;
    [[nodiscard]] auto scene           () const -> const erhe::scene::Scene&;
    [[nodiscard]] auto name            () const -> const std::string&;

    void add_instance(const Instance& instance);

    auto camera_combo(
        const char*           label,
        erhe::scene::Camera*& camera,
        const bool            nullptr_option = false
    ) const -> bool;

    void sort_lights();

    [[nodiscard]] auto create_rendertarget_node(
        const erhe::components::Components& components,
        Viewport_window&                    host_viewport_window,
        const int                           width,
        const int                           height,
        const double                        dots_per_meter
    ) -> std::shared_ptr<Rendertarget_node>;

    void update_pointer_for_rendertarget_nodes();

private:
    std::string                                     m_name;
    mutable std::mutex                              m_mutex;
    std::unique_ptr<erhe::physics::IWorld>          m_physics_world;
    std::unique_ptr<erhe::raytrace::IScene>         m_raytrace_scene;
    std::unique_ptr<erhe::scene::Scene>             m_scene;
    std::shared_ptr<erhe::scene::Camera>            m_camera;
    std::shared_ptr<Frame_controller>               m_camera_controls;
    std::shared_ptr<Material_library>               m_material_library;
    std::mutex                                      m_rendertarget_nodes_mutex;
    std::vector<std::shared_ptr<Rendertarget_node>> m_rendertarget_nodes;
    Scene_layers                                    m_layers;
};

} // namespace editor
