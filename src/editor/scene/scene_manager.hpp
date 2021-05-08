#pragma once

#include "scene/frame_controller.hpp"

#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/components/component.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <map>
#include <memory>
#include <vector>

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::primitive
{
    struct Material;
    struct Primitive_geometry;
    struct Primitive;
}

namespace erhe::scene
{
    class Layer;
    class Camera;
    class Light;
    class Mesh;
}

namespace editor
{

class Programs;
class Scene_manager;

class Scene_manager
    : public erhe::components::Component
{
public:
    struct Geometry_entry
    {
        explicit Geometry_entry(erhe::geometry::Geometry&&                        geometry,
                                erhe::primitive::Primitive_geometry::Normal_style normal_style = erhe::primitive::Primitive_geometry::Normal_style::corner_normals);
        explicit Geometry_entry(const std::shared_ptr<erhe::geometry::Geometry>&  geometry,
                                erhe::primitive::Primitive_geometry::Normal_style normal_style = erhe::primitive::Primitive_geometry::Normal_style::corner_normals);
        std::shared_ptr<erhe::geometry::Geometry>         geometry;
        erhe::primitive::Primitive_geometry::Normal_style normal_style;
    };

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

    //auto meshes() -> std::vector<std::shared_ptr<erhe::scene::Mesh>>&;

    auto primitive_geometries() const -> const std::vector<std::shared_ptr<erhe::primitive::Primitive_geometry>>&
    {
        return m_primitive_geometries;
    }

    auto geometries() -> std::vector<Geometry_entry>&;

    auto geometries() const -> const std::vector<Geometry_entry>&;

    //auto lights() -> std::vector<std::shared_ptr<erhe::scene::Light>>&;

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

    auto make_primitive_geometry(const Geometry_entry& geometry_entry)
    -> std::shared_ptr<erhe::primitive::Primitive_geometry>;

    auto make_primitive_geometry(const std::shared_ptr<erhe::geometry::Geometry>& geometry)
    -> std::shared_ptr<erhe::primitive::Primitive_geometry>;

    auto make_primitive_geometry(erhe::geometry::Geometry&&                        geometry,
                                 erhe::primitive::Primitive_geometry::Normal_style normal_style = erhe::primitive::Primitive_geometry::Normal_style::corner_normals)
    -> std::shared_ptr<erhe::primitive::Primitive_geometry>;

    auto make_mesh_node(const std::string&                                   name,
                        std::shared_ptr<erhe::primitive::Primitive_geometry> primitive_geometry,
                        std::shared_ptr<erhe::primitive::Material>           material,
                        std::shared_ptr<erhe::scene::Layer>                  layer = {},
                        erhe::scene::Node*                                   parent = nullptr,
                        glm::vec3                                            position = glm::vec3(0.0f))
    -> std::shared_ptr<erhe::scene::Mesh>;

    void update_once_per_frame(double time);

    auto scene() -> erhe::scene::Scene&;

private:
    void add_floor();
    void make_materials();
    void make_geometries();
    void make_mesh_nodes();
    void make_punctual_light_nodes();
    void make_geometry(erhe::geometry::Geometry&&                         geometry,
                       erhe::primitive::Primitive_geometry::Normal_style normal_style = erhe::primitive::Primitive_geometry::Normal_style::corner_normals);
    void make_geometry(const std::shared_ptr<erhe::geometry::Geometry>&  geometry,
                       erhe::primitive::Primitive_geometry::Normal_style normal_style = erhe::primitive::Primitive_geometry::Normal_style::corner_normals);

    // Components
    std::shared_ptr<Programs>                               m_programs;

    // Self owned parts
    std::vector<Geometry_entry>                                       m_geometries;
    std::vector<std::shared_ptr<erhe::primitive::Primitive_geometry>> m_primitive_geometries;
    std::vector<std::shared_ptr<erhe::primitive::Material>>           m_materials;

    erhe::scene::Scene                                      m_scene;
    std::shared_ptr<erhe::scene::Layer>                     m_content_layer;
    std::shared_ptr<erhe::scene::Layer>                     m_selection_layer;
    std::shared_ptr<erhe::scene::Layer>                     m_tool_layer;
    std::shared_ptr<erhe::scene::Layer>                     m_brush_layer;
    std::shared_ptr<erhe::scene::Camera>                    m_camera;
    std::shared_ptr<Frame_controller>                       m_camera_controls;

    std::vector<std::shared_ptr<erhe::scene::Layer>>        m_all_layers;
    std::vector<std::shared_ptr<erhe::scene::Layer>>        m_content_layers;
    std::vector<std::shared_ptr<erhe::scene::Layer>>        m_selection_layers;
    std::vector<std::shared_ptr<erhe::scene::Layer>>        m_tool_layers;
    std::vector<std::shared_ptr<erhe::scene::Layer>>        m_brush_layers;

    erhe::primitive::Primitive_builder::Format_info         m_format_info;
    erhe::primitive::Primitive_builder::Buffer_info         m_buffer_info;

    static constexpr bool s_single_mesh{false};
};

} // namespace editor
