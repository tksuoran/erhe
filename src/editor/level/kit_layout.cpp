#include "level/kit_layout.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_property/attached_group.hpp"
#include "erhe_property/enum_info.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_scene/curve.hpp"
#include "erhe_scene/node.hpp"

#include <fmt/format.h>


namespace editor {

namespace {

using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;

constexpr std::string_view c_group = "Kit Layout";

// The labels are the codegen enum's names, the text form of the Level Kit window setting and the MCP key (U52).
constexpr erhe::property::Enum_entry c_surface_support_entries[] = {
    { "five_lines",      static_cast<int32_t>(Surface_support::five_lines)      },
    { "center_blocks",   static_cast<int32_t>(Surface_support::center_blocks)   },
    { "center_fallback", static_cast<int32_t>(Surface_support::center_fallback) }
};
const erhe::property::Enum_info c_surface_support_enum_info{"Surface_support", c_surface_support_entries};

[[nodiscard]] auto make_metadata(erhe::property::Property_value default_value, const char* label, const char* tooltip, const bool key) -> Property_metadata
{
    return Property_metadata{
        .default_value = std::move(default_value),
        .inherits      = false,
        .ui            = Property_ui{
            .group          = c_group,
            .tooltip        = tooltip,
            .developer_only = true,
            .label          = label,
            .visible_when   = key ? Property_ui::Visible_when{} : erhe::property::attached_group_visible_when(Kit_layout::pattern_property.get())
        }
    };
}

} // anonymous namespace

auto Kit_layout::property_owner_type() -> erhe::property::Owner_type
{
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(
        erhe::property::root_owner_type, "Kit_layout"
    );
    return s_id;
}

// The key property comes first, so the rest of the group can take attached_group_visible_when on it.
const Property<std::string> Kit_layout::pattern_property = Property<std::string>::register_attached(
    "pattern", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(std::string{}, "Pattern", "The arrangement as canonical JSON text in the node's frame (doc/editor/level_tools.md)", true)
);
const Property<std::string> Kit_layout::items_property = Property<std::string>::register_attached(
    "items", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(std::string{}, "Items", "The kit items placed, element i taking item i modulo the item count", false)
);
const Property<bool> Kit_layout::module_snap_property = Property<bool>::register_attached(
    "module_snap", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(false, "Snap To Module", "Element positions round to the module", false)
);
const Property<bool> Kit_layout::surface_snap_property = Property<bool>::register_attached(
    "surface_snap", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(false, "Snap To Surface", "Each element snaps to the surface under it", false)
);
const Property<bool> Kit_layout::surface_orient_property = Property<bool>::register_attached(
    "surface_orient", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(false, "Orient To Surface", "Each element's up axis takes the surface normal", false)
);
const Property<glm::vec3> Kit_layout::surface_direction_property = Property<glm::vec3>::register_attached(
    "surface_direction", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(c_default_surface_direction, "Surface Direction", "World space: the direction an element searches along to snap to the surface; down by default, up for a ceiling", false)
);
const Property<float> Kit_layout::surface_forward_distance_property = Property<float>::register_attached(
    "surface_forward_distance", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(c_default_surface_forward_distance, "Surface Forward Distance", "Meters: an element moves at most this far along the search direction to snap to the surface; beyond, it stays", false)
);
const Property<float> Kit_layout::surface_backward_distance_property = Property<float>::register_attached(
    "surface_backward_distance", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(c_default_surface_backward_distance, "Surface Backward Distance", "Meters: an element moves at most this far against the search direction to snap to the surface; 0 searches along it only", false)
);
const Property<Surface_support> Kit_layout::surface_support_property = Property<Surface_support>::register_attached(
    "surface_support", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(), c_surface_support_enum_info,
    make_metadata(
        erhe::property::make_value(Surface_support::five_lines), "Surface Support",
        "Which base lines support and block an element's snap without orient: all five (base center and footprint corners), "
        "all five supporting with only the center blocking, or the nearest place with a refused element resting on the surface under its base center, along the search direction",
        false
    )
);
const Property<float> Kit_layout::legacy_surface_max_distance_property = Property<float>::register_attached(
    "surface_max_distance", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(c_default_surface_max_distance, "Surface Max Distance (Legacy)", "Saved before U51: meters searched either way; read as the forward and backward distance a node does not hold", false)
);
const Property<bool> Kit_layout::per_piece_seed_property = Property<bool>::register_attached(
    "per_piece_seed", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(false, "Per Piece Seed", "Each element takes a seed derived from the layout seed and its index", false)
).with_former_name("random_seeds");
const Property<int> Kit_layout::seed_property = Property<int>::register_attached(
    "seed", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(0, "Seed", "The layout seed the element seeds derive from", false)
);
const Property<bool> Kit_layout::locked_property = Property<bool>::register_attached(
    "locked", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(false, "Locked", "Locked, the layout makes its children and they are not edited directly", false)
);

const Property<bool> Kit_layout::hide_all_connectors_property = Property<bool>::register_attached(
    "hide_all_connectors", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(false, "Hide All Connectors", "Path and segments: the children's connectors are hidden, an open curve's first and last child keeping all but the one facing its neighbour", false)
);
const Property<bool> Kit_layout::hide_plus_tangent_connectors_property = Property<bool>::register_attached(
    "hide_plus_tangent_connectors", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(false, "Hide +Tangent Connectors", "Path and segments: the children's connectors facing along the curve are hidden (an open curve's first child included)", false)
);
const Property<bool> Kit_layout::hide_minus_tangent_connectors_property = Property<bool>::register_attached(
    "hide_minus_tangent_connectors", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(false, "Hide -Tangent Connectors", "Path and segments: the children's connectors facing back along the curve are hidden (an open curve's last child included)", false)
);
const Property<bool> Kit_layout::hide_plus_normal_connectors_property = Property<bool>::register_attached(
    "hide_plus_normal_connectors", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(false, "Hide +Normal Connectors", "Path and segments: the inner children's connectors facing the right of the curve's direction are hidden", false)
);
const Property<bool> Kit_layout::hide_minus_normal_connectors_property = Property<bool>::register_attached(
    "hide_minus_normal_connectors", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(false, "Hide -Normal Connectors", "Path and segments: the inner children's connectors facing the left of the curve's direction are hidden", false)
);
const Property<bool> Kit_layout::hide_plus_binormal_connectors_property = Property<bool>::register_attached(
    "hide_plus_binormal_connectors", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(false, "Hide +Binormal Connectors", "Path and segments: the inner children's connectors facing up are hidden", false)
);
const Property<bool> Kit_layout::hide_minus_binormal_connectors_property = Property<bool>::register_attached(
    "hide_minus_binormal_connectors", Kit_layout::property_owner_type(), erhe::scene::Node::property_owner_type(),
    make_metadata(false, "Hide -Binormal Connectors", "Path and segments: the inner children's connectors facing down are hidden", false)
);

auto Kit_layout::get_hide_direction_property(const Kit_layout_direction direction) -> const Property<bool>&
{
    switch (direction) {
        case Kit_layout_direction::plus_tangent:   return hide_plus_tangent_connectors_property;
        case Kit_layout_direction::minus_tangent:  return hide_minus_tangent_connectors_property;
        case Kit_layout_direction::plus_normal:    return hide_plus_normal_connectors_property;
        case Kit_layout_direction::minus_normal:   return hide_minus_normal_connectors_property;
        case Kit_layout_direction::plus_binormal:  return hide_plus_binormal_connectors_property;
        case Kit_layout_direction::minus_binormal:
        default:                                   return hide_minus_binormal_connectors_property;
    }
}

auto is_layout_connector_switch_property(const erhe::property::Dependency_property& property) -> bool
{
    for (const erhe::property::Dependency_property* const member : {
        &Kit_layout::hide_all_connectors_property.get(),
        &Kit_layout::hide_plus_tangent_connectors_property.get(),
        &Kit_layout::hide_minus_tangent_connectors_property.get(),
        &Kit_layout::hide_plus_normal_connectors_property.get(),
        &Kit_layout::hide_minus_normal_connectors_property.get(),
        &Kit_layout::hide_plus_binormal_connectors_property.get(),
        &Kit_layout::hide_minus_binormal_connectors_property.get()
    }) {
        if (member == &property) {
            return true;
        }
    }
    return false;
}

auto read_layout_connector_hiding(const erhe::scene::Node& node) -> Kit_layout_connector_hiding
{
    Kit_layout_connector_hiding hiding;
    hiding.all = node.get_value(Kit_layout::hide_all_connectors_property) ? Kit_layout_connector_visibility::hidden : Kit_layout_connector_visibility::shown;
    for (std::size_t i = 0; i < c_layout_direction_count; ++i) {
        const Kit_layout_direction direction = static_cast<Kit_layout_direction>(i);
        hiding.set(
            direction,
            node.get_value(Kit_layout::get_hide_direction_property(direction)) ? Kit_layout_connector_visibility::hidden : Kit_layout_connector_visibility::shown
        );
    }
    return hiding;
}

void write_layout_connector_hiding(erhe::scene::Node& node, const Kit_layout_connector_hiding& hiding)
{
    node.set_value(Kit_layout::hide_all_connectors_property, hiding.all == Kit_layout_connector_visibility::hidden);
    for (std::size_t i = 0; i < c_layout_direction_count; ++i) {
        const Kit_layout_direction direction = static_cast<Kit_layout_direction>(i);
        node.set_value(Kit_layout::get_hide_direction_property(direction), hiding.is_hidden(direction));
    }
}

auto is_kit_layout_property(const erhe::property::Dependency_property& property) -> bool
{
    if (is_layout_connector_switch_property(property)) {
        return true;
    }
    for (const erhe::property::Dependency_property* const member : {
        &Kit_layout::pattern_property.get(),
        &Kit_layout::items_property.get(),
        &Kit_layout::module_snap_property.get(),
        &Kit_layout::surface_snap_property.get(),
        &Kit_layout::surface_orient_property.get(),
        &Kit_layout::surface_direction_property.get(),
        &Kit_layout::surface_forward_distance_property.get(),
        &Kit_layout::surface_backward_distance_property.get(),
        &Kit_layout::surface_support_property.get(),
        &Kit_layout::per_piece_seed_property.get(),
        &Kit_layout::seed_property.get(),
        &Kit_layout::locked_property.get()
    }) {
        if (member == &property) {
            return true;
        }
    }
    return false;
}

auto Kit_layout_generated::property_owner_type() -> erhe::property::Owner_type
{
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(
        erhe::property::root_owner_type, "Kit_layout_generated"
    );
    return s_id;
}

const Property<std::string> Kit_layout_generated::source_property = Property<std::string>::register_attached(
    "source", Kit_layout_generated::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value = std::string{},
        .inherits      = false,
        .ui            = Property_ui{
            .group          = "Kit Layout Generated",
            .tooltip        = "The curve and the values a stairs layout generated this child from (doc/editor/level_tools.md \"Stairs layouts\")",
            .developer_only = true,
            .label          = "Source"
        }
    }
);

