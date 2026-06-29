#pragma once

#include "renderers/render_context.hpp"

#include "erhe_scene/node_attachment.hpp"

#include <glm/glm.hpp>

#include <array>

struct Grid_config;

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

class Grid : public erhe::Item<erhe::Item_base, erhe::scene::Node_attachment, Grid, erhe::Item_kind::clone_using_custom_clone_constructor>
{
public:
    Grid();
    Grid(const Grid& src, erhe::for_clone);
    // Implements Item_base
    static constexpr std::string_view static_type_name{"Grid"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::node_attachment | erhe::Item_type::grid; }

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
    void read_config     (const Grid_config& config);
    void write_config    (Grid_config& config) const;
    void set_snap_enabled(bool snap_enabled) { m_snap_enabled = snap_enabled; }
    void set_cell_size   (float cell_size) { m_cell_size = cell_size; }
    void set_cell_div    (int cell_div) { m_cell_div = cell_div; }
    void set_cell_count  (int cell_count) { m_cell_count = cell_count; }

private:
    void update();

    Grid_plane_type m_plane_type      {Grid_plane_type::XZ};
    bool            m_intersect_enable{true};
    bool            m_snap_enabled    {true};
    float           m_rotation        {0.0f}; // Used only if plane type != node
    glm::vec3       m_center          {0.0f}; // Used only if plane type != node
    float           m_cell_size       {1.0f};
    int             m_cell_div        {2};
    // Bounds the ray-intersection (snap) region; the rendered grid is
    // infinite. Config-only, not exposed in the UI.
    int             m_cell_count      {100};
    bool            m_label_enable       {true};  // grid.frag axis coordinate labels
    float           m_label_text_fraction{0.15f}; // text height as fraction of label spacing
    float           m_label_spacing      {1.0f};  // label spacing in world units (integer >= 1)
    float           m_label_fade         {4.0f};  // pixels per em for full label visibility (smaller = visible further)
    // Per-LOD-level line colors, line widths (fraction of the level cell
    // size) and axis label color for the grid composition pass
    // (grid.frag). Defaults match Grid_parameters.
    std::array<glm::vec4, 4> m_level_colors{
        glm::vec4{0.0f,  0.0f,  0.01f, 1.0f},
        glm::vec4{0.0f,  0.0f,  0.0f,  1.0f},
        glm::vec4{0.01f, 0.0f,  0.0f,  1.0f},
        glm::vec4{0.0f,  0.01f, 0.0f,  1.0f}
    };
    std::array<float, 4> m_level_widths{0.006f, 0.02f, 0.02f, 0.02f};
    glm::vec4       m_label_color        {0.0f, 0.0f, 0.0f, 1.0f};
    glm::mat4       m_world_from_grid {1.0f};
    glm::mat4       m_grid_from_world {1.0f};
};

}
