#pragma once

#include "tools/tool.hpp"

#include <memory>

namespace erhe::geometry {
    class Geometry;
}

namespace erhe::scene {
    class Mesh;
}

namespace sample
{

class Scene_manager;
class Selection_tool;

class Mesh_properties
    : public Tool
{
public:
    Mesh_properties(const std::shared_ptr<Scene_manager>&  scene_manager,
                    const std::shared_ptr<Selection_tool>& selection_tool);

    virtual ~Mesh_properties() = default;

    auto name() -> const char* override { return "Mesh Debug"; }

    void window(Pointer_context& pointer_context) override;

    void render(Render_context& render_context) override;

    auto state() const -> State override;

private:
    std::shared_ptr<Scene_manager>  m_scene_manager;
    std::shared_ptr<Selection_tool> m_selection_tool;

    int  m_max_labels     {400};
    int  m_primitive_index{0};
    bool m_show_points    {false};
    bool m_show_polygons  {false};
    bool m_show_edges     {false};
};

} // namespace sample