auto read_layout_generated_source(const erhe::scene::Node& node) -> std::string
{
    if (!erhe::property::carries_attached_group(node, Kit_layout_generated::source_property.get())) {
        return {};
    }
    return node.get_value(Kit_layout_generated::source_property);
}

auto carries_kit_layout(const erhe::scene::Node& node) -> bool
{
    return erhe::property::carries_attached_group(node, Kit_layout::pattern_property.get());
}

auto check_layout_kind(const erhe::scene::Node& node, const Repeat_pattern_kind kind) -> std::string
{
    const bool is_curve = (dynamic_cast<const erhe::scene::Curve*>(&node) != nullptr);
    if (is_curve_layout_kind(kind) && !is_curve) {
        return fmt::format("'{}' is not a curve node: a {} layout is the curve node itself, carrying the layout values", node.get_name(), c_str(kind));
    }
    if (!is_curve_layout_kind(kind) && is_curve) {
        return fmt::format("'{}' is a curve node: it carries a path, segments or stairs layout only (a {} layout is an Xform node)", node.get_name(), c_str(kind));
    }
    return {};
}

auto get_layout_surface_options(const Kit_layout_data& data) -> Surface_options
{
    return Surface_options{
        .snap              = data.surface_snap,
        .orient            = data.surface_orient,
        .direction         = glm::normalize(data.surface_direction),
        .forward_distance  = data.surface_forward_distance,
        .backward_distance = data.surface_backward_distance,
        .support           = data.surface_support
    };
}

