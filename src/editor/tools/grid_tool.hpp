#pragma once

#include "tools/tool.hpp"
#include "windows/imgui_window.hpp"

#include <glm/glm.hpp>

namespace editor
{

class Line_renderer;

class Grid_tool
    : public erhe::components::Component
    , public Tool
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name       {"Grid_tool"};
    static constexpr std::string_view c_description{"Grid"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Grid_tool ();
    ~Grid_tool() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             ()                   override;
    void initialize_component()                   override;

    // Implements Tool
    void tool_render(const Render_context& context) override;
    auto state      () const -> State               override;
    auto description() -> const char*               override;

    // Implements Imgui_window
    void imgui() override;

    auto snap(const glm::vec3 v) const -> glm::vec3;

private:
    Line_renderer* m_line_renderer{nullptr};
    bool  m_enable    {false};
    float m_cell_size {1.0f};
    int   m_cell_div  {10};
    int   m_cell_count{20};
};

} // namespace editor
