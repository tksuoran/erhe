#pragma once

#include "tools/tool.hpp"

namespace editor
{

class Line_renderer;

class Grid_tool
    : public Tool
{
public:
    Grid_tool();

    virtual ~Grid_tool() = default;

    auto name() -> const char* override { return "Grid_tool"; }

    void render(Render_context& render_context) override;

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
