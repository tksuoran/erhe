#pragma once

#include "erhe/primitive/primitive_geometry.hpp"

#include <glm/glm.hpp>

#include <map>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe::application
{
class Configuration;
}

namespace erhe::geometry
{
class Geometry;
}

namespace erhe::primitive
{
class Material;
}

namespace erhe::scene
{
class Mesh;
class Node;
}

namespace editor
{

class Icon_set;
class Mesh_memory;
class Raytrace_primitive;
class Trs_tool;

enum class Handle : unsigned int
{
    e_handle_none         = 0,
    e_handle_translate_x  = 1,
    e_handle_translate_y  = 2,
    e_handle_translate_z  = 3,
    e_handle_translate_xy = 4,
    e_handle_translate_xz = 5,
    e_handle_translate_yz = 6,
    e_handle_rotate_x     = 7,
    e_handle_rotate_y     = 8,
    e_handle_rotate_z     = 9,
};

enum class Handle_type : unsigned int
{
    e_handle_type_none            = 0,
    e_handle_type_translate_axis  = 1,
    e_handle_type_translate_plane = 2,
    e_handle_type_rotate          = 3
};

[[nodiscard]] auto c_str(const Handle handle) -> const char*;

class Handle_visualizations
{
public:
    explicit Handle_visualizations(Trs_tool& trs_tool);

    enum class Mode : unsigned int
    {
        Normal = 0,
        Hover  = 1,
        Active = 2
    };

    class Part
    {
    public:
        std::shared_ptr<erhe::geometry::Geometry> geometry;
        erhe::primitive::Primitive_geometry       primitive_geometry;
        std::shared_ptr<Raytrace_primitive>       raytrace_primitive;
    };

    [[nodiscard]] auto get_handle           (erhe::scene::Mesh* mesh) const -> Handle;
    [[nodiscard]] auto get_handle_material  (Handle handle, Mode mode) -> std::shared_ptr<erhe::primitive::Material>;
    [[nodiscard]] auto get_handle_visibility(Handle handle) const -> bool;
    [[nodiscard]] auto get_scale            () const -> float;
    [[nodiscard]] auto get_translate        () const -> bool;
    [[nodiscard]] auto get_rotate           () const -> bool;
    void imgui           ();
    void viewport_toolbar(bool& hovered, const Icon_set& icon_set);

    void initialize(
        const erhe::application::Configuration& configuration,
        Mesh_memory&                            mesh_memory
    );

    void update_visibility(const bool show);
    void update_scale     (const glm::vec3 view_position_in_world);
    void update_transforms(); //const uint64_t serial);

    void update_mesh_visibility(
        const std::shared_ptr<erhe::scene::Mesh>& mesh,
        bool                                      trs_visible
    );

    void set_target   (const std::shared_ptr<erhe::scene::Node>& node);
    void set_translate(bool value);
    void set_rotate   (bool value);
    void set_local    (bool local);

private:
    [[nodiscard]] auto make_arrow_cylinder(Mesh_memory& mesh_memory) -> Part;
    [[nodiscard]] auto make_arrow_cone    (Mesh_memory& mesh_memory) -> Part;
    [[nodiscard]] auto make_box           (Mesh_memory& mesh_memory) -> Part;
    [[nodiscard]] auto make_rotate_ring   (Mesh_memory& mesh_memory) -> Part;

    [[nodiscard]] auto make_material(
        const char*      name,
        const glm::vec3& color,
        Mode             mode = Mode::Normal
    ) -> std::shared_ptr<erhe::primitive::Material>;

    [[nodiscard]] auto make_mesh(
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

    Trs_tool& m_trs_tool;
    bool      m_show_translate{true};
    bool      m_show_rotate   {false};
    bool      m_hide_inactive {true};
    float     m_scale         {1.0f};

    std::map<erhe::scene::Mesh*, Handle>             m_handles;
    std::weak_ptr<erhe::scene::Node>                 m_root;
    std::shared_ptr<erhe::scene::Node>               m_tool_node;
    bool                                             m_local{true};
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
    std::shared_ptr<erhe::scene::Mesh>               m_xy_box_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_xz_box_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_yz_box_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_x_rotate_ring_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_y_rotate_ring_mesh;
    std::shared_ptr<erhe::scene::Mesh>               m_z_rotate_ring_mesh;
    std::vector<std::shared_ptr<Raytrace_primitive>> m_rt_primitives;
};

}
