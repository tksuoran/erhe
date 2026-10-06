// Mcp_server item property tools (get_item_properties, set_item_properties,
// set_item_property, get_addable_item_properties):
// the generic erhe::property view of any item (doc/erhe/property_system.md
// D13). Values travel as strings through erhe_property/property_string.hpp,
// so enumerations travel as their labels.

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "app_scenes.hpp"
#include "content_library/content_library.hpp"
#include "content_library/style.hpp"
#include "grid/grid.hpp"
#include "grid/grid_tool.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/library_attach_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/item_property_apply.hpp"
#include "operations/property_edit_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/style_set_operation.hpp"
#include "scene/item_lookup.hpp"
#include "scene/scene_root.hpp"
#include "windows/attached_property_listing.hpp"
#include "windows/property_origin.hpp"

#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/enum_info.hpp"
#include "erhe_property/expression.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_string.hpp"
#include "erhe_property/property_style.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace editor {

using namespace mcp_server_detail;

namespace {

// A grid is owned by Grid_tool and belongs to no scene
// (doc/editor/grid.md "Frame"), so it is addressed through
// the tool's own list. `item_id` wins over `item_name` when it is given.
auto find_grid(App_context& context, const std::size_t item_id, const std::string& item_name) -> std::shared_ptr<erhe::Item_base>
{
    if (context.grid_tool == nullptr) {
        return {};
    }
    for (const std::shared_ptr<Grid>& grid : context.grid_tool->get_grids()) {
        if (!grid) {
            continue;
        }
        if (item_id != 0) {
            if (grid->get_id() == item_id) {
                return grid;
            }
        } else if (grid->get_name() == item_name) {
            return grid;
        }
    }
    return {};
}

// Resolves args.item_id (any scene, or a grid) or args.item_name - an item
// name or an item path (doc/erhe/usd_compatibility_design.md M1) - in
// args.scene_name, or in the first scene when absent, falling back to the
// grids.
auto resolve_item(App_context& context, const json& args, std::string& out_error) -> std::shared_ptr<erhe::Item_base>
{
    const std::size_t item_id    = args.value("item_id", std::size_t{0});
    const std::string item_name  = args.value("item_name", "");
    const std::string scene_name = args.value("scene_name", "");

    if ((item_id == 0) && item_name.empty()) {
        out_error = "item_id or item_name is required";
        return {};
    }

    if (item_id != 0) {
        if (context.app_scenes != nullptr) {
            for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
                if (!scene_root) {
                    continue;
                }
                std::shared_ptr<erhe::Item_base> item = find_item_in_scene_by_id(*scene_root, item_id);
                if (item) {
                    return item;
                }
            }
        }
        std::shared_ptr<erhe::Item_base> grid = find_grid(context, item_id, {});
        if (grid) {
            return grid;
        }
        out_error = "Item not found with id: " + std::to_string(item_id);
        return {};
    }

    Scene_root* scene_root = nullptr;
    if (context.app_scenes != nullptr) {
        if (!scene_name.empty()) {
            for (const std::shared_ptr<Scene_root>& candidate : context.app_scenes->get_scene_roots()) {
                if (candidate && (candidate->get_name() == scene_name)) {
                    scene_root = candidate.get();
                    break;
                }
            }
            if (scene_root == nullptr) {
                out_error = "Scene not found: " + scene_name;
                return {};
            }
        } else if (!context.app_scenes->get_scene_roots().empty()) {
            scene_root = context.app_scenes->get_scene_roots().front().get();
        }
    }
    std::shared_ptr<erhe::Item_base> item = (scene_root != nullptr)
        ? find_item_in_scene_by_reference(*scene_root, item_name)
        : std::shared_ptr<erhe::Item_base>{};
    if (!item) {
        item = find_grid(context, 0, item_name);
    }
    if (!item) {
        out_error = (scene_root != nullptr)
            ? ("Item not found with name or path: " + item_name)
            : ("No scene, and no grid named: " + item_name);
    }
    return item;
}

auto value_json(const erhe::property::Dependency_property& property, const erhe::property::Property_value& value) -> json
{
    return json(erhe::property::to_string(property, value));
}

