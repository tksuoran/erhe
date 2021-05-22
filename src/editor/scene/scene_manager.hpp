#pragma once

#include "scene/collision_generator.hpp"
#include "scene/frame_controller.hpp"

#include "erhe/components/component.hpp"
#include "erhe/primitive/buffer_info.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/primitive/format_info.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

class btCollisionShape;

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::graphics
{
    class Buffer;
    class Vertex_format;
}

namespace erhe::primitive
{
    struct Material;
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
class Node_physics;
class Programs;
class Scene_manager;
class Brush;


class Scene_manager
    : public erhe::components::Component
{
public:
    Scene_manager();

    virtual ~Scene_manager() = default;

    // Implements Component
    void connect() override;

    // Implements Component
    void initialize_component() override;

    void add_scene();

    void initialize_cameras();

    auto content_layer() const -> std::shared_ptr<erhe::scene::Layer>
    {
        return m_content_layer;
    }

    auto selection_layer() const -> std::shared_ptr<erhe::scene::Layer>
    {
        return m_selection_layer;
    }

    auto tool_layer() const -> std::shared_ptr<erhe::scene::Layer>
    {
        return m_tool_layer;
    }

    auto all_layers      () -> std::vector<std::shared_ptr<erhe::scene::Layer>>& { return m_all_layers; }
    auto content_layers  () -> std::vector<std::shared_ptr<erhe::scene::Layer>>& { return m_content_layers; }
    auto selection_layers() -> std::vector<std::shared_ptr<erhe::scene::Layer>>& { return m_selection_layers; }
    auto tool_layers     () -> std::vector<std::shared_ptr<erhe::scene::Layer>>& { return m_tool_layers; }

    auto add(std::shared_ptr<erhe::primitive::Material> material) -> std::shared_ptr<erhe::primitive::Material>;

    auto add(std::shared_ptr<erhe::scene::Mesh> mesh) -> std::shared_ptr<erhe::scene::Mesh>;

    auto add(std::shared_ptr<erhe::scene::Light> light) -> std::shared_ptr<erhe::scene::Light>;

    auto camera() -> erhe::scene::ICamera&;

    auto camera() const -> const erhe::scene::Camera&;

    auto materials() -> std::vector<std::shared_ptr<erhe::primitive::Material>>&;

    auto materials() const -> const std::vector<std::shared_ptr<erhe::primitive::Material>>&;

    auto brushes() -> std::vector<Brush>&;

    auto brushes() const -> const std::vector<Brush>&;

    auto vertex_buffer() -> erhe::graphics::Buffer*;

    auto index_buffer() -> erhe::graphics::Buffer*;

    auto index_type() -> gl::Draw_elements_type;

    auto vertex_format() -> std::shared_ptr<erhe::graphics::Vertex_format>;

    void sort_lights();

    auto make_directional_light(const std::string&  name,
                                glm::vec3           position,
                                glm::vec3           color,
                                float               intensity,
                                erhe::scene::Layer* layer = nullptr)
    -> std::shared_ptr<erhe::scene::Light>;

    auto make_spot_light(const std::string& name,
                         glm::vec3          position,
                         glm::vec3          direction,
                         glm::vec3          color,
                         float              intensity,
                         glm::vec2          spot_cone_angle)
    -> std::shared_ptr<erhe::scene::Light>;

    auto make_mesh_node(const std::string&                                   name,
                        std::shared_ptr<erhe::primitive::Primitive_geometry> primitive_geometry,
                        std::shared_ptr<erhe::primitive::Material>           material,
                        std::shared_ptr<erhe::scene::Layer>                  layer = {},
                        erhe::scene::Node*                                   parent = nullptr,
                        glm::vec3                                            position = glm::vec3(0.0f))
    -> std::shared_ptr<erhe::scene::Mesh>;

    void attach(std::shared_ptr<erhe::scene::Node> node,
                std::shared_ptr<erhe::scene::Mesh> mesh,
                std::shared_ptr<Node_physics>      node_physics);

    void detach(std::shared_ptr<erhe::scene::Node> node,
                std::shared_ptr<erhe::scene::Mesh> mesh,
                std::shared_ptr<Node_physics>      node_physics);

    void update_once_per_frame(double time);

    void update_fixed_step(double dt);

    auto scene() -> erhe::scene::Scene&;

    void debug_render();

    auto physics_world() -> erhe::physics::World&
    {
        return *m_physics_world.get();
    }

    auto make_primitive_geometry(erhe::geometry::Geometry&     geometry,
                                 erhe::primitive::Normal_style normal_style = erhe::primitive::Normal_style::corner_normals)
    -> std::shared_ptr<erhe::primitive::Primitive_geometry>;

private:
    void add_floor();
    void make_materials();
    void make_brushes();
    void make_mesh_nodes();
    void make_punctual_light_nodes();

    auto make_brush(erhe::geometry::Geometry&&        geometry,
                    erhe::primitive::Normal_style     normal_style    = erhe::primitive::Normal_style::corner_normals,
                    std::shared_ptr<btCollisionShape> collision_shape = {})
    -> Brush&;

    auto make_brush(const std::shared_ptr<erhe::geometry::Geometry>& geometry,
                    erhe::primitive::Normal_style                    normal_style    = erhe::primitive::Normal_style::corner_normals,
                    std::shared_ptr<btCollisionShape>                collision_shape = {})
    -> Brush&;

    auto make_brush(const std::shared_ptr<erhe::geometry::Geometry>& geometry,
                    erhe::primitive::Normal_style                    normal_style,
                    Collision_volume_calculator                      collision_volume_calculator,
                    Collision_shape_generator                        collision_shape_generator)
    -> Brush&;

    bool m_instantiate_brush_to_scene{false};
    void instantiate_brush_to_scene(bool value)
    {
        m_instantiate_brush_to_scene = value;
    }

    // Components
    std::shared_ptr<Programs>   m_programs;
    std::shared_ptr<Debug_draw> m_debug_draw;

    // Self owned parts
    std::vector<Brush>                                      m_brushes;
    std::vector<size_t>                                     m_scene_brushes;
    std::vector<std::shared_ptr<erhe::primitive::Material>> m_materials;

    std::unique_ptr<erhe::physics::World>                   m_physics_world;

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

    erhe::primitive::Format_info                     m_format_info;
    erhe::primitive::Buffer_info                     m_buffer_info;

    static constexpr bool s_single_mesh{false};
};

} // namespace editor
