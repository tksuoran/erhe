#pragma once

#include "tools/tool.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/scene/node.hpp"

#include <glm/glm.hpp>

namespace editor
{

class Grid;

class Grid_hover_position
{
public:
    glm::vec3   position{0.0f};
    const Grid* grid    {nullptr};
};

class Grid_tool
    : public erhe::application::Imgui_window
    , public erhe::components::Component
    , public Tool
{
public:
    class Config
    {
    public:
        bool      enabled   {false};
        glm::vec4 major_color{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 minor_color{0.5f, 0.5f, 0.5f, 0.5f};
        float     major_width{4.0f};
        float     minor_width{2.0f};
        float     cell_size {1.0f};
        int       cell_div  {10};
        int       cell_count{2};
    };
    Config config;

    static constexpr std::string_view c_type_name{"Grid_tool"};
    static constexpr std::string_view c_title{"Grid"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Grid_tool ();
    ~Grid_tool() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Implements Tool
    void tool_render(const Render_context& context)  override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    void viewport_toolbar(bool& hovered);

    auto update_hover(
        const glm::vec3 ray_origin,
        const glm::vec3 ray_direction
    ) const -> Grid_hover_position;

private:
    bool                               m_enable{true};
    std::vector<std::shared_ptr<Grid>> m_grids;
    int                                m_grid_index{0};
};

extern Grid_tool* g_grid_tool;

} // namespace editor