// One property of `object` as get_item_properties and
// get_addable_item_properties list it. `origin_item` is the item whose
// composition origin the entry reports (doc/erhe/usd_compatibility_design.md X5);
// it is null for the properties of a sub-object (D29), which no file spells
// as a prim of its own.
auto property_json(
    App_context&                               context,
    const erhe::property::Dependency_object&   object,
    const erhe::property::Dependency_property& property,
    const erhe::Item_base*                     origin_item
) -> json
{
    const erhe::property::Owner_type         owner_type = object.get_property_owner_type();
    const erhe::property::Property_registry& registry   = erhe::property::Property_registry::get();
    {
        const erhe::property::Property_metadata& metadata = property.get_metadata(owner_type);
        const std::optional<erhe::property::Property_value> local      = object.read_local_value(property);
        const std::optional<std::string_view>               expression = object.get_expression(property);
        json entry = {
            {"name",       registry.qualified_name(object, property)}, // an attached or secondary property by its qualified name (D3, D30)
            {"attached",   property.is_attached()},
            {"type",       erhe::property::c_str(property.get_type())},
            {"value",      value_json(property, object.get_value(property))},
            {"source",     erhe::property::c_str(object.get_value_source(property))},
            {"local",      local.has_value() ? value_json(property, local.value()) : json(nullptr)},
            {"expression", expression.has_value() ? json(std::string{expression.value()}) : json(nullptr)},
            {"default",    value_json(property, object.get_default_value(property))}, // D31: per-object default layer
            {"read_only",  property.is_read_only()},
            {"inherits",   metadata.inherits},
            {"coerced",    object.is_coerced(property)},
            {"flags",      metadata.flags}
        };
        if (const std::string_view error = object.get_expression_error(property); !error.empty()) {
            entry["expression_error"] = std::string{error};
        }
        if (metadata.is_computed_writable()) {
            // D26: a set of this property writes that stored property.
            entry["writes"] = std::string{metadata.compute_writes->get_name()};
        }
        if (erhe::property::is_object_reference_type(property.get_type())) {
            // D28: the referenced item's session id and type next to its name.
            const erhe::property::Property_value        value      = object.get_value(property);
            const std::shared_ptr<erhe::Item_base>      referenced = std::dynamic_pointer_cast<erhe::Item_base>(erhe::property::get_referenced_object(value));
            entry["reference_id"]   = referenced ? json(referenced->get_id()) : json(nullptr);
            entry["reference_type"] = referenced ? json(std::string{referenced->get_type_name()}) : json(nullptr);
            entry["reference_item_types"] = metadata.ui.reference_item_types;
        }
        if (const erhe::property::Enum_info* info = property.get_enum_info(); info != nullptr) {
            json labels = json::array();
            for (const erhe::property::Enum_entry& e : info->get_entries()) {
                labels.push_back(std::string{e.label});
            }
            entry["enum_labels"] = labels;
        }
        if (!metadata.ui.label.empty()) {
            entry["label"] = std::string{metadata.ui.label};
        }
        if (!metadata.ui.group.empty()) {
            entry["group"] = std::string{metadata.ui.group};
        }
        if (origin_item != nullptr) {
            const Property_origin origin = describe_property_origin(context, *origin_item, property);
            entry["origin"] = {
                {"layer",       origin.layer},
                {"prim_path",   origin.prim_path},
                {"arc",         std::string{c_str(origin.arc)}},
                {"arc_target",  origin.arc_target},
                {"authored_as", origin.authored_as}
            };
        }
        return entry;
    }
}

// The registered properties of `object` (an item or one of its sub-objects,
// D29) as get_item_properties lists them.
auto properties_json(App_context& context, const erhe::property::Dependency_object& object, const erhe::Item_base* origin_item) -> json
{
    json properties = json::array();
    const erhe::property::Owner_type         owner_type = object.get_property_owner_type();
    const erhe::property::Property_registry& registry   = erhe::property::Property_registry::get();
    const auto add = [&](const erhe::property::Dependency_property& property) {
        properties.push_back(property_json(context, object, property, origin_item));
    };
    registry.for_each_property_of_object(owner_type, add);
    // Attached properties: the D12 listing rule.
    registry.for_each_attached_property_of(owner_type, [&](const erhe::property::Dependency_property& property) {
        if (is_extra_property_listed(object, property)) {
            add(property);
        }
    });
    // Secondary properties (D30): the same rule.
    registry.for_each_secondary_property(object, [&](const erhe::property::Dependency_property& property) {
        if (is_extra_property_listed(object, property)) {
            add(property);
        }
    });
    return properties;
}

} // anonymous namespace