auto read_kit_layout(const erhe::scene::Node& node, Kit_layout_data& out_data) -> std::string
{
    if (!carries_kit_layout(node)) {
        return fmt::format("'{}' is not a kit layout node", node.get_name());
    }
    Kit_layout_data data;
    std::string error = parse_layout_pattern(node.get_value(Kit_layout::pattern_property), data.pattern);
    if (!error.empty()) {
        return fmt::format("'{}': the pattern does not read: {}", node.get_name(), error);
    }
    error = check_layout_kind(node, data.pattern.kind);
    if (!error.empty()) {
        return fmt::format("the values do not read: {}", error);
    }
    error = parse_layout_items(node.get_value(Kit_layout::items_property), data.items);
    if (!error.empty()) {
        return fmt::format("'{}': the items do not read: {}", node.get_name(), error);
    }
    data.snap           = node.get_value(Kit_layout::module_snap_property)    ? Kit_snap::module           : Kit_snap::none;
    data.surface_snap   = node.get_value(Kit_layout::surface_snap_property)   ? Surface_snap::on           : Surface_snap::off;
    data.surface_orient = node.get_value(Kit_layout::surface_orient_property) ? Surface_orient::on         : Surface_orient::off;
    data.surface_direction         = node.get_value(Kit_layout::surface_direction_property);
    data.surface_forward_distance  = node.get_value(Kit_layout::surface_forward_distance_property);
    data.surface_backward_distance = node.get_value(Kit_layout::surface_backward_distance_property);
    data.surface_support           = node.get_value(Kit_layout::surface_support_property);
    const std::optional<float> legacy_distance = node.read_local_value(Kit_layout::legacy_surface_max_distance_property);
    if (legacy_distance.has_value()) {
        if (!node.read_local_value(Kit_layout::surface_forward_distance_property).has_value()) {
            data.surface_forward_distance = legacy_distance.value();
        }
        if (!node.read_local_value(Kit_layout::surface_backward_distance_property).has_value()) {
            data.surface_backward_distance = legacy_distance.value();
        }
    }
    for (const std::string& value_error : {
        check_surface_direction(data.surface_direction),
        check_surface_max_distance(data.surface_forward_distance, "surface forward distance"),
        check_surface_max_distance(data.surface_backward_distance, "surface backward distance")
    }) {
        if (!value_error.empty()) {
            return fmt::format("'{}': {}", node.get_name(), value_error);
        }
    }
    data.seeds          = node.get_value(Kit_layout::per_piece_seed_property)   ? Kit_layout_seeds::per_element : Kit_layout_seeds::item;
    data.seed           = node.get_value(Kit_layout::seed_property);
    data.lock           = node.get_value(Kit_layout::locked_property)         ? Kit_layout_lock::locked    : Kit_layout_lock::unlocked;
    data.connector_hiding = read_layout_connector_hiding(node);
    out_data = std::move(data);
    return {};
}

