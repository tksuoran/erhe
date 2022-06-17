#pragma once


#include "scene/collision_generator.hpp"
#include "scene/frame_controller.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/components/components.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/primitive/buffer_info.hpp"
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
    class ICamera;
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

class Debug_draw;
class Mesh_memory;
class Node_physics;
class Node_raytrace;
class Raytrace_primitive;
class Scene_root;
class Brush;


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

class Create_new_camera_command
    : public erhe::application::Command
{
public:
    explicit Create_new_camera_command(Scene_root& scene_root)
        : Command{"Scene_root.create_new_camera"}
        , m_scene_root{scene_root}
    {
    }

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Scene_root& m_scene_root;
};

class Create_new_empty_node_command
    : public erhe::application::Command
{
public:
    explicit Create_new_empty_node_command(Scene_root& scene_root)
        : Command       {"Editor_tools.create_new_empty_node"}
        , m_scene_root{scene_root}
    {
    }

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Scene_root& m_scene_root;
};

class Create_new_light_command
    : public erhe::application::Command
{
public:
    explicit Create_new_light_command(Scene_root& scene_root)
        : Command     {"Editor_tools.create_new_light"}
        , m_scene_root{scene_root}
    {
    }

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Scene_root& m_scene_root;
};

class Scene_root
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Scene_root"};
    static constexpr uint32_t         hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Scene_root ();
    ~Scene_root() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Public API
    auto create_new_camera    () -> bool;
    auto create_new_empty_node() -> bool;
    auto create_new_light     () -> bool;

    void attach_to_selection(const std::shared_ptr<erhe::scene::Node>& node);

    template <typename ...Args>
    auto make_material(
        const std::string_view name,
        Args&& ...args
    ) -> std::shared_ptr<erhe::primitive::Material>
    {
        auto material = std::make_shared<erhe::primitive::Material>(
            name,
            std::forward<Args>(args)...
        );
        const std::lock_guard<std::mutex> lock{m_materials_mutex};

        material->index = m_materials.size();
        m_materials.push_back(material);
        return material;
    }

    void add(
        const std::shared_ptr<erhe::scene::Mesh>& mesh,
        erhe::scene::Mesh_layer*                  layer = nullptr
    );

    [[nodiscard]] auto brush_layer     () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto content_layer   () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto controller_layer() const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto tool_layer      () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto gui_layer       () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto light_layer     () const -> erhe::scene::Light_layer*;
    [[nodiscard]] auto materials       () -> std::vector<std::shared_ptr<erhe::primitive::Material>>&;
    [[nodiscard]] auto materials       () const -> const std::vector<std::shared_ptr<erhe::primitive::Material>>&;
    [[nodiscard]] auto physics_world   () -> erhe::physics::IWorld&;
    [[nodiscard]] auto raytrace_scene  () -> erhe::raytrace::IScene&;
    [[nodiscard]] auto scene           () -> erhe::scene::Scene&;
    [[nodiscard]] auto scene           () const -> const erhe::scene::Scene&;

    void add_instance(const Instance& instance);

    auto camera_combo(
        const char*            label,
        erhe::scene::ICamera*& camera,
        const bool             nullptr_option = false
    ) const -> bool;

    auto material_combo(
        const char*                                 label,
        std::shared_ptr<erhe::primitive::Material>& material,
        const bool                                  empty_option = false
    ) const -> bool;

    void sort_lights();

private:
    // Commands
    Create_new_camera_command     m_create_new_camera_command;
    Create_new_empty_node_command m_create_new_empty_node_command;
    Create_new_light_command      m_create_new_light_command;

    mutable std::mutex                                      m_materials_mutex;
    mutable std::mutex                                      m_scene_mutex;
    std::vector<std::shared_ptr<erhe::primitive::Material>> m_materials;

    std::unique_ptr<erhe::physics::IWorld>    m_physics_world;
    std::unique_ptr<erhe::raytrace::IScene>   m_raytrace_scene;
    std::unique_ptr<erhe::scene::Scene>       m_scene;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_content_layer;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_controller_layer;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_tool_layer;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_brush_layer;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_gui_layer;
    std::shared_ptr<erhe::scene::Light_layer> m_light_layer;
    std::shared_ptr<erhe::scene::Camera>      m_camera;
    std::shared_ptr<Frame_controller>         m_camera_controls;
};

} // namespace editor
