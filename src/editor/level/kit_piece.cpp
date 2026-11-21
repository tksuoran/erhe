#include "level/kit_piece.hpp"
#include "level/kit_shape.hpp"

#include "erhe_property/attached_group.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_scene/curve.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>

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

const Property<std::string> Kit_piece::attachment_property = Property<std::string>::register_attached(
    "attachment", Kit_piece::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value = std::string{},
        .inherits      = false,
        .ui            = Property_ui{
            .group          = c_group,
            .tooltip        = "The piece's connector join it follows: owner, target uid, connectors, twist and gap as canonical JSON text",
            .developer_only = true,
            .label          = "Attachment",
            .visible_when   = erhe::property::attached_group_visible_when(Kit_piece::shape_property.get())
        }
    }
);

const Property<std::string> Kit_piece::surface_attachment_property = Property<std::string>::register_attached(
    "surface_attachment", Kit_piece::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value = std::string{},
        .inherits      = false,
        .ui            = Property_ui{
            .group          = c_group,
            .tooltip        = "The piece's place on its support (its parent): target uid, position in the support's bounds, direction, orient and rest as canonical JSON text",
            .developer_only = true,
            .label          = "Surface Attachment",
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
        disabled_connectors_property.get_ptr(),
        attachment_property         .get_ptr(),
        surface_attachment_property .get_ptr()
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

auto read_piece_connector_switches(const erhe::scene::Node& node) -> Kit_connector_switches
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

namespace {

[[nodiscard]] auto attachment_json(const Kit_attachment& attachment) -> nlohmann::json
{
    return nlohmann::json{
        {"connector",        attachment.connector},
        {"gap",              format_hexfloat(attachment.gap)},
        {"owner",            attachment.owner},
        {"target",           attachment.target},
        {"target_connector", attachment.target_connector},
        {"twist_steps",      attachment.twist_steps}
    };
}

[[nodiscard]] auto parse_attachment_object(const nlohmann::json& object, Kit_attachment& out_attachment) -> std::string
{
    if (!object.is_object()) {
        return "the attachment is not a JSON object";
    }
    const auto read_text = [&object](const char* key, std::string& out_value) -> bool {
        const nlohmann::json::const_iterator i = object.find(key);
        if ((i == object.end()) || !i->is_string() || i->get_ref<const std::string&>().empty()) {
            return false;
        }
        out_value = i->get<std::string>();
        return true;
    };
    Kit_attachment attachment;
    if (!read_text("owner", attachment.owner) || !read_text("target", attachment.target) ||
        !read_text("connector", attachment.connector) || !read_text("target_connector", attachment.target_connector)) {
        return "the attachment needs the texts owner, target, connector and target_connector";
    }
    const nlohmann::json::const_iterator twist = object.find("twist_steps");
    if ((twist == object.end()) || !twist->is_number_integer()) {
        return "the attachment's twist_steps must be an integer";
    }
    attachment.twist_steps = twist->get<int>();
    if ((attachment.twist_steps < 0) || (attachment.twist_steps > 3)) {
        return "the attachment's twist_steps must be 0 - 3 (quarter turns)";
    }
    const nlohmann::json::const_iterator gap = object.find("gap");
    if ((gap == object.end()) || !gap->is_string() || !parse_hexfloat(gap->get_ref<const std::string&>(), attachment.gap) || !std::isfinite(attachment.gap)) {
        return "the attachment's gap must be a finite hexadecimal float text";
    }
    out_attachment = std::move(attachment);
    return {};
}

} // anonymous namespace

auto make_attachment_text(const Kit_attachment& attachment) -> std::string
{
    return attachment_json(attachment).dump();
}

auto parse_attachment_text(const std::string_view text, Kit_attachment& out_attachment) -> std::string
{
    return parse_attachment_object(nlohmann::json::parse(text, nullptr, false), out_attachment);
}

auto make_attachments_text(std::vector<Kit_attachment> attachments) -> std::string
{
    if (attachments.empty()) {
        return {};
    }
    if (attachments.size() == 1) {
        return make_attachment_text(attachments.front()); // the form a piece of one connection always had
    }
    std::sort(attachments.begin(), attachments.end(), [](const Kit_attachment& a, const Kit_attachment& b) { return a.connector < b.connector; });
    nlohmann::json array = nlohmann::json::array();
    for (const Kit_attachment& attachment : attachments) {
        array.push_back(attachment_json(attachment));
    }
    return array.dump();
}

auto parse_attachments_text(const std::string_view text, std::vector<Kit_attachment>& out_attachments) -> std::string
{
    out_attachments.clear();
    const nlohmann::json value = nlohmann::json::parse(text, nullptr, false);
    if (value.is_object()) {
        Kit_attachment attachment;
        std::string error = parse_attachment_object(value, attachment);
        if (error.empty()) {
            out_attachments.push_back(std::move(attachment));
        }
        return error;
    }
    if (!value.is_array()) {
        return "the connections are neither a JSON object nor an array";
    }
    for (const nlohmann::json& object : value) {
        Kit_attachment attachment;
        std::string error = parse_attachment_object(object, attachment);
        if (!error.empty()) {
            out_attachments.clear();
            return error;
        }
        for (const Kit_attachment& other : out_attachments) {
            if (other.connector == attachment.connector) {
                out_attachments.clear();
                return "two connections of one connector '" + attachment.connector + "'";
            }
        }
        out_attachments.push_back(std::move(attachment));
    }
    return {};
}

auto read_kit_attachments(const erhe::scene::Node& node) -> std::vector<Kit_attachment>
{
    std::vector<Kit_attachment> attachments;
    // A kit piece, or a floor layout (a Curve node; floors connect to floors, K6).
    if (!carries_kit_piece(node) && (dynamic_cast<const erhe::scene::Curve*>(&node) == nullptr)) {
        return attachments;
    }
    // The local value only: a style or an inherited value never supplies an attachment.
    const std::optional<std::string> text = node.read_local_value(Kit_piece::attachment_property);
    if (!text.has_value() || text.value().empty() || !parse_attachments_text(text.value(), attachments).empty()) {
        attachments.clear();
        return attachments;
    }
    std::erase_if(attachments, [&node](const Kit_attachment& attachment) { return attachment.owner != node.get_gltf_uid(); });
    return attachments;
}

namespace {

[[nodiscard]] auto format_hexdouble(const double value) -> std::string
{
    std::string text;
    if (std::signbit(value)) {
        text.push_back('-');
    }
    text.append("0x");
    std::array<char, 64> buffer{};
    const std::to_chars_result result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), std::fabs(value), std::chars_format::hex);
    text.append(buffer.data(), result.ptr);
    return text;
}

[[nodiscard]] auto parse_hexdouble(const std::string_view text, double& out_value) -> bool
{
    std::string_view rest = text;
    bool negative = false;
    if (!rest.empty() && (rest.front() == '-')) {
        negative = true;
        rest.remove_prefix(1);
    }
    if ((rest.size() < 3) || (rest[0] != '0') || ((rest[1] != 'x') && (rest[1] != 'X'))) {
        return false;
    }
    // strtod on a null-terminated copy, as parse_hexfloat() (no floating point from_chars on the Android NDK).
    const std::string copy{rest};
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if ((end != (copy.c_str() + copy.size())) || !std::isfinite(value)) {
        return false;
    }
    out_value = negative ? -value : value;
    return true;
}

[[nodiscard]] auto read_hex_array(const nlohmann::json& object, const char* key, const std::size_t count, double* out_values) -> bool
{
    const nlohmann::json::const_iterator i = object.find(key);
    if ((i == object.end()) || !i->is_array() || (i->size() != count)) {
        return false;
    }
    for (std::size_t k = 0; k < count; ++k) {
        const nlohmann::json& element = (*i)[k];
        if (!element.is_string() || !parse_hexdouble(element.get_ref<const std::string&>(), out_values[k])) {
            return false;
        }
    }
    return true;
}

} // anonymous namespace

