#pragma once

#include "tools/tool.hpp"

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
}

namespace editor
{

class Editor;
class Operation_stack;

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

    auto get_brush_transform() const -> glm::mat4;

    void do_insert_operation();

    void add_hover_mesh();

    void remove_hover_mesh();

    void update_mesh_node_transform();

    std::shared_ptr<Editor>                                           m_editor;
    std::shared_ptr<Operation_stack>                                  m_operation_stack;
    std::shared_ptr<Scene_manager>                                    m_scene_manager;
    std::vector<std::shared_ptr<erhe::primitive::Primitive_geometry>> m_brushes;
    std::vector<std::shared_ptr<erhe::primitive::Material>>           m_materials;
    int                                                               m_selected_brush{0};
    int                                                               m_selected_material{0};
    bool                     m_snap_to_hover_polygon{false};
    bool                     m_snap_to_grid         {false};
    std::vector<const char*> m_brush_names;
    std::vector<const char*> m_material_names;
    bool                     m_hover_content{false};
    bool                     m_hover_tool   {false};
    std::optional<glm::vec3> m_hover_position;
    std::optional<glm::vec3> m_hover_normal;

    State                              m_state{State::passive};
    std::shared_ptr<erhe::scene::Mesh> m_brush_mesh;
    float                              m_scale{1.0f};
};

} // namespace editor
