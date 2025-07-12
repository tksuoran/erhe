#pragma once

// #include "tools/tool.hpp"
 #include "renderers/render_context.hpp"

#include "erhe_scene/node_attachment.hpp"

#include <glm/glm.hpp>

namespace erhe::renderer { class Line_renderer_set; }

namespace editor {

class Selection_tool;

// TODO Negative half planes
enum class Grid_plane_type : unsigned int {
    XZ = 0,
    XY,
    YZ,
    Node
};

static constexpr const char* grid_plane_type_strings[] = {
    "XZ-Plane Y+",
    "XY-Plane Z+",
    "YZ-Plane X+",
    "Node"
};

auto get_plane_transform(Grid_plane_type plane_type) -> glm::mat4;

class Grid : public erhe::Item<erhe::Item_base, erhe::scene::Node_attachment, Grid>
{
public:
    Grid();
    // Implements Item_base
    static constexpr std::string_view static_type_name{"Grid"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    // Public API
    [[nodiscard]] auto is_snap_enabled    () const -> bool { return m_snap_enabled; }
    [[nodiscard]] auto snap_world_position(const glm::vec3& position_in_world) const -> glm::vec3;
    [[nodiscard]] auto snap_grid_position (const glm::vec3& position_in_grid ) const -> glm::vec3;
    [[nodiscard]] auto world_from_grid    () const -> glm::mat4;
    [[nodiscard]] auto grid_from_world    () const -> glm::mat4;
    [[nodiscard]] auto intersect_ray      (const glm::vec3& ray_origin_in_world, const glm::vec3& ray_direction_in_world) -> std::optional<glm::vec3>;
    [[nodiscard]] auto normal_in_world    () const -> glm::vec3;
    [[nodiscard]] auto tangent_in_world   () const -> glm::vec3;
    [[nodiscard]] auto bitangent_in_world () const -> glm::vec3;
    [[nodiscard]] auto get_cell_size      () const -> float;

    void render          (const Render_context& context);
    void imgui           (App_context& context);
    void set_snap_enabled(bool snap_enabled) { m_snap_enabled = snap_enabled; }
    void set_cell_size   (float cell_size) { m_cell_size = cell_size; }
    void set_cell_div    (int cell_div) { m_cell_div = cell_div; }
    void set_cell_count  (int cell_count) { m_cell_count = cell_count; }
    void set_major_color (glm::vec4 color) { m_major_color = color; }
    void set_minor_color (glm::vec4 color) { m_minor_color = color; }
    void set_major_width (float width) { m_major_width = width; }
    void set_minor_width (float width) { m_minor_width = width; }

private:
    void render(const Render_context& context, bool major);
    void update();

    Grid_plane_type m_plane_type      {Grid_plane_type::XZ};
    bool            m_intersect_enable{true};
    bool            m_snap_enabled    {true};
    float           m_rotation        {0.0f}; // Used only if plane type != node
    glm::vec3       m_center          {0.0f}; // Used only if plane type != node
    bool            m_see_hidden_major{false};
    bool            m_see_hidden_minor{false};
    float           m_cell_size       {1.0f};
    int             m_cell_div        {2};
    int             m_cell_count      {10};
    float           m_major_width     {4.0f};
    float           m_minor_width     {2.0f};
    glm::vec4       m_major_color     {0.0f, 0.0f, 0.0f, 0.729f};
    glm::vec4       m_minor_color     {0.0f, 0.0f, 0.0f, 0.737f};
    glm::mat4       m_world_from_grid {1.0f};
    glm::mat4       m_grid_from_world {1.0f};
};

}