void write_kit_layout(erhe::scene::Node& node, const Kit_layout_data& data)
{
    node.set_value(Kit_layout::pattern_property,        to_canonical_text(data.pattern));
    node.set_value(Kit_layout::items_property,          to_canonical_text(std::span<const Kit_layout_item>{data.items}));
    node.set_value(Kit_layout::module_snap_property,    data.snap == Kit_snap::module);
    node.set_value(Kit_layout::surface_snap_property,   data.surface_snap == Surface_snap::on);
    node.set_value(Kit_layout::surface_orient_property, data.surface_orient == Surface_orient::on);
    node.set_value(Kit_layout::surface_direction_property,         data.surface_direction);
    node.set_value(Kit_layout::surface_forward_distance_property,  data.surface_forward_distance);
    node.set_value(Kit_layout::surface_backward_distance_property, data.surface_backward_distance);
    node.set_value(Kit_layout::surface_support_property,           data.surface_support);
    node.set_value(Kit_layout::per_piece_seed_property,   data.seeds == Kit_layout_seeds::per_element);
    node.set_value(Kit_layout::seed_property,           data.seed);
    node.set_value(Kit_layout::locked_property,         data.lock == Kit_layout_lock::locked);
    write_layout_connector_hiding(node, data.connector_hiding);
}

auto is_locked_kit_layout(const erhe::scene::Node& node) -> bool
{
    // The bool first: it reads without a copy, and most nodes are not layouts.
    return node.get_value(Kit_layout::locked_property) && carries_kit_layout(node);
}

auto find_locking_kit_layout(const erhe::Item_base& item) -> std::shared_ptr<erhe::scene::Node>
{
    const erhe::Hierarchy* const hierarchy = dynamic_cast<const erhe::Hierarchy*>(&item);
    if (hierarchy == nullptr) {
        return {};
    }
    std::shared_ptr<erhe::scene::Node> found;
    std::shared_ptr<erhe::Hierarchy>   parent = hierarchy->get_parent().lock();
    while (parent) {
        const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(parent);
        if (node && is_locked_kit_layout(*node)) {
            found = node;
        }
        parent = parent->get_parent().lock();
    }
    return found;
}

auto locked_layout_refusal(const erhe::Item_base& item) -> std::optional<std::string>
{
    const std::shared_ptr<erhe::scene::Node> layout = find_locking_kit_layout(item);
    if (!layout) {
        return {};
    }
    return fmt::format(
        "'{}' is a child of the locked kit layout node '{}'; edit the layout's values, or unlock it to edit its children",
        item.get_name(), layout->get_name()
    );
}

auto locked_layout_child_refusal(const erhe::Hierarchy& parent) -> std::optional<std::string>
{
    const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(&parent);
    if ((node != nullptr) && is_locked_kit_layout(*node)) {
        return fmt::format(
            "'{}' is a locked kit layout node: it makes its own children; unlock it to add to it", parent.get_name()
        );
    }
    return locked_layout_refusal(parent);
}

} // namespace editor