auto make_surface_attachment_text(const Kit_surface_attachment& attachment) -> std::string
{
    nlohmann::json direction = nlohmann::json::array();
    nlohmann::json position  = nlohmann::json::array();
    for (int axis = 0; axis < 3; ++axis) {
        direction.push_back(format_hexfloat(attachment.direction[axis]));
        position .push_back(format_hexdouble(attachment.position[axis]));
    }
    return nlohmann::json{
        {"direction", direction},
        {"on_surface", attachment.on_surface},
        {"orient",    attachment.orient == Surface_orient::on},
        {"position",  position},
        {"rest",      std::string{to_string(attachment.rest)}},
        {"target",    attachment.target}
    }.dump();
}

auto parse_surface_attachment_text(const std::string_view text, Kit_surface_attachment& out_attachment) -> std::string
{
    const nlohmann::json object = nlohmann::json::parse(text, nullptr, false);
    if (!object.is_object()) {
        return "the surface attachment is not a JSON object";
    }
    Kit_surface_attachment attachment;
    const nlohmann::json::const_iterator target = object.find("target");
    if ((target == object.end()) || !target->is_string() || target->get_ref<const std::string&>().empty()) {
        return "the surface attachment's target must be a uid text";
    }
    attachment.target = target->get<std::string>();
    double position[3]{};
    if (!read_hex_array(object, "position", 3, position)) {
        return "the surface attachment's position must be 3 finite hexadecimal float texts";
    }
    attachment.position = glm::dvec3{position[0], position[1], position[2]};
    double direction[3]{};
    if (!read_hex_array(object, "direction", 3, direction)) {
        return "the surface attachment's direction must be 3 finite hexadecimal float texts";
    }
    attachment.direction = glm::vec3{static_cast<float>(direction[0]), static_cast<float>(direction[1]), static_cast<float>(direction[2])};
    const float length = glm::length(attachment.direction);
    if (!(std::abs(length - 1.0f) <= 1.0e-4f)) {
        return "the surface attachment's direction must be a unit vector";
    }
    const nlohmann::json::const_iterator orient = object.find("orient");
    if ((orient == object.end()) || !orient->is_boolean()) {
        return "the surface attachment's orient must be true or false";
    }
    attachment.orient = orient->get<bool>() ? Surface_orient::on : Surface_orient::off;
    const nlohmann::json::const_iterator rest = object.find("rest");
    if ((rest == object.end()) || !rest->is_string() || !from_string(rest->get_ref<const std::string&>(), attachment.rest)) {
        return "the surface attachment's rest must be leading_face or anchor";
    }
    const nlohmann::json::const_iterator on_surface = object.find("on_surface");
    if ((on_surface == object.end()) || !on_surface->is_boolean()) {
        return "the surface attachment's on_surface must be true or false";
    }
    attachment.on_surface = on_surface->get<bool>();
    if (object.size() != 6) {
        return "the surface attachment has keys other than direction, on_surface, orient, position, rest and target";
    }
    out_attachment = std::move(attachment);
    return {};
}

