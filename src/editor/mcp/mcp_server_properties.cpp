// Mcp_server item property tools (get_item_properties, set_item_property,
// get_addable_item_properties):
// the generic erhe::property view of any item (doc/property-system.md
// D13). Values travel as strings through erhe_property/property_string.hpp,
// so enumerations travel as their labels.

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "app_scenes.hpp"
#include "content_library/content_library.hpp"
#include "content_library/style.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/property_set_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/style_set_operation.hpp"
#include "scene/item_lookup.hpp"
#include "scene/scene_root.hpp"
#include "windows/attached_property_listing.hpp"

#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/enum_info.hpp"
#include "erhe_property/expression.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_property/property_string.hpp"
#include "erhe_property/property_style.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

namespace editor {

using namespace mcp_server_detail;

namespace {

// Resolves args.item_id (any scene) or args.item_name - an item name or an
// item path (doc/usd-compatibility-plan.md M1) - in args.scene_name, or in
// the first scene when absent.
auto resolve_item(App_context& context, const json& args, std::string& out_error) -> std::shared_ptr<erhe::Item_base>
{
    if (context.app_scenes == nullptr) {
        out_error = "No scenes";
        return {};
    }
    const std::size_t item_id   = args.value("item_id", std::size_t{0});
    const std::string item_name = args.value("item_name", "");
    const std::string scene_name = args.value("scene_name", "");

    if (item_id != 0) {
        for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
            if (!scene_root) {
                continue;
            }
            std::shared_ptr<erhe::Item_base> item = find_item_in_scene_by_id(*scene_root, item_id);
            if (item) {
                return item;
            }
        }
        out_error = "Item not found with id: " + std::to_string(item_id);
        return {};
    }
    if (item_name.empty()) {
        out_error = "item_id or item_name is required";
        return {};
    }
    Scene_root* scene_root = nullptr;
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
    if (scene_root == nullptr) {
        out_error = "No scene";
        return {};
    }
    std::shared_ptr<erhe::Item_base> item = find_item_in_scene_by_reference(*scene_root, item_name);
    if (!item) {
        out_error = "Item not found with name or path: " + item_name;
    }
    return item;
}

auto value_json(const erhe::property::Dependency_property& property, const erhe::property::Property_value& value) -> json
{
    return json(erhe::property::to_string(property, value));
}

// One property of `object` as get_item_properties and
// get_addable_item_properties list it.
auto property_json(const erhe::property::Dependency_object& object, const erhe::property::Dependency_property& property) -> json
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
        if (property.get_type() == erhe::property::Property_type::object) {
            // D28: the referenced item's session id and type next to its name.
            const erhe::property::Property_value        value      = object.get_value(property);
            const std::shared_ptr<erhe::Item_base>      referenced = std::dynamic_pointer_cast<erhe::Item_base>(std::get<erhe::property::Object_reference>(value).object);
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
        return entry;
    }
}

// The registered properties of `object` (an item or one of its sub-objects,
// D29) as get_item_properties lists them.
auto properties_json(const erhe::property::Dependency_object& object) -> json
{
    json properties = json::array();
    const erhe::property::Owner_type         owner_type = object.get_property_owner_type();
    const erhe::property::Property_registry& registry   = erhe::property::Property_registry::get();
    const auto add = [&](const erhe::property::Dependency_property& property) {
        properties.push_back(property_json(object, property));
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
    json properties = properties_json(*item);
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
            {"properties", properties_json(*sub_object)}
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
// (doc/property-system.md D13): every attached registration the D12
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
        properties.push_back(property_json(*item, *property));
    }
    result["properties"] = properties;
    return make_json_content(result).dump();
}