auto Mcp_server::query_item_properties(const json& args) -> std::string
{
    std::string error;
    const std::shared_ptr<erhe::Item_base> item = resolve_item(m_context, args, error);
    if (!item) {
        return make_error_content(error);
    }

    json result;
    result["item"] = {
        {"id",     item->get_id()},
        {"name",   item->get_name()},
        {"type",   std::string{item->get_type_name()}},
        {"sealed", item->is_sealed()}, // lock_edit (D24): writes are refused
        {"style",  item->get_style() ? json(item->get_style()->get_reference_path()) : json(nullptr)} // D25
    };
    json properties = properties_json(m_context, *item, item.get());
    // Property sub-objects (D29): a mesh's primitives.
    json sub_objects = json::array();
    for (std::size_t i = 0, end = item->get_property_sub_object_count(); i < end; ++i) {
        const erhe::property::Dependency_object* sub_object = item->get_property_sub_object(i);
        if (sub_object == nullptr) {
            continue;
        }
        sub_objects.push_back({
            {"index",      i},
            {"label",      item->get_property_sub_object_label(i)},
            {"properties", properties_json(m_context, *sub_object, nullptr)}
        });
    }
    // Attached properties with a local value on this item
    json attached = json::array();
    item->for_each_local_value(
        [&](const erhe::property::Dependency_property& property, const erhe::property::Property_value& value) {
            if (property.is_attached()) {
                attached.push_back({
                    {"name",  std::string{property.get_name()}},
                    {"type",  erhe::property::c_str(property.get_type())},
                    {"value", value_json(property, value)}
                });
            }
        }
    );
    result["properties"]  = properties;
    result["sub_objects"] = sub_objects;
    result["attached"]    = attached;
    return make_json_content(result).dump();
}

// The attached properties "Add Property" offers for the item
// (doc/erhe/property_system.md D13): every attached registration the D12
// rule does not list for it. `value` is the effective value the add would
// make local.
auto Mcp_server::query_addable_item_properties(const json& args) -> std::string
{
    std::string error;
    const std::shared_ptr<erhe::Item_base> item = resolve_item(m_context, args, error);
    if (!item) {
        return make_error_content(error);
    }
    const Developer_mode developer_mode = args.value("include_developer_only", false) ? Developer_mode::shown : Developer_mode::hidden;
    std::vector<const erhe::property::Dependency_property*> candidates;
    collect_addable_properties(*item, developer_mode, candidates);

    json result;
    result["item"] = {
        {"id",     item->get_id()},
        {"name",   item->get_name()},
        {"type",   std::string{item->get_type_name()}},
        {"sealed", item->is_sealed()}
    };
    json properties = json::array();
    for (const erhe::property::Dependency_property* property : candidates) {
        properties.push_back(property_json(m_context, *item, *property, nullptr));
    }
    result["properties"] = properties;
    return make_json_content(result).dump();
}

