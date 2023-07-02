#pragma once

#include "tools/transform/transform_tool_settings.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/scene/trs_transform.hpp"

#include <glm/glm.hpp>

#include <map>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::primitive {
    class Material;
}
namespace erhe::scene {
    class Mesh;
    class Node;
}

namespace editor
{

class Editor_context;
class Mesh_memory;
class Raytrace_primitive;
class Scene_view;
class Tools;
class Transform_tool;

enum class Handle : unsigned int;

class Handle_visualizations
{
public:
    Handle_visualizations(
        Editor_context& editor_context,
        Mesh_memory&    mesh_memory,
        Tools&          tools
    );

    enum class Mode : unsigned int
    {
        Normal = 0,
        Hover  = 1,
        Active = 2
    };

    class Part
    {
    public:
        Part(
            Mesh_memory&                                     mesh_memory,
            const std::shared_ptr<erhe::geometry::Geometry>& geometry
        );

        std::shared_ptr<erhe::geometry::Geometry> geometry;
        erhe::primitive::Primitive_geometry       primitive_geometry;
        std::shared_ptr<Raytrace_primitive>       raytrace_primitive;
    };

    [[nodiscard]] auto get_handle           (erhe::scene::Mesh* mesh) const -> Handle;
    [[nodiscard]] auto get_handle_material  (Handle handle, Mode mode) -> std::shared_ptr<erhe::primitive::Material>;
    [[nodiscard]] auto get_handle_visibility(Handle handle) const -> bool;
    void viewport_toolbar (bool& hovered);
    void update_visibility();
    void update_for_view  (Scene_view* scene_view);
    void update_transforms(); //const uint64_t serial);

    void update_mesh_visibility(const std::shared_ptr<erhe::scene::Mesh>& mesh);

    void set_anchor(const erhe::scene::Trs_transform& world_from_anchor);

private:
    [[nodiscard]] auto make_arrow_cylinder(Mesh_memory& mesh_memory) -> Part;
    [[nodiscard]] auto make_arrow_cone    (Mesh_memory& mesh_memory) -> Part;
    [[nodiscard]] auto make_box           (Mesh_memory& mesh_memory, bool uniform) -> Part;
    [[nodiscard]] auto make_rotate_ring   (Mesh_memory& mesh_memory) -> Part;

    [[nodiscard]] auto make_material(
        Tools&           tools,
        const char*      name,
        const glm::vec3& color,
        Mode             mode = Mode::Normal
    ) -> std::shared_ptr<erhe::primitive::Material>;

    [[nodiscard]] auto make_mesh(
        Tools&                                            tools,
        const std::string_view                            name,
        const std::shared_ptr<erhe::primitive::Material>& material,
        const Part&                                       part
    ) -> std::shared_ptr<erhe::scene::Mesh>;

    [[nodiscard]] auto get_mode_material(
        Mode                                              mode,
        const std::shared_ptr<erhe::primitive::Material>& active,
        const std::shared_ptr<erhe::primitive::Material>& hover,
        const std::shared_ptr<erhe::primitive::Material>& ready
    ) -> std::shared_ptr<erhe::primitive::Material>;

    Editor_context& m_context;
    float           m_scale {1.0f};

    std::map<erhe::scene::Mesh*, Handle>             m_handles;
    erhe::scene::Trs_transform                       m_world_from_anchor;
    std::shared_ptr<erhe::scene::Node>               m_tool_node;
    float                                            m_view_distance{1.0f};
    std::shared_ptr<erhe::primitive::Material>       m_x_material;
    std::shared_ptr<erhe::primitive::Material>       m_y_material;
    std::shared_ptr<erhe::primitive::Material>       m_z_material;
    std::shared_ptr<erhe::primitive::Material>       m_x_hover_material;
    std::shared_ptr<erhe::primitive::Material>       m_y_hover_material;
    std::shared_ptr<erhe::primitive::Material>       m_z_hover_material;
    std::shared_ptr<erhe::primitive::Material>       m_x_active_material;
    std::shared_ptr<erhe::primitive::Material>       m_y_active_material;
    std::shared_ptr<erhe::primitive::Material>       m_z_active_material;
    std::shared_ptr<erhe::scene::Mesh>               m_x_arrow_cylinder_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_x_arrow_neg_cone_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_x_arrow_pos_cone_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_y_arrow_cylinder_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_y_arrow_neg_cone_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_y_arrow_pos_cone_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_z_arrow_cylinder_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_z_arrow_neg_cone_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_z_arrow_pos_cone_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_xy_translate_box_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_xz_translate_box_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_yz_translate_box_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_x_rotate_ring_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_y_rotate_ring_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_z_rotate_ring_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_x_neg_scale_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_x_pos_scale_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_y_neg_scale_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_y_pos_scale_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_z_neg_scale_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_z_pos_scale_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_xy_scale_box_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_xz_scale_box_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_yz_scale_box_mesh;
    std::vector<std::shared_ptr<Raytrace_primitive>> m_rt_primitives;
};

}
