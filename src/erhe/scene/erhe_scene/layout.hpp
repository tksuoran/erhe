#pragma once

#include "erhe_scene/node_attachment.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_property/dependency_property.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe::scene {

class Xformable; using Node = Xformable;

// The kind of arrangement a Layout performs on its node's children.
enum class Layout_type : unsigned int {
    stack = 0, // children distributed along a single (primary) signed axis
    grid,      // children placed into an explicit X/Y/Z cell grid
    flow       // children wrapped into lines -> sheets -> tertiary stack
};

// Signed principal axis selection (axis + sign).
enum class Axis_direction : unsigned int {
    pos_x = 0,
    neg_x,
    pos_y,
    neg_y,
    pos_z,
    neg_z
};

[[nodiscard]] auto axis_index (Axis_direction direction) -> int;       // 0, 1, 2
[[nodiscard]] auto axis_sign  (Axis_direction direction) -> float;     // +1.0f / -1.0f
[[nodiscard]] auto axis_vector(Axis_direction direction) -> glm::vec3; // signed unit vector

// Alignment of a child within its layout cell, per axis.
//   negative : pin the child to the cell's minimum face
//   positive : pin the child to the cell's maximum face
//   stretch  : scale the child to fill the cell on this axis
enum class Layout_alignment : unsigned int {
    negative = 0,
    positive,
    stretch
};

extern const erhe::property::Enum_info c_layout_type_enum_info;
extern const erhe::property::Enum_info c_axis_direction_enum_info;
extern const erhe::property::Enum_info c_layout_alignment_enum_info;

// A Layout is a Node_attachment that owns a volume (an axis-aligned box in the
// node's local space) and computes the local transform of each direct child
// node so the children are arranged inside that volume.
class Layout : public erhe::Item<Item_base, Node_attachment, Layout, erhe::Item_kind::clone_using_custom_clone_constructor>
{
public:
    using Type = Layout_type;

    static constexpr const char* c_type_strings[] = {
        "Stack",
        "Grid",
        "Flow"
    };
    static constexpr const char* c_axis_direction_strings[] = {
        "+X", "-X", "+Y", "-Y", "+Z", "-Z"
    };

    Layout(const Layout&);
    Layout& operator=(const Layout&) = delete;
    ~Layout() noexcept override;

