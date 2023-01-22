#pragma once

#include "tools/tool.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/scene/node.hpp"

#include <glm/glm.hpp>

namespace editor
{

// TODO Negative half planes
enum class Grid_plane_type : unsigned int
{
    XZ = 0,
    XY,
    YZ,
    Node
};

static constexpr const char* grid_plane_type_strings[] =
{
    "XZ-Plane Y+",
    "XY-Plane Z+",
    "YZ-Plane X+",
    "Node"
};

auto get_plane_transform(Grid_plane_type plane_type) -> glm::dmat4;

class Grid
    : public erhe::scene::Node_attachment
{
public:
    Grid();

    // Implements Node_attachment
    [[nodiscard]] auto type_name() const -> const char* override;

    // Public API
    [[nodiscard]] auto get_name           () const -> const std::string&;
    [[nodiscard]] auto snap_world_position(const glm::dvec3& position_in_world) const -> glm::dvec3;
    [[nodiscard]] auto snap_grid_position (const glm::dvec3& position_in_grid ) const -> glm::dvec3;
    [[nodiscard]] auto world_from_grid    () const -> glm::dmat4;
    [[nodiscard]] auto grid_from_world    () const -> glm::dmat4;
    [[nodiscard]] auto intersect_ray(
        const glm::dvec3& ray_origin_in_world,
        const glm::dvec3& ray_direction_in_world
    ) -> std::optional<glm::dvec3>;

    void render(const Render_context& context);
    void imgui ();

private:
    std::string     m_name;
    Grid_plane_type m_plane_type      {Grid_plane_type::XZ};
    double          m_rotation        {0.0}; // Used only if plane type != node
    glm::dvec3      m_center          {0.0}; // Used only if plane type != node
    bool            m_enable          {true};
    bool            m_see_hidden_major{false};
    bool            m_see_hidden_minor{false};
    float           m_cell_size       {1.0f};
    int             m_cell_div        {2};
    int             m_cell_count      {10};
    float           m_major_width     {4.0f};
    float           m_minor_width     {2.0f};
    glm::vec4       m_major_color     {0.0f, 0.0f, 0.0f, 0.729f};
    glm::vec4       m_minor_color     {0.0f, 0.0f, 0.0f, 0.737f};
    glm::dmat4      m_world_from_grid {1.0f};
    glm::dmat4      m_grid_from_world {1.0f};
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
    void deinitialize_component     () override;

    // Implements Tool
    void tool_render(const Render_context& context)  override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    void viewport_toolbar(bool& hovered);

    auto update_hover(
        const glm::dvec3 ray_origin,
        const glm::dvec3 ray_direction
    ) const -> Grid_hover_position;

private:
    bool                               m_enable{true};
    std::vector<std::shared_ptr<Grid>> m_grids;
    int                                m_grid_index{0};
};

extern Grid_tool* g_grid_tool;

} // namespace editor
