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
    Grid_tool() : erhe::components::Component(c_name) {}
    virtual ~Grid_tool() = default;

    // Implements Tool
    void render(Render_context& render_context) override;
    auto description() -> const char* override { return c_name; }

    // Implements Window
    void window(Pointer_context& pointer_context) override;

    void toolbar();

    auto state() const -> State override;

private:
    bool  m_enable    {true};
    float m_cell_size {1.0f};
    int   m_cell_div  {10};
    int   m_cell_count{20};
};

} // namespace editor
