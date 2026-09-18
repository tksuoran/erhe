#pragma once

#include "renderers/render_context.hpp"

#include "erhe_scene/node_attachment.hpp"
#include "erhe_property/dependency_property.hpp"

#include <glm/glm.hpp>

#include <array>

struct Grid_config;

namespace erhe::renderer { class Line_renderer_set; }

namespace editor {

class Editor_settings_store;
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

extern const erhe::property::Enum_info c_grid_plane_type_enum_info;

class Grid : public erhe::Item<erhe::Item_base, erhe::scene::Node_attachment, Grid, erhe::Item_kind::clone_using_custom_clone_constructor>
{
public:
    Grid();
    Grid(const Grid& src, erhe::for_clone);
    // Implements Item_base
    static constexpr std::string_view static_type_name{"Grid"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::node_attachment | erhe::Item_type::grid; }

    // Registered properties (erhe::property, doc/property_system.md
    // section 4.11), stored in the entry store and inheriting from the
    // node chain (D30): a node or a style holds "Grid.cell_size" for the
    // grids below it. The members below are a mirror of the effective
    // values kept current by on_property_changed, which also asks the
    // settings store for the autosave and re-derives the grid transform
    // after plane_type, center or rotation.
    static const erhe::property::Property<Grid_plane_type> plane_type_property;
    static const erhe::property::Property<glm::vec3>       center_property;
    static const erhe::property::Property<float>           rotation_property;
    static const erhe::property::Property<bool>            intersect_enable_property;
    static const erhe::property::Property<bool>            snap_enabled_property;
    static const erhe::property::Property<float>           cell_size_property;
    static const erhe::property::Property<int>             cell_div_property;
    static const erhe::property::Property<int>             cell_count_property;
    static const erhe::property::Property<glm::vec4>       level0_color_property;
    static const erhe::property::Property<glm::vec4>       level1_color_property;
    static const erhe::property::Property<glm::vec4>       level2_color_property;
    static const erhe::property::Property<glm::vec4>       level3_color_property;
    static const erhe::property::Property<float>           level0_width_property;
    static const erhe::property::Property<float>           level1_width_property;
    static const erhe::property::Property<float>           level2_width_property;
    static const erhe::property::Property<float>           level3_width_property;
    static const erhe::property::Property<bool>            label_enable_property;
    static const erhe::property::Property<float>           label_text_fraction_property;
    static const erhe::property::Property<float>           label_spacing_property;
    static const erhe::property::Property<float>           label_fade_property;
    static const erhe::property::Property<glm::vec4>       label_color_property;

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
    // The rows that are not properties: the name, and the host node
    // attach / detach of the Node plane type. Returns true when one of them
    // edited the grid, so the caller can schedule the settings autosave;
    // the property rows (Dependency_property_rows) schedule it themselves.
    auto imgui           (App_context& context) -> bool;
    void read_config     (const Grid_config& config);
    void write_config    (Grid_config& config) const;
    void set_snap_enabled(bool snap_enabled) { set_value(snap_enabled_property, snap_enabled); }
    void set_cell_size   (float cell_size)   { set_value(cell_size_property, cell_size); }
    void set_cell_div    (int cell_div)      { set_value(cell_div_property, cell_div); }
    void set_cell_count  (int cell_count)    { set_value(cell_count_property, cell_count); }
    // The store whose autosave a property change (and the visibility
    // flag) touches; the Grid_tool that owns the grid sets it.
    void set_settings_store(Editor_settings_store* settings_store) { m_settings_store = settings_store; }

    // Overrides Item_base: the visible flag persists with the grid config.
    void handle_flag_bits_update(uint64_t old_flag_bits, uint64_t new_flag_bits) override;

    // Implements erhe::property::Dependency_object: refreshes the mirror
    // on every change of a Grid property, whatever its source, re-derives
    // the transform and touches the settings store.
    void on_property_changed(const erhe::property::Property_changed_args& args) override;

private:
    void update();
    void refresh_mirror();
    void touch_settings();

    Editor_settings_store* m_settings_store{nullptr};

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
