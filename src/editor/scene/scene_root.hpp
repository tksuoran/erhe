#pragma once

#include "scene/collision_generator.hpp"
#include "scene/frame_controller.hpp"

#include "erhe/components/component.hpp"
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
class Scene_manager;
class Scene_root;
class Brush;


class Camera_rig
{
public:
    Camera_rig(
        Scene_root&                          scene_root,
        std::shared_ptr<erhe::scene::Camera> camera
    );

    std::shared_ptr<erhe::scene::Camera> position;
    std::shared_ptr<erhe::scene::Camera> position_fps_heading;           // parent = position
    std::shared_ptr<erhe::scene::Camera> position_fps_heading_elevation; // parent = fps_position_heading
    std::shared_ptr<erhe::scene::Camera> position_free;                  // parent = position
};

class Scene_root
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Scene_root"};
    
    Scene_root ();
    ~Scene_root() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    template <typename ...Args>
    auto make_material(std::string_view name, Args&& ...args)
    -> std::shared_ptr<erhe::primitive::Material>
    {
        auto material = std::make_shared<erhe::primitive::Material>(name, std::forward<Args>(args)...);
        std::lock_guard<std::mutex> lock(m_materials_mutex);
        material->index = m_materials.size();
        m_materials.push_back(material);
        return material;
    }


    auto brush_layer        () const -> std::shared_ptr<erhe::scene::Mesh_layer>;
    auto content_layer      () const -> std::shared_ptr<erhe::scene::Mesh_layer>;
    auto controller_layer   () const -> std::shared_ptr<erhe::scene::Mesh_layer>;
    auto tool_layer         () const -> std::shared_ptr<erhe::scene::Mesh_layer>;
    auto light_layer        () const -> std::shared_ptr<erhe::scene::Light_layer>;
    auto all_layers         () -> std::vector<const erhe::scene::Mesh_layer*>& { return m_all_layers; }
    auto content_fill_layers() -> std::vector<const erhe::scene::Mesh_layer*>& { return m_content_fill_layers; }
    auto content_layers     () -> std::vector<const erhe::scene::Mesh_layer*>& { return m_content_layers; }
    auto tool_layers        () -> std::vector<const erhe::scene::Mesh_layer*>& { return m_tool_layers; }
    auto brush_layers       () -> std::vector<const erhe::scene::Mesh_layer*>& { return m_brush_layers; }

    auto make_mesh_node(
        const std::string_view                                      name,
        const std::shared_ptr<erhe::primitive::Primitive_geometry>& primitive_geometry,
        const std::shared_ptr<erhe::primitive::Material>&           material,
        erhe::scene::Node*                                          parent   = nullptr,
        const glm::vec3                                             position = glm::vec3{0.0f}
    ) -> std::shared_ptr<erhe::scene::Mesh>;

    auto make_mesh_node(
        const std::string_view                                      name,
        const std::shared_ptr<erhe::primitive::Primitive_geometry>& primitive_geometry,
        const std::shared_ptr<erhe::primitive::Material>&           material,
        erhe::scene::Mesh_layer&                                    mesh_layer,
        erhe::scene::Node*                                          parent   = nullptr,
        const glm::vec3                                             position = glm::vec3{0.0f}
    ) -> std::shared_ptr<erhe::scene::Mesh>;

    auto add          (const std::shared_ptr<erhe::primitive::Material>& material) -> std::shared_ptr<erhe::primitive::Material>;
    auto add          (const std::shared_ptr<erhe::scene::Mesh>&         mesh)     -> std::shared_ptr<erhe::scene::Mesh>;
    auto add          (const std::shared_ptr<erhe::scene::Light>&        light)    -> std::shared_ptr<erhe::scene::Light>;
    auto materials    () -> std::vector<std::shared_ptr<erhe::primitive::Material>>&;
    auto materials    () const -> const std::vector<std::shared_ptr<erhe::primitive::Material>>&;
    auto physics_world() -> erhe::physics::IWorld&;
    auto scene        () -> erhe::scene::Scene&;
    auto content_layer() -> erhe::scene::Mesh_layer&;

private:
    std::mutex                                              m_materials_mutex;
    std::vector<std::shared_ptr<erhe::primitive::Material>> m_materials;

    std::unique_ptr<erhe::physics::IWorld>    m_physics_world;
    std::unique_ptr<erhe::scene::Scene>       m_scene;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_content_layer;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_controller_layer;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_tool_layer;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_brush_layer;
    std::shared_ptr<erhe::scene::Light_layer> m_light_layer;
    std::shared_ptr<erhe::scene::Camera>      m_camera;
    std::shared_ptr<Frame_controller>         m_camera_controls;

    std::vector<const erhe::scene::Mesh_layer*> m_all_layers;
    std::vector<const erhe::scene::Mesh_layer*> m_content_fill_layers;
    std::vector<const erhe::scene::Mesh_layer*> m_content_layers;
    std::vector<const erhe::scene::Mesh_layer*> m_tool_layers;
    std::vector<const erhe::scene::Mesh_layer*> m_brush_layers;
};

} // namespace editor
