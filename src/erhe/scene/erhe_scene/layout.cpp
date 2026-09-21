#include "erhe_scene/layout.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node_system.hpp"
#include "erhe_primitive/primitive.hpp"

#include "erhe_property/attached_group.hpp"
#include "erhe_property/property_metadata.hpp"

#include <memory>
#include <vector>

namespace erhe::scene {

auto axis_index(const Axis_direction direction) -> int
{
    switch (direction) {
        case Axis_direction::pos_x:
        case Axis_direction::neg_x: return 0;
        case Axis_direction::pos_y:
        case Axis_direction::neg_y: return 1;
        case Axis_direction::pos_z:
        case Axis_direction::neg_z: return 2;
        default:                    return 0;
    }
}

auto axis_sign(const Axis_direction direction) -> float
{
    switch (direction) {
        case Axis_direction::pos_x:
        case Axis_direction::pos_y:
        case Axis_direction::pos_z: return  1.0f;
        case Axis_direction::neg_x:
        case Axis_direction::neg_y:
        case Axis_direction::neg_z: return -1.0f;
        default:                    return  1.0f;
    }
}

auto axis_vector(const Axis_direction direction) -> glm::vec3
{
    glm::vec3 result{0.0f, 0.0f, 0.0f};
    result[axis_index(direction)] = axis_sign(direction);
    return result;
}

namespace {

// An Aabb is empty (carries no extent) when its minimum exceeds its maximum on
// any axis. The default-constructed Aabb (min = FLT_MAX, max = -FLT_MAX) is empty.
// Note: erhe::math::Aabb::is_valid() uses '||' and is unreliable here, so this
// feature uses its own explicit emptiness test.
[[nodiscard]] auto is_empty(const erhe::math::Aabb& aabb) -> bool
{
    return (aabb.min.x > aabb.max.x) ||
           (aabb.min.y > aabb.max.y) ||
           (aabb.min.z > aabb.max.z);
}

[[nodiscard]] auto node_own_local_aabb(const Node& node) -> erhe::math::Aabb
{
    erhe::math::Aabb aabb{};
    const std::shared_ptr<Mesh> mesh = get_mesh(&node);
    if (mesh) {
        for (const Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
            if (!mesh_primitive.primitive) {
                continue;
            }
            const erhe::math::Aabb local = mesh_primitive.primitive->get_bounding_box();
            if (!is_empty(local)) {
                aabb.include(local);
            }
        }
    }
    return aabb;
}

using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;

constexpr erhe::property::Enum_entry c_layout_type_entries[] = {
    {"None",  static_cast<int32_t>(Layout_type::none)},
    {"Stack", static_cast<int32_t>(Layout_type::stack)},
    {"Grid",  static_cast<int32_t>(Layout_type::grid)},
    {"Flow",  static_cast<int32_t>(Layout_type::flow)},
};

constexpr erhe::property::Enum_entry c_axis_direction_entries[] = {
    {"+X", static_cast<int32_t>(Axis_direction::pos_x)},
    {"-X", static_cast<int32_t>(Axis_direction::neg_x)},
    {"+Y", static_cast<int32_t>(Axis_direction::pos_y)},
    {"-Y", static_cast<int32_t>(Axis_direction::neg_y)},
    {"+Z", static_cast<int32_t>(Axis_direction::pos_z)},
    {"-Z", static_cast<int32_t>(Axis_direction::neg_z)},
};

constexpr erhe::property::Enum_entry c_layout_alignment_entries[] = {
    {"Negative", static_cast<int32_t>(Layout_alignment::negative)},
    {"Positive", static_cast<int32_t>(Layout_alignment::positive)},
    {"Stretch",  static_cast<int32_t>(Layout_alignment::stretch)},
};

constexpr std::string_view c_group      = "Layout";
constexpr std::string_view c_item_group = "Layout Item";

[[nodiscard]] auto as_node(const erhe::property::Dependency_object& object) -> const Node*
{
    return dynamic_cast<const Node*>(&object);
}

// The layout of the node the object is a direct child of, if any: the
// attached hints are listed on (and meaningful for) such children.
[[nodiscard]] auto parent_layout_type(const erhe::property::Dependency_object& object) -> Layout_type
{
    const Node* const node = as_node(object);
    if (node == nullptr) {
        return Layout_type::none;
    }
    const std::shared_ptr<Node> parent = node->get_parent_node();
    if (!parent) {
        return Layout_type::none;
    }
    return parent->get_value(Layout::type_property);
}

[[nodiscard]] auto under_layout(const erhe::property::Dependency_object& object) -> bool
{
    return parent_layout_type(object) != Layout_type::none;
}

[[nodiscard]] auto under_grid_layout(const erhe::property::Dependency_object& object) -> bool
{
    return parent_layout_type(object) == Layout_type::grid;
}

[[nodiscard]] auto under_grid_layout_explicit_cell(const erhe::property::Dependency_object& object) -> bool
{
    return under_grid_layout(object) && !object.get_value(Layout::grid_cell_auto_property);
}

[[nodiscard]] auto positive_tracks(const Property_value& value) -> bool
{
    const glm::ivec3 v = std::get<glm::ivec3>(value);
    return (v.x >= 1) && (v.y >= 1) && (v.z >= 1);
}

[[nodiscard]] auto non_negative_cell(const Property_value& value) -> bool
{
    const glm::ivec3 v = std::get<glm::ivec3>(value);
    return (v.x >= 0) && (v.y >= 0) && (v.z >= 0);
}

// Coerce (D7) of a per-axis grid track extent list: an empty list means
// uniform tracks and is left alone; a non-empty one carries exactly one
// extent per track of that axis, padded with zero-size tracks. The rule
// depends on another value of the group (grid_track_count), so the list is
// sized here - where the value is produced - and a change of the track count
// re-runs it from grid_track_count's property_changed callback.
//
// Only a layout node is coerced. A plain node or a Style may hold the list
// for the layout nodes below it (D30); such a holder has no track count of
// its own to size against, so it keeps the list as authored and the layout
// node that reads it coerces its own copy.
[[nodiscard]] auto coerce_track_extent(
    const erhe::property::Dependency_object& object,
    const Property_value&                    value,
    const int                                axis
) -> Property_value
{
    const std::vector<float>* const extents = std::get_if<std::vector<float>>(&value);
    if ((extents == nullptr) || extents->empty()) {
        return value;
    }
    const Node* const node = as_node(object);
    if ((node == nullptr) || !carries_layout(*node)) {
        return value;
    }
    const glm::ivec3  track_count = node->get_value(Layout::grid_track_count_property);
    const int         axis_count  = track_count[axis];
    const std::size_t count       = static_cast<std::size_t>((axis_count > 1) ? axis_count : 1);
    if (extents->size() == count) {
        return value;
    }
    std::vector<float> sized = *extents;
    sized.resize(count, 0.0f);
    return Property_value{std::move(sized)};
}

// The track count is what sizes a non-empty extent list, so a list already
// stored in its coerced form is re-coerced when the count moves: change
// driven, and never from the draw code.
void grid_track_count_changed(
    erhe::property::Dependency_object&           object,
    const erhe::property::Property_changed_args& args
)
{
    for (int axis = 0; axis < 3; ++axis) {
        object.coerce_value(Layout::grid_track_extent_property(axis).get());
    }
    node_system_property_changed(object, args);
}

} // anonymous namespace

const erhe::property::Enum_info c_layout_type_enum_info     {"Layout_type",      c_layout_type_entries};
const erhe::property::Enum_info c_axis_direction_enum_info  {"Axis_direction",   c_axis_direction_entries};
const erhe::property::Enum_info c_layout_alignment_enum_info{"Layout_alignment", c_layout_alignment_entries};

const char* const c_layout_type_strings[]    = {"None", "Stack", "Grid", "Flow"};
const char* const c_axis_direction_strings[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};

auto Layout::property_owner_type() -> erhe::property::Owner_type
{
    // Layout is not a Dependency_object, so there is no Item<> to allocate the
    // id: the registering class's own id sits directly under the root and
    // serves only to qualify the names (Layout.type).
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(
        erhe::property::root_owner_type, "Layout"
    );
    return s_id;
}

// The key property comes first, so its registration is complete when the rest
// of the group takes attached_group_visible_when on it (D1).
const Property<Layout_type> Layout::type_property = Property<Layout_type>::register_attached(
    "type", Layout::property_owner_type(), Node::property_owner_type(), c_layout_type_enum_info,
    Property_metadata{
        .default_value    = erhe::property::make_value(Layout_type::none),
        .property_changed = node_system_property_changed,
        .ui               = Property_ui{
            .group   = c_group,
            .tooltip = "Arrange this node's children: stacked along the primary axis, in an explicit cell grid, or in lines wrapped into sheets. 'None' means the node arranges nothing",
            .label   = "Type"
        }
    }
);

namespace {

// The grid rows are of use only while the node asks for a grid.
[[nodiscard]] auto is_grid_node(const erhe::property::Dependency_object& object) -> bool
{
    const Node* const node = as_node(object);
    return (node != nullptr) && (node->get_value(Layout::type_property) == Layout_type::grid);
}

// Every non-key container value is listed exactly on the layout nodes (D1),
// and a change of any of them reaches the scene's layout system.
[[nodiscard]] auto group_visible_when() -> Property_ui::Visible_when
{
    return erhe::property::attached_group_visible_when(Layout::type_property.get());
}

[[nodiscard]] auto grid_visible_when() -> Property_ui::Visible_when
{
    return erhe::property::attached_group_visible_when(Layout::type_property.get(), is_grid_node);
}

} // anonymous namespace

const Property<glm::vec3> Layout::volume_min_property = Property<glm::vec3>::register_attached(
    "volume_min", Layout::property_owner_type(), Node::property_owner_type(),
    Property_metadata{
        .default_value    = glm::vec3{-0.5f, -0.5f, -0.5f},
        .property_changed = node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{.step = 0.01f, .group = c_group, .tooltip = "Local-space box the children are arranged in", .label = "Volume Min", .visible_when = group_visible_when()}
    }
);
const Property<glm::vec3> Layout::volume_max_property = Property<glm::vec3>::register_attached(
    "volume_max", Layout::property_owner_type(), Node::property_owner_type(),
    Property_metadata{
        .default_value    = glm::vec3{0.5f, 0.5f, 0.5f},
        .property_changed = node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{.step = 0.01f, .group = c_group, .tooltip = "Local-space box the children are arranged in", .label = "Volume Max", .visible_when = group_visible_when()}
    }
);
const Property<Axis_direction> Layout::primary_property = Property<Axis_direction>::register_attached(
    "primary", Layout::property_owner_type(), Node::property_owner_type(), c_axis_direction_enum_info,
    Property_metadata{
        .default_value    = erhe::property::make_value(Axis_direction::pos_x),
        .property_changed = node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{.group = c_group, .tooltip = "Signed axis the children advance along", .label = "Primary", .visible_when = group_visible_when()}
    }
);
const Property<Axis_direction> Layout::secondary_property = Property<Axis_direction>::register_attached(
    "secondary", Layout::property_owner_type(), Node::property_owner_type(), c_axis_direction_enum_info,
    Property_metadata{
        .default_value    = erhe::property::make_value(Axis_direction::pos_y),
        .property_changed = node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{.group = c_group, .tooltip = "Signed axis lines wrap into (grid and flow)", .label = "Secondary", .visible_when = group_visible_when()}
    }
);
const Property<Axis_direction> Layout::tertiary_property = Property<Axis_direction>::register_attached(
    "tertiary", Layout::property_owner_type(), Node::property_owner_type(), c_axis_direction_enum_info,
    Property_metadata{
        .default_value    = erhe::property::make_value(Axis_direction::pos_z),
        .property_changed = node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{.group = c_group, .tooltip = "Signed axis sheets stack along (grid and flow)", .label = "Tertiary", .visible_when = group_visible_when()}
    }
);
const Property<glm::vec3> Layout::gap_property = Property<glm::vec3>::register_attached(
    "gap", Layout::property_owner_type(), Node::property_owner_type(),
    Property_metadata{
        .default_value    = glm::vec3{0.0f, 0.0f, 0.0f},
        .property_changed = node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{.min = 0.0f, .max = 10000.0f, .step = 0.01f, .group = c_group, .tooltip = "Spacing per level: primary, secondary, tertiary", .label = "Gap", .visible_when = group_visible_when()}
    }
);
const Property<glm::ivec3> Layout::grid_track_count_property = Property<glm::ivec3>::register_attached(
    "grid_track_count", Layout::property_owner_type(), Node::property_owner_type(),
    Property_metadata{
        .default_value    = glm::ivec3{1, 1, 1},
        .property_changed = grid_track_count_changed,
        .inherits         = true,
        .ui               = Property_ui{.min = 1.0f, .max = 1000.0f, .step = 0.1f, .group = c_group, .tooltip = "Grid: cells per axis", .label = "Grid Tracks", .visible_when = grid_visible_when()}
    },
    positive_tracks
);

const Property<std::vector<float>> Layout::grid_track_extent_x_property = Property<std::vector<float>>::register_attached(
    "grid_track_extent_x", Layout::property_owner_type(), Node::property_owner_type(),
    Property_metadata{
        .default_value    = Property_value{std::vector<float>{}},
        .property_changed = node_system_property_changed,
        .coerce           = [](const erhe::property::Dependency_object& object, const Property_value& value) -> Property_value { return coerce_track_extent(object, value, 0); },
        .inherits         = true,
        .ui               = Property_ui{.min = 0.0f, .max = 10000.0f, .step = 0.01f, .group = c_group, .tooltip = "Grid: size of each track along X; empty = uniform tracks", .label = "Sizes X", .visible_when = grid_visible_when()}
    }
);
const Property<std::vector<float>> Layout::grid_track_extent_y_property = Property<std::vector<float>>::register_attached(
    "grid_track_extent_y", Layout::property_owner_type(), Node::property_owner_type(),
    Property_metadata{
        .default_value    = Property_value{std::vector<float>{}},
        .property_changed = node_system_property_changed,
        .coerce           = [](const erhe::property::Dependency_object& object, const Property_value& value) -> Property_value { return coerce_track_extent(object, value, 1); },
        .inherits         = true,
        .ui               = Property_ui{.min = 0.0f, .max = 10000.0f, .step = 0.01f, .group = c_group, .tooltip = "Grid: size of each track along Y; empty = uniform tracks", .label = "Sizes Y", .visible_when = grid_visible_when()}
    }
);
const Property<std::vector<float>> Layout::grid_track_extent_z_property = Property<std::vector<float>>::register_attached(
    "grid_track_extent_z", Layout::property_owner_type(), Node::property_owner_type(),
    Property_metadata{
        .default_value    = Property_value{std::vector<float>{}},
        .property_changed = node_system_property_changed,
        .coerce           = [](const erhe::property::Dependency_object& object, const Property_value& value) -> Property_value { return coerce_track_extent(object, value, 2); },
        .inherits         = true,
        .ui               = Property_ui{.min = 0.0f, .max = 10000.0f, .step = 0.01f, .group = c_group, .tooltip = "Grid: size of each track along Z; empty = uniform tracks", .label = "Sizes Z", .visible_when = grid_visible_when()}
    }
);

auto Layout::grid_track_extent_property(const int axis) -> const Property<std::vector<float>>&
{
    switch (axis) {
        case 1:  return grid_track_extent_y_property;
        case 2:  return grid_track_extent_z_property;
        default: return grid_track_extent_x_property;
    }
}

auto Layout::container_properties() -> const std::vector<const erhe::property::Dependency_property*>&
{
    static const std::vector<const erhe::property::Dependency_property*> s_properties = {
        type_property.get_ptr(),
        volume_min_property.get_ptr(),
        volume_max_property.get_ptr(),
        primary_property.get_ptr(),
        secondary_property.get_ptr(),
        tertiary_property.get_ptr(),
        gap_property.get_ptr(),
        grid_track_count_property.get_ptr(),
        grid_track_extent_x_property.get_ptr(),
        grid_track_extent_y_property.get_ptr(),
        grid_track_extent_z_property.get_ptr()
    };
    return s_properties;
}

const Property<Layout_alignment> Layout::align_x_property = Property<Layout_alignment>::register_attached(
    "align_x", Layout::property_owner_type(), Node::property_owner_type(), c_layout_alignment_enum_info,
    Property_metadata{.default_value = erhe::property::make_value(Layout_alignment::negative), .ui = Property_ui{.group = c_item_group, .tooltip = "Pin to the cell's minimum or maximum face, or stretch to fill it", .label = "Align X", .visible_when = under_layout}}
);
const Property<Layout_alignment> Layout::align_y_property = Property<Layout_alignment>::register_attached(
    "align_y", Layout::property_owner_type(), Node::property_owner_type(), c_layout_alignment_enum_info,
    Property_metadata{.default_value = erhe::property::make_value(Layout_alignment::negative), .ui = Property_ui{.group = c_item_group, .tooltip = "Pin to the cell's minimum or maximum face, or stretch to fill it", .label = "Align Y", .visible_when = under_layout}}
);
const Property<Layout_alignment> Layout::align_z_property = Property<Layout_alignment>::register_attached(
    "align_z", Layout::property_owner_type(), Node::property_owner_type(), c_layout_alignment_enum_info,
    Property_metadata{.default_value = erhe::property::make_value(Layout_alignment::negative), .ui = Property_ui{.group = c_item_group, .tooltip = "Pin to the cell's minimum or maximum face, or stretch to fill it", .label = "Align Z", .visible_when = under_layout}}
);
const Property<glm::vec3> Layout::margin_min_property = Property<glm::vec3>::register_attached(
    "margin_min", Layout::property_owner_type(), Node::property_owner_type(),
    Property_metadata{.default_value = glm::vec3{0.0f, 0.0f, 0.0f}, .ui = Property_ui{.step = 0.01f, .group = c_item_group, .tooltip = "Inset at the cell minimum face", .label = "Margin Min", .visible_when = under_layout}}
);
const Property<glm::vec3> Layout::margin_max_property = Property<glm::vec3>::register_attached(
    "margin_max", Layout::property_owner_type(), Node::property_owner_type(),
    Property_metadata{.default_value = glm::vec3{0.0f, 0.0f, 0.0f}, .ui = Property_ui{.step = 0.01f, .group = c_item_group, .tooltip = "Inset at the cell maximum face", .label = "Margin Max", .visible_when = under_layout}}
);
const Property<bool> Layout::grid_cell_auto_property = Property<bool>::register_attached(
    "grid_cell_auto", Layout::property_owner_type(), Node::property_owner_type(),
    Property_metadata{.default_value = true, .ui = Property_ui{.group = c_item_group, .tooltip = "Grid: place into the next free cell; off = use Grid Cell", .label = "Auto Cell", .visible_when = under_grid_layout}}
);
const Property<glm::ivec3> Layout::grid_cell_property = Property<glm::ivec3>::register_attached(
    "grid_cell", Layout::property_owner_type(), Node::property_owner_type(),
    Property_metadata{.default_value = glm::ivec3{0, 0, 0}, .ui = Property_ui{.min = 0.0f, .max = 1000.0f, .step = 0.1f, .group = c_item_group, .tooltip = "Grid: explicit cell (i, j, k)", .label = "Grid Cell", .visible_when = under_grid_layout_explicit_cell}},
    non_negative_cell
);
const Property<glm::ivec3> Layout::grid_span_property = Property<glm::ivec3>::register_attached(
    "grid_span", Layout::property_owner_type(), Node::property_owner_type(),
    Property_metadata{.default_value = glm::ivec3{1, 1, 1}, .ui = Property_ui{.min = 1.0f, .max = 1000.0f, .step = 0.1f, .group = c_item_group, .tooltip = "Grid: cells spanned per axis; honored for auto placement too", .label = "Grid Span", .visible_when = under_grid_layout}},
    positive_tracks
);

auto carries_layout(const Node& node) -> bool
{
    return erhe::property::carries_attached_group(node, Layout::type_property.get());
}

auto read_layout(const Node& node) -> std::optional<Layout_data>
{
    if (!carries_layout(node)) {
        return {};
    }
    Layout_data data;
    data.type             = node.get_value(Layout::type_property);
    data.volume.min       = node.get_value(Layout::volume_min_property);
    data.volume.max       = node.get_value(Layout::volume_max_property);
    data.primary          = node.get_value(Layout::primary_property);
    data.secondary        = node.get_value(Layout::secondary_property);
    data.tertiary         = node.get_value(Layout::tertiary_property);
    data.gap              = node.get_value(Layout::gap_property);
    data.grid_track_count = node.get_value(Layout::grid_track_count_property);
    for (int axis = 0; axis < 3; ++axis) {
        data.grid_track_extent[static_cast<std::size_t>(axis)] = node.get_value(Layout::grid_track_extent_property(axis));
    }
    return data;
}

auto get_layout_volume(const Node& node, erhe::math::Aabb& out) -> bool
{
    if (!carries_layout(node)) {
        return false;
    }
    out.min = node.get_value(Layout::volume_min_property);
    out.max = node.get_value(Layout::volume_max_property);
    return true;
}

auto measure_child_content(const Node& child) -> erhe::math::Aabb
{
    erhe::math::Aabb volume;
    if (get_layout_volume(child, volume)) {
        return volume;
    }
    return compute_content_local_aabb(child);
}

auto compute_content_local_aabb(const Node& node) -> erhe::math::Aabb
{
    erhe::math::Aabb aabb = node_own_local_aabb(node);

    for (const std::shared_ptr<erhe::Hierarchy>& child_item : node.get_children()) {
        const std::shared_ptr<Node> child = std::dynamic_pointer_cast<Node>(child_item);
        if (!child) {
            continue;
        }
        const erhe::math::Aabb child_box = measure_child_content(*child);
        if (!is_empty(child_box)) {
            const glm::mat4 child_from_node = child->parent_from_node(); // child-local -> node-local
            aabb.include(child_box.transformed_by(child_from_node));
        }
    }
    return aabb;
}

} // namespace erhe::scene
