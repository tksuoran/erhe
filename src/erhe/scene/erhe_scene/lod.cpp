#include "erhe_scene/lod.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/node_system.hpp"

#include "erhe_property/attached_group.hpp"
#include "erhe_property/property_metadata.hpp"

#include <algorithm>
#include <memory>

namespace erhe::scene {

namespace {

using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;

constexpr erhe::property::Enum_entry c_lod_mode_entries[] = {
    {"Automatic", static_cast<int32_t>(Lod_mode::automatic)},
    {"Forced",    static_cast<int32_t>(Lod_mode::forced)},
    {"Off",       static_cast<int32_t>(Lod_mode::off)},
};

constexpr std::string_view c_group = "Level of Detail";

// A level index: the chain holds at most erhe::primitive::max_lod_levels (8)
// levels, and a value past the chain is clamped to it where it is used.
constexpr float c_max_level = 7.0f;

} // anonymous namespace

const erhe::property::Enum_info c_lod_mode_enum_info{"Lod_mode", c_lod_mode_entries};

auto get_forced_lod_level(const Lod_data& data) -> std::size_t
{
    if (data.mode != Lod_mode::forced) {
        return 0;
    }
    const int low   = std::max(data.min_level, 0);
    const int high  = std::max(data.max_level, low);
    const int level = std::clamp(data.forced_level, low, high);
    return static_cast<std::size_t>(level);
}

auto Lod::property_owner_type() -> erhe::property::Owner_type
{
    // Lod is not a Dependency_object, so there is no Item<> to allocate the
    // id: the registering class's own id sits directly under the root and
    // serves only to qualify the names (Lod.apply_lod).
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(
        erhe::property::root_owner_type, "Lod"
    );
    return s_id;
}

// The key property comes first, so its registration is complete when the rest
// of the group takes attached_group_visible_when on it.
const Property<bool> Lod::apply_lod_property = Property<bool>::register_attached(
    "apply_lod", Lod::property_owner_type(), Node::property_owner_type(),
    Property_metadata{
        .default_value    = false,
        .property_changed = node_system_property_changed,
        .ui               = Property_ui{
            .group   = c_group,
            .tooltip = "This node carries a level of detail opinion: the values below apply to the meshes at and below it",
            .label   = "Apply LOD"
        }
    }
);

namespace {

[[nodiscard]] auto group_visible_when() -> Property_ui::Visible_when
{
    return erhe::property::attached_group_visible_when(Lod::apply_lod_property.get());
}

} // anonymous namespace

const Property<Lod_mode> Lod::mode_property = Property<Lod_mode>::register_attached(
    "mode", Lod::property_owner_type(), Node::property_owner_type(), c_lod_mode_enum_info,
    Property_metadata{
        .default_value    = erhe::property::make_value(Lod_mode::automatic),
        .property_changed = node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{
            .group        = c_group,
            .tooltip      = "Automatic: runtime selection (level 0 until it exists). Forced: draw the forced level. Off: draw level 0",
            .label        = "Mode",
            .visible_when = group_visible_when()
        }
    }
);
const Property<int> Lod::forced_level_property = Property<int>::register_attached(
    "forced_level", Lod::property_owner_type(), Node::property_owner_type(),
    Property_metadata{
        .default_value    = 0,
        .property_changed = node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{.min = 0.0f, .max = c_max_level, .step = 1.0f, .group = c_group, .tooltip = "Level drawn in Forced mode, clamped to Min / Max Level and to the mesh's chain", .label = "Forced Level", .visible_when = group_visible_when()}
    }
);
const Property<int> Lod::bias_property = Property<int>::register_attached(
    "bias", Lod::property_owner_type(), Node::property_owner_type(),
    Property_metadata{
        .default_value    = 0,
        .property_changed = node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{.min = -c_max_level, .max = c_max_level, .step = 1.0f, .group = c_group, .tooltip = "Added to the automatically selected level", .label = "Bias", .visible_when = group_visible_when()}
    }
);
const Property<int> Lod::min_level_property = Property<int>::register_attached(
    "min_level", Lod::property_owner_type(), Node::property_owner_type(),
    Property_metadata{
        .default_value    = 0,
        .property_changed = node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{.min = 0.0f, .max = c_max_level, .step = 1.0f, .group = c_group, .tooltip = "Finest level drawn", .label = "Min Level", .visible_when = group_visible_when()}
    }
);
const Property<int> Lod::max_level_property = Property<int>::register_attached(
    "max_level", Lod::property_owner_type(), Node::property_owner_type(),
    Property_metadata{
        .default_value    = 7,
        .property_changed = node_system_property_changed,
        .inherits         = true,
        .ui               = Property_ui{.min = 0.0f, .max = c_max_level, .step = 1.0f, .group = c_group, .tooltip = "Coarsest level drawn", .label = "Max Level", .visible_when = group_visible_when()}
    }
);

auto carries_lod(const Node& node) -> bool
{
    return erhe::property::carries_attached_group(node, Lod::apply_lod_property.get());
}

namespace {

[[nodiscard]] auto read_lod_values(const Node& node) -> Lod_data
{
    Lod_data data;
    data.mode         = node.get_value(Lod::mode_property);
    data.forced_level = node.get_value(Lod::forced_level_property);
    data.bias         = node.get_value(Lod::bias_property);
    data.min_level    = node.get_value(Lod::min_level_property);
    data.max_level    = node.get_value(Lod::max_level_property);
    return data;
}

} // anonymous namespace

auto read_lod(const Node& node) -> std::optional<Lod_data>
{
    if (!carries_lod(node)) {
        return {};
    }
    return read_lod_values(node);
}

auto resolve_lod(const Node& node) -> Lod_data
{
    if (carries_lod(node)) {
        return read_lod_values(node);
    }
    std::shared_ptr<Node> ancestor = node.get_parent_node();
    while (ancestor) {
        if (carries_lod(*ancestor)) {
            return read_lod_values(node);
        }
        ancestor = ancestor->get_parent_node();
    }
    return Lod_data{};
}

} // namespace erhe::scene
