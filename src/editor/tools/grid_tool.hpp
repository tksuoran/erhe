#pragma once

#include "tools/tool.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

namespace erhe::application
{
    class Line_renderer_set;
}

namespace editor
{

// TODO Negative half planes
enum class Grid_plane_type : unsigned int
{
    XZ = 0,
    XY,
    YZ,
    Custom
};

static constexpr const char* grid_plane_type_strings[] =
{
    "XZ-Plane Y+",
    "XY-Plane Z+",
    "YZ-Plane X+",
    "Custom"
};

auto get_plane_transform(Grid_plane_type plane_type) -> glm::dmat4;

class Grid
{
public:
    auto snap_world_position(const glm::dvec3& position_in_world) const -> glm::dvec3;
    auto snap_grid_position (const glm::dvec3& position_in_grid ) const -> glm::dvec3;

    std::string     name;
    Grid_plane_type plane_type      {Grid_plane_type::XZ};
    double          rotation        {0.0};
    glm::dvec3      center          {0.0};
    glm::dmat4      world_from_grid {1.0f};
    glm::dmat4      grid_from_world {1.0f};
    bool            enable          {true};
    bool            see_hidden_major{false};
    bool            see_hidden_minor{false};
    float           cell_size       {1.0f};
    int             cell_div        {2};
    int             cell_count      {1};
    float           major_width     {4.0f};
    float           minor_width     {2.0f};
    glm::vec4       major_color     {0.0f, 0.0f, 0.0f, 0.729f};
    glm::vec4       minor_color     {0.0f, 0.0f, 0.0f, 0.737f};
};

class Grid_hover_position
{
public:
    glm::dvec3  position{0.0};
    const Grid* grid    {nullptr};
};

class Grid_tool
    : public erhe::application::Imgui_window
    , public erhe::components::Component
    , public Tool
{
public:
    static constexpr std::string_view c_type_name{"Grid_tool"};
    static constexpr std::string_view c_title{"Grid"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Grid_tool ();
    ~Grid_tool() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override;
    void tool_render(const Render_context& context)  override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    void viewport_toolbar();

    auto update_hover(
        const glm::dvec3 ray_origin,
        const glm::dvec3 ray_direction
    ) const -> Grid_hover_position;

private:
    // Component dependencies
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;

    bool              m_enable{true};
    std::vector<Grid> m_grids;
    int               m_grid_index{0};
};

} // namespace editor
