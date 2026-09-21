#pragma once

#include "grid/grid_frame.hpp"
#include "renderers/render_context.hpp"

#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"
// erhe::scene::Node is an alias of Xformable, so it cannot be forward declared.
#include "erhe_scene/node.hpp"
#include "erhe_scene/transform_observer.hpp"

#include <glm/glm.hpp>

#include <array>
#include <memory>

struct Grid_config;

namespace erhe::renderer { class Line_renderer_set; }
namespace erhe::scene    { class Camera; }

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

// A grid is an item of its own, owned by Grid_tool in every case
// (doc/editor/grid.md, doc/plans/node_attachments_to_properties.md D6). It is
// editor-settings content that outlives every scene, so it is never part of a
// scene hierarchy and is never cloned.
class Grid : public erhe::Item<erhe::Item_base, erhe::Item_base, Grid, erhe::Item_kind::not_clonable>
{
public:
    Grid();
    // Implements Item_base
    static constexpr std::string_view static_type_name{"Grid"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::grid; }

    // Registered properties (erhe::property, doc/erhe/property_system.md
    // section 4.11), stored in the entry store and inheriting from the
    // node chain (D30): a node or a style holds "Grid.cell_size" for the
    // grids below it. The members below are a mirror of the effective
    // values kept current by on_property_changed, which also asks the
    // settings store for the autosave and re-derives the grid transform
    // after plane_type, center or rotation.
    static const erhe::property::Property<Grid_plane_type> plane_type_property;
    // D6: the node whose world transform the Node plane follows. Null is the
    // world frame. Session state - a grid lives in the editor settings, which
    // cannot name a node of a scene - so it carries no serialize flag.
    static const erhe::property::Property<erhe::property::Weak_object_reference> frame_node_property;
    static const erhe::property::Property<glm::vec3>       center_property;
    static const erhe::property::Property<float>           rotation_property;
    static const erhe::property::Property<bool>            intersect_enable_property;
    static const erhe::property::Property<bool>            snap_enabled_property;
    static const erhe::property::Property<bool>            behind_content_property;
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
    // D6: the node the Node plane follows, null when none is named (or the
    // named one is gone).
    [[nodiscard]] auto get_frame_node     () const -> std::shared_ptr<erhe::scene::Node>;
    void set_frame_node(const std::shared_ptr<erhe::scene::Node>& node);

    // The grid's own plane.
    [[nodiscard]] auto get_frame          () const -> Grid_frame;
    // The plane a view through camera shows and hovers: for a free-plane
    // grid seen through an axis-aligned orthogonal camera, the axis plane
    // facing the camera through the grid origin, with grid x along the
    // camera's right and grid z along its down (upright, unmirrored
    // labels); the grid's own plane otherwise.
    [[nodiscard]] auto get_view_frame     (const erhe::scene::Camera* camera) const -> Grid_frame;
    [[nodiscard]] auto snap_world_position(const Grid_frame& frame, const glm::vec3& position_in_world) const -> glm::vec3;
    [[nodiscard]] auto intersect_ray      (const Grid_frame& frame, const glm::vec3& ray_origin_in_world, const glm::vec3& ray_direction_in_world) const -> std::optional<glm::vec3>;

    void render          (const Render_context& context);
    // The one row that is not a property: the name. Returns true when it
    // edited the grid, so the caller can schedule the settings autosave;
    // the property rows (Dependency_property_rows) schedule it themselves.
    auto imgui           () -> bool;
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
    // D7: re-takes the transform observer token on the node frame_node names,
    // so the grid transform follows that node's world transform on change
    // instead of being re-read every frame.
    void update_frame_node_observer();

    Editor_settings_store* m_settings_store{nullptr};

    Grid_plane_type m_plane_type      {Grid_plane_type::XZ};
    bool            m_intersect_enable{true};
    bool            m_snap_enabled    {true};
    bool            m_behind_content  {false};
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
    // Mirror of frame_node_property and the subscription to it.
    std::weak_ptr<erhe::scene::Node>      m_frame_node;
    erhe::scene::Transform_observer_token m_frame_node_observer;
};

}
