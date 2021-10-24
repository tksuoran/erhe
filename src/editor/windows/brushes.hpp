#pragma once

#include "scene/collision_generator.hpp"
#include "tools/tool.hpp"
#include "windows/imgui_window.hpp"
#include "erhe/primitive/enums.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace erhe::geometry
{
    class Geometry;
}

namespace Physics
{
    class ICollision_shape;
}

namespace erhe::primitive
{
    class Build_info_set;
    class Material;
    class Primitive_geometry;
}

namespace erhe::scene
{
    class Mesh;
    class Node;
}

namespace editor
{

class Brush;
class Editor;
class Grid_tool;
class Operation_stack;
class Scene_root;
class Selection_tool;

class Brush_create_context
{
public:
    explicit Brush_create_context(erhe::primitive::Build_info_set&    build_info_set,
                                  const erhe::primitive::Normal_style normal_style = erhe::primitive::Normal_style::corner_normals);

    erhe::primitive::Build_info_set& build_info_set;
    erhe::primitive::Normal_style    normal_style{erhe::primitive::Normal_style::corner_normals};
};

class Brushes
    : public erhe::components::Component
    , public Tool
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name{"Brushes"};

    Brushes ();
    ~Brushes() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    // Implements Tool
    auto update       (Pointer_context& pointer_context) -> bool override;
    void render       (const Render_context& render_context) override;
    auto state        () const -> State override;
    void cancel_ready () override;
    auto description  () -> const char* override { return c_name.data(); }
    void render_update(const Render_context&) override;

    // Implements Imgui_window
    void imgui(Pointer_context& pointer_context) override;

    //void add_brush     (const std::shared_ptr<erhe::primitive::Primitive_geometry>& primitive_geometry);
    void add_material  (const std::shared_ptr<erhe::primitive::Material>& material);
    auto allocate_brush(erhe::primitive::Build_info_set& build_info_set) -> std::shared_ptr<Brush>;
    auto make_brush    (
        erhe::geometry::Geometry&&                              geometry,
        const Brush_create_context&                             context,
        const std::shared_ptr<erhe::physics::ICollision_shape>& collision_shape = {}
    ) -> std::shared_ptr<Brush>;

    auto make_brush(
        std::shared_ptr<erhe::geometry::Geometry>               geometry,
        const Brush_create_context&                             context,
        const std::shared_ptr<erhe::physics::ICollision_shape>& collision_shape = {}
    ) -> std::shared_ptr<Brush>;

    auto make_brush(
        std::shared_ptr<erhe::geometry::Geometry> geometry,
        const Brush_create_context&               context,
        Collision_volume_calculator               collision_volume_calculator,
        Collision_shape_generator                 collision_shape_generator
    ) -> std::shared_ptr<Brush>;

private:
    void make_materials            ();
    void update_mesh               ();
    auto get_brush_transform       () -> glm::mat4;
    void do_insert_operation       ();
    void add_hover_mesh            ();
    void remove_hover_mesh         ();
    void update_mesh_node_transform();

    std::shared_ptr<Editor>             m_editor;
    std::shared_ptr<Operation_stack>    m_operation_stack;
    std::shared_ptr<Scene_root>         m_scene_root;
    std::shared_ptr<Selection_tool>     m_selection_tool;
    std::shared_ptr<Grid_tool>          m_grid_tool;

    std::vector<std::shared_ptr<erhe::primitive::Material>> m_materials;
    std::vector<const char*>            m_material_names;

    std::mutex                          m_brush_mutex;
    std::vector<std::shared_ptr<Brush>> m_brushes;

    Brush*                              m_brush{nullptr};
    int                                 m_selected_brush_index{0};
    int                                 m_selected_material   {0};

    bool                                m_snap_to_hover_polygon{true};
    bool                                m_snap_to_grid         {false};
    bool                                m_hover_content        {false};
    bool                                m_hover_tool           {false};
    std::optional<glm::vec3>            m_hover_position;
    std::optional<glm::vec3>            m_hover_normal;
    std::shared_ptr<erhe::scene::Node>  m_hover_node;
    erhe::geometry::Geometry*           m_hover_geometry   {nullptr};
    size_t                              m_hover_primitive  {0};
    size_t                              m_hover_local_index{0};
    State                               m_state            {State::Passive};
    std::shared_ptr<erhe::scene::Mesh>  m_brush_mesh;
    float                               m_scale            {1.0f};
    float                               m_transform_scale  {1.0f};

    class Debug_info
    {
    public:
        float hover_frame_scale{0.0f};
        float brush_frame_scale{0.0f};
        float transform_scale{0.0f};
    };
    Debug_info debug_info;
};

} // namespace editor