auto Mcp_server::action_set_item_property(const json& args) -> std::string
{
    if (m_context.operation_stack == nullptr) {
        return make_error_content("Operation stack not available");
    }
    std::string error;
    const std::shared_ptr<erhe::Item_base> item = resolve_item(m_context, args, error);
    if (!item) {
        return make_error_content(error);
    }
    const std::string property_name = args.value("property", "");
    if (property_name.empty()) {
        return make_error_content("property is required");
    }
    // D29: an optional sub-object index addresses e.g. a mesh primitive.
    std::optional<std::size_t>         sub_object;
    erhe::property::Dependency_object* target = item.get();
    const auto sub_object_it = args.find("sub_object");
    if ((sub_object_it != args.end()) && !sub_object_it->is_null()) {
        if (!sub_object_it->is_number_unsigned()) {
            return make_error_content("sub_object must be an integer index (see get_item_properties sub_objects)");
        }
        sub_object = sub_object_it->get<std::size_t>();
        target     = item->get_property_sub_object(sub_object.value());
        if (target == nullptr) {
            return make_error_content("Item '" + item->get_name() + "' has no sub-object " + std::to_string(sub_object.value()) + " (it has " + std::to_string(item->get_property_sub_object_count()) + ")");
        }
    }
    const erhe::property::Dependency_property* property = erhe::property::Property_registry::get().find_for_object(*target, property_name);
    if (property == nullptr) {
        return make_error_content("Item '" + item->get_name() + "' (" + std::string{item->get_type_name()} + ")" + (sub_object.has_value() ? " sub-object " + std::to_string(sub_object.value()) : std::string{}) + " has no property '" + property_name + "'");
    }
    if (property->is_read_only()) {
        return make_error_content("Property '" + property_name + "' is read-only");
    }
    if (target->is_write_sealed(*property)) { // D24; lock_edit itself stays writable
        return make_error_content("Item '" + item->get_name() + "' is sealed (lock_edit): set lock_edit false or unlock_items first, or edit the prefab's source scene");
    }
    const bool computed_writable = property->get_metadata(target->get_property_owner_type()).is_computed_writable(); // D26

    // An expression (doc/property-system.md D22) instead of a value.
    const auto expression_it = args.find("expression");
    if ((expression_it != args.end()) && !expression_it->is_null()) {
        if (computed_writable) {
            return make_error_content("Property '" + property_name + "' is computed: an expression cannot drive it (set '" + std::string{property->get_metadata(target->get_property_owner_type()).compute_writes->get_name()} + "' instead)");
        }
        if (!expression_it->is_string()) {
            return make_error_content("expression must be a string");
        }
        const std::string text = expression_it->get<std::string>();
        if (!erhe::property::validate_expression_text(*property, text, error)) {
            return make_error_content("expression '" + text + "' rejected for property '" + property_name + "': " + error);
        }
        const std::optional<erhe::property::Local_state> before = target->read_local_state(*property);
        m_context.operation_stack->queue(
            std::make_shared<Property_set_operation>(item, sub_object, *property, before, erhe::property::Local_state{erhe::property::Expression_text{text}})
        );
        json result = {
            {"item",       {{"id", item->get_id()}, {"name", item->get_name()}, {"type", std::string{item->get_type_name()}}}},
            {"property",   property_name},
            {"before",     describe_local_state(*property, before)},
            {"expression", text},
            {"queued",     true}
        };
        return make_json_content(result).dump();
    }

    std::optional<erhe::property::Property_value> after;
    const auto value_it        = args.find("value");
    const auto reference_id_it = args.find("reference_id");
    const bool has_reference_id = (reference_id_it != args.end()) && !reference_id_it->is_null();
    const bool clear = !has_reference_id && ((value_it == args.end()) || value_it->is_null());
    if (has_reference_id) {
        // D28: an object reference by the referenced item's session id,
        // which disambiguates same-named items.
        if (property->get_type() != erhe::property::Property_type::object) {
            return make_error_content("reference_id applies to object properties only; '" + property_name + "' is " + erhe::property::c_str(property->get_type()));
        }
        if (!reference_id_it->is_number_unsigned()) {
            return make_error_content("reference_id must be an integer item id");
        }
        json id_args = json::object();
        id_args["item_id"] = reference_id_it->get<std::size_t>();
        const std::shared_ptr<erhe::Item_base> referenced = resolve_item(m_context, id_args, error);
        if (!referenced) {
            return make_error_content("reference_id: " + error);
        }
        after = erhe::property::Object_reference{referenced};
        std::string validation_error;
        if (!target->validate_value(*property, after.value(), validation_error)) {
            return make_error_content("'" + referenced->get_name() + "' (" + std::string{referenced->get_type_name()} + ") was rejected by property '" + property_name + "': " + validation_error);
        }
    } else if (!clear) {
        // Accept a string in property_string form, or a JSON number / bool /
        // array of numbers, which is rendered to that form first.
        std::string text;
        if (value_it->is_string()) {
            text = value_it->get<std::string>();
        } else if (value_it->is_boolean()) {
            text = value_it->get<bool>() ? "true" : "false";
        } else if (value_it->is_number()) {
            text = value_it->dump();
        } else if (value_it->is_array()) {
            for (const json& component : *value_it) {
                if (!component.is_number()) {
                    return make_error_content("value array entries must be numbers");
                }
                if (!text.empty()) {
                    text += " ";
                }
                text += component.dump();
            }
        } else {
            return make_error_content("value must be a string, number, bool, array of numbers, or null (reset to default)");
        }
        if (property->get_type() == erhe::property::Property_type::object) {
            // D28: a name resolved in the item's scene; empty clears.
            if (text.empty()) {
                after = erhe::property::Object_reference{};
            } else {
                const std::shared_ptr<erhe::Item_base> referenced = resolve_reference_by_name(m_context, *item, text);
                if (!referenced) {
                    return make_error_content("'" + text + "' does not name an item of the scene of '" + item->get_name() + "' (use reference_id for an item id)");
                }
                after = erhe::property::Object_reference{referenced};
            }
        } else {
            after = erhe::property::parse_value(*property, text);
        }
        if (!after.has_value()) {
            return make_error_content("'" + text + "' is not a valid " + erhe::property::c_str(property->get_type()) + " for property '" + property_name + "'");
        }
        std::string validation_error;
        if (!target->validate_value(*property, after.value(), validation_error)) {
            log_mcp->warn("set_item_property '{}': {}", property_name, validation_error);
            return make_error_content("'" + text + "' was rejected by property '" + property_name + "': " + validation_error);
        }
    }

    if (computed_writable) {
        // D26: the value goes through the setter and the operation records
        // the stored property it wrote.
        if (!after.has_value()) {
            return make_error_content("Property '" + property_name + "' is computed: it has no local value to clear");
        }
        const std::shared_ptr<Property_set_operation> operation = make_computed_write_operation(item, sub_object, *target, *property, after.value());
        if (!operation) {
            return make_error_content("Property '" + property_name + "' refused the value");
        }
        m_context.operation_stack->queue(operation);
        json result = {
            {"item",       {{"id", item->get_id()}, {"name", item->get_name()}, {"type", std::string{item->get_type_name()}}}},
            {"sub_object", sub_object.has_value() ? json(sub_object.value()) : json(nullptr)},
            {"property",   property_name},
            {"writes",     std::string{operation->get_property().get_name()}},
            {"after",      erhe::property::to_string(*property, after.value())},
            {"queued",     true}
        };
        return make_json_content(result).dump();
    }
    const std::optional<erhe::property::Local_state> before = target->read_local_state(*property);
    m_context.operation_stack->queue(std::make_shared<Property_set_operation>(item, sub_object, *property, before, to_local_state(after)));

    json result = {
        {"item",     {{"id", item->get_id()}, {"name", item->get_name()}, {"type", std::string{item->get_type_name()}}}},
        {"sub_object", sub_object.has_value() ? json(sub_object.value()) : json(nullptr)},
        {"property", property_name},
        {"before",   before.has_value() ? json(describe_local_state(*property, before)) : json(nullptr)},
        {"after",    after.has_value() ? value_json(*property, after.value()) : json(nullptr)},
        {"queued",   true}
    };
    return make_json_content(result).dump();
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

// An empty style item in the scene's Styles folder (doc/style-library.md
// R1); fill it with set_item_property by qualified name and assign it
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
    if (!library || !library->styles) {
        return make_error_content("Scene has no content library");
    }
    std::shared_ptr<Style> style{};
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{library->mutex};
        style = std::make_shared<Style>(make_unique_style_name(*library->styles, name));
    }
    m_context.operation_stack->execute_now(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = std::make_shared<Content_library_node>(style),
                .parent  = library->styles,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
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
