#pragma once

#include "erhe_math/aabb.hpp"
#include "erhe_property/dependency_property.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace erhe::scene {

class Xformable; using Node = Xformable;

// The kind of arrangement a layout node performs on its children. `none` is
// the default of the group's key property `Layout.type`: a node whose type is
// `none` arranges nothing and carries no layout at all
// (doc/erhe/property_system.md section 4.13).
enum class Layout_type : unsigned int {
    none = 0,  // not a layout node
    stack,     // children distributed along a single (primary) signed axis
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

extern const char* const c_layout_type_strings[];      // "None", "Stack", "Grid", "Flow"
extern const char* const c_axis_direction_strings[];   // "+X", "-X", "+Y", "-Y", "+Z", "-Z"

// The effective container values of one layout node, read once per change
// (doc/erhe/property_system.md section 4.13). `Layout_system` keeps one of
// these per layout node and refreshes it when a value changes, so the
// per-frame solve reads a record and allocates nothing.
class Layout_data
{
public:
    Layout_type      type     {Layout_type::stack};
    erhe::math::Aabb volume   {glm::vec3{-0.5f, -0.5f, -0.5f}, glm::vec3{0.5f, 0.5f, 0.5f}};
    Axis_direction   primary  {Axis_direction::pos_x};
    Axis_direction   secondary{Axis_direction::pos_y};
    Axis_direction   tertiary {Axis_direction::pos_z};
    glm::vec3        gap      {0.0f, 0.0f, 0.0f}; // spacing per level (primary, secondary, tertiary)
    glm::ivec3       grid_track_count{1, 1, 1};
    // Grid: per-track extents per axis; an empty list means uniform tracks.
    std::array<std::vector<float>, 3> grid_track_extent{};
};

// A layout as a value group of the node itself
// (doc/plans/node_attachments_to_properties.md D1,
// doc/erhe/property_system.md sections 4.13 and 4.14): a node owns a volume
// (an axis-aligned box in its own local space) and the layout computes the
// local transform of each direct child so the children are arranged inside
// that volume.
//
// `Layout` is a registration holder with static members only, not an item and
// not a Dependency_object: it owns the property registrations (owner type
// `Layout`, so the qualified names are `Layout.type` .. `Layout.grid_span`)
// and the holder of every value is an `erhe::scene::Node`. The runtime state
// the group implies - the solve registration - is owned by `Layout_system`,
// one per scene.
class Layout
{
public:
    Layout() = delete;

    // The registering class's owner type id. Layout has no instances, so it is
    // allocated directly under the root rather than by Item<>.
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;

    // The KEY property, registered first (D1) and with inherits = false: the
    // node is a layout node exactly while its effective type differs from
    // `none`, so a child of a layout node is not itself one.
    static const erhe::property::Property<Layout_type> type_property;

    // The rest of the container values, UI group "Layout". Each of them
    // inherits: an empty node or a Style holds "Layout.gap" for the layout
    // nodes below it (D30).
    static const erhe::property::Property<glm::vec3>      volume_min_property;
    static const erhe::property::Property<glm::vec3>      volume_max_property;
    static const erhe::property::Property<Axis_direction> primary_property;
    static const erhe::property::Property<Axis_direction> secondary_property;
    static const erhe::property::Property<Axis_direction> tertiary_property;
    static const erhe::property::Property<glm::vec3>      gap_property;
    static const erhe::property::Property<glm::ivec3>     grid_track_count_property;

    // Grid: per-track extents per axis, one float_array property per axis
    // (doc/erhe/property_system.md D34). An empty list means uniform tracks;
    // a non-empty list held by a layout node is coerced (D7) to that axis's
    // track count, so the list is sized where the value is produced.
    static const erhe::property::Property<std::vector<float>> grid_track_extent_x_property;
    static const erhe::property::Property<std::vector<float>> grid_track_extent_y_property;
    static const erhe::property::Property<std::vector<float>> grid_track_extent_z_property;

    // The extent property of axis 0, 1 or 2 (any other index gives axis 0).
    [[nodiscard]] static auto grid_track_extent_property(int axis) -> const erhe::property::Property<std::vector<float>>&;

    // Per-child hints as attached properties (R7, doc/erhe/property_system.md
    // section 4.14; WPF Grid.Row): set on the child Node, qualified names
    // "Layout.align_x" .. "Layout.grid_span". The solve reads them from each
    // direct child; a child without a local value is laid out with the
    // defaults below.
    static const erhe::property::Property<Layout_alignment> align_x_property;        // negative
    static const erhe::property::Property<Layout_alignment> align_y_property;        // negative
    static const erhe::property::Property<Layout_alignment> align_z_property;        // negative
    static const erhe::property::Property<glm::vec3>        margin_min_property;     // inset at the cell minimum face
    static const erhe::property::Property<glm::vec3>        margin_max_property;     // inset at the cell maximum face
    static const erhe::property::Property<bool>             grid_cell_auto_property; // grid: true = next free cell, false = grid_cell
    static const erhe::property::Property<glm::ivec3>       grid_cell_property;      // grid: explicit cell (i, j, k)
    static const erhe::property::Property<glm::ivec3>       grid_span_property;      // grid: cells spanned per axis (>= 1)

    // Every container value of the group, registration order, for generic walks.
    [[nodiscard]] static auto container_properties() -> const std::vector<const erhe::property::Dependency_property*>&;
};

// True while the node is a layout node: the key property's effective value
// differs from its default (erhe::property::carries_attached_group).
[[nodiscard]] auto carries_layout(const Node& node) -> bool;

// The effective container values of one node, or nothing when the node is no
// layout node. Copies the per-track extent lists, so this is a change-time
// and query-time reader; the per-frame solve reads the record
// `Layout_system` keeps.
[[nodiscard]] auto read_layout(const Node& node) -> std::optional<Layout_data>;

// The declared volume of a layout node, without reading the rest of the
// record: true and `out` filled when the node is a layout node. Allocates
// nothing, which is what the per-frame measuring pass needs.
[[nodiscard]] auto get_layout_volume(const Node& node, erhe::math::Aabb& out) -> bool;

// Content bounding box of a node expressed in that node's own local space:
// the node's own mesh primitives plus its descendants. A descendant that is
// itself a layout node contributes its declared volume (not its geometry),
// which both matches intent and breaks the recursion cycle.
[[nodiscard]] auto compute_content_local_aabb(const Node& node) -> erhe::math::Aabb;

// The content box of a direct child in the child's own local space, the
// measure every layout algorithm takes: a child that is itself a layout node
// contributes its declared volume.
[[nodiscard]] auto measure_child_content(const Node& child) -> erhe::math::Aabb;

} // namespace erhe::scene
