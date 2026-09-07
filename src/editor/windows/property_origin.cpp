#include "windows/property_origin.hpp"

#include "prefabs/instance_structure.hpp"
#include "prefabs/prefab_instance.hpp"
#include "scene/item_lookup.hpp"
#include "scene/scene_root.hpp"

#include "erhe_file/file.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/owner_type.hpp"
#include "erhe_property/property_metadata.hpp"

#if defined(ERHE_USD_LIBRARY_LIGHTUSD)
#   include "erhe_usd/usd.hpp"
#endif

#include <fmt/format.h>

namespace editor {

using erhe::property::Dependency_object;
using erhe::property::Dependency_property;
using erhe::property::Property_metadata;
using erhe::property::Value_source;

auto c_str(const Property_arc arc) -> const char*
{
    switch (arc) {
        case Property_arc::none:       return "none";
        case Property_arc::root_layer: return "root layer";
        case Property_arc::reference:  return "reference";
        case Property_arc::payload:    return "payload";
        case Property_arc::inherits:   return "inherits";
    }
    return "none";
}

namespace {

// An `inherited` value is described by the ancestor that authors it, and that
// ancestor's own value can be inherited in turn; the walk stops here so a
// hierarchy the editor did not expect cannot spin.
constexpr std::size_t c_max_walk = 32;

// The prim path an item has in its own scene's root layer, as USD spells it:
// '/' followed by the M1 path (erhe::Hierarchy::get_path(), the names below
// the scene root). The writer sanitizes a name that is not a USD identifier
// and makes the result sibling-unique; the erhe name is what the user reads
// in the hierarchy, so it is what this reports.
[[nodiscard]] auto scene_prim_path(const erhe::Item_base& item) -> std::string
{
    const erhe::Hierarchy* hierarchy = get_structural_hierarchy(item);
    if (hierarchy == nullptr) {
        return {};
    }
    const std::string path = hierarchy->get_path();
    return path.empty() ? std::string{"/"} : ("/" + path);
}

// The prim path an arc names in the file it targets. A USD arc that names the
// target layer's default prim, and every glTF prefab, store an empty path.
[[nodiscard]] auto arc_target_prim_path(const Prefab_instance& prefab_instance) -> std::string
{
    const std::string& prim_path = prefab_instance.get_prefab_prim_path();
    if (prim_path.empty()) {
        return "/<defaultPrim>";
    }
    return (prim_path.front() == '/') ? prim_path : ("/" + prim_path);
}

[[nodiscard]] auto arc_of(const Prefab_instance& prefab_instance) -> Property_arc
{
    return (prefab_instance.get_prefab_arc_kind() == Prefab_arc_kind::payload)
        ? Property_arc::payload
        : Property_arc::reference;
}

// "<file></prim>": the arc as it is authored on the carrier prim.
[[nodiscard]] auto arc_target_text(const Prefab_instance& prefab_instance) -> std::string
{
    return fmt::format(
        "{}{}",
        erhe::file::to_string(prefab_instance.get_prefab_source_path()),
        arc_target_prim_path(prefab_instance)
    );
}

[[nodiscard]] auto join_prim_path(const std::string& prim_path, const std::string& relative_path) -> std::string
{
    if (relative_path.empty()) {
        return prim_path;
    }
    return prim_path + "/" + relative_path;
}

// The name the scene's own format writes this value under. A value the format
// does not carry says so instead of naming an attribute.
[[nodiscard]] auto authored_as_of(
    const erhe::Item_base&     item,
    const Dependency_property& property,
    const Property_metadata&   metadata,
    const Scene_source_format  format,
    const Instance_position&   position
) -> std::string
{
    const std::string_view owner = erhe::property::get_owner_type_name(property.get_owner_type());
    const std::string_view name  = property.get_name();
    if ((metadata.flags & erhe::property::Property_flags::serialize) == 0u) {
        return "not saved (session state)";
    }
    if (metadata.is_computed()) {
        return "computed";
    }
    if (metadata.bridge.is_bound()) {
        // A bridged property is the item's own engineered representation and
        // travels in the form the format owns, not as a property value.
        if ((owner == "Xformable") && ((name == "translation") || (name == "rotation") || (name == "scale"))) {
            return (format == Scene_source_format::usd) ? "xformOp:*" : "the node's TRS";
        }
        if (owner == "Item_base") {
            if (name == "name") {
                return "the prim name";
            }
            if (name == "tags") {
                return (format == Scene_source_format::usd) ? "collection membership" : "ERHE_scene tags";
            }
        }
        return "a native field of the format";
    }
#if defined(ERHE_USD_LIBRARY_LIGHTUSD)
    if (format == Scene_source_format::usd) {
        // A prim that carries no schema - an `over` below an instance carrier
        // (X2), a `class` prim of a style (X3) - spells every value as a
        // custom attribute.
        const bool is_typeless_prim = (position.carrier != nullptr) || ((item.get_type() & erhe::Item_type::style) != 0u);
        return erhe::usd::get_usd_authored_as(
            owner,
            name,
            is_typeless_prim
                ? erhe::usd::Native_property_form::custom_attributes
                : erhe::usd::Native_property_form::schema_attributes
        );
    }
#else
    static_cast<void>(position);
#endif
    // glTF: the ERHE_* extension of the item carries the value in its
    // `properties` map, keyed by the registry's qualified name
    // (erhe::gltf::item_local_properties_to_json).
    return fmt::format("properties[\"{}\"]", erhe::property::Property_registry::get().qualified_name(item, property));
}

} // anonymous namespace

auto describe_property_origin(
    App_context&               context,
    const erhe::Item_base&     item,
    const Dependency_property& property
) -> Property_origin
{
    Property_origin          origin{};
    const Property_metadata& metadata = property.get_metadata(item.get_property_owner_type());
    const Value_source       source   = item.get_value_source(property);

    if (source == Value_source::computed) {
        origin.authored_as = "computed";
        return origin;
    }
    if (source == Value_source::default_value) {
        origin.authored_as = "schema fallback";
        return origin;
    }

    if (source == Value_source::inherited) {
        // The value is the closest ancestor's; that ancestor's own origin is
        // the answer, one level of the chain at a time.
        const Dependency_object* ancestor = item.get_inheritance_parent();
        for (std::size_t depth = 0; (ancestor != nullptr) && (depth < c_max_walk); ++depth) {
            const erhe::Item_base* ancestor_item = dynamic_cast<const erhe::Item_base*>(ancestor);
            if ((ancestor_item != nullptr) && (ancestor->get_value_source(property) != Value_source::inherited)) {
                return describe_property_origin(context, *ancestor_item, property);
            }
            ancestor = ancestor->get_inheritance_parent();
        }
        return origin;
    }

    Scene_root* const         scene_root = find_scene_root_for_item(context, item);
    const Scene_source_format format     = (scene_root != nullptr) ? scene_root->get_source_format() : Scene_source_format::none;
    const std::string         root_layer = ((scene_root != nullptr) && !scene_root->get_source_path().empty())
        ? erhe::file::to_string(scene_root->get_source_path())
        : std::string{"session"};
    const Instance_position   position   = find_instance_position(item);

    if (source == Value_source::style) {
        // X3 writes a style as a `class` prim and its user's `inherits` arc.
        // The chain of M7 is walked to the style that authors the value.
        std::shared_ptr<const Dependency_object> style = item.get_style();
        for (std::size_t depth = 0; style && (depth < c_max_walk); ++depth) {
            if (style->has_local_value(property)) {
                break;
            }
            style = style->get_style();
        }
        const erhe::Item_base* style_item = (style ? dynamic_cast<const erhe::Item_base*>(style.get()) : nullptr);
        origin.layer      = root_layer;
        origin.prim_path  = (style_item != nullptr) ? scene_prim_path(*style_item) : std::string{};
        origin.arc        = Property_arc::inherits;
        origin.arc_target = origin.prim_path;
        origin.authored_as = (style_item != nullptr)
            ? authored_as_of(*style_item, property, metadata, format, Instance_position{})
            : std::string{};
        return origin;
    }

    if (source == Value_source::reference) {
        // X2: the value is the template counterpart's, reached through the
        // carrier's arc. The counterpart sits at the item's own path below the
        // arc's target prim, the level X1 collapses onto the carrier.
        if (!position.prefab_instance) {
            origin.authored_as = "a reference counterpart";
            return origin;
        }
        const Prefab_instance& prefab_instance = *position.prefab_instance;
        origin.layer      = erhe::file::to_string(prefab_instance.get_prefab_source_path());
        origin.prim_path  = join_prim_path(arc_target_prim_path(prefab_instance), position.relative_path);
        origin.arc        = arc_of(prefab_instance);
        origin.arc_target = arc_target_text(prefab_instance);
        // A counterpart that is itself inside an instance names its own arc
        // after an arrow; one level of nesting is enough for the text.
        const std::shared_ptr<const Dependency_object>& counterpart = item.get_reference();
        const erhe::Item_base* counterpart_item = (counterpart ? dynamic_cast<const erhe::Item_base*>(counterpart.get()) : nullptr);
        if (counterpart_item != nullptr) {
            const Instance_position nested = find_instance_position(*counterpart_item);
            if (nested.prefab_instance) {
                origin.arc_target += " -> " + arc_target_text(*nested.prefab_instance);
            }
        }
        origin.authored_as = authored_as_of(item, property, metadata, format, Instance_position{});
        return origin;
    }

    // local / expression: authored on the item's own prim in the scene's root
    // layer - as an `over` at the collapsed path when the item is inside an
    // instance (X2).
    origin.layer = root_layer;
    origin.arc   = Property_arc::root_layer;
    if (position.carrier != nullptr) {
        origin.prim_path  = join_prim_path(scene_prim_path(*position.carrier), position.relative_path);
        origin.arc        = arc_of(*position.prefab_instance);
        origin.arc_target = arc_target_text(*position.prefab_instance);
    } else {
        origin.prim_path = scene_prim_path(item);
    }
    origin.authored_as = (source == Value_source::expression)
        ? std::string{"not saved (expression)"} // D14: a formula is session state
        : authored_as_of(item, property, metadata, format, position);
    return origin;
}

auto property_origin_tooltip(const Property_origin& origin) -> std::string
{
    std::string text;
    if (!origin.layer.empty()) {
        text += "\nLayer: ";
        text += origin.layer;
    }
    if (!origin.prim_path.empty()) {
        text += "\nPrim: ";
        text += origin.prim_path;
    }
    if (origin.arc != Property_arc::none) {
        text += "\nArc: ";
        text += c_str(origin.arc);
        if (!origin.arc_target.empty() && (origin.arc_target != origin.prim_path)) {
            text += " -> ";
            text += origin.arc_target;
        }
    }
    if (!origin.authored_as.empty()) {
        text += "\nAuthored as: ";
        text += origin.authored_as;
    }
    return text;
}

}
