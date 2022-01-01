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
class Raytrace_primitive;
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
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});
    
    Scene_root ();
    ~Scene_root() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
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

    [[nodiscard]] auto brush_layer        () const -> std::shared_ptr<erhe::scene::Mesh_layer>;
    [[nodiscard]] auto content_layer      () const -> std::shared_ptr<erhe::scene::Mesh_layer>;
    [[nodiscard]] auto controller_layer   () const -> std::shared_ptr<erhe::scene::Mesh_layer>;
    [[nodiscard]] auto tool_layer         () const -> std::shared_ptr<erhe::scene::Mesh_layer>;
    [[nodiscard]] auto light_layer        () const -> std::shared_ptr<erhe::scene::Light_layer>;
    [[nodiscard]] auto all_layers         () -> std::vector<const erhe::scene::Mesh_layer*>& { return m_all_layers; }
    [[nodiscard]] auto content_fill_layers() -> std::vector<const erhe::scene::Mesh_layer*>& { return m_content_fill_layers; }
    [[nodiscard]] auto content_layers     () -> std::vector<const erhe::scene::Mesh_layer*>& { return m_content_layers; }
    [[nodiscard]] auto tool_layers        () -> std::vector<const erhe::scene::Mesh_layer*>& { return m_tool_layers; }
    [[nodiscard]] auto brush_layers       () -> std::vector<const erhe::scene::Mesh_layer*>& { return m_brush_layers; }
    [[nodiscard]] auto materials          () -> std::vector<std::shared_ptr<erhe::primitive::Material>>&;
    [[nodiscard]] auto materials          () const -> const std::vector<std::shared_ptr<erhe::primitive::Material>>&;
    [[nodiscard]] auto physics_world      () -> erhe::physics::IWorld&;
    [[nodiscard]] auto raytrace_scene     () -> erhe::raytrace::IScene&;
    [[nodiscard]] auto scene              () -> erhe::scene::Scene&;
    [[nodiscard]] auto scene              () const -> const erhe::scene::Scene&;
    [[nodiscard]] auto content_layer      () -> erhe::scene::Mesh_layer&;

    auto camera_combo(
        const char*            label,
        erhe::scene::ICamera*& camera,
        const bool             nullptr_option = false
    ) const -> bool;

private:
    std::mutex                                              m_materials_mutex;
    std::mutex                                              m_scene_mutex;
    std::vector<std::shared_ptr<erhe::primitive::Material>> m_materials;

    std::unique_ptr<erhe::physics::IWorld>    m_physics_world;
    std::unique_ptr<erhe::raytrace::IScene>   m_raytrace_scene;
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