    explicit Layout(std::string_view name);
    Layout(const Layout& src, erhe::for_clone);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Layout"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return erhe::Item_type::node_attachment | erhe::Item_type::layout;
    }

    // Implements Node_attachment: registers with / unregisters from the
    // Scene_host so Scene::update_layouts() finds this layout without a
    // hierarchy scan.
    void handle_item_host_update(erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;

    // Public API
    // Recompute and apply the local transform of every direct child. The editor
    // calls this once per frame for every registered layout
    // (Scene::update_layouts(), from App_scenes::update_layout_nodes) before
    // the world-transform passes. A dirty/serial-gated optimization is
    // deliberately left for later; recomputing each frame is simple and correct.
    void update();

    // Registered properties (doc/property-system.md section 4.13), stored
    // in the entry store and inheriting from the node chain (D30): an
    // empty node or a style holds "Layout.gap" for the layouts below it.
    // The members are a mirror of the effective values kept current by
    // on_property_changed; update() reads the mirror each frame.
    static const erhe::property::Property<Type>           type_property;
    static const erhe::property::Property<glm::vec3>      volume_min_property;
    static const erhe::property::Property<glm::vec3>      volume_max_property;
    static const erhe::property::Property<Axis_direction> primary_property;
    static const erhe::property::Property<Axis_direction> secondary_property;
    static const erhe::property::Property<Axis_direction> tertiary_property;
    static const erhe::property::Property<glm::vec3>      gap_property;
    static const erhe::property::Property<glm::ivec3>     grid_track_count_property;

    // Per-child hints as attached properties (R7, doc/property-system.md
    // section 4.14; WPF Grid.Row): registered by Layout, set on the child
    // Node, qualified names "Layout.align_x" .. "Layout.grid_span". update()
    // reads them from each direct child; a child without a local value
    // is laid out with the defaults below.
    static const erhe::property::Property<Layout_alignment> align_x_property;        // negative
    static const erhe::property::Property<Layout_alignment> align_y_property;        // negative
    static const erhe::property::Property<Layout_alignment> align_z_property;        // negative
    static const erhe::property::Property<glm::vec3>        margin_min_property;     // inset at the cell minimum face
    static const erhe::property::Property<glm::vec3>        margin_max_property;     // inset at the cell maximum face
    static const erhe::property::Property<bool>             grid_cell_auto_property; // grid: true = next free cell, false = grid_cell
    static const erhe::property::Property<glm::ivec3>       grid_cell_property;      // grid: explicit cell (i, j, k)
    static const erhe::property::Property<glm::ivec3>       grid_span_property;      // grid: cells spanned per axis (>= 1)

    [[nodiscard]] auto get_layout_type     () const -> Type                    { return m_type; }
    [[nodiscard]] auto get_volume          () const -> const erhe::math::Aabb& { return m_volume; }
    [[nodiscard]] auto get_primary         () const -> Axis_direction          { return m_primary; }
    [[nodiscard]] auto get_secondary       () const -> Axis_direction          { return m_secondary; }
    [[nodiscard]] auto get_tertiary        () const -> Axis_direction          { return m_tertiary; }
    [[nodiscard]] auto get_gap             () const -> const glm::vec3&        { return m_gap; }
    [[nodiscard]] auto get_grid_track_count() const -> const glm::ivec3&       { return m_grid_track_count; }
    void set_layout_type     (Type value);
    void set_volume_min      (const glm::vec3& value);
    void set_volume_max      (const glm::vec3& value);
    void set_primary         (Axis_direction value);
    void set_secondary       (Axis_direction value);
    void set_tertiary        (Axis_direction value);
    void set_gap             (const glm::vec3& value);
    void set_grid_track_count(const glm::ivec3& value);

    // Grid: per-track extents per axis; empty = uniform tracks. A list, not
    // a property; edited in place.
    [[nodiscard]] auto get_grid_track_extent(int axis) -> std::vector<float>&             { return m_grid_track_extent[static_cast<std::size_t>(axis)]; }
    [[nodiscard]] auto get_grid_track_extent(int axis) const -> const std::vector<float>& { return m_grid_track_extent[static_cast<std::size_t>(axis)]; }

    // Implements erhe::property::Dependency_object: refreshes the mirror
    // on every change of a Layout property, whatever its source.
    void on_property_changed(const erhe::property::Property_changed_args& args) override;

private:
    void refresh_mirror();

    void layout_stack(Node& layout_node);
    void layout_grid (Node& layout_node);
    void layout_flow (Node& layout_node);

    Type             m_type     {Type::stack};
    erhe::math::Aabb m_volume   {glm::vec3{-0.5f, -0.5f, -0.5f}, glm::vec3{0.5f, 0.5f, 0.5f}};
    Axis_direction   m_primary  {Axis_direction::pos_x};
    Axis_direction   m_secondary{Axis_direction::pos_y};
    Axis_direction   m_tertiary {Axis_direction::pos_z};
    glm::vec3        m_gap      {0.0f, 0.0f, 0.0f}; // spacing per level (primary, secondary, tertiary)
    glm::ivec3       m_grid_track_count{1, 1, 1};
    std::array<std::vector<float>, 3> m_grid_track_extent{}; // grid: per-track extents; empty => uniform
};

// Content bounding box of a node expressed in that node's own local space:
// the node's own mesh primitives plus its descendants. A descendant that is
// itself a layout node contributes its declared volume (not its geometry),
// which both matches intent and breaks the recursion cycle.
[[nodiscard]] auto compute_content_local_aabb(const Node& node) -> erhe::math::Aabb;

} // namespace erhe::scene
