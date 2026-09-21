#include "brushes/brush_placement.hpp"
#include "brushes/brush.hpp"

#include "erhe_property/attached_group.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_scene/node.hpp"

namespace editor {

namespace {

using erhe::property::Dependency_property;
using erhe::property::Object_reference;
using erhe::property::Property;
using erhe::property::Property_flags;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;

constexpr std::string_view c_group = "Brush Placement";

// GEO::index_t <-> the int property value; NO_INDEX is -1.
[[nodiscard]] auto to_index_value(const GEO::index_t index) -> int
{
    return (index == GEO::NO_INDEX) ? -1 : static_cast<int>(index);
}

[[nodiscard]] auto from_index_value(const int value) -> GEO::index_t
{
    return (value < 0) ? GEO::NO_INDEX : static_cast<GEO::index_t>(value);
}

// The brush reference is null or names a Brush.
[[nodiscard]] auto validate_brush(const Property_value& value) -> bool
{
    const Object_reference& reference = std::get<Object_reference>(value);
    return !reference.object || (dynamic_cast<const Brush*>(reference.object.get()) != nullptr);
}

} // anonymous namespace

auto Brush_placement::property_owner_type() -> erhe::property::Owner_type
{
    // Brush_placement is not a Dependency_object, so there is no Item<> to
    // allocate the id: the registering class's own id sits directly under the
    // root and serves only to qualify the names (Brush_placement.brush).
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(
        erhe::property::root_owner_type, "Brush_placement"
    );
    return s_id;
}

// The key property comes first, so its registration is complete when the rest
// of the group takes attached_group_visible_when on it (D1). Session state
// (D5): a placement is never written to a file, so none of the three values
// carries the serialize flag.
const Property<Object_reference> Brush_placement::brush_property = Property<Object_reference>::register_attached(
    "brush", Brush_placement::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value = Object_reference{},
        .inherits      = false,
        .flags         = Property_flags::none,
        .ui            = Property_ui{
            .group                = c_group,
            .tooltip              = "The brush this node was placed with; session state, never saved",
            .label                = "Brush",
            .reference_item_types = erhe::Item_type::brush
        }
    },
    validate_brush
);

namespace {

[[nodiscard]] auto placement_ui(const std::string_view label, const std::string_view tooltip) -> Property_ui
{
    return Property_ui{
        .group          = c_group,
        .tooltip        = tooltip,
        .developer_only = true,
        .label          = label,
        .visible_when   = erhe::property::attached_group_visible_when(Brush_placement::brush_property.get())
    };
}

} // anonymous namespace

const Property<int> Brush_placement::facet_property = Property<int>::register_attached(
    "facet", Brush_placement::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value = -1,
        .inherits      = false,
        .flags         = Property_flags::none,
        .ui            = placement_ui("Facet", "The facet of the hovered geometry this instance was seated on; -1 is none")
    }
);
const Property<int> Brush_placement::corner_property = Property<int>::register_attached(
    "corner", Brush_placement::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value = -1,
        .inherits      = false,
        .flags         = Property_flags::none,
        .ui            = placement_ui("Corner", "The corner of the seat facet the instance is rotated to; -1 is none")
    }
);

auto Brush_placement::all_properties() -> const std::vector<const Dependency_property*>&
{
    static const std::vector<const Dependency_property*> s_properties{
        brush_property .get_ptr(),
        facet_property .get_ptr(),
        corner_property.get_ptr()
    };
    return s_properties;
}

auto carries_brush_placement(const erhe::scene::Node& node) -> bool
{
    return erhe::property::carries_attached_group(node, Brush_placement::brush_property.get());
}

auto read_brush_placement(const erhe::scene::Node& node) -> std::optional<Brush_placement_data>
{
    if (!carries_brush_placement(node)) {
        return {};
    }
    Brush_placement_data data{};
    data.brush  = std::dynamic_pointer_cast<Brush>(node.get_value(Brush_placement::brush_property).object);
    data.facet  = from_index_value(node.get_value(Brush_placement::facet_property));
    data.corner = from_index_value(node.get_value(Brush_placement::corner_property));
    return data;
}

void set_brush_placement(
    erhe::scene::Node&            node,
    const std::shared_ptr<Brush>& brush,
    const GEO::index_t            facet,
    const GEO::index_t            corner
)
{
    if (brush) {
        node.set_value(Brush_placement::brush_property, Object_reference{brush});
    }
    if (facet != GEO::NO_INDEX) {
        node.set_value(Brush_placement::facet_property, to_index_value(facet));
    }
    if (corner != GEO::NO_INDEX) {
        node.set_value(Brush_placement::corner_property, to_index_value(corner));
    }
}

void set_brush_placement_corner(erhe::scene::Node& node, const GEO::index_t corner)
{
    node.set_value(Brush_placement::corner_property, to_index_value(corner));
}

} // namespace editor
