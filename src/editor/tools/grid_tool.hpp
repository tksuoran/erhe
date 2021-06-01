#pragma once

#include "tools/tool.hpp"

namespace editor
{

class Line_renderer;

class Grid_tool
    : public erhe::components::Component
    , public Tool
    , public Window
{
public:
    static constexpr const char* c_name = "Grid_tool";
    Grid_tool();
    virtual ~Grid_tool();

    // Implements Tool
    void render(const Render_context& render_context) override;
    auto state() const -> State override;
    auto description() -> const char* override;

    // Implements Window
    void window(Pointer_context& pointer_context) override;

    auto snap(glm::vec3 v) const -> glm::vec3;

private:
    bool  m_enable    {true};
    float m_cell_size {1.0f};
    int   m_cell_div  {10};
    int   m_cell_count{20};
};

} // namespace editor