namespace {

// What one entry of set_item_properties (or the one entry of
// set_item_property) writes: a value, an expression, or a clear of the local
// layer (doc/plans/property_undo_and_reflective_mcp.md section 4.1).
enum class Property_write_kind : unsigned int {
    value      = 0,
    expression = 1,
    clear      = 2
};

class Property_write_entry
{
public:
    std::string                                   name;
    const erhe::property::Dependency_property*    property{nullptr};
    // D26: the stored property a writable computed property's setter
    // writes; null for every other property.
    const erhe::property::Dependency_property*    writes  {nullptr};
    Property_write_kind                           kind    {Property_write_kind::clear};
    std::optional<erhe::property::Property_value> value;
    std::string                                   expression;
};

// The item, the D29 sub-object and the checked entries of one call.
class Property_write_request
{
public:
    std::shared_ptr<erhe::Item_base>    item;
    std::optional<std::size_t>          sub_object;
    erhe::property::Dependency_object*  target{nullptr};
    std::vector<Property_write_entry>   entries;
};

// Resolves args (item_id | item_name, scene_name, sub_object) into
// out_request.item / sub_object / target.
auto resolve_write_target(App_context& context, const json& args, Property_write_request& out_request, std::string& out_error) -> bool
{
    out_request.item = resolve_item(context, args, out_error);
    if (!out_request.item) {
        return false;
    }
    erhe::Item_base& item = *out_request.item;
    out_request.target = &item;
    // D29: an optional sub-object index addresses e.g. a mesh primitive.
    const auto sub_object_it = args.find("sub_object");
    if ((sub_object_it != args.end()) && !sub_object_it->is_null()) {
        if (!sub_object_it->is_number_unsigned()) {
            out_error = "sub_object must be an integer index (see get_item_properties sub_objects)";
            return false;
        }
        out_request.sub_object = sub_object_it->get<std::size_t>();
        out_request.target     = item.get_property_sub_object(out_request.sub_object.value());
        if (out_request.target == nullptr) {
            out_error = "Item '" + item.get_name() + "' has no sub-object " + std::to_string(out_request.sub_object.value()) + " (it has " + std::to_string(item.get_property_sub_object_count()) + ")";
            return false;
        }
    }
    return true;
}

// The referenced item of an object value checked like the write will be:
// the property's validation and bridge validate, then the D28 host check.
auto check_object_value(
    App_context&                                context,
    const Property_write_request&               request,
    const Property_write_entry&                 entry,
    const std::shared_ptr<erhe::Item_base>&     referenced,
    std::string&                                out_error
) -> bool
{
    std::string validation_error;
    if (!request.target->validate_value(*entry.property, entry.value.value(), validation_error)) {
        out_error = "'" + referenced->get_name() + "' (" + std::string{referenced->get_type_name()} + ") was rejected by property '" + entry.name + "': " + validation_error;
        return false;
    }
    if (!is_item_reference_allowed(context, *request.item, *referenced)) {
        out_error = "property '" + entry.name + "' of '" + request.item->get_name() + "' cannot reference '" + referenced->get_name() + "' (" + std::string{referenced->get_type_name()} + "): it is in another scene and is not a cross-scene referenceable asset";
        return false;
    }
    return true;
}

// Checks one entry against the live state of request.target - lookup,
// read-only, seal (D24, the item's seal covering a sub-object), the value
// form, parse, the property's validation and bridge validate, expression
// compilation and the D28 host check - and fills out_entry. Nothing is
// written. Every error names the property.
auto parse_write_entry(
    App_context&                  context,
    const Property_write_request& request,
    const std::string&            name,
    const json&                   value_json,
    Property_write_entry&         out_entry,
    std::string&                  out_error
) -> bool
{
    const erhe::Item_base&            item   = *request.item;
    erhe::property::Dependency_object& target = *request.target;
    out_entry.name = name;
    const erhe::property::Dependency_property* property = erhe::property::Property_registry::get().find_for_object(target, name);
    if (property == nullptr) {
        out_error = "Item '" + item.get_name() + "' (" + std::string{item.get_type_name()} + ")" + (request.sub_object.has_value() ? " sub-object " + std::to_string(request.sub_object.value()) : std::string{}) + " has no property '" + name + "'";
        return false;
    }
    out_entry.property = property;
    if (property->is_read_only()) {
        out_error = "Property '" + name + "' is read-only";
        return false;
    }
    if (target.is_write_sealed(*property) || is_sealed_sub_object(item, target)) { // D24; lock_edit itself stays writable
        out_error = "Property '" + name + "': item '" + item.get_name() + "' is sealed (lock_edit): set lock_edit false or unlock_items first, or edit the prefab's source scene";
        return false;
    }
    const erhe::property::Property_metadata& metadata = property->get_metadata(target.get_property_owner_type());
    if (metadata.is_computed_writable()) { // D26
        out_entry.writes = metadata.compute_writes;
    }
    const bool is_object = erhe::property::is_object_reference_type(property->get_type());

    if (value_json.is_object()) {
        if (value_json.size() != 1) {
            out_error = "Property '" + name + "': an object value takes exactly one of 'expression', 'reference_id' or 'reference_name'";
            return false;
        }
        if (const auto expression_it = value_json.find("expression"); expression_it != value_json.end()) {
            // An expression (doc/erhe/property_system.md D22).
            if (out_entry.writes != nullptr) {
                out_error = "Property '" + name + "' is computed: an expression cannot drive it (set '" + std::string{out_entry.writes->get_name()} + "' instead)";
                return false;
            }
            if (!expression_it->is_string()) {
                out_error = "Property '" + name + "': expression must be a string";
                return false;
            }
            const std::string text = expression_it->get<std::string>();
            std::string expression_error;
            if (!erhe::property::validate_expression_text(*property, text, expression_error)) {
                out_error = "expression '" + text + "' rejected for property '" + name + "': " + expression_error;
                return false;
            }
            out_entry.kind       = Property_write_kind::expression;
            out_entry.expression = text;
            return true;
        }
        std::shared_ptr<erhe::Item_base> referenced{};
        if (const auto reference_id_it = value_json.find("reference_id"); reference_id_it != value_json.end()) {
            // D28: an object reference by the referenced item's session id,
            // which disambiguates same-named items.
            if (!is_object) {
                out_error = "reference_id applies to object properties only; '" + name + "' is " + erhe::property::c_str(property->get_type());
                return false;
            }
            if (!reference_id_it->is_number_unsigned()) {
                out_error = "Property '" + name + "': reference_id must be an integer item id";
                return false;
            }
            json id_args = json::object();
            id_args["item_id"] = reference_id_it->get<std::size_t>();
            std::string lookup_error;
            referenced = resolve_item(context, id_args, lookup_error);
            if (!referenced) {
                out_error = "Property '" + name + "': reference_id: " + lookup_error;
                return false;
            }
        } else if (const auto reference_name_it = value_json.find("reference_name"); reference_name_it != value_json.end()) {
            // D28: a name or path resolved in the item's scene.
            if (!is_object) {
                out_error = "reference_name applies to object properties only; '" + name + "' is " + erhe::property::c_str(property->get_type());
                return false;
            }
            if (!reference_name_it->is_string()) {
                out_error = "Property '" + name + "': reference_name must be a string";
                return false;
            }
            const std::string text = reference_name_it->get<std::string>();
            referenced = resolve_reference_by_name(context, item, text);
            if (!referenced) {
                out_error = "Property '" + name + "': '" + text + "' does not name an item of the scene of '" + item.get_name() + "' (use reference_id for an item id)";
                return false;
            }
        } else {
            out_error = "Property '" + name + "': an object value takes exactly one of 'expression', 'reference_id' or 'reference_name'";
            return false;
        }
        out_entry.kind  = Property_write_kind::value;
        out_entry.value = erhe::property::make_object_reference(property->get_type(), referenced);
        return check_object_value(context, request, out_entry, referenced, out_error);
    }

    if (value_json.is_null()) {
        // Clear the local layer: the property falls back to its style,
        // inherited or default value.
        if (out_entry.writes != nullptr) {
            out_error = "Property '" + name + "' is computed: it has no local value to clear";
            return false;
        }
        out_entry.kind = Property_write_kind::clear;
        return true;
    }

    // A string in property_string form, or a JSON number / bool / array of
    // numbers, which is rendered to that form first.
    std::string text;
    if (value_json.is_string()) {
        text = value_json.get<std::string>();
    } else if (value_json.is_boolean()) {
        text = value_json.get<bool>() ? "true" : "false";
    } else if (value_json.is_number()) {
        text = value_json.dump();
    } else if (value_json.is_array()) {
        for (const json& component : value_json) {
            if (!component.is_number()) {
                out_error = "Property '" + name + "': value array entries must be numbers";
                return false;
            }
            if (!text.empty()) {
                text += " ";
            }
            text += component.dump();
        }
    } else {
        out_error = "Property '" + name + "': value must be a string, number, bool, array of numbers, null (reset to default), or an object with 'expression', 'reference_id' or 'reference_name'";
        return false;
    }
    out_entry.kind = Property_write_kind::value;
    if (is_object) {
        // D28: a name resolved in the item's scene; empty clears the
        // reference (a null reference as the local value).
        if (text.empty()) {
            out_entry.value = erhe::property::make_object_reference(property->get_type(), {});
        } else {
            const std::shared_ptr<erhe::Item_base> referenced = resolve_reference_by_name(context, item, text);
            if (!referenced) {
                out_error = "Property '" + name + "': '" + text + "' does not name an item of the scene of '" + item.get_name() + "' (use reference_id for an item id)";
                return false;
            }
            out_entry.value = erhe::property::make_object_reference(property->get_type(), referenced);
            return check_object_value(context, request, out_entry, referenced, out_error);
        }
    } else {
        out_entry.value = erhe::property::parse_value(*property, text);
    }
    if (!out_entry.value.has_value()) {
        out_error = "'" + text + "' is not a valid " + erhe::property::c_str(property->get_type()) + " for property '" + name + "'";
        return false;
    }
    std::string validation_error;
    if (!target.validate_value(*property, out_entry.value.value(), validation_error)) {
        out_error = "'" + text + "' was rejected by property '" + name + "': " + validation_error;
        return false;
    }
    return true;
}

// The local layer as the replies report it, in the form set_item_properties
// accepts back: null (unauthored), the value's string form, or
// {"expression": text}.
auto local_state_json(const erhe::property::Dependency_property& property, const std::optional<erhe::property::Local_state>& state) -> json
{
    if (!state.has_value()) {
        return json(nullptr);
    }
    if (const erhe::property::Expression_text* text = std::get_if<erhe::property::Expression_text>(&state.value()); text != nullptr) {
        return json{{"expression", text->text}};
    }
    return value_json(property, std::get<erhe::property::Property_value>(state.value()));
}

auto item_json(const erhe::Item_base& item) -> json
{
    return json{{"id", item.get_id()}, {"name", item.get_name()}, {"type", std::string{item.get_type_name()}}};
}

// The result of write_item_properties: the error, or one reply object per
// entry (name, before, after, value, writes) plus the recorded writes no
// entry asked for (changed-callback cascades).
class Property_write_result
{
public:
    std::string                       error;
    std::shared_ptr<erhe::Item_base>  item;
    std::optional<std::size_t>        sub_object;
    json                              entries{json::array()};
    json                              cascaded{json::array()};
    bool                              changed{false};
};

// The shared path of set_item_properties and set_item_property
// (doc/plans/property_undo_and_reflective_mcp.md section 4.1): every entry is
// checked against the live state before anything is written, then all of
// them are written by one Property_edit_operation run with execute_now - one
// undo entry. A refusal that shows only at write time puts the operation in
// error: nothing stays written, no undo entry, and the error names the
// property. Entries are written in the order given (property-name order for
// a JSON object).
auto write_item_properties(
    App_context&                                     context,
    const json&                                      args,
    const std::vector<std::pair<std::string, json>>& properties
) -> Property_write_result
{
    Property_write_result result;
    if (context.operation_stack == nullptr) {
        result.error = "Operation stack not available";
        return result;
    }
    Property_write_request request;
    if (!resolve_write_target(context, args, request, result.error)) {
        return result;
    }
    result.item       = request.item;
    result.sub_object = request.sub_object;
    if (properties.empty()) {
        result.error = "properties must name at least one property";
        return result;
    }
    request.entries.reserve(properties.size());
    for (const std::pair<std::string, json>& property : properties) {
        Property_write_entry& entry = request.entries.emplace_back();
        if (!parse_write_entry(context, request, property.first, property.second, entry, result.error)) {
            log_mcp->warn("set_item_properties: {}", result.error);
            return result;
        }
    }

    // A clear of a property with no local layer writes nothing; a call made
    // only of such clears changes nothing and leaves no undo entry.
    const bool writes_anything = std::any_of(
        request.entries.begin(), request.entries.end(),
        [&request](const Property_write_entry& entry) {
            return (entry.kind != Property_write_kind::clear) || request.target->read_local_state(*entry.property).has_value();
        }
    );

    std::shared_ptr<Property_edit_operation> operation{};
    if (writes_anything) {
        erhe::Item_base& item = *request.item;
        std::string description = (request.entries.size() == 1)
            ? fmt::format(
                "Set {} '{}'{} {}",
                item.get_type_name(), item.get_name(),
                request.sub_object.has_value() ? fmt::format(" [{}]", item.get_property_sub_object_label(request.sub_object.value())) : std::string{},
                request.entries.front().name
            )
            : fmt::format(
                "Set {} properties on {} '{}'{}",
                request.entries.size(), item.get_type_name(), item.get_name(),
                request.sub_object.has_value() ? fmt::format(" [{}]", item.get_property_sub_object_label(request.sub_object.value())) : std::string{}
            );
        // The edit captures the item: it owns the target sub-object.
        operation = std::make_shared<Property_edit_operation>(
            std::move(description),
            [item_owner = request.item, target = request.target, entries = request.entries]() {
                static_cast<void>(item_owner);
                for (const Property_write_entry& entry : entries) {
                    // A refused write is recorded as a refusal and fails the
                    // operation; the return values add nothing to that.
                    switch (entry.kind) {
                        case Property_write_kind::value: {
                            // D26: a writable computed property's setter
                            // writes its stored property, which is recorded.
                            static_cast<void>(target->set_value(*entry.property, entry.value.value()));
                            break;
                        }
                        case Property_write_kind::expression: {
                            static_cast<void>(target->set_expression(*entry.property, entry.expression));
                            break;
                        }
                        case Property_write_kind::clear: {
                            static_cast<void>(target->clear_value(*entry.property));
                            break;
                        }
                    }
                }
            },
            // The connected bones a Rig.tail / Rig.connected write moves, as
            // the Properties window's rows (Property_set_operation) do.
            Property_edit_follow_ups::bone_connect
        );
        context.operation_stack->execute_now(operation);
        if (operation->has_error()) {
            result.error = operation->get_error();
            return result;
        }
        result.changed = true;
    }

    // Per entry: the record of the property it wrote (the stored property
    // for a writable computed one), or its unchanged local layer when it
    // wrote nothing.
    const std::span<const Property_edit_operation::Record> records = operation ? operation->get_records() : std::span<const Property_edit_operation::Record>{};
    std::vector<bool> reported(records.size(), false);
    for (const Property_write_entry& entry : request.entries) {
        const erhe::property::Dependency_property& written = (entry.writes != nullptr) ? *entry.writes : *entry.property;
        json reply = {{"name", entry.name}};
        bool found = false;
        for (std::size_t i = 0, end = records.size(); i < end; ++i) {
            const Property_edit_operation::Record& record = records[i];
            if ((record.item == request.item) && (record.sub_object == request.sub_object) && (record.property == &written)) {
                reply["before"] = local_state_json(written, record.before);
                reply["after"]  = local_state_json(written, record.after);
                reported[i] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            const std::optional<erhe::property::Local_state> state = request.target->read_local_state(written);
            reply["before"] = local_state_json(written, state);
            reply["after"]  = local_state_json(written, state);
        }
        if (entry.writes != nullptr) {
            reply["writes"] = std::string{entry.writes->get_name()};
        }
        reply["value"] = value_json(*entry.property, request.target->get_value(*entry.property)); // the effective value after the write
        result.entries.push_back(std::move(reply));
    }
    for (std::size_t i = 0, end = records.size(); i < end; ++i) {
        if (reported[i]) {
            continue;
        }
        const Property_edit_operation::Record& record = records[i];
        result.cascaded.push_back({
            {"item",       item_json(*record.item)},
            {"sub_object", record.sub_object.has_value() ? json(record.sub_object.value()) : json(nullptr)},
            {"property",   std::string{record.property->get_name()}},
            {"before",     local_state_json(*record.property, record.before)},
            {"after",      local_state_json(*record.property, record.after)}
        });
    }
    return result;
}

} // anonymous namespace

auto Mcp_server::action_set_item_properties(const json& args) -> std::string
{
    const auto properties_it = args.find("properties");
    if ((properties_it == args.end()) || !properties_it->is_object()) {
        return make_error_content("properties must be an object mapping property names to values");
    }
    std::vector<std::pair<std::string, json>> properties;
    properties.reserve(properties_it->size());
    for (json::const_iterator it = properties_it->cbegin(), end = properties_it->cend(); it != end; ++it) {
        properties.emplace_back(it.key(), it.value());
    }
    const Property_write_result result = write_item_properties(m_context, args, properties);
    if (!result.error.empty()) {
        return make_error_content(result.error);
    }
    json reply = {
        {"item",       item_json(*result.item)},
        {"sub_object", result.sub_object.has_value() ? json(result.sub_object.value()) : json(nullptr)},
        {"properties", result.entries},
        {"changed",    result.changed}
    };
    if (!result.cascaded.empty()) {
        reply["cascaded"] = result.cascaded;
    }
    return make_json_content(reply).dump();
}

// The one-entry form of set_item_properties: value / expression /
// reference_id arguments become the entry's value form.
auto Mcp_server::action_set_item_property(const json& args) -> std::string
{
    const std::string property_name = args.value("property", "");
    if (property_name.empty()) {
        return make_error_content("property is required");
    }
    json value = nullptr;
    const auto expression_it   = args.find("expression");
    const auto reference_id_it = args.find("reference_id");
    const auto value_it        = args.find("value");
    if ((expression_it != args.end()) && !expression_it->is_null()) {
        value = json{{"expression", *expression_it}};
    } else if ((reference_id_it != args.end()) && !reference_id_it->is_null()) {
        value = json{{"reference_id", *reference_id_it}};
    } else if (value_it != args.end()) {
        if (value_it->is_object()) {
            return make_error_content("value must be a string, number, bool, array of numbers, or null (reset to default); use expression or reference_id for the other forms");
        }
        value = *value_it;
    }
    const std::vector<std::pair<std::string, json>> properties{std::pair<std::string, json>{property_name, value}};
    const Property_write_result result = write_item_properties(m_context, args, properties);
    if (!result.error.empty()) {
        return make_error_content(result.error);
    }
    const json& entry = result.entries.front();
    json reply = {
        {"item",       item_json(*result.item)},
        {"sub_object", result.sub_object.has_value() ? json(result.sub_object.value()) : json(nullptr)},
        {"property",   property_name},
        {"before",     entry.at("before")},
        {"after",      entry.at("after")},
        {"value",      entry.at("value")},
        {"changed",    result.changed}
    };
    if (entry.contains("writes")) {
        reply["writes"] = entry.at("writes");
    }
    if (!result.cascaded.empty()) {
        reply["cascaded"] = result.cascaded;
    }
    return make_json_content(reply).dump();
}

// Style layer (D25): the source item's local values become the target's
// style, named after the source. Local values of the target stay on top.
auto Mcp_server::action_set_item_style(const json& args) -> std::string
{
    std::string error;
    const std::shared_ptr<erhe::Item_base> item = resolve_item(m_context, args, error);
    if (!item) {
        return make_error_content(error);
    }
    json source_args = json::object();
    if (args.contains("source_item_id")) {
        source_args["item_id"] = args["source_item_id"];
    }
    if (args.contains("source_item_name")) {
        source_args["item_name"] = args["source_item_name"];
    }
    if (args.contains("scene_name")) {
        source_args["scene_name"] = args["scene_name"];
    }
    const std::shared_ptr<erhe::Item_base> source = resolve_item(m_context, source_args, error);
    if (!source) {
        return make_error_content("source: " + error);
    }
    if (item->is_sealed()) {
        return make_error_content("Item '" + item->get_name() + "' is sealed (lock_edit): unlock_items first");
    }
    const erhe::property::Property_set source_values = erhe::property::Property_set::read_local_values(*source);
    const std::shared_ptr<Compound_operation> compound = make_style_from_values(m_context, {item}, source_values, source->get_name());
    if (!compound) {
        return make_error_content("Item '" + source->get_name() + "' has no local values that '" + item->get_name() + "' (" + std::string{item->get_type_name()} + ") could use, or the item is in no scene");
    }
    json names = json::array();
    for (const erhe::property::Property_set::Entry& entry : source_values.entries()) {
        names.push_back(std::string{entry.property->get_name()});
    }
    m_context.operation_stack->queue(compound);
    return make_json_content(
        json{
            {"item",       {{"id", item->get_id()}, {"name", item->get_name()}, {"type", std::string{item->get_type_name()}}}},
            {"style",      source->get_name()},
            {"properties", names},
            {"before",     item->get_style() ? json(item->get_style()->get_reference_path()) : json(nullptr)},
            {"queued",     true}
        }
    ).dump();
}

// An empty style item under the parent prim, or in the scene's Styles
// scope without one (doc/editor/style_library.md R1); fill it with set_item_property by qualified name and assign it
// through an item's 'style' property.
auto Mcp_server::action_create_style(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    const std::string name       = args.value("name", "New Style");
    Scene_root* scene_root = nullptr;
    if (scene_name.empty()) {
        const std::vector<std::shared_ptr<Scene_root>>& scene_roots = m_context.app_scenes->get_scene_roots();
        scene_root = scene_roots.empty() ? nullptr : scene_roots.front().get();
    } else {
        scene_root = find_scene(scene_name);
    }
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = scene_root->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library");
    }
    std::shared_ptr<erhe::Hierarchy> parent{};
    const std::optional<std::string> parent_error = find_resource_parent(*scene_root, args, parent);
    if (parent_error.has_value()) {
        return make_error_content(parent_error.value());
    }
    std::shared_ptr<Style> style{};
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{library->mutex};
        style = std::make_shared<Style>(make_unique_style_name(*library, name));
    }
    m_context.operation_stack->execute_now(
        make_resource_insert_operation(m_context, library, style, parent)
    );
    return make_json_content(
        json{
            {"style", {{"id", style->get_id()}, {"name", style->get_name()}}}
        }
    ).dump();
}

auto Mcp_server::action_clear_item_style(const json& args) -> std::string
{
    std::string error;
    const std::shared_ptr<erhe::Item_base> item = resolve_item(m_context, args, error);
    if (!item) {
        return make_error_content(error);
    }
    if (item->is_sealed()) {
        return make_error_content("Item '" + item->get_name() + "' is sealed (lock_edit): unlock_items first");
    }
    if (!item->get_style()) {
        return make_error_content("Item '" + item->get_name() + "' has no style");
    }
    const std::string before{item->get_style()->get_reference_path()};
    m_context.operation_stack->queue(std::make_shared<Style_set_operation>(item, item->get_style(), nullptr));
    return make_json_content(
        json{
            {"item",   {{"id", item->get_id()}, {"name", item->get_name()}, {"type", std::string{item->get_type_name()}}}},
            {"before", before},
            {"queued", true}
        }
    ).dump();
}

} // namespace editor
