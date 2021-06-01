#pragma once

#include "tools/tool.hpp"

#include <memory>

namespace erhe::geometry {
    class Geometry;
}

namespace erhe::scene {
    class Mesh;
}

namespace editor
{

class Editor;
class Scene_root;
class Selection_tool;

class Mesh_properties
    : public erhe::components::Component
    , public Tool
    , public Window
{
public:
    static constexpr const char* c_name = "Mesh_properties";
    Mesh_properties() : erhe::components::Component(c_name) {}
    virtual ~Mesh_properties() = default;

    // Implements Component
    void connect() override;

    // Implements Tool
    auto description() -> const char* override { return c_name; }
    void render(const Render_context& render_context) override;
    auto state() const -> State override;

    // Implements Window
    void window(Pointer_context& pointer_context) override;

private:
    std::shared_ptr<Editor>         m_editor;
    std::shared_ptr<Scene_root>     m_scene_root;
    std::shared_ptr<Selection_tool> m_selection_tool;

    int  m_max_labels     {400};
    int  m_primitive_index{0};
    bool m_show_points    {false};
    bool m_show_polygons  {false};
    bool m_show_edges     {false};
};

} // namespace editor
