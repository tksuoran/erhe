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
    struct Material;
    struct Primitive_build_context;
    struct Primitive_geometry;
    struct Primitive;
}

namespace erhe::scene
{
    class Camera;
    class ICamera;
    class Layer;
    class Light;
    class Mesh;
    class Scene;
}

namespace erhe::physics
{
    class World;
}

namespace editor
{

class Debug_draw;
class Mesh_memory;
class Node_physics;
class Scene_manager;
class Brush;


class Scene_root
    : public erhe::components::Component
{
public:
    Scene_root();
    virtual ~Scene_root();

    // Implements Component
    void connect() override;
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


    auto content_layer() const -> std::shared_ptr<erhe::scene::Layer>;

    auto selection_layer() const -> std::shared_ptr<erhe::scene::Layer>;

    auto tool_layer() const -> std::shared_ptr<erhe::scene::Layer>;

    auto all_layers      () -> std::vector<std::shared_ptr<erhe::scene::Layer>>& { return m_all_layers; }
    auto content_layers  () -> std::vector<std::shared_ptr<erhe::scene::Layer>>& { return m_content_layers; }
    auto selection_layers() -> std::vector<std::shared_ptr<erhe::scene::Layer>>& { return m_selection_layers; }
    auto tool_layers     () -> std::vector<std::shared_ptr<erhe::scene::Layer>>& { return m_tool_layers; }

    auto make_mesh_node(std::string_view                                     name,
                        std::shared_ptr<erhe::primitive::Primitive_geometry> primitive_geometry,
                        std::shared_ptr<erhe::primitive::Material>           material,
                        erhe::scene::Node*                                   parent   = nullptr,
                        const glm::vec3                                      position = glm::vec3(0.0f))
    -> std::shared_ptr<erhe::scene::Mesh>;

    auto make_mesh_node(std::string_view                                     name,
                        std::shared_ptr<erhe::primitive::Primitive_geometry> primitive_geometry,
                        std::shared_ptr<erhe::primitive::Material>           material,
                        erhe::scene::Layer&                                  layer,
                        erhe::scene::Node*                                   parent = nullptr,
                        const glm::vec3                                      position = glm::vec3(0.0f))
    -> std::shared_ptr<erhe::scene::Mesh>;

    auto add(std::shared_ptr<erhe::primitive::Material> material) -> std::shared_ptr<erhe::primitive::Material>;

    auto add(std::shared_ptr<erhe::scene::Mesh> mesh) -> std::shared_ptr<erhe::scene::Mesh>;

    auto add(std::shared_ptr<erhe::scene::Light> light) -> std::shared_ptr<erhe::scene::Light>;

    //auto camera() -> erhe::scene::ICamera&;

    //auto camera() const -> const erhe::scene::Camera&;

    auto materials() -> std::vector<std::shared_ptr<erhe::primitive::Material>>&;

    auto materials() const -> const std::vector<std::shared_ptr<erhe::primitive::Material>>&;

    auto physics_world() -> erhe::physics::World&;

    auto scene() -> erhe::scene::Scene&;

    auto content_layer() -> erhe::scene::Layer&;

private:
    std::mutex                                              m_materials_mutex;
    std::vector<std::shared_ptr<erhe::primitive::Material>> m_materials;

    std::unique_ptr<erhe::physics::World>            m_physics_world;
    std::unique_ptr<erhe::scene::Scene>              m_scene;
    std::shared_ptr<erhe::scene::Layer>              m_content_layer;
    std::shared_ptr<erhe::scene::Layer>              m_selection_layer;
    std::shared_ptr<erhe::scene::Layer>              m_tool_layer;
    std::shared_ptr<erhe::scene::Layer>              m_brush_layer;
    std::shared_ptr<erhe::scene::Camera>             m_camera;
    std::shared_ptr<Frame_controller>                m_camera_controls;

    std::vector<std::shared_ptr<erhe::scene::Layer>> m_all_layers;
    std::vector<std::shared_ptr<erhe::scene::Layer>> m_content_layers;
    std::vector<std::shared_ptr<erhe::scene::Layer>> m_selection_layers;
    std::vector<std::shared_ptr<erhe::scene::Layer>> m_tool_layers;
    std::vector<std::shared_ptr<erhe::scene::Layer>> m_brush_layers;
};

} // namespace editor