auto read_kit_surface_attachment(const erhe::scene::Node& node, std::string* const out_problem) -> std::optional<Kit_surface_attachment>
{
    // The local value only: a style or an inherited value never supplies a record.
    const std::optional<std::string> text = node.read_local_value(Kit_piece::surface_attachment_property);
    if (!text.has_value() || text.value().empty()) {
        return {};
    }
    Kit_surface_attachment attachment;
    const std::string problem = parse_surface_attachment_text(text.value(), attachment);
    if (!problem.empty()) {
        if (out_problem != nullptr) {
            *out_problem = problem;
        }
        return {};
    }
    return attachment;
}

auto is_surface_attached_to_parent(const erhe::scene::Node& node) -> bool
{
    // Most nodes carry no record: the bool test first, the parse only for a node that does.
    if (!node.has_local_value(Kit_piece::surface_attachment_property.get())) {
        return false;
    }
    const std::shared_ptr<erhe::scene::Node> parent = node.get_parent_node();
    if (!parent || (dynamic_cast<const erhe::scene::Mesh*>(parent.get()) == nullptr)) {
        return false;
    }
    const std::optional<Kit_surface_attachment> record = read_kit_surface_attachment(node);
    return record.has_value() && (record.value().target == parent->get_gltf_uid());
}

namespace {

void collect_copied_nodes(const erhe::Hierarchy& source, erhe::Hierarchy& copy, std::vector<std::pair<std::string, erhe::scene::Node*>>& out_pairs)
{
    const erhe::scene::Node* const source_node = dynamic_cast<const erhe::scene::Node*>(&source);
    erhe::scene::Node* const       copy_node   = dynamic_cast<erhe::scene::Node*>(&copy);
    if ((source_node != nullptr) && (copy_node != nullptr)) {
        out_pairs.emplace_back(source_node->get_gltf_uid(), copy_node);
    }
    const std::vector<std::shared_ptr<erhe::Hierarchy>>& source_children = source.get_children();
    const std::vector<std::shared_ptr<erhe::Hierarchy>>& copy_children   = copy.get_children();
    for (std::size_t i = 0, end = std::min(source_children.size(), copy_children.size()); i < end; ++i) {
        collect_copied_nodes(*source_children[i], *copy_children[i], out_pairs);
    }
}

} // anonymous namespace

void remap_copied_surface_attachments(const erhe::Hierarchy& source, erhe::Hierarchy& copy)
{
    std::vector<std::pair<std::string, erhe::scene::Node*>> pairs;
    collect_copied_nodes(source, copy, pairs);
    for (const std::pair<std::string, erhe::scene::Node*>& pair : pairs) {
        erhe::scene::Node& node = *pair.second;
        if (!node.has_local_value(Kit_piece::surface_attachment_property.get())) {
            continue;
        }
        std::optional<Kit_surface_attachment> record = read_kit_surface_attachment(node);
        if (!record.has_value()) {
            continue;
        }
        for (const std::pair<std::string, erhe::scene::Node*>& other : pairs) {
            if (other.first == record.value().target) {
                record.value().target = other.second->get_gltf_uid();
                node.set_value(Kit_piece::surface_attachment_property, make_surface_attachment_text(record.value()));
                break;
            }
        }
    }
}

auto read_kit_attachment(const erhe::scene::Node& node) -> std::optional<Kit_attachment>
{
    const std::vector<Kit_attachment> attachments = read_kit_attachments(node);
    if (attachments.empty()) {
        return {};
    }
    return attachments.front();
}

} // namespace editor
