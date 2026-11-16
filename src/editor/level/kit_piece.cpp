#include "level/kit_piece.hpp"

#include "erhe_property/attached_group.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_scene/node.hpp"

namespace editor {

namespace {

using erhe::property::Dependency_property;
using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;

constexpr std::string_view c_group = "Kit Piece";

} // anonymous namespace

auto Kit_piece::property_owner_type() -> erhe::property::Owner_type
{
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(
        erhe::property::root_owner_type, "Kit_piece"
    );
    return s_id;
}

const Property<std::string> Kit_piece::shape_property = Property<std::string>::register_attached(
    "shape", Kit_piece::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value = std::string{},
        .inherits      = false,
        .ui            = Property_ui{
            .group          = c_group,
            .tooltip        = "The kit shape this piece was placed from (doc/editor/level_tools.md)",
            .developer_only = true,
            .label          = "Shape"
        }
    }
);

const Property<std::string> Kit_piece::parameters_property = Property<std::string>::register_attached(
    "parameters", Kit_piece::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value = std::string{},
        .inherits      = false,
        .ui            = Property_ui{
            .group          = c_group,
            .tooltip        = "The piece's parameter values as canonical JSON text with hexadecimal floats",
            .developer_only = true,
            .label          = "Parameters",
            .visible_when   = erhe::property::attached_group_visible_when(Kit_piece::shape_property.get())
        }
    }
);

const Property<bool> Kit_piece::connectors_enabled_property = Property<bool>::register_attached(
    "connectors_enabled", Kit_piece::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value = true,
        .inherits      = false,
        .ui            = Property_ui{
            .group          = c_group,
            .tooltip        = "Off disables every connector of the piece: none joins, snaps or is drawn",
            .developer_only = true,
            .label          = "Connectors",
            .visible_when   = erhe::property::attached_group_visible_when(Kit_piece::shape_property.get())
        }
    }
);

const Property<std::string> Kit_piece::disabled_connectors_property = Property<std::string>::register_attached(
    "disabled_connectors", Kit_piece::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value = std::string{},
        .inherits      = false,
        .ui            = Property_ui{
            .group          = c_group,
            .tooltip        = "The names of the piece's disabled connectors, comma separated",
            .developer_only = true,
            .label          = "Disabled Connectors",
            .visible_when   = erhe::property::attached_group_visible_when(Kit_piece::shape_property.get())
        }
    }
);

auto Kit_piece::all_properties() -> const std::vector<const Dependency_property*>&
{
    static const std::vector<const Dependency_property*> s_properties{
        shape_property              .get_ptr(),
        parameters_property         .get_ptr(),
        connectors_enabled_property .get_ptr(),
        disabled_connectors_property.get_ptr()
    };
    return s_properties;
}

auto carries_kit_piece(const erhe::scene::Node& node) -> bool
{
    return erhe::property::carries_attached_group(node, Kit_piece::shape_property.get());
}

auto read_kit_piece(const erhe::scene::Node& node) -> std::optional<Kit_piece_data>
{
    if (!carries_kit_piece(node)) {
        return {};
    }
    return Kit_piece_data{
        .shape      = node.get_value(Kit_piece::shape_property),
        .parameters = node.get_value(Kit_piece::parameters_property)
    };
}

void write_kit_piece(erhe::scene::Node& node, const Kit_piece_data& data)
{
    node.set_value(Kit_piece::shape_property,      data.shape);
    node.set_value(Kit_piece::parameters_property, data.parameters);
}

auto read_connector_switches(const erhe::scene::Node& node) -> Kit_connector_switches
{
    return Kit_connector_switches{
        .connectors_enabled  = node.get_value(Kit_piece::connectors_enabled_property),
        .disabled_connectors = node.get_value(Kit_piece::disabled_connectors_property)
    };
}

auto is_connector_switch_property(const erhe::property::Dependency_property& property) -> bool
{
    return (&property == &Kit_piece::connectors_enabled_property.get()) || (&property == &Kit_piece::disabled_connectors_property.get());
}

} // namespace editor
