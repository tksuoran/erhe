#pragma once

#include "tools/tool.hpp"
#include "erhe/geometry/geometry.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::primitive
{
    struct Material;
    struct Primitive_geometry;
}

namespace erhe::scene
{
    class Mesh;
    class Node;
}

namespace editor
{

class Editor;
class Grid_tool;
class Operation_stack;
class Selection_tool;

class Reference_frame
{
public:
    Reference_frame() = default;

    Reference_frame(const erhe::geometry::Geometry& geometry,
                    erhe::geometry::Polygon_id      polygon_id);

    void transform_by(glm::mat4 m);

    auto transform() const -> glm::mat4;

    auto scale() const -> float;

    uint32_t                   corner_count{0};
    erhe::geometry::Polygon_id polygon_id{0};
    glm::vec3                  centroid{0.0f, 0.0f, 0.0f}; // polygon centroid
    glm::vec3                  position{1.0f, 0.0f, 0.0f}; // one of polygon corner points
    glm::vec3                  B{0.0f, 0.0f, 1.0f};
    glm::vec3                  T{1.0f, 0.0f, 0.0f};
    glm::vec3                  N{0.0f, 1.0f, 0.0f};
};

class Brush
{
public:
    Brush() = default;
    Brush(const std::shared_ptr<erhe::primitive::Primitive_geometry>& primitive_geometry);
    Brush(const Brush&) = delete;
    auto operator=(const Brush&) -> Brush& = delete;
    Brush(Brush&& other) noexcept
        : primitive_geometry{std::move(other.primitive_geometry)}
    {
    }
    auto operator=(Brush&& other) noexcept -> Brush&
    {
        primitive_geometry = std::move(other.primitive_geometry);
    }

    auto get_reference_frame(uint32_t corner_count) -> Reference_frame;

    static constexpr const float c_scale_factor = 65536.0f;

    auto get_scaled_primitive_geometry(float          scale,
                                       Scene_manager& scene_manager)
    -> std::shared_ptr<erhe::primitive::Primitive_geometry>;

    auto create_scaled(int scale_key, Scene_manager& scene_manager)
    -> std::shared_ptr<erhe::primitive::Primitive_geometry>;
        
    struct Scaled_primitive_geometry
    {
        int                                                  scale_key;
        std::shared_ptr<erhe::primitive::Primitive_geometry> primitive_geometry;
    };

    std::shared_ptr<erhe::primitive::Primitive_geometry> primitive_geometry;
    std::vector<Reference_frame>                         reference_frames;
    std::vector<Scaled_primitive_geometry>               scaled_primitive_geometries;
};


class Brushes
    : public erhe::components::Component
    , public Tool
    , public Window
{
public:
    static constexpr const char* c_name = "Brushes";
    Brushes() : erhe::components::Component(c_name) {}
    virtual ~Brushes() = default;

    // Implements Component
    void connect() override;
    void initialize_component() override;

    // Implements Tool
    auto update(Pointer_context& pointer_context) -> bool override;
    void render(Render_context& render_context) override;
    auto state() const -> State override;
    void cancel_ready() override;
    auto description() -> const char* override { return c_name; }

    // Implements Window
    void window(Pointer_context& pointer_context) override;

    void render_update() override;

    void add_brush(const std::shared_ptr<erhe::primitive::Primitive_geometry>& geometry);

    void add_material(const std::shared_ptr<erhe::primitive::Material>& material);

private:
    void update_mesh();

    auto get_brush_transform() -> glm::mat4;

    void do_insert_operation();

    void add_hover_mesh();

    void remove_hover_mesh();

    void update_mesh_node_transform();

    auto get_brush_primitive_geometry()
    -> std::shared_ptr<erhe::primitive::Primitive_geometry>;

    std::shared_ptr<Editor>            m_editor;
    std::shared_ptr<Operation_stack>   m_operation_stack;
    std::shared_ptr<Scene_manager>     m_scene_manager;
    std::shared_ptr<Selection_tool>    m_selection_tool;
    std::shared_ptr<Grid_tool>         m_grid_tool;

    std::vector<Brush>                 m_brushes;
    std::vector<std::shared_ptr<erhe::primitive::Material>> m_materials;
    std::vector<const char*>           m_brush_names;
    std::vector<const char*>           m_material_names;
    int                                m_selected_brush   {0};
    int                                m_selected_material{0};

    bool                               m_snap_to_hover_polygon{true};
    bool                               m_snap_to_grid         {false};
    bool                               m_hover_content        {false};
    bool                               m_hover_tool           {false};
    std::optional<glm::vec3>           m_hover_position;
    std::optional<glm::vec3>           m_hover_normal;
    std::shared_ptr<erhe::scene::Node> m_hover_node;
    erhe::geometry::Geometry*          m_hover_geometry   {nullptr};
    size_t                             m_hover_primitive  {0};
    size_t                             m_hover_local_index{0};
    State                              m_state            {State::passive};
    std::shared_ptr<erhe::scene::Mesh> m_brush_mesh;
    float                              m_scale            {1.0f};
    float                              m_transform_scale  {1.0f};
};

} // namespace editor
