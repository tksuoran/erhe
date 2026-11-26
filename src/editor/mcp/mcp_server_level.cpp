// Level kit MCP tools (doc/editor/level_tools.md "MCP").

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"
#include "app_context.hpp"
#include "app_scenes.hpp"
#include "editor_log.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_placement.hpp"
#include "brushes/brush_tool.hpp"
#include "content_library/content_library.hpp"
#include "config/generated/foliage_paint_config.hpp"
#include "level/foliage_paint_tool.hpp"
#include "level/foliage_palette.hpp"
#include "level/foliage_stroke.hpp"
#include "level/foliage_system.hpp"
#include "level/kit_connector_indicators.hpp"
#include "level/kit_connector_snap.hpp"
#include "level/kit_layout.hpp"
#include "level/kit_layout_indicators.hpp"
#include "level/kit_layout_outline.hpp"
#include "level/kit_layout_region_tool.hpp"
#include "level/kit_light.hpp"
#include "level/kit_piece.hpp"
#include "level/kit_piece_light.hpp"
#include "level/kit_shape_checks.hpp"
#include "level/kit_shapes.hpp"
#include "level/kit_thumbnails.hpp"
#include "level/level_kits.hpp"
#include "level/level_queries.hpp"
#include "level/level_repeat.hpp"
#include "operations/curve_set_operation.hpp"
#include "operations/operation_stack.hpp"
#include "prefabs/instance_structure.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_physics_system.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "transform/transform_tool.hpp"
#include "windows/inventory_window.hpp"
#include "windows/viewport_window.hpp"
#include "tools/curve_tool.hpp"
#include "tools/hotbar.hpp"
#include "tools/debug_visualizations.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_math/curve_path.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/curve.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform.hpp"

#include <geogram/mesh/mesh.h>
#include <glm/gtc/constants.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace editor {

using json = nlohmann::json;
using namespace mcp_server_detail;

namespace {

[[nodiscard]] auto vec3_json(const glm::vec3& v) -> json
{
    return json::array({v.x, v.y, v.z});
}

// The values by name and "derive": the masked parameter names in parameter order (D4.5).
[[nodiscard]] auto values_json(const Kit_parameter_values& values) -> json
{
    json result = json::object();
    json derive = json::array();
    const Kit_shape_definition& shape = *values.get_shape();
    for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
        const Kit_parameter&       parameter = shape.parameters[i];
        const Kit_parameter_value& value     = values.get_values()[i];
        switch (parameter.type) {
            case Kit_parameter_type::float_value: result[parameter.name] = value.float_value; break;
            case Kit_parameter_type::int_value:   result[parameter.name] = value.int_value; break;
            case Kit_parameter_type::enum_value:  result[parameter.name] = std::string{values.get_enum(parameter.name)}; break;
            default: break;
        }
        if (values.get_value_source(i) == Kit_value_source::seed) {
            derive.push_back(parameter.name);
        }
    }
    result["derive"] = derive;
    return result;
}

[[nodiscard]] auto aabb_json(const erhe::math::Aabb& aabb) -> json
{
    if (!aabb.is_valid_3d()) {
        return json(nullptr);
    }
    return json{{"min", vec3_json(aabb.min)}, {"max", vec3_json(aabb.max)}};
}

// The body's box in body space (node space at the node's world scale) when its shape is a box, alone or as the one
// unrotated child of a compound, below scale wrappers; invalid for any other shape.
[[nodiscard]] auto get_body_box(const erhe::physics::ICollision_shape* shape) -> erhe::math::Aabb
{
    using erhe::physics::Collision_shape_type;
    glm::vec3 scale{1.0f};
    while ((shape != nullptr) && shape->get_inner_shape()) {
        scale *= shape->get_scale().value_or(glm::vec3{1.0f});
        shape = shape->get_inner_shape().get();
    }
    glm::vec3 center{0.0f};
    if ((shape != nullptr) && (shape->get_shape_type() == Collision_shape_type::e_compound)) {
        const std::vector<erhe::physics::Compound_child>& children = shape->get_children();
        if ((children.size() != 1) || (children.front().transform.basis != glm::mat3{1.0f})) {
            return erhe::math::Aabb{};
        }
        center = children.front().transform.origin;
        shape  = children.front().shape.get();
    }
    if ((shape == nullptr) || (shape->get_shape_type() != Collision_shape_type::e_box)) {
        return erhe::math::Aabb{};
    }
    const glm::vec3 half_extents = shape->get_half_extents().value_or(glm::vec3{0.0f});
    erhe::math::Aabb box{};
    box.min = scale * (center - half_extents);
    box.max = scale * (center + half_extents);
    return box;
}

[[nodiscard]] auto world_aabb_json(const erhe::scene::Node& node) -> json
{
    const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(&node);
    return mesh ? aabb_json(mesh->get_aabb_world()) : json(nullptr);
}

[[nodiscard]] auto brush_triangle_count(Brush& brush) -> unsigned int
{
    const std::shared_ptr<erhe::geometry::Geometry> geometry = brush.get_geometry();
    return geometry ? count_kit_triangles(geometry->get_mesh()) : 0u;
}

// Bounds of the shape's mesh at the given values, from the generator alone.
[[nodiscard]] auto shape_bounds(const Kit_parameter_values& values) -> erhe::math::Aabb
{
    erhe::geometry::Geometry geometry{values.get_shape()->id};
    values.get_shape()->generator(values, geometry.get_mesh());
    const GEO::Mesh& mesh = geometry.get_mesh();
    erhe::math::Aabb bounds{};
    for (const GEO::index_t vertex : mesh.vertices) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, vertex);
        bounds.include(glm::vec3{p.x, p.y, p.z});
    }
    return bounds;
}

[[nodiscard]] auto parameter_json(const Kit_parameter& parameter, const float module, const float wall_height) -> json
{
    json result{
        {"name",        parameter.name},
        {"description", parameter.description},
        {"type",        c_str(parameter.type)},
        {"unit",        c_str(parameter.unit)}
    };
    switch (parameter.type) {
        case Kit_parameter_type::float_value: {
            result["default"]        = parameter.resolve_float_default(module, wall_height);
            result["default_scale"]  = c_str(parameter.default_scale);
            result["default_factor"] = parameter.float_default;
            result["min"]            = parameter.float_min;
            result["max"]            = parameter.float_max;
            break;
        }
        case Kit_parameter_type::int_value: {
            result["default"] = parameter.int_default;
            result["min"]     = parameter.int_min;
            result["max"]     = parameter.int_max;
            break;
        }
        case Kit_parameter_type::enum_value: {
            const bool in_range = (parameter.int_default >= 0) && (static_cast<std::size_t>(parameter.int_default) < parameter.enum_values.size());
            result["default"] = in_range ? parameter.enum_values[static_cast<std::size_t>(parameter.int_default)] : std::string{};
            result["values"]  = parameter.enum_values;
            break;
        }
        default: break;
    }
    result["derive"]          = c_str(parameter.derive);
    result["default_derived"] = (parameter.derive == Kit_derive::derivable) && (parameter.default_source == Kit_value_source::seed);
    if (parameter.derive == Kit_derive::derivable) {
        if (parameter.type == Kit_parameter_type::float_value) {
            const Kit_derive_range range = parameter.resolve_derive_range(module, wall_height);
            result["derive_min"]  = range.min_value;
            result["derive_max"]  = range.max_value;
            result["derive_step"] = range.step_value;
        } else if (parameter.type == Kit_parameter_type::int_value) {
            result["derive_min"] = parameter.derive_int_min;
            result["derive_max"] = parameter.derive_int_max;
        }
    }
    return result;
}

[[nodiscard]] auto unrecognized_keys(const json& object, const std::vector<std::string_view>& allowed) -> std::string
{
    std::string unrecognized;
    for (const auto& [key, value] : object.items()) {
        static_cast<void>(value);
        if (std::find(allowed.begin(), allowed.end(), std::string_view{key}) == allowed.end()) {
            unrecognized += (unrecognized.empty() ? "" : ", ") + key;
        }
    }
    return unrecognized;
}

// A JSON number as a float without an out-of-range conversion: beyond the float range it is an infinity of its sign.
[[nodiscard]] auto to_float_or_infinity(const json& value) -> float
{
    const double number = value.get<double>();
    if (!std::isfinite(number) || (std::abs(number) > static_cast<double>(std::numeric_limits<float>::max()))) {
        return std::signbit(number) ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity();
    }
    return static_cast<float>(number);
}

[[nodiscard]] auto read_finite_numbers(const json& value, const std::size_t count, float* out) -> bool
{
    if (!value.is_array() || (value.size() != count)) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!value[i].is_number()) {
            return false;
        }
        out[i] = value[i].get<float>();
        if (!std::isfinite(out[i])) {
            return false;
        }
    }
    return true;
}

// Writes the given parameters over the values; "derive" replaces the mask. The error names the parameter and what it takes.
[[nodiscard]] auto apply_parameters_json(const json& parameters, Kit_parameter_values& values) -> std::string
{
    if (!parameters.is_object()) {
        return "'parameters' must be an object of parameter name to value";
    }
    const Kit_shape_definition& shape = *values.get_shape();
    if (parameters.contains("derive")) {
        const json& derive = parameters.at("derive");
        if (!derive.is_array()) {
            return shape.id + ": 'derive' must be an array of parameter names";
        }
        for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
            values.set_value_source(i, Kit_value_source::explicit_value);
        }
        for (const json& name : derive) {
            const std::optional<std::size_t> index = name.is_string() ? shape.find_parameter(name.get<std::string>()) : std::optional<std::size_t>{};
            if (!index.has_value()) {
                return shape.id + ": 'derive' names " + name.dump() + ", not a parameter of the shape";
            }
            const Kit_parameter& parameter = shape.parameters[index.value()];
            if (parameter.derive != Kit_derive::derivable) {
                return shape.id + ": parameter '" + parameter.name + "' cannot be derived from the seed (level_get_kits: derive)";
            }
            values.set_value_source(index.value(), Kit_value_source::seed);
        }
    }
    for (const auto& [key, value] : parameters.items()) {
        if (key == "derive") {
            continue;
        }
        const std::optional<std::size_t> index = shape.find_parameter(key);
        if (!index.has_value()) {
            std::string names;
            for (const Kit_parameter& parameter : shape.parameters) {
                names += (names.empty() ? "" : ", ") + parameter.name;
            }
            return shape.id + ": unknown parameter '" + key + "' (parameters: " + names + ")";
        }
        const Kit_parameter& parameter = shape.parameters[index.value()];
        if (values.get_value_source(index.value()) == Kit_value_source::seed) {
            return shape.id + ": parameter '" + key + "' is derived from the seed; give it no value, or leave it out of 'derive'";
        }
        switch (parameter.type) {
            case Kit_parameter_type::float_value: {
                if (!value.is_number()) {
                    return shape.id + ": parameter '" + key + "' takes a number";
                }
                static_cast<void>(values.set_float(key, value.get<float>()));
                break;
            }
            case Kit_parameter_type::int_value: {
                if (!value.is_number_integer()) {
                    return shape.id + ": parameter '" + key + "' takes an integer";
                }
                const long long number = value.get<long long>();
                if ((number < std::numeric_limits<int>::min()) || (number > std::numeric_limits<int>::max())) {
                    return shape.id + ": parameter '" + key + "' is " + value.dump() + ", outside its range " +
                        std::to_string(parameter.int_min) + " - " + std::to_string(parameter.int_max);
                }
                static_cast<void>(values.set_int(key, static_cast<int>(number)));
                break;
            }
            case Kit_parameter_type::enum_value: {
                if (!value.is_string() || !values.set_enum(key, value.get<std::string>())) {
                    std::string names;
                    for (const std::string& name : parameter.enum_values) {
                        names += (names.empty() ? "" : ", ") + name;
                    }
                    return shape.id + ": parameter '" + key + "' takes one of " + names;
                }
                break;
            }
            default: {
                return shape.id + ": parameter '" + key + "' has an unknown type";
            }
        }
    }
    return {};
}

[[nodiscard]] auto find_scene_material(Scene_root& scene_root, const std::string& name) -> std::shared_ptr<erhe::primitive::Material>
{
    const std::shared_ptr<Content_library> library = scene_root.get_content_library();
    if (!library) {
        return {};
    }
    for (const std::shared_ptr<erhe::primitive::Material>& material : library->get_all<erhe::primitive::Material>()) {
        if (material->get_name() == name) {
            return material;
        }
    }
    return {};
}

// A piece's material per slot name, from its mesh primitives in slot order; a primitive beyond the shape's slots
// (a loaded mesh that does not follow the kit) is named by its index.
[[nodiscard]] auto piece_materials_json(const erhe::scene::Node& node, const Kit_shape_definition* const shape) -> json
{
    json result = json::object();
    const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(&node);
    if (!mesh) {
        return result;
    }
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    for (std::size_t i = 0, end = primitives.size(); i < end; ++i) {
        const std::string key = ((shape != nullptr) && (i < shape->material_slots.size()))
            ? shape->material_slots[i]
            : ("primitive " + std::to_string(i));
        result[key] = primitives[i].material ? primitives[i].material->get_name() : std::string{};
    }
    return result;
}

[[nodiscard]] auto quat_json(const glm::quat& q) -> json
{
    return json::array({q.x, q.y, q.z, q.w});
}

// A JSON boolean argument; nothing when absent or not a boolean (then out_error says so).
[[nodiscard]] auto read_bool(const json& object, const char* key, std::string& out_error) -> std::optional<bool>
{
    if (!object.contains(key)) {
        return {};
    }
    const json& value = object.at(key);
    if (!value.is_boolean()) {
        out_error = std::string{key} + " must be true or false, not " + value.dump();
        return {};
    }
    return value.get<bool>();
}

[[nodiscard]] auto surface_rest_name(const Surface_snap_rest rest) -> std::string
{
    return std::string{to_string(rest)};
}

// U60: empty when the value is "leading_face" or "anchor", else why not.
[[nodiscard]] auto read_surface_rest(const json& value, Surface_snap_rest& out_rest) -> std::string
{
    if (!value.is_string() || !from_string(value.get<std::string>(), out_rest)) {
        return "must be \"leading_face\" or \"anchor\", not " + value.dump();
    }
    return {};
}

// A direction argument: 3 finite numbers, not all zero, normalized; absent gives straight down.
[[nodiscard]] auto read_direction(const json& object, const char* key, std::string& out_error) -> glm::vec3
{
    const glm::vec3 down{0.0f, -1.0f, 0.0f};
    if (!object.contains(key)) {
        return down;
    }
    float xyz[3]{};
    if (!read_finite_numbers(object.at(key), 3, xyz)) {
        out_error = std::string{key} + " must be an array of 3 finite numbers";
        return down;
    }
    const glm::vec3 direction{xyz[0], xyz[1], xyz[2]};
    const float length = glm::length(direction);
    if (!(length > 0.0f)) {
        out_error = std::string{key} + " must not be zero";
        return down;
    }
    return direction / length;
}

// A surface max distance argument (U30): meters >= 0; absent gives the fallback.
[[nodiscard]] auto read_max_distance(const json& object, const char* key, const float fallback, std::string& out_error) -> float
{
    if (!object.contains(key)) {
        return fallback;
    }
    const json& value = object.at(key);
    if (!value.is_number() || !check_surface_max_distance(value.get<float>()).empty()) {
        out_error = std::string{key} + " must be a number >= 0, not " + value.dump();
        return fallback;
    }
    return value.get<float>();
}

// C2.2: the standard properties a shape takes, each onto a parameter or a material slot.
[[nodiscard]] auto property_mappings_json(const Kit_shape_definition& shape) -> json
{
    json result = json::array();
    for (const Kit_property_mapping& mapping : shape.property_mappings) {
        result.push_back(json{{"property", c_str(mapping.property)}, {"target", mapping.target}});
    }
    return result;
}

[[nodiscard]] auto connector_types_json(const Kit_connector_types types) -> json
{
    json result = json::array();
    for (unsigned int i = 0; i < c_connector_type_count; ++i) {
        if (types.contains(static_cast<Kit_connector_type>(i))) {
            result.push_back(c_str(static_cast<Kit_connector_type>(i)));
        }
    }
    return result;
}

[[nodiscard]] auto connector_json(const Kit_connector& connector) -> json
{
    return json{
        {"name",     connector.name},
        {"position", vec3_json(connector.position)},
        {"outward",  vec3_json(connector.outward)},
        {"up",       vec3_json(connector.up)},
        {"types",    connector_types_json(connector.types)}
    };
}

[[nodiscard]] auto connectors_json(const std::vector<Kit_connector>& connectors) -> json
{
    json result = json::array();
    for (const Kit_connector& connector : connectors) {
        result.push_back(connector_json(connector));
    }
    return result;
}

// A shape's light frames at the values (D3.15): name, position, direction and the values a new light node starts with.
[[nodiscard]] auto light_frames_json(const std::vector<Kit_light_frame>& frames) -> json
{
    json result = json::array();
    for (const Kit_light_frame& frame : frames) {
        result.push_back({
            {"name",             frame.name},
            {"position",         vec3_json(frame.position)},
            {"direction",        vec3_json(frame.direction)},
            {"type",             c_str(frame.start.type)},
            {"color",            vec3_json(frame.start.color)},
            {"intensity",        frame.start.intensity},
            {"range",            frame.start.range},
            {"inner_spot_angle", frame.start.inner_spot_angle},
            {"outer_spot_angle", frame.start.outer_spot_angle}
        });
    }
    return result;
}

// A piece's kit light nodes (D3.15): id, name, frame, the local position and shine direction, the light's own values.
[[nodiscard]] auto piece_lights_json(const erhe::scene::Node& piece) -> json
{
    std::vector<std::shared_ptr<erhe::scene::Light>> lights;
    collect_piece_lights(piece, lights);
    json result = json::array();
    for (const std::shared_ptr<erhe::scene::Light>& light : lights) {
        const erhe::scene::Trs_transform& local = light->parent_from_node_transform();
        result.push_back({
            {"id",               light->get_id()},
            {"name",             light->get_name()},
            {"frame",            read_kit_light_frame(*light).value_or(std::string{})},
            {"position",         vec3_json(local.get_translation())},
            {"direction",        vec3_json(local.get_rotation() * glm::vec3{0.0f, 0.0f, -1.0f})},
            {"type",             erhe::scene::Light::c_type_strings[static_cast<std::size_t>(light->get_light_type())]},
            {"color",            vec3_json(light->get_color())},
            {"intensity",        light->get_intensity()},
            {"range",            light->get_range()},
            {"inner_spot_angle", light->get_inner_spot_angle()},
            {"outer_spot_angle", light->get_outer_spot_angle()},
            {"cast_shadow",      light->get_cast_shadow()}
        });
    }
    return result;
}

// A placed piece's connectors with their state (U33): every connector of the shape and whether it is enabled.
[[nodiscard]] auto piece_connectors_json(const std::vector<Kit_connector>& connectors, const Kit_connector_switches& switches) -> json
{
    json result = json::array();
    for (std::size_t i = 0, end = connectors.size(); i < end; ++i) {
        json entry = connector_json(connectors[i]);
        entry["enabled"] = switches.is_enabled(connectors[i].name, i);
        result.push_back(std::move(entry));
    }
    return result;
}

// U39: the seven connector switches of a path or segments layout, by name.
constexpr std::array<std::pair<const char*, std::optional<Kit_layout_direction>>, 7> c_connector_hiding_keys{{
    {"all",            std::nullopt},
    {"plus_tangent",   Kit_layout_direction::plus_tangent},
    {"minus_tangent",  Kit_layout_direction::minus_tangent},
    {"plus_normal",    Kit_layout_direction::plus_normal},
    {"minus_normal",   Kit_layout_direction::minus_normal},
    {"plus_binormal",  Kit_layout_direction::plus_binormal},
    {"minus_binormal", Kit_layout_direction::minus_binormal}
}};

// The switches given merge over hiding; returns the problem, empty when read.
[[nodiscard]] auto read_connector_hiding_json(const json& value, Kit_layout_connector_hiding& hiding) -> std::string
{
    const char* const accepted = "all, plus_tangent, minus_tangent, plus_normal, minus_normal, plus_binormal, minus_binormal";
    if (!value.is_object()) {
        return std::string{"connector_hiding must be an object of switch name: true (hidden) or false ("} + accepted + ")";
    }
    Kit_layout_connector_hiding result = hiding;
    for (const auto& [key, flag] : value.items()) {
        const std::pair<const char*, std::optional<Kit_layout_direction>>* found = nullptr;
        for (const std::pair<const char*, std::optional<Kit_layout_direction>>& entry : c_connector_hiding_keys) {
            if (key == entry.first) {
                found = &entry;
            }
        }
        if (found == nullptr) {
            return "connector_hiding has no switch '" + key + "' (" + accepted + ")";
        }
        if (!flag.is_boolean()) {
            return "connector_hiding." + key + " must be true or false";
        }
        const Kit_layout_connector_visibility visibility = flag.get<bool>() ? Kit_layout_connector_visibility::hidden : Kit_layout_connector_visibility::shown;
        if (found->second.has_value()) {
            result.set(found->second.value(), visibility);
        } else {
            result.all = visibility;
        }
    }
    hiding = result;
    return {};
}

[[nodiscard]] auto connector_hiding_json(const Kit_layout_connector_hiding& hiding) -> json
{
    json result = json::object();
    for (const std::pair<const char*, std::optional<Kit_layout_direction>>& entry : c_connector_hiding_keys) {
        result[entry.first] = entry.second.has_value()
            ? hiding.is_hidden(entry.second.value())
            : (hiding.all == Kit_layout_connector_visibility::hidden);
    }
    return result;
}

[[nodiscard]] auto mirror_planes_json(const std::vector<Kit_mirror_plane>& planes) -> json
{
    json result = json::array();
    for (const Kit_mirror_plane plane : planes) {
        result.push_back(c_str(plane));
    }
    return result;
}

} // anonymous namespace

// The Level Kit window's shape thumbnails (doc/editor/level_tools.md "Shape thumbnails").
auto Mcp_server::query_level_kit_thumbnails(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"request_all", "scene_name"});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_kit_thumbnails: unrecognized argument(s): " + unrecognized + " (accepted: request_all, scene_name)");
    }
    const bool request_all = args.value("request_all", false);
    if (args.contains("scene_name") && !request_all) {
        return make_error_content("level_get_kit_thumbnails: scene_name is read with request_all only");
    }
    Kit_thumbnails& thumbnails      = m_context.level_kits->get_thumbnails();
    std::size_t     requested_count = 0;
    if (request_all) {
        if (args.contains("scene_name")) {
            const std::string scene_name = args.value("scene_name", "");
            std::shared_ptr<Scene_root> scene_root{};
            for (const std::shared_ptr<Scene_root>& candidate : m_context.app_scenes->get_scene_roots()) {
                if (candidate->get_name() == scene_name) {
                    scene_root = candidate;
                    break;
                }
            }
            if (!scene_root) {
                return make_error_content("level_get_kit_thumbnails: scene not found: " + scene_name);
            }
            thumbnails.follow_scene(scene_root);
        } else if (!thumbnails.get_lengths().has_value()) {
            return make_error_content("level_get_kit_thumbnails: request_all needs scene_name until the Level Kit window has drawn its shape rows");
        }
        requested_count = thumbnails.request_all();
    }
    const std::vector<Kit_shape_definition>& shapes     = m_context.level_kits->get_library().get_shapes();
    const std::vector<Kit_thumbnail>&        entries    = thumbnails.get_thumbnails();
    std::array<std::size_t, c_kit_thumbnail_state_count> state_counts{};
    json shape_entries = json::array();
    for (std::size_t i = 0, end = entries.size(); i < end; ++i) {
        const Kit_thumbnail& entry = entries[i];
        ++state_counts[static_cast<std::size_t>(entry.state)];
        shape_entries.push_back({
            {"id",           shapes[i].id},
            {"state",        c_str(entry.state)},
            {"has_image",    entry.has_image},
            {"render_count", entry.render_count},
            {"mesh_key",     entry.mesh_key}
        });
    }
    const std::optional<Kit_lengths>& lengths = thumbnails.get_lengths();
    return make_json_content({
        {"lengths",       lengths.has_value() ? json{{"module", lengths.value().module}, {"wall_height", lengths.value().wall_height}} : json{}},
        {"size_pixels",   thumbnails.get_size_pixels()},
        {"shape_count",   entries.size()},
        {"none_count",    state_counts[static_cast<std::size_t>(Kit_thumbnail_state::none)]},
        {"queued_count",  state_counts[static_cast<std::size_t>(Kit_thumbnail_state::queued)]},
        {"building_count", state_counts[static_cast<std::size_t>(Kit_thumbnail_state::building)]},
        {"current_count", state_counts[static_cast<std::size_t>(Kit_thumbnail_state::current)]},
        {"failed_count",  state_counts[static_cast<std::size_t>(Kit_thumbnail_state::failed)]},
        {"render_count",  thumbnails.get_render_count()},
        {"requested_count", requested_count},
        {"shapes",        shape_entries}
    }).dump();
}

auto Mcp_server::query_level_kits(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name"});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_kits: unrecognized argument(s): " + unrecognized + " (accepted: scene_name)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    Level_kits&              level_kits = *m_context.level_kits;
    const Kit_shape_library& library    = level_kits.get_library();
    const Kit_lengths        lengths    = level_kits.get_lengths(*scene_root);

    json kits = json::array();
    for (const Kit_definition& kit : library.get_kits()) {
        json shape_ids = json::array();
        for (const Kit_shape_definition& shape : library.get_shapes()) {
            if (shape.kit == kit.name) {
                shape_ids.push_back(shape.id);
            }
        }
        kits.push_back({
            {"name",             kit.name},
            {"display_name",     kit.display_name},
            {"triangle_ceiling", kit.triangle_ceiling},
            {"shapes",           shape_ids}
        });
    }
    json shapes = json::array();
    for (const Kit_shape_definition& shape : library.get_shapes()) {
        json parameters = json::array();
        for (const Kit_parameter& parameter : shape.parameters) {
            parameters.push_back(parameter_json(parameter, lengths.module, lengths.wall_height));
        }
        const Kit_parameter_values      defaults = level_kits.make_default_values(*scene_root, shape);
        const erhe::math::Aabb          bounds   = shape_bounds(defaults);
        json material_slots = json::array();
        for (const std::string& slot : shape.material_slots) {
            const Kit_slot_palette* const slot_palette = library.find_slot_palette(slot);
            material_slots.push_back({
                {"slot",             slot},
                {"default_material", (slot_palette != nullptr) ? slot_palette->default_material : std::string{}},
                {"alternatives",     (slot_palette != nullptr) ? json(slot_palette->alternatives) : json::array()}
            });
        }
        std::vector<Kit_connector> connectors;
        get_shape_connectors(defaults, connectors);
        std::vector<Kit_light_frame> light_frames;
        get_shape_light_frames(defaults, light_frames);
        json default_derive = json::array();
        for (const Kit_parameter& parameter : shape.parameters) {
            if ((parameter.derive == Kit_derive::derivable) && (parameter.default_source == Kit_value_source::seed)) {
                default_derive.push_back(parameter.name);
            }
        }
        // The count parameters the level of detail scales; empty: one level of detail.
        json detail_counts = json::array();
        for (const editor::Kit_detail_count& count : shape.detail_counts) {
            detail_counts.push_back({{"parameter", count.parameter}, {"dimensions", count.dimensions}, {"step", count.step}});
        }
        shapes.push_back({
            {"id",              shape.id},
            {"former_ids",      shape.former_ids},
            {"default_derive",  default_derive},
            {"detail_counts",   detail_counts},
            {"kit",             shape.kit},
            {"name",            shape.display_name},
            {"kind",            shape.parameters.empty() ? "basic" : "parametric"},
            {"description",     shape.description},
            {"tags",            shape.tags},
            {"parameters",      parameters},
            {"default_size",    vec3_json(bounds.max - bounds.min)},
            {"default_bounds",  aabb_json(bounds)},
            {"material_slots",  material_slots},
            {"connectors",      connectors_json(connectors)},
            {"property_mappings", property_mappings_json(shape)},
            {"light_frames",    light_frames_json(light_frames)},
            {"normal_style",    c_str(shape.normal_style)},
            {"triangle_budget", shape.triangle_budget},
            {"collision",       c_str(get_kit_collision(defaults))},
            {"collision_by_values", static_cast<bool>(shape.get_collision)},
            {"mesh_kind",       c_str(shape.mesh_kind)},
            {"seeded",          shape.randomness == Kit_randomness::seeded},
            {"mirror_planes",   mirror_planes_json(shape.mirror_planes)}
        });
    }
    json palette = json::array();
    for (const Kit_palette_material& material : library.get_palette()) {
        palette.push_back({{"name", material.name}, {"base_color", vec3_json(material.base_color)}, {"roughness", material.roughness}, {"metallic", material.metallic}, {"emissive", vec3_json(material.emissive)}});
    }
    return make_json_content({
        {"scene_name",  scene_root->get_name()},
        {"level",       {{"module", lengths.module}, {"wall_height", lengths.wall_height}}},
        {"kits",        kits},
        {"shape_count", shapes.size()},
        {"shapes",      shapes},
        {"palette",     palette},
        {"cached_brush_count", level_kits.get_cached_brush_count()}
    }).dump();
}

namespace {

// The call-wide placement keys of level_place_pieces.
class Placement_options
{
public:
    Kit_snap  snap             {Kit_snap::none};
    bool      surface_snap     {false};
    bool      surface_orient   {false};
    glm::vec3 surface_direction{0.0f, -1.0f, 0.0f};
    float     surface_max_distance{c_default_surface_max_distance};
    Surface_snap_rest surface_rest     {Surface_snap_rest::leading_face};
    bool      attach           {true}; // S2.7: a piece snapped onto a support becomes its child
};

[[nodiscard]] auto read_placement_options(const json& args, Placement_options& out_options) -> std::string
{
    const json snap_value = args.value("snap", json("none"));
    if (snap_value == json("module")) {
        out_options.snap = Kit_snap::module;
    } else if (snap_value != json("none")) {
        return "snap is " + snap_value.dump() + " (none, module)";
    }
    std::string surface_error{};
    out_options.surface_snap      = read_bool(args, "surface_snap",   surface_error).value_or(false);
    out_options.surface_orient    = read_bool(args, "surface_orient", surface_error).value_or(false);
    out_options.surface_direction = read_direction(args, "surface_direction", surface_error);
    out_options.surface_max_distance = read_max_distance(args, "surface_max_distance", c_default_surface_max_distance, surface_error);
    out_options.attach            = read_bool(args, "attach", surface_error).value_or(true);
    if (surface_error.empty() && args.contains("surface_rest")) {
        const std::string rest_error = read_surface_rest(args.at("surface_rest"), out_options.surface_rest);
        if (!rest_error.empty()) {
            return "surface_rest " + rest_error;
        }
    }
    return surface_error;
}

// A piece's own surface_snap and surface_orient over the call's.
[[nodiscard]] auto read_piece_surface(const json& piece, const Placement_options& options, Kit_placement& placement) -> std::string
{
    std::string error{};
    placement.surface = make_surface_options(
        (read_bool(piece, "surface_snap",   error).value_or(options.surface_snap))   ? Surface_snap::on   : Surface_snap::off,
        (read_bool(piece, "surface_orient", error).value_or(options.surface_orient)) ? Surface_orient::on : Surface_orient::off,
        options.surface_direction,
        options.surface_max_distance
    );
    placement.surface.rest   = options.surface_rest;
    placement.surface.attach = options.attach ? Surface_attach::on : Surface_attach::off;
    return error;
}

// shape and parameters: the values over the shape's defaults, derived and validated.
[[nodiscard]] auto read_piece_values_json(Level_kits& level_kits, Scene_root& scene_root, const json& piece, Kit_placement& placement) -> std::string
{
    const std::string shape_id = piece.value("shape", "");
    placement.shape = level_kits.get_library().find(shape_id);
    if (placement.shape == nullptr) {
        return "unknown shape '" + shape_id + "' (level_get_kits lists the shapes)";
    }
    Kit_parameter_values values = level_kits.make_default_values(scene_root, *placement.shape);
    if (piece.contains("parameters")) {
        const std::string error = apply_parameters_json(piece.at("parameters"), values);
        if (!error.empty()) {
            return error;
        }
    }
    {
        // The masked values follow the piece's seed and mask (D1.8).
        const Kit_lengths lengths = level_kits.get_lengths(scene_root);
        values.resolve_derived(lengths.module, lengths.wall_height);
    }
    const std::string range_error = values.validate();
    if (!range_error.empty()) {
        return range_error;
    }
    placement.values = values;
    return {};
}

[[nodiscard]] auto read_piece_pose(const json& piece, Kit_placement& placement) -> std::string
{
    if (piece.contains("position")) {
        float xyz[3]{};
        if (!read_finite_numbers(piece.at("position"), 3, xyz)) {
            return "position must be an array of 3 finite numbers";
        }
        placement.position = glm::vec3{xyz[0], xyz[1], xyz[2]};
    }
    if (piece.contains("rotation_xyzw")) {
        float xyzw[4]{};
        if (!read_finite_numbers(piece.at("rotation_xyzw"), 4, xyzw)) {
            return "rotation_xyzw must be an array of 4 numbers";
        }
        std::string rotation_error{};
        const std::optional<glm::quat> rotation = make_unit_quaternion(xyzw, "rotation_xyzw", rotation_error);
        if (!rotation.has_value()) {
            return rotation_error;
        }
        placement.rotation = rotation.value();
    }
    return {};
}

[[nodiscard]] auto read_piece_name(const json& piece, const char* key, Kit_placement& placement) -> std::string
{
    if (piece.contains(key)) {
        if (!piece.at(key).is_string()) {
            return std::string{key} + " must be a string";
        }
        placement.name = piece.at(key).get<std::string>();
    }
    return {};
}

// A material name of the scene's content library, else of the kit palette (added to the scene when placed).
[[nodiscard]] auto read_slot_material(
    Level_kits&        level_kits,
    Scene_root&        scene_root,
    const std::string& slot,
    const json&        value,
    Kit_slot_material& out_material
) -> std::string
{
    if (!value.is_string()) {
        return "materials['" + slot + "'] must be a material name";
    }
    const std::string name = value.get<std::string>();
    out_material.material = find_scene_material(scene_root, name);
    if (out_material.material) {
        return {};
    }
    if (level_kits.get_library().find_palette_material(name) != nullptr) {
        out_material.palette_name = name;
        return {};
    }
    return "materials['" + slot + "']: no material '" + name + "' in the scene or the kit palette";
}

// materials: {slot name: material name}; a slot not named takes its default palette material.
[[nodiscard]] auto read_piece_materials(Level_kits& level_kits, Scene_root& scene_root, const json& piece, Kit_placement& placement) -> std::string
{
    if (!piece.contains("materials")) {
        return {};
    }
    const json& materials = piece.at("materials");
    if (!materials.is_object()) {
        return "materials must be an object of slot name to material name";
    }
    const Kit_shape_definition& shape = *placement.shape;
    placement.materials.assign(shape.material_slots.size(), Kit_slot_material{});
    for (const auto& [slot, value] : materials.items()) {
        const std::vector<std::string>::const_iterator i = std::find(shape.material_slots.begin(), shape.material_slots.end(), slot);
        if (i == shape.material_slots.end()) {
            std::string slots;
            for (const std::string& name : shape.material_slots) {
                slots += (slots.empty() ? "" : ", ") + name;
            }
            return "materials: " + shape.id + " has no material slot '" + slot + "' (its slots: " + slots + ")";
        }
        const std::string error = read_slot_material(
            level_kits, scene_root, slot, value, placement.materials[static_cast<std::size_t>(i - shape.material_slots.begin())]
        );
        if (!error.empty()) {
            return error;
        }
    }
    return {};
}

[[nodiscard]] auto read_piece_parent(Scene_root& scene_root, const json& piece, Kit_placement& placement) -> std::string
{
    if (piece.contains("parent_id")) {
        if (!piece.at("parent_id").is_number_unsigned()) {
            return "parent_id must be a node id";
        }
        std::string error{};
        placement.parent = find_unique_prim_in_scene(scene_root, piece, "parent_id", "parent_name", "Parent", Absent_prim::error, error);
        if (!placement.parent) {
            return error;
        }
        const std::optional<std::string> refusal = instance_child_refusal(*placement.parent);
        if (refusal.has_value()) {
            return refusal.value();
        }
    }
    return {};
}

// connector, to_piece_id, to_connector, twist_steps and gap of a connector join (D3.12).
[[nodiscard]] auto read_connect_json(Scene_root& scene_root, const json& object, Kit_connect& out_connect) -> std::string
{
    if (!object.contains("connector") || !object.at("connector").is_string()) {
        return "connector must be the name of a connector of the piece (level_get_kits: connectors)";
    }
    if (!object.contains("to_connector") || !object.at("to_connector").is_string()) {
        return "to_connector must be the name of a connector of the target piece (level_get_connectors)";
    }
    if (!object.contains("to_piece_id") || !object.at("to_piece_id").is_number_unsigned()) {
        return "to_piece_id must be the node id of a kit piece";
    }
    out_connect.connector        = object.at("connector").get<std::string>();
    out_connect.target_connector = object.at("to_connector").get<std::string>();
    out_connect.target           = find_node_in_scene(scene_root, json{{"node_id", object.at("to_piece_id")}}, "node_id", "node_name");
    if (!out_connect.target) {
        return "no node " + object.at("to_piece_id").dump() + " in scene " + scene_root.get_name();
    }
    if (object.contains("twist_steps")) {
        if (!object.at("twist_steps").is_number_integer()) {
            return "twist_steps must be an integer (quarter turns)";
        }
        out_connect.twist_steps = object.at("twist_steps").get<int>();
    }
    if (object.contains("gap")) {
        if (!object.at("gap").is_number() || !std::isfinite(object.at("gap").get<float>())) {
            return "gap must be a finite number (meters)";
        }
        out_connect.gap = object.at("gap").get<float>();
    }
    if (object.contains("attach")) {
        if (!object.at("attach").is_boolean()) {
            return "attach must be true or false";
        }
        out_connect.attach = object.at("attach").get<bool>() ? Kit_attach::attach : Kit_attach::none;
    }
    return {};
}

[[nodiscard]] auto flow_piece_json(const Kit_flow_piece& piece) -> json
{
    return json{{"id", piece.id}, {"name", piece.name}};
}

[[nodiscard]] auto flow_value_json(const Kit_standard_property property, const float length, const std::string& material) -> json
{
    return (get_kind(property) == Kit_property_kind::material) ? json(material) : json(length);
}

[[nodiscard]] auto flow_connections_json(const std::vector<Kit_flow_connection>& connections) -> json
{
    json list = json::array();
    for (const Kit_flow_connection& connection : connections) {
        list.push_back(json{
            {"piece",           flow_piece_json(connection.piece)},
            {"connector",       connection.connector},
            {"other",           flow_piece_json(connection.other)},
            {"other_connector", connection.other_connector}
        });
    }
    return list;
}

// C3.13, design K7.6 (doc/editor/level_tools.md "Flow report").
[[nodiscard]] auto flow_report_json(const Kit_flow_report& report) -> json
{
    json taken = json::array();
    for (const Kit_flow_value& value : report.taken) {
        taken.push_back(json{
            {"piece",              flow_piece_json(value.piece)},
            {"property",           c_str(value.property)},
            {"value",              flow_value_json(value.property, value.length, value.material)},
            {"supplier",           flow_piece_json(value.supplier)},
            {"supplier_connector", value.supplier_connector},
            {"priority",           value.priority},
            {"tie",                c_str(value.tie)}
        });
    }
    json not_taken = json::array();
    for (const Kit_flow_skipped_value& value : report.not_taken) {
        not_taken.push_back(json{
            {"piece",    flow_piece_json(value.piece)},
            {"property", c_str(value.property)},
            {"value",    flow_value_json(value.property, value.length, value.material)},
            {"supplier", flow_piece_json(value.supplier)},
            {"priority", value.priority},
            {"reason",   c_str(value.reason)}
        });
    }
    json responses = json::array();
    for (const Kit_flow_response& response : report.responses) {
        responses.push_back(json{{"piece", flow_piece_json(response.piece)}, {"kind", c_str(response.kind)}, {"from", flow_piece_json(response.from)}});
    }
    json pending = json::array();
    for (const Kit_flow_piece& piece : report.pending) {
        pending.push_back(flow_piece_json(piece));
    }
    return json{
        {"taken",               std::move(taken)},
        {"not_taken",           std::move(not_taken)},
        {"responses",           std::move(responses)},
        {"new_connections",     flow_connections_json(report.new_connections)},
        {"removed_connections", flow_connections_json(report.removed_connections)},
        {"apart_connections",   flow_connections_json(report.apart_connections)},
        {"refusal",             report.refusal.empty() ? json(nullptr) : json(report.refusal)},
        {"limit_reached",       report.limit_reached},
        {"rounds",              report.rounds},
        {"pending",             std::move(pending)},
        {"summary",             report.make_summary()}
    };
}

// C6.2: the join tools' flow_mask and auto_connect; returns the refusal.
[[nodiscard]] auto read_flow_overrides(const json& args, Kit_flow_overrides& out_overrides) -> std::string
{
    if (args.contains("auto_connect")) {
        if (!args.at("auto_connect").is_boolean()) {
            return "auto_connect must be true or false";
        }
        out_overrides.auto_connect = args.at("auto_connect").get<bool>();
    }
    if (args.contains("flow_mask")) {
        const json& value = args.at("flow_mask");
        Kit_connector_types mask{.bits = 0};
        bool ok = value.is_array();
        for (const json& name : ok ? value : json::array()) {
            const std::optional<Kit_connector_type> type = name.is_string() ? parse_connector_type(name.get<std::string>()) : std::nullopt;
            if (!type.has_value()) {
                ok = false;
                break;
            }
            mask.bits |= Kit_connector_types::of(type.value()).bits;
        }
        if (!ok) {
            return "flow_mask must be an array of connector types (generic, wall, wall-floor, floor, rail), not " + value.dump();
        }
        out_overrides.flow_mask = mask;
    }
    return {};
}

// The overrides of one call, cleared when it returns.
class Flow_overrides_scope
{
public:
    Flow_overrides_scope(Level_kits& level_kits, const Kit_flow_overrides& overrides)
        : m_level_kits{level_kits}
    {
        m_level_kits.set_flow_overrides(overrides);
    }
    ~Flow_overrides_scope() noexcept
    {
        m_level_kits.set_flow_overrides(std::nullopt);
    }
    Flow_overrides_scope(const Flow_overrides_scope&) = delete;
    auto operator=(const Flow_overrides_scope&) -> Flow_overrides_scope& = delete;

private:
    Level_kits& m_level_kits;
};

// One connection a piece records (doc/editor/level_tools.md "Attachments", "Connections").
[[nodiscard]] auto attachment_status_json(const Kit_attachment_status& status) -> json
{
    return json{
        {"state",            (status.state == Kit_attachment_state::active) ? "active" : "dormant"},
        {"reason",           status.reason},
        {"target_uid",       status.attachment.target},
        {"target_id",        status.target ? json(status.target->get_id()) : json(nullptr)},
        {"target_name",      status.target ? json(status.target->get_name()) : json(nullptr)},
        {"connector",        status.attachment.connector},
        {"target_connector", status.attachment.target_connector},
        {"twist_steps",      status.attachment.twist_steps},
        {"gap",              status.attachment.gap}
    };
}

// S7.2: the support a node is attached to after a call, {id, name}; null when its surface attachment is not active.
[[nodiscard]] auto attached_to_json(const Level_kits& level_kits, const erhe::scene::Node& node) -> json
{
    const Kit_surface_attachment_status status = level_kits.get_surface_attachment_status(node);
    if (!is_surface_attached(status.state) || !status.target) {
        return json(nullptr);
    }
    return json{{"id", status.target->get_id()}, {"name", status.target->get_name()}};
}

// S7.2: the piece's surface attachment (doc/editor/level_tools.md "Surface attachments"); null without a record.
[[nodiscard]] auto surface_attachment_json(const Level_kits& level_kits, const erhe::scene::Node& node) -> json
{
    const Kit_surface_attachment_status status = level_kits.get_surface_attachment_status(node);
    if (status.state == Kit_surface_attachment_state::none) {
        return json(nullptr);
    }
    const Kit_surface_attachment& record = status.attachment;
    return json{
        {"state",       c_str(status.state)},
        {"reason",      status.reason},
        {"target_uid",  record.target},
        {"target_id",   status.target ? json(status.target->get_id()) : json(nullptr)},
        {"target_name", status.target ? json(status.target->get_name()) : json(nullptr)},
        {"position",    json::array({record.position.x, record.position.y, record.position.z})},
        {"direction",   vec3_json(record.direction)},
        {"orient",      record.orient == Surface_orient::on},
        {"rest",        surface_rest_name(record.rest)}
    };
}

// Every connection the piece records, one status each (K6.0), in the record's connector order.
[[nodiscard]] auto attachments_json(const Level_kits& level_kits, const erhe::scene::Node& node) -> json
{
    std::vector<Kit_attachment_status> statuses;
    level_kits.get_attachment_statuses(node, statuses);
    json list = json::array();
    for (const Kit_attachment_status& status : statuses) {
        list.push_back(attachment_status_json(status));
    }
    return list;
}

// The first connection the piece records; null when it records none.
[[nodiscard]] auto attachment_json(const Level_kits& level_kits, const erhe::scene::Node& node) -> json
{
    const json list = attachments_json(level_kits, node);
    return list.empty() ? json(nullptr) : list.front();
}

// The keys of a level_place_pieces piece (parent_index aside).
[[nodiscard]] auto read_piece_json(
    Level_kits&              level_kits,
    Scene_root&              scene_root,
    const json&              piece,
    const Placement_options& options,
    const char*              name_key,
    Kit_placement&           placement
) -> std::string
{
    std::string error = read_piece_surface(piece, options, placement);
    if (error.empty()) { error = read_piece_values_json(level_kits, scene_root, piece, placement); }
    if (error.empty()) { error = read_piece_pose(piece, placement); }
    if (error.empty()) { error = read_piece_name(piece, name_key, placement); }
    if (error.empty()) { error = read_piece_materials(level_kits, scene_root, piece, placement); }
    if (error.empty()) { error = read_piece_parent(scene_root, piece, placement); }
    return error;
}

[[nodiscard]] auto placed_pieces_json(const Kit_place_result& result) -> json
{
    json pieces = json::array();
    for (const Kit_placed_piece& placed : result.pieces) {
        pieces.push_back({
            {"id",             placed.node->get_id()},
            {"name",           placed.node->get_name()},
            {"shape",          placed.values.get_shape()->id},
            {"parameters",     values_json(placed.values)},
            {"position",       vec3_json(placed.position)},
            {"rotation_xyzw",  quat_json(placed.rotation)},
            {"surface_found",  placed.surface_found},
            {"world_aabb",     world_aabb_json(*placed.node)},
            {"triangle_count", brush_triangle_count(*placed.brush)},
            {"materials",      piece_materials_json(*placed.node, placed.values.get_shape())},
            {"brush_id",       placed.brush->get_id()},
            {"mesh_key",       Level_kits::get_mesh_key(placed.values)},
            {"support",        placed.support ? json{{"id", placed.support->get_id()}, {"name", placed.support->get_name()}} : json()}
        });
    }
    return pieces;
}

[[nodiscard]] auto placement_reply(Level_kits& level_kits, Scene_root& scene_root, const Placement_options& options, const Kit_place_result& result) -> json
{
    const Kit_lengths lengths = level_kits.get_lengths(scene_root);
    const json        pieces  = placed_pieces_json(result);
    return json{
        {"scene_name",        scene_root.get_name()},
        {"snap",              (options.snap == Kit_snap::module) ? "module" : "none"},
        {"surface_snap",      options.surface_snap},
        {"surface_orient",    options.surface_orient},
        {"surface_direction", vec3_json(options.surface_direction)},
        {"surface_max_distance", options.surface_max_distance},
        {"surface_rest",      surface_rest_name(options.surface_rest)},
        {"attach",            options.attach},
        {"level",             {{"module", lengths.module}, {"wall_height", lengths.wall_height}}},
        {"count",             pieces.size()},
        {"pieces",            pieces}
    };
}

} // anonymous namespace

auto Mcp_server::action_level_place_pieces(const json& args) -> std::string
{
    const std::vector<std::string_view> top_keys{
        "scene_name", "snap", "surface_snap", "surface_orient", "surface_direction", "surface_max_distance", "surface_rest", "attach", "pieces", "flow_mask", "auto_connect"
    };
    const std::vector<std::string_view> piece_keys{
        "shape", "parameters", "position", "rotation_xyzw", "name", "materials", "parent_id", "parent_index", "surface_snap", "surface_orient", "connect"
    };
    {
        const std::string unrecognized = unrecognized_keys(args, top_keys);
        if (!unrecognized.empty()) {
            return make_error_content(
                "level_place_pieces: unrecognized argument(s): " + unrecognized +
                " (accepted: scene_name, snap, surface_snap, surface_orient, surface_direction, surface_max_distance, surface_rest, attach, pieces, flow_mask, auto_connect)"
            );
        }
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    Placement_options options;
    {
        const std::string error = read_placement_options(args, options);
        if (!error.empty()) {
            return make_error_content("level_place_pieces: " + error);
        }
    }
    Kit_flow_overrides overrides;
    {
        const std::string error = read_flow_overrides(args, overrides);
        if (!error.empty()) {
            return make_error_content("level_place_pieces: " + error + "; nothing was placed");
        }
    }
    const json pieces_json = args.value("pieces", json());
    if (!pieces_json.is_array() || pieces_json.empty()) {
        return make_error_content("level_place_pieces: 'pieces' must be a non-empty array");
    }

    Level_kits& level_kits = *m_context.level_kits;
    // Every piece is read and checked before any is placed (R6.5).
    std::vector<Kit_placement> placements;
    placements.reserve(pieces_json.size());
    for (std::size_t i = 0, end = pieces_json.size(); i < end; ++i) {
        const json&       piece  = pieces_json[i];
        const std::string prefix = "level_place_pieces pieces[" + std::to_string(i) + "]: ";
        const std::string tail   = "; nothing was placed";
        if (!piece.is_object()) {
            return make_error_content(prefix + "a piece must be an object" + tail);
        }
        const std::string unrecognized = unrecognized_keys(piece, piece_keys);
        if (!unrecognized.empty()) {
            return make_error_content(
                prefix + "unrecognized key(s): " + unrecognized +
                " (accepted: shape, parameters, position, rotation_xyzw, name, materials, parent_id, parent_index, surface_snap, surface_orient, connect)" + tail
            );
        }
        if (piece.contains("parent_id") && piece.contains("parent_index")) {
            return make_error_content(prefix + "give parent_id or parent_index, not both" + tail);
        }
        Kit_placement placement;
        const std::string error = read_piece_json(level_kits, *scene_root, piece, options, "name", placement);
        if (!error.empty()) {
            return make_error_content(prefix + error + tail);
        }
        if (piece.contains("connect")) {
            const json& connect = piece.at("connect");
            if (piece.contains("position") || piece.contains("rotation_xyzw") || piece.contains("surface_snap") || piece.contains("surface_orient")) {
                return make_error_content(prefix + "a connected piece takes its pose from the join: give no position, rotation_xyzw, surface_snap or surface_orient" + tail);
            }
            if (!connect.is_object()) {
                return make_error_content(prefix + "connect must be an object {connector, to_piece_id, to_connector, twist_steps, gap, attach}" + tail);
            }
            const std::string unrecognized_connect = unrecognized_keys(connect, {"connector", "to_piece_id", "to_connector", "twist_steps", "gap", "attach"});
            if (!unrecognized_connect.empty()) {
                return make_error_content(prefix + "connect: unrecognized key(s): " + unrecognized_connect + " (accepted: connector, to_piece_id, to_connector, twist_steps, gap, attach)" + tail);
            }
            Kit_connect connect_value;
            const std::string connect_error = read_connect_json(*scene_root, connect, connect_value);
            if (!connect_error.empty()) {
                return make_error_content(prefix + "connect: " + connect_error + tail);
            }
            placement.connect = std::move(connect_value);
        }
        if (piece.contains("parent_index")) {
            const json& parent_index = piece.at("parent_index");
            if (!parent_index.is_number_unsigned() || (parent_index.get<std::size_t>() >= i)) {
                return make_error_content(prefix + "parent_index " + parent_index.dump() + " must name an earlier piece of this call" + tail);
            }
            placement.parent_index = parent_index.get<std::size_t>();
        }
        placements.push_back(std::move(placement));
    }

    const std::size_t serial = get_recording_serial();
    Kit_place_result result;
    {
        const Flow_overrides_scope scope{level_kits, overrides};
        result = level_kits.place(*scene_root, placements, options.snap);
    }
    if (!result.error.empty()) {
        return make_error_content("level_place_pieces pieces[" + std::to_string(result.error_index) + "]: " + result.error + "; nothing was placed");
    }
    json reply = placement_reply(level_kits, *scene_root, options, result);
    reply["flow"] = make_flow_reply(serial);
    return make_json_content(reply).dump();
}

namespace {

[[nodiscard]] auto read_number(const json& object, const char* key, float& out_value, std::string& out_error) -> bool
{
    if (!object.contains(key)) {
        return true;
    }
    const json& value = object.at(key);
    if (!value.is_number() || !std::isfinite(value.get<float>())) {
        out_error = std::string{"'"} + key + "' must be a finite number, not " + value.dump();
        return false;
    }
    out_value = value.get<float>();
    return true;
}

} // anonymous namespace

auto Mcp_server::query_level_connectors(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "node_ids"});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_connectors: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, node_ids)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    if (args.contains("node_ids")) {
        const json& node_ids = args.at("node_ids");
        if (!node_ids.is_array() || node_ids.empty()) {
            return make_error_content("level_get_connectors: node_ids must be a non-empty array of node ids");
        }
        for (const json& id : node_ids) {
            const std::shared_ptr<erhe::scene::Node> node = id.is_number_unsigned()
                ? find_node_in_scene(*scene_root, json{{"node_id", id}}, "node_id", "node_name")
                : std::shared_ptr<erhe::scene::Node>{};
            if (!node) {
                return make_error_content("level_get_connectors: no node " + id.dump() + " in scene " + scene_root->get_name());
            }
            nodes.push_back(node);
        }
    } else {
        for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
            const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
            if (node && (node->get_item_host() == scene_root) && (carries_kit_piece(*node) || is_floor_layout(*node))) {
                nodes.push_back(node);
            }
        }
        if (nodes.empty()) {
            return make_error_content("level_get_connectors: no node_ids given and no kit piece of scene " + scene_root->get_name() + " is selected");
        }
    }
    json pieces = json::array();
    std::vector<Kit_connector> connectors;
    std::vector<Kit_connector> local_connectors;
    for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
        const std::string error = m_context.level_kits->get_world_connectors(*node, connectors, Kit_connector_set::all);
        if (!error.empty()) {
            return make_error_content("level_get_connectors: node " + std::to_string(node->get_id()) + ": " + error);
        }
        const std::optional<Kit_piece_data> record = read_kit_piece(*node);
        static_cast<void>(m_context.level_kits->get_local_connectors(*node, local_connectors, Kit_connector_set::all));
        const Kit_connector_switches switches = m_context.level_kits->get_connector_switches(*node, local_connectors);
        json connectors_json = piece_connectors_json(connectors, switches);
        json entry{
            {"id",                  node->get_id()},
            {"name",                node->get_name()},
            {"shape",               record.has_value() ? record->shape : std::string{}},
            {"connectors_enabled",  switches.connectors_enabled},
            {"disabled_connectors", parse_connector_names(switches.disabled_connectors)}
        };
        // U39: a locked path or segments layout's child: each connector's direction in its element frame and why it is hidden.
        const std::optional<Kit_layout_child> child = m_context.level_kits->get_layout_child(*node);
        if (child.has_value()) {
            for (std::size_t i = 0, end = std::min(local_connectors.size(), connectors_json.size()); i < end; ++i) {
                const Kit_layout_connector_verdict verdict = get_layout_connector_verdict(child.value(), local_connectors[i].outward);
                connectors_json[i]["layout_direction"]   = verdict.direction.has_value() ? json(c_str(verdict.direction.value())) : json(nullptr);
                connectors_json[i]["hidden_by_layout"]   = (verdict.reason != Kit_layout_hide_reason::shown);
                connectors_json[i]["layout_hide_reason"] = c_str(verdict.reason);
            }
            entry["layout"] = {
                {"id",               child->layout_id},
                {"role",             c_str(child->role)},
                {"connector_hiding", connector_hiding_json(child->hiding)}
            };
        }
        entry["connectors"] = std::move(connectors_json);
        pieces.push_back(std::move(entry));
    }
    return make_json_content({
        {"scene_name", scene_root->get_name()},
        {"count",      pieces.size()},
        {"pieces",     pieces}
    }).dump();
}

// The per-view Connectors setting of the Debug Visualization popup (doc/editor/level_tools.md "Connector indicators").
auto Mcp_server::action_level_set_connector_visualization(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"mode", "viewport"});
    if (!unrecognized.empty()) {
        return make_error_content("level_set_connector_visualization: unrecognized argument(s): " + unrecognized + " (accepted: mode, viewport)");
    }
    if (m_context.scene_views == nullptr) {
        return make_error_content("No scene views available");
    }
    const std::string mode_name = (args.contains("mode") && args.at("mode").is_string()) ? args.at("mode").get<std::string>() : std::string{};
    Visualization_mode mode{Visualization_mode::selected};
    if (!from_string(mode_name, mode) || (mode == Visualization_mode::hovered)) {
        return make_error_content("level_set_connector_visualization: 'mode' must be \"off\", \"selected\" or \"all\"");
    }
    const std::string viewport_title = args.value("viewport", std::string{});
    json viewports = json::array();
    for (const std::shared_ptr<Viewport_window>& viewport_window : m_context.scene_views->get_viewport_windows()) {
        const std::shared_ptr<Viewport_scene_view> scene_view = viewport_window->viewport_scene_view();
        if (!scene_view || (!viewport_title.empty() && (viewport_window->get_title() != viewport_title))) {
            continue;
        }
        scene_view->get_debug_visualizations().edit_settings().connectors = mode;
        viewports.push_back(json{{"viewport", viewport_window->get_title()}, {"mode", std::string{to_string(mode)}}});
    }
    if (viewports.empty()) {
        return make_error_content(
            viewport_title.empty()
                ? std::string{"No viewport scene views available"}
                : ("No viewport window titled '" + viewport_title + "' (see get_viewports)")
        );
    }
    log_mcp->info("level_set_connector_visualization: {} in {} viewport(s)", to_string(mode), viewports.size());
    return make_json_content(json{{"viewports", viewports}}).dump();
}

// What a viewport draws as connector indicators this frame, collected as connectors_visualization() collects it.
auto Mcp_server::query_level_connector_indicators(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"viewport"});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_connector_indicators: unrecognized argument(s): " + unrecognized + " (accepted: viewport)");
    }
    if ((m_context.scene_views == nullptr) || (m_context.level_kits == nullptr) || (m_context.selection == nullptr)) {
        return make_error_content("No scene views available");
    }
    const std::string viewport_title = args.value("viewport", std::string{});
    std::shared_ptr<Viewport_window>      window;
    std::shared_ptr<Viewport_scene_view> scene_view;
    for (const std::shared_ptr<Viewport_window>& viewport_window : m_context.scene_views->get_viewport_windows()) {
        const std::shared_ptr<Viewport_scene_view> view = viewport_window->viewport_scene_view();
        if (view && (viewport_title.empty() || (viewport_window->get_title() == viewport_title))) {
            window     = viewport_window;
            scene_view = view;
            break;
        }
    }
    if (!scene_view) {
        return make_error_content(
            viewport_title.empty()
                ? std::string{"No viewport scene views available"}
                : ("No viewport window titled '" + viewport_title + "' (see get_viewports)")
        );
    }
    const Visualization_mode mode = scene_view->get_debug_visualizations().get_settings().connectors;
    const std::shared_ptr<Scene_root> scene_root = scene_view->get_scene_root();
    const Kit_connector_drag_snap* const drag_snap = (m_context.transform_tool != nullptr) ? &m_context.transform_tool->get_connector_snap() : nullptr;
    const bool drag_active = scene_root && (drag_snap != nullptr) && (drag_snap->get_scene_root() == scene_root.get());
    std::vector<Kit_connector_indicator> indicators;
    const bool brush_join = scene_root && m_context.level_kits->get_connector_indicators().has_brush_join(*scene_root);
    if (scene_root && ((mode != Visualization_mode::off) || drag_active || brush_join)) {
        m_context.level_kits->get_connector_indicators().collect(*scene_root, mode, *m_context.selection, drag_snap, indicators);
    }
    const Debug_visualizations_style& style = m_context.editor_settings->debug_visualizations_style;
    json list = json::array();
    for (const Kit_connector_indicator& indicator : indicators) {
        const glm::vec4& color = get_connector_indicator_color(style, indicator.role, indicator.frame.types);
        json entry = connector_json(indicator.frame);
        entry["piece_id"] = indicator.piece_id;
        entry["role"]     = c_str(indicator.role);
        entry["color"]    = json::array({color.r, color.g, color.b, color.a});
        list.push_back(std::move(entry));
    }
    return make_json_content({
        {"viewport",    window->get_title()},
        {"scene_name",  scene_root ? scene_root->get_name() : std::string{}},
        {"mode",        std::string{to_string(mode)}},
        {"drag_active", drag_active},
        {"cached_pieces", m_context.level_kits->get_connector_indicators().get_cached_piece_count()},
        {"count",       list.size()},
        {"indicators",  list}
    }).dump();
}

// The per-view Kit Layouts setting of the Debug Visualization popup (doc/editor/level_tools.md "Kit layout drawing").
auto Mcp_server::action_level_set_layout_visualization(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"mode", "viewport"});
    if (!unrecognized.empty()) {
        return make_error_content("level_set_layout_visualization: unrecognized argument(s): " + unrecognized + " (accepted: mode, viewport)");
    }
    if (m_context.scene_views == nullptr) {
        return make_error_content("No scene views available");
    }
    const std::string mode_name = (args.contains("mode") && args.at("mode").is_string()) ? args.at("mode").get<std::string>() : std::string{};
    Visualization_mode mode{Visualization_mode::selected};
    if (!from_string(mode_name, mode) || (mode == Visualization_mode::hovered)) {
        return make_error_content("level_set_layout_visualization: 'mode' must be \"off\", \"selected\" or \"all\"");
    }
    const std::string viewport_title = args.value("viewport", std::string{});
    json viewports = json::array();
    for (const std::shared_ptr<Viewport_window>& viewport_window : m_context.scene_views->get_viewport_windows()) {
        const std::shared_ptr<Viewport_scene_view> scene_view = viewport_window->viewport_scene_view();
        if (!scene_view || (!viewport_title.empty() && (viewport_window->get_title() != viewport_title))) {
            continue;
        }
        scene_view->get_debug_visualizations().edit_settings().kit_layouts = mode;
        viewports.push_back(json{{"viewport", viewport_window->get_title()}, {"mode", std::string{to_string(mode)}}});
    }
    if (viewports.empty()) {
        return make_error_content(
            viewport_title.empty()
                ? std::string{"No viewport scene views available"}
                : ("No viewport window titled '" + viewport_title + "' (see get_viewports)")
        );
    }
    log_mcp->info("level_set_layout_visualization: {} in {} viewport(s)", to_string(mode), viewports.size());
    return make_json_content(json{{"viewports", viewports}}).dump();
}

// What a viewport draws as kit layout outlines this frame, collected and transformed as kit_layouts_visualization() does.
auto Mcp_server::query_level_layout_indicators(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"viewport"});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_layout_indicators: unrecognized argument(s): " + unrecognized + " (accepted: viewport)");
    }
    if ((m_context.scene_views == nullptr) || (m_context.level_kits == nullptr) || (m_context.selection == nullptr)) {
        return make_error_content("No scene views available");
    }
    const std::string viewport_title = args.value("viewport", std::string{});
    std::shared_ptr<Viewport_window>     window;
    std::shared_ptr<Viewport_scene_view> scene_view;
    for (const std::shared_ptr<Viewport_window>& viewport_window : m_context.scene_views->get_viewport_windows()) {
        const std::shared_ptr<Viewport_scene_view> view = viewport_window->viewport_scene_view();
        if (view && (viewport_title.empty() || (viewport_window->get_title() == viewport_title))) {
            window     = viewport_window;
            scene_view = view;
            break;
        }
    }
    if (!scene_view) {
        return make_error_content(
            viewport_title.empty()
                ? std::string{"No viewport scene views available"}
                : ("No viewport window titled '" + viewport_title + "' (see get_viewports)")
        );
    }
    const Visualization_mode          mode       = scene_view->get_debug_visualizations().get_settings().kit_layouts;
    const std::shared_ptr<Scene_root> scene_root = scene_view->get_scene_root();
    Kit_layout_indicators&            indicators = m_context.level_kits->get_layout_indicators();
    std::vector<Kit_layout_shown>     shown;
    if (scene_root) {
        indicators.collect(*scene_root, mode, *m_context.selection, shown);
    }
    json layouts = json::array();
    for (const Kit_layout_shown& entry : shown) {
        const Kit_layout_outline& outline         = *entry.outline;
        const glm::mat4           world_from_node = entry.node->world_from_node();
        const glm::mat3           basis{world_from_node};
        const auto to_world = [&world_from_node](const glm::vec3& p) -> glm::vec3 {
            return glm::vec3{world_from_node * glm::vec4{p, 1.0f}};
        };
        json polyline = json::array();
        for (const glm::vec3& p : outline.polyline) {
            polyline.push_back(vec3_json(to_world(p)));
        }
        json arcs = json::array();
        for (const Kit_layout_arc& arc : outline.arcs) {
            arcs.push_back({
                {"center",  vec3_json(to_world(arc.center))},
                {"start",   vec3_json(to_world(get_arc_point(arc, 0.0f)))},
                {"normal",  vec3_json(glm::normalize(basis * glm::vec3{0.0f, 1.0f, 0.0f}))},
                {"radius",  arc.radius},
                {"degrees", arc.degrees}
            });
        }
        json ticks = json::array();
        for (const Kit_layout_tick& tick : outline.ticks) {
            ticks.push_back({
                {"position",  vec3_json(to_world(tick.position))},
                {"direction", vec3_json(glm::normalize(basis * tick.direction))}
            });
        }
        const bool circles_shown = (entry.circles == Kit_layout_circles::shown);
        json circles = json::array();
        if (circles_shown) {
            for (const Kit_layout_spacing_circle& circle : outline.circles) {
                circles.push_back({
                    {"center",           vec3_json(to_world(circle.center))},
                    {"footprint_radius", circle.footprint_radius},
                    {"spacing_radius",   circle.spacing_radius}
                });
            }
        }
        json region_handles = json::array();
        for (std::size_t i = 0, end = outline.region_handles.size(); i < end; ++i) {
            const Kit_region_handle& handle = outline.region_handles[i];
            region_handles.push_back({
                {"index",    i},
                {"value",    c_str(handle.value)},
                {"position", vec3_json(to_world(handle.position))}
            });
        }
        layouts.push_back({
            {"id",             entry.node->get_id()},
            {"name",           entry.node->get_name()},
            {"kind",           c_str(outline.kind)},
            {"polyline",       polyline},
            {"closed",         outline.closed},
            {"arcs",           arcs},
            {"ticks",          ticks},
            {"circles_shown",  circles_shown},
            {"circles",        circles},
            {"region_handles", region_handles}
        });
    }
    return make_json_content({
        {"viewport",       window->get_title()},
        {"scene_name",     scene_root ? scene_root->get_name() : std::string{}},
        {"mode",           std::string{to_string(mode)}},
        {"cached_layouts", indicators.get_cached_layout_count()},
        {"count",          layouts.size()},
        {"layouts",        layouts}
    }).dump();
}

auto Mcp_server::action_level_connect_pieces(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(
        args, {"scene_name", "node_id", "connector", "to_piece_id", "to_connector", "twist_steps", "gap", "attach", "flow_mask", "auto_connect"}
    );
    if (!unrecognized.empty()) {
        return make_error_content(
            "level_connect_pieces: unrecognized argument(s): " + unrecognized +
            " (accepted: scene_name, node_id, connector, to_piece_id, to_connector, twist_steps, gap, attach, flow_mask, auto_connect)"
        );
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::string tail = "; nothing changed";
    if (!args.contains("node_id") || !args.at("node_id").is_number_unsigned()) {
        return make_error_content("level_connect_pieces: node_id must be the node id of the kit piece to move" + tail);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*scene_root, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("level_connect_pieces: no node " + args.at("node_id").dump() + " in scene " + scene_root->get_name() + tail);
    }
    Kit_connect connect;
    const std::string connect_error = read_connect_json(*scene_root, args, connect);
    if (!connect_error.empty()) {
        return make_error_content("level_connect_pieces: " + connect_error + tail);
    }
    Kit_flow_overrides overrides;
    const std::string overrides_error = read_flow_overrides(args, overrides);
    if (!overrides_error.empty()) {
        return make_error_content("level_connect_pieces: " + overrides_error + tail);
    }
    const std::size_t serial = get_recording_serial();
    Kit_connect_result result;
    {
        const Flow_overrides_scope scope{*m_context.level_kits, overrides};
        result = m_context.level_kits->connect_piece(*scene_root, node, connect);
    }
    if (!result.error.empty()) {
        return make_error_content("level_connect_pieces: " + result.error + tail);
    }
    std::vector<Kit_connector> connectors;
    const std::string connectors_error = m_context.level_kits->get_world_connectors(*node, connectors, Kit_connector_set::all);
    return make_json_content({
        {"scene_name",    scene_root->get_name()},
        {"id",            node->get_id()},
        {"name",          node->get_name()},
        {"position",      vec3_json(result.position)},
        {"rotation_xyzw", quat_json(result.rotation)},
        {"changed",       result.changed},
        {"world_aabb",    world_aabb_json(*node)},
        {"connectors",    connectors_error.empty() ? piece_connectors_json(connectors, m_context.level_kits->get_connector_switches(*node)) : json::array()},
        {"attachment",    attachment_json(*m_context.level_kits, *node)},
        {"not_attached",  result.not_attached},
        {"flow",          make_flow_reply(serial)}
    }).dump();
}

auto Mcp_server::action_level_detach_piece(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "node_id"});
    if (!unrecognized.empty()) {
        return make_error_content("level_detach_piece: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, node_id)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::string tail = "; nothing changed";
    if (!args.contains("node_id") || !args.at("node_id").is_number_unsigned()) {
        return make_error_content("level_detach_piece: node_id must be the node id of a kit piece" + tail);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*scene_root, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("level_detach_piece: no node " + args.at("node_id").dump() + " in scene " + scene_root->get_name() + tail);
    }
    const Kit_detach_result result = m_context.level_kits->detach_piece(*scene_root, node);
    if (!result.error.empty()) {
        return make_error_content("level_detach_piece: " + result.error + tail);
    }
    return make_json_content({
        {"scene_name", scene_root->get_name()},
        {"id",         node->get_id()},
        {"name",       node->get_name()},
        {"changed",    result.changed}
    }).dump();
}

auto Mcp_server::action_level_detach_pieces(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "node_ids"});
    if (!unrecognized.empty()) {
        return make_error_content("level_detach_pieces: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, node_ids)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::string tail = "; nothing changed";
    if (!args.contains("node_ids") || !args.at("node_ids").is_array() || args.at("node_ids").empty()) {
        return make_error_content("level_detach_pieces: node_ids must be a non-empty array of node ids" + tail);
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    for (const json& id : args.at("node_ids")) {
        const json one{{"node_id", id}};
        const std::shared_ptr<erhe::scene::Node> node = id.is_number_unsigned() ? find_node_in_scene(*scene_root, one, "node_id", "node_name") : nullptr;
        if (!node) {
            return make_error_content("level_detach_pieces: no node " + id.dump() + " in scene " + scene_root->get_name() + tail);
        }
        nodes.push_back(node);
    }
    const Kit_detach_result result = m_context.level_kits->detach_pieces(*scene_root, nodes);
    if (!result.error.empty()) {
        return make_error_content("level_detach_pieces: " + result.error + tail);
    }
    return make_json_content({{"scene_name", scene_root->get_name()}, {"changed", result.changed}}).dump();
}

auto Mcp_server::action_level_get_connections(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "node_id"});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_connections: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, node_id)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*scene_root, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("level_get_connections: node_id must name a node of scene " + scene_root->get_name());
    }
    // K6.7: the node's own records and the records of other nodes naming it, each with whether it is joined (the rule of
    // its connection kind, Level_kits::is_connection_joined()).
    json connections = json::array();
    const auto add = [&](const erhe::scene::Node& owner, const Kit_attachment& attachment, const bool recorded_here) {
        std::shared_ptr<erhe::scene::Node> target;
        scene_root->get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& n) -> bool {
            if (n->get_gltf_uid() == attachment.target) { target = n; return false; }
            return true;
        });
        const erhe::scene::Node* const other = recorded_here ? target.get() : &owner;
        json entry{
            {"connector",       recorded_here ? attachment.connector : attachment.target_connector},
            {"other_id",        (other != nullptr) ? json(other->get_id()) : json()},
            {"other_name",      (other != nullptr) ? json(other->get_name()) : json()},
            {"other_connector", recorded_here ? attachment.target_connector : attachment.connector},
            {"recorded_on",     owner.get_id()},
            {"joined",          (target != nullptr) && m_context.level_kits->is_connection_joined(owner, attachment, *target)}
        };
        connections.push_back(std::move(entry));
    };
    for (const Kit_attachment& attachment : read_kit_attachments(*node)) {
        add(*node, attachment, true);
    }
    const std::string uid = node->get_gltf_uid();
    scene_root->get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& other) -> bool {
        if ((other != node) && other->has_local_value(Kit_piece::attachment_property.get())) {
            for (const Kit_attachment& attachment : read_kit_attachments(*other)) {
                if (attachment.target == uid) {
                    add(*other, attachment, false);
                }
            }
        }
        return true;
    });
    return make_json_content({{"scene_name", scene_root->get_name()}, {"id", node->get_id()}, {"name", node->get_name()}, {"connections", connections}}).dump();
}

auto Mcp_server::action_level_get_connected_groups(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "node_id"});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_connected_groups: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, node_id)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::shared_ptr<erhe::scene::Node> node;
    if (args.contains("node_id")) {
        node = find_node_in_scene(*scene_root, args, "node_id", "node_name");
        if (!node) {
            return make_error_content("level_get_connected_groups: node_id must name a node of scene " + scene_root->get_name());
        }
    }
    std::vector<std::vector<std::shared_ptr<erhe::scene::Node>>> groups;
    m_context.level_kits->get_connected_groups(*scene_root, groups);
    json out = json::array();
    for (const std::vector<std::shared_ptr<erhe::scene::Node>>& group : groups) {
        if (node && (std::find(group.begin(), group.end(), node) == group.end())) {
            continue;
        }
        json nodes = json::array();
        for (const std::shared_ptr<erhe::scene::Node>& member : group) {
            nodes.push_back({{"id", member->get_id()}, {"name", member->get_name()}});
        }
        out.push_back({{"nodes", std::move(nodes)}});
    }
    return make_json_content({{"scene_name", scene_root->get_name()}, {"groups", std::move(out)}}).dump();
}

auto Mcp_server::action_level_remove_connection(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "node_id", "connector"});
    if (!unrecognized.empty()) {
        return make_error_content("level_remove_connection: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, node_id, connector)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*scene_root, args, "node_id", "node_name");
    if (!node || !args.contains("connector") || !args.at("connector").is_string()) {
        return make_error_content("level_remove_connection: node_id must name a node of scene " + scene_root->get_name() + " and connector a connector name; nothing changed");
    }
    const Kit_remove_connection_result result = m_context.level_kits->remove_connection(*scene_root, node, args.at("connector").get<std::string>());
    if (!result.error.empty()) {
        return make_error_content("level_remove_connection: " + result.error + "; nothing changed");
    }
    return make_json_content({{"scene_name", scene_root->get_name()}, {"changed", result.changed}}).dump();
}

auto Mcp_server::action_level_set_piece_connectors(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "node_id", "connectors_enabled", "connectors"});
    if (!unrecognized.empty()) {
        return make_error_content("level_set_piece_connectors: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, node_id, connectors_enabled, connectors)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::string tail = "; nothing was changed";
    if (!args.contains("node_id") || !args.at("node_id").is_number_unsigned()) {
        return make_error_content("level_set_piece_connectors: node_id must be a node id" + tail);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*scene_root, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("level_set_piece_connectors: no node " + args.at("node_id").dump() + " in scene " + scene_root->get_name() + tail);
    }
    Kit_connector_switch_edit edit;
    if (args.contains("connectors_enabled")) {
        if (!args.at("connectors_enabled").is_boolean()) {
            return make_error_content("level_set_piece_connectors: connectors_enabled must be true or false" + tail);
        }
        edit.piece = args.at("connectors_enabled").get<bool>() ? Kit_connector_state::enabled : Kit_connector_state::disabled;
    }
    if (args.contains("connectors")) {
        const json& connectors = args.at("connectors");
        if (!connectors.is_object()) {
            return make_error_content("level_set_piece_connectors: connectors must be an object of connector name: true or false" + tail);
        }
        for (const auto& [name, value] : connectors.items()) {
            if (!value.is_boolean()) {
                return make_error_content("level_set_piece_connectors: connectors." + name + " must be true or false" + tail);
            }
            edit.connectors.push_back(
                Kit_connector_switch_edit::Connector{
                    .name  = name,
                    .state = value.get<bool>() ? Kit_connector_state::enabled : Kit_connector_state::disabled
                }
            );
        }
    }
    if (!edit.piece.has_value() && edit.connectors.empty()) {
        return make_error_content("level_set_piece_connectors: give connectors_enabled, connectors or both" + tail);
    }
    const Kit_connector_switch_result result = m_context.level_kits->set_connector_switches(*scene_root, node, edit);
    if (!result.error.empty()) {
        return make_error_content("level_set_piece_connectors: " + result.error + tail);
    }
    std::vector<Kit_connector> world_connectors;
    const std::string connectors_error = m_context.level_kits->get_world_connectors(*node, world_connectors, Kit_connector_set::all);
    const std::optional<Kit_piece_data> record = read_kit_piece(*node);
    return make_json_content({
        {"scene_name",          scene_root->get_name()},
        {"id",                  node->get_id()},
        {"name",                node->get_name()},
        {"shape",               record.has_value() ? record->shape : std::string{}},
        {"changed",             result.changed},
        {"connectors_enabled",  result.switches.connectors_enabled},
        {"disabled_connectors", parse_connector_names(result.switches.disabled_connectors)},
        {"connectors",          connectors_error.empty() ? piece_connectors_json(world_connectors, result.switches) : json::array()}
    }).dump();
}

auto Mcp_server::query_level_pieces(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name"});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_pieces: unrecognized argument(s): " + unrecognized + " (accepted: scene_name)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const Kit_shape_library& library = m_context.level_kits->get_library();

    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    scene_root->get_scene().for_each_node(
        [&nodes](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
            if (carries_kit_piece(*node)) {
                nodes.push_back(node);
            }
            return true;
        }
    );
    std::sort(
        nodes.begin(), nodes.end(),
        [](const std::shared_ptr<erhe::scene::Node>& lhs, const std::shared_ptr<erhe::scene::Node>& rhs) -> bool {
            return lhs->get_id() < rhs->get_id();
        }
    );

    json pieces = json::array();
    for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
        const Kit_piece_data        record = read_kit_piece(*node).value();
        const Kit_shape_definition* shape  = library.find(record.shape);
        const Kit_piece_check       check  = m_context.level_kits->check_piece(*node);
        const glm::mat4             world  = node->world_from_node();
        const glm::quat             rotation = glm::quat_cast(glm::mat3{world});
        json piece{
            {"id",              node->get_id()},
            {"name",            node->get_name()},
            {"shape",           record.shape},
            {"known_shape",     shape != nullptr},
            {"parameters_text", record.parameters},
            {"position",        vec3_json(glm::vec3{world[3]})},
            {"rotation_xyzw",   json::array({rotation.x, rotation.y, rotation.z, rotation.w})},
            {"world_aabb",      world_aabb_json(*node)},
            {"materials",       piece_materials_json(*node, shape)},
            {"follows_kit",     check.follows_kit},
            {"problem",         check.problem},
            {"lights",          piece_lights_json(*node)},
            {"uid",             node->get_gltf_uid()}
        };
        piece["attachments"] = attachments_json(*m_context.level_kits, *node);
        piece["attachment"]  = piece["attachments"].empty() ? json(nullptr) : piece["attachments"].front();
        piece["surface_attachment"] = surface_attachment_json(*m_context.level_kits, *node);
        if (shape != nullptr) {
            Kit_parameter_values values{*shape};
            const std::string error = parse_canonical_text(*shape, record.parameters, values);
            if (error.empty()) {
                piece["parameters"] = values_json(values);
                piece["mesh_key"]   = Level_kits::get_mesh_key(values);
                piece["collision"]  = c_str(get_kit_collision(values));
                std::vector<Kit_light_frame> light_frames;
                get_shape_light_frames(values, light_frames);
                piece["light_frames"] = light_frames_json(light_frames);
                if (shape->id == "basic.stairs") {
                    const Kit_stairs_layout layout = get_stairs_layout(values);
                    piece["stairs"] = {
                        {"step_count",    layout.step_count},
                        {"step_rise",     layout.step_rise},
                        {"tread_depth",   layout.tread_depth},
                        {"run",           layout.run},
                        {"slope_degrees", layout.slope_degrees}
                    };
                }
            } else {
                piece["parameters_error"] = error;
            }
        }
        // The mesh shares the brush's primitive (R3.1, R3.3): equal brush ids mean one mesh.
        const std::optional<Brush_placement_data> placement = read_brush_placement(*node);
        if (placement.has_value() && placement.value().brush) {
            const std::shared_ptr<Brush>             brush = placement.value().brush;
            const std::shared_ptr<erhe::scene::Mesh> mesh  = erhe::scene::get_mesh(node.get());
            const Brush::Scaled* const               scaled = brush->get_scaled(1.0);
            piece["brush_id"]              = brush->get_id();
            bool shares = (mesh != nullptr) && (scaled != nullptr) && !mesh->get_primitives().empty() &&
                (mesh->get_primitives().size() == scaled->part_primitives.size());
            for (std::size_t i = 0, end = shares ? mesh->get_primitives().size() : 0; i < end; ++i) {
                shares = shares && (mesh->get_primitives()[i].primitive == scaled->part_primitives[i]);
            }
            piece["shares_brush_primitive"] = shares;
        }
        const std::optional<Node_physics_data> physics = read_node_physics(*node);
        const std::shared_ptr<erhe::physics::ICollision_shape> collision_shape = get_node_collision_shape(*node);
        piece["physics"] = {
            {"motion_mode",     physics.has_value() ? motion_mode_to_string(physics.value().motion_mode) : std::string{"none"}},
            {"collision_shape", collision_shape ? collision_shape->describe() : std::string{}},
            {"box",             aabb_json(get_body_box(collision_shape.get()))},
            {"has_body",        get_node_rigid_body(*node) != nullptr}
        };
        piece["pass_through"] = (node->get_flag_bits() & Editor_item_flags::pass_through) != 0;
        piece["foliage"]      = (node->get_flag_bits() & Editor_item_flags::foliage) != 0;
        piece["foliage_radius"] = node->get_value(Kit_piece::foliage_radius_property);
        pieces.push_back(std::move(piece));
    }
    return make_json_content({
        {"scene_name", scene_root->get_name()},
        {"count",      pieces.size()},
        {"pieces",     pieces}
    }).dump();
}

auto Mcp_server::action_level_set_piece_parameters(const json& args) -> std::string
{
    const std::size_t serial = get_recording_serial();
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "node_id", "parameters"});
    if (!unrecognized.empty()) {
        return make_error_content("level_set_piece_parameters: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, node_id, parameters)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::string tail = "; nothing was changed";
    if (!args.contains("node_id") || !args.at("node_id").is_number_unsigned()) {
        return make_error_content("level_set_piece_parameters: node_id must be a node id");
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*scene_root, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("level_set_piece_parameters: no node " + args.at("node_id").dump() + " in scene " + scene_root->get_name());
    }
    const std::optional<Kit_piece_data> record = read_kit_piece(*node);
    if (!record.has_value()) {
        return make_error_content("level_set_piece_parameters: node " + args.at("node_id").dump() + " '" + node->get_name() + "' is not a kit piece");
    }
    Level_kits&                       level_kits = *m_context.level_kits;
    const Kit_shape_definition* const shape      = level_kits.get_library().find(record.value().shape);
    if (shape == nullptr) {
        return make_error_content("level_set_piece_parameters: the piece's shape '" + record.value().shape + "' is not in the kits" + tail);
    }
    // The given parameters merge over the piece's current values (D4.2).
    Kit_parameter_values values{*shape};
    const std::string parse_error = parse_canonical_text(*shape, record.value().parameters, values);
    if (!parse_error.empty()) {
        return make_error_content("level_set_piece_parameters: the piece's parameters do not read: " + parse_error + tail);
    }
    const int      record_seed = values.get_seed();
    const uint64_t record_mask = values.get_derive_mask();
    const std::string apply_error = apply_parameters_json(args.value("parameters", json()), values);
    if (!apply_error.empty()) {
        return make_error_content("level_set_piece_parameters: " + apply_error + tail);
    }
    // The saved values stand until the seed or the mask changes; then the masked values are derived again.
    if ((values.get_seed() != record_seed) || (values.get_derive_mask() != record_mask)) {
        const Kit_lengths lengths = level_kits.get_lengths(*scene_root);
        values.resolve_derived(lengths.module, lengths.wall_height);
    }
    const Kit_set_parameters_result result = level_kits.set_parameters(node, values);
    if (!result.error.empty()) {
        return make_error_content("level_set_piece_parameters: " + result.error + tail);
    }
    const std::string parameters_text = values.to_canonical_text();
    return make_json_content({
        {"scene_name",     scene_root->get_name()},
        {"id",             node->get_id()},
        {"name",           node->get_name()},
        {"shape",          shape->id},
        {"changed",        parameters_text != record.value().parameters},
        {"parameters",     values_json(values)},
        {"mesh_key",       Level_kits::get_mesh_key(values)},
        {"brush_id",       result.brush->get_id()},
        {"world_aabb",     world_aabb_json(*node)},
        {"triangle_count", brush_triangle_count(*result.brush)},
        {"materials",      piece_materials_json(*node, shape)},
        {"flow",           make_flow_reply(serial)}
    }).dump();
}

auto Mcp_server::action_level_set_piece_material(const json& args) -> std::string
{
    const std::size_t serial = get_recording_serial();
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "node_id", "slot", "material"});
    if (!unrecognized.empty()) {
        return make_error_content("level_set_piece_material: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, node_id, slot, material)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::string tail = "; nothing was changed";
    if (!args.contains("node_id") || !args.at("node_id").is_number_unsigned()) {
        return make_error_content("level_set_piece_material: node_id must be a node id" + tail);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*scene_root, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("level_set_piece_material: no node " + args.at("node_id").dump() + " in scene " + scene_root->get_name() + tail);
    }
    const std::optional<Kit_piece_data> record = read_kit_piece(*node);
    if (!record.has_value()) {
        return make_error_content("level_set_piece_material: node " + args.at("node_id").dump() + " '" + node->get_name() + "' is not a kit piece" + tail);
    }
    Level_kits&                       level_kits = *m_context.level_kits;
    const Kit_shape_definition* const shape      = level_kits.get_library().find(record.value().shape);
    if (shape == nullptr) {
        return make_error_content("level_set_piece_material: the piece's shape '" + record.value().shape + "' is not in the kits" + tail);
    }
    if (!args.contains("slot") || !args.at("slot").is_string()) {
        return make_error_content("level_set_piece_material: slot must be a material slot name of " + shape->id + tail);
    }
    const std::string slot_name = args.at("slot").get<std::string>();
    const std::vector<std::string>::const_iterator slot = std::find(shape->material_slots.begin(), shape->material_slots.end(), slot_name);
    if (slot == shape->material_slots.end()) {
        std::string slots;
        for (const std::string& name : shape->material_slots) {
            slots += (slots.empty() ? "" : ", ") + name;
        }
        return make_error_content("level_set_piece_material: " + shape->id + " has no material slot '" + slot_name + "' (its slots: " + slots + ")" + tail);
    }
    if (!args.contains("material")) {
        return make_error_content("level_set_piece_material: material must be a material name" + tail);
    }
    Kit_slot_material material;
    const std::string error = read_slot_material(level_kits, *scene_root, slot_name, args.at("material"), material);
    if (!error.empty()) {
        return make_error_content("level_set_piece_material: " + error + tail);
    }
    const Kit_set_material_result result = level_kits.set_slot_material(
        *scene_root, node, static_cast<std::size_t>(slot - shape->material_slots.begin()), material
    );
    if (!result.error.empty()) {
        return make_error_content("level_set_piece_material: " + result.error + tail);
    }
    return make_json_content({
        {"scene_name",        scene_root->get_name()},
        {"id",                node->get_id()},
        {"name",              node->get_name()},
        {"shape",             shape->id},
        {"slot",              slot_name},
        {"material",          result.material ? result.material->get_name() : std::string{}},
        {"previous_material", result.previous ? result.previous->get_name() : std::string{}},
        {"changed",           result.changed},
        {"materials",         piece_materials_json(*node, shape)},
        {"flow",              make_flow_reply(serial)}
    }).dump();
}

auto Mcp_server::action_level_replace_pieces(const json& args) -> std::string
{
    const std::size_t serial = get_recording_serial();
    const std::string unrecognized = unrecognized_keys(
        args, {"scene_name", "node_ids", "shape", "parameters", "materials", "surface_snap", "surface_orient", "surface_direction", "surface_max_distance", "attach"}
    );
    if (!unrecognized.empty()) {
        return make_error_content(
            "level_replace_pieces: unrecognized argument(s): " + unrecognized +
            " (accepted: scene_name, node_ids, shape, parameters, materials, surface_snap, surface_orient, surface_direction, surface_max_distance, attach)"
        );
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::string tail = "; nothing was changed";
    Level_kits& level_kits = *m_context.level_kits;
    if (!args.contains("shape") || !args.at("shape").is_string()) {
        return make_error_content("level_replace_pieces: shape must be a shape id (level_get_kits)" + tail);
    }
    const Kit_shape_definition* const shape = level_kits.get_library().find(args.at("shape").get<std::string>());
    if (shape == nullptr) {
        return make_error_content("level_replace_pieces: unknown shape " + args.at("shape").dump() + " (level_get_kits lists the shapes)" + tail);
    }
    // The call's own surface arguments, as level_place_pieces takes them; the UI setting is not read.
    std::string surface_error{};
    const Surface_snap    surface_snap   = read_bool(args, "surface_snap",   surface_error).value_or(false) ? Surface_snap::on   : Surface_snap::off;
    const Surface_orient  surface_orient = read_bool(args, "surface_orient", surface_error).value_or(false) ? Surface_orient::on : Surface_orient::off;
    const glm::vec3       surface_direction = read_direction(args, "surface_direction", surface_error);
    const float           surface_max_distance = read_max_distance(args, "surface_max_distance", c_default_surface_max_distance, surface_error);
    Surface_options       surface = make_surface_options(surface_snap, surface_orient, surface_direction, surface_max_distance);
    surface.attach = read_bool(args, "attach", surface_error).value_or(true) ? Surface_attach::on : Surface_attach::off;
    if (!surface_error.empty()) {
        return make_error_content("level_replace_pieces: " + surface_error + tail);
    }

    // node_ids, else the nodes of the scene's selection (the ones that cannot be replaced are skipped).
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    if (args.contains("node_ids")) {
        const json& node_ids = args.at("node_ids");
        if (!node_ids.is_array() || node_ids.empty()) {
            return make_error_content("level_replace_pieces: node_ids must be a non-empty array of node ids" + tail);
        }
        for (const json& id : node_ids) {
            if (!id.is_number_unsigned()) {
                return make_error_content("level_replace_pieces: node_ids holds " + id.dump() + ", not a node id" + tail);
            }
            const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*scene_root, json{{"node_id", id}}, "node_id", "node_name");
            if (!node) {
                return make_error_content("level_replace_pieces: no node " + id.dump() + " in scene " + scene_root->get_name() + tail);
            }
            nodes.push_back(node);
        }
    } else {
        for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
            const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
            if (node && (node->get_item_host() == scene_root)) {
                nodes.push_back(node);
            }
        }
        if (nodes.empty()) {
            return make_error_content("level_replace_pieces: no node_ids given and no node of scene " + scene_root->get_name() + " is selected" + tail);
        }
    }

    Kit_placement material_reader;
    material_reader.shape = shape;
    const std::string material_error = read_piece_materials(level_kits, *scene_root, args, material_reader);
    if (!material_error.empty()) {
        return make_error_content("level_replace_pieces: " + material_error + tail);
    }
    std::vector<std::string> given;
    if (args.contains("parameters") && args.at("parameters").is_object()) {
        for (const auto& [key, value] : args.at("parameters").items()) {
            static_cast<void>(value);
            if (key != "derive") {
                given.push_back(key);
            }
        }
    }

    // The given parameters merge over each piece's carried values, as level_set_piece_parameters merges them.
    const Kit_lengths            lengths = level_kits.get_lengths(*scene_root);
    std::vector<Kit_replacement> replacements;
    for (std::size_t i = 0, end = nodes.size(); i < end; ++i) {
        Kit_replacement replacement{.node = nodes[i], .values = {}, .materials = material_reader.materials};
        // Parameters apply to the nodes replace_shape() replaces; the ones it skips are not read.
        if (args.contains("parameters") && (nodes[i]->get_item_host() == scene_root) && !level_kits.find_replace_skip(nodes[i]).has_value()) {
            Kit_parameter_values             values;
            std::vector<Kit_parameter_carry> carry;
            const std::string carry_error = level_kits.make_replace_values(*scene_root, *nodes[i], *shape, values, carry);
            if (!carry_error.empty()) {
                return make_error_content("level_replace_pieces: piece " + std::to_string(i) + " (id " + std::to_string(nodes[i]->get_id()) + "): " + carry_error + tail);
            }
            const int      carried_seed = values.get_seed();
            const uint64_t carried_mask = values.get_derive_mask();
            const std::string apply_error = apply_parameters_json(args.at("parameters"), values);
            if (!apply_error.empty()) {
                return make_error_content("level_replace_pieces: piece " + std::to_string(i) + " (id " + std::to_string(nodes[i]->get_id()) + "): " + apply_error + tail);
            }
            if ((values.get_seed() != carried_seed) || (values.get_derive_mask() != carried_mask)) {
                values.resolve_derived(lengths.module, lengths.wall_height);
            }
            replacement.values = std::move(values);
        }
        replacements.push_back(std::move(replacement));
    }

    const Kit_replace_result result = level_kits.replace_shape(*scene_root, *shape, replacements, surface);
    if (!result.error.empty()) {
        const std::string which = result.failed_index.has_value()
            ? ("piece " + std::to_string(result.failed_index.value()) + " (id " + std::to_string(nodes[result.failed_index.value()]->get_id()) + "): ")
            : std::string{};
        return make_error_content("level_replace_pieces: " + which + result.error + tail);
    }

    json pieces = json::array();
    for (const Kit_replaced_piece& piece : result.pieces) {
        json carried   = json::array();
        json clamped   = json::array();
        json defaulted = json::array();
        for (const Kit_parameter_carry& carry : piece.parameters) {
            if (std::find(given.begin(), given.end(), carry.name) != given.end()) {
                continue;
            }
            switch (carry.carry) {
                case Kit_carry::carried:   carried.push_back(carry.name); break;
                case Kit_carry::clamped:   clamped.push_back(carry.name); break;
                case Kit_carry::defaulted: defaulted.push_back(carry.name); break;
                default: break;
            }
        }
        json kept_materials = json::array();
        for (std::size_t slot = 0, end = piece.slot_matches.size(); slot < end; ++slot) {
            if (piece.slot_matches[slot].has_value()) {
                kept_materials.push_back(shape->material_slots[slot]);
            }
        }
        const std::shared_ptr<erhe::physics::ICollision_shape> collision_shape = get_node_collision_shape(*piece.mesh);
        const Kit_shape_definition* const piece_shape = level_kits.get_library().find(piece.replaced ? shape->id : piece.old_shape);
        json entry = {
            {"id",               piece.mesh->get_id()},
            {"name",             piece.mesh->get_name()},
            {"old_shape",        piece.old_shape},
            {"shape",            piece_shape->id},
            {"replaced",         piece.replaced},
            {"parameters",       values_json(piece.values)},
            {"carried",          carried},
            {"clamped",          clamped},
            {"defaulted",        defaulted},
            {"given",            piece.replaced ? json(given) : json::array()},
            {"materials",        piece_materials_json(*piece.mesh, piece_shape)},
            {"kept_materials",   kept_materials},
            {"world_aabb",       world_aabb_json(*piece.mesh)},
            {"collision_shape",  collision_shape ? collision_shape->describe() : std::string{}},
            {"snapped",          piece.snapped}
        };
        json dropped = json::array();
        for (const Kit_dropped_connection& connection : piece.dropped_connections) {
            dropped.push_back({
                {"connector",       connection.connector},
                {"other_id",        connection.other ? json(connection.other->get_id()) : json(nullptr)},
                {"other_name",      connection.other ? json(connection.other->get_name()) : json(nullptr)},
                {"other_connector", connection.other_connector},
                {"reason",          connection.reason}
            });
        }
        entry["dropped_connections"] = std::move(dropped);
        if (any_surface_option(surface) && piece.replaced) {
            entry["surface_found"] = piece.surface.surface_found;
            entry["snap_distance"] = piece.surface.distance;
        }
        pieces.push_back(std::move(entry));
    }
    json skipped = json::array();
    for (const Kit_replace_skipped& entry : result.skipped) {
        skipped.push_back({
            {"id",      entry.node->get_id()},
            {"name",    entry.node->get_name()},
            {"reason",  c_str(entry.skip)},
            {"message", entry.message}
        });
    }
    return make_json_content({
        {"scene_name",     scene_root->get_name()},
        {"shape",          shape->id},
        {"replaced_count", result.replaced_count},
        {"snapped_count",  result.snapped_count},
        {"dropped_connection_count", result.dropped_connection_count},
        {"skipped_count",  result.skipped.size()},
        {"pieces",         pieces},
        {"skipped",        skipped},
        {"flow",           make_flow_reply(serial)}
    }).dump();
}

auto Mcp_server::action_level_snap_to_surface(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "node_ids", "orient", "direction", "max_distance", "query_only", "attach"});
    if (!unrecognized.empty()) {
        return make_error_content(
            "level_snap_to_surface: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, node_ids, orient, direction, max_distance, query_only, attach)"
        );
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::string argument_error{};
    const bool      orient     = read_bool(args, "orient",     argument_error).value_or(false);
    const bool      query_only = read_bool(args, "query_only", argument_error).value_or(false);
    const bool      attach     = read_bool(args, "attach",     argument_error).value_or(true);
    const glm::vec3 direction  = read_direction(args, "direction", argument_error);
    const float     max_distance = read_max_distance(args, "max_distance", c_default_surface_max_distance, argument_error);
    if (!argument_error.empty()) {
        return make_error_content("level_snap_to_surface: " + argument_error);
    }
    const json node_ids = args.value("node_ids", json());
    if (!node_ids.is_array() || node_ids.empty()) {
        return make_error_content("level_snap_to_surface: node_ids must be a non-empty array of node ids");
    }
    std::vector<std::shared_ptr<erhe::scene::Mesh>> meshes;
    std::vector<const erhe::scene::Mesh*>           excluded;
    for (const json& id : node_ids) {
        if (!id.is_number_unsigned()) {
            return make_error_content("level_snap_to_surface: node_ids holds " + id.dump() + ", not a node id; nothing was moved");
        }
        const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*scene_root, json{{"node_id", id}}, "node_id", "node_name");
        const std::shared_ptr<erhe::scene::Mesh> mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(node);
        if (!mesh) {
            return make_error_content("level_snap_to_surface: node " + id.dump() + " is not a mesh of scene " + scene_root->get_name() + "; nothing was moved");
        }
        if (std::find(excluded.begin(), excluded.end(), mesh.get()) != excluded.end()) {
            return make_error_content("level_snap_to_surface: node " + id.dump() + " is listed twice; nothing was moved");
        }
        const std::optional<std::string> refusal = locked_layout_refusal(*mesh);
        if (refusal.has_value() && !query_only) {
            return make_error_content("level_snap_to_surface: " + refusal.value() + "; nothing was moved");
        }
        meshes.push_back(mesh);
        excluded.push_back(mesh.get());
    }

    // The moved nodes are no surfaces for each other (D7.3).
    Surface_options options = make_surface_options(Surface_snap::on, orient ? Surface_orient::on : Surface_orient::off, direction, max_distance);
    options.attach = attach ? Surface_attach::on : Surface_attach::off;
    std::vector<Kit_snap_result> snapped;
    std::string refusal;
    const std::size_t moved_count = m_context.level_kits->snap_to_surface(
        *scene_root, meshes, options, query_only ? Kit_snap_apply::query_only : Kit_snap_apply::move, snapped, &refusal
    );
    if (!refusal.empty()) {
        return make_error_content("level_snap_to_surface: refused: " + refusal + "; nothing was changed");
    }
    json results = json::array();
    for (const Kit_snap_result& result : snapped) {
        const erhe::scene::Mesh* const support = result.placement.support_mesh;
        results.push_back({
            {"id",            result.mesh->get_id()},
            {"name",          result.mesh->get_name()},
            {"position",      vec3_json(result.placement.pose.position)},
            {"rotation_xyzw", quat_json(result.placement.pose.rotation)},
            {"distance",      result.placement.distance},
            {"surface_found", result.placement.surface_found},
            {"support",       (support != nullptr) ? json{{"id", support->get_id()}, {"name", support->get_name()}} : json()},
            {"attached_to",   attached_to_json(*m_context.level_kits, *result.mesh)}
        });
    }
    return make_json_content({
        {"scene_name",  scene_root->get_name()},
        {"orient",      orient},
        {"direction",   vec3_json(direction)},
        {"max_distance", max_distance},
        {"query_only",  query_only},
        {"attach",      attach},
        {"moved_count", moved_count},
        {"nodes",       results}
    }).dump();
}

auto Mcp_server::query_level_placement_preview(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_placement_preview: unrecognized argument(s): " + unrecognized + " (accepts none)");
    }
    Brush_tool* const brush_tool = m_context.brush_tool;
    if (brush_tool == nullptr) {
        return make_error_content("Brush tool not available");
    }
    const Kit_placement_settings settings = m_context.level_kits->get_placement_settings();
    json result{
        {"active",                brush_tool->is_kit_mode()},
        {"tool_active",           (m_context.tools != nullptr) && (m_context.tools->get_priority_tool() == brush_tool)},
        {"rotation_step_degrees", settings.rotation_step_degrees},
        {"yaw_steps",             brush_tool->get_kit_yaw_steps()},
        {"surface_rest",          surface_rest_name(settings.surface.rest)}
    };
    const std::optional<glm::vec3> hover = brush_tool->get_hover_world_position();
    result["hover_position"] = hover.has_value() ? vec3_json(hover.value()) : json(nullptr);
    const std::optional<glm::vec3> hover_normal = brush_tool->get_hover_world_normal();
    result["hover_normal"]   = hover_normal.has_value() ? vec3_json(hover_normal.value()) : json(nullptr);
    const std::optional<glm::vec3> preview_position = brush_tool->get_preview_world_position();
    result["preview_position"] = preview_position.has_value() ? vec3_json(preview_position.value()) : json(nullptr);
    // Step 40: the connector join the preview shows and a click places (Kit_placement::connect).
    result["connector_snap_distance"]  = m_context.level_kits->get_connector_snap_distance();
    result["connector_join_suspended"] = brush_tool->is_kit_join_suspended();
    const std::optional<Kit_brush_join> join = brush_tool->get_kit_join();
    result["connector_join"] = join.has_value()
        ? json{
            {"target_piece_id",  join.value().target_id},
            {"target_connector", join.value().target_connector},
            {"connector",        join.value().connector},
            {"twist_steps",      join.value().twist_steps},
            {"distance",         join.value().distance}
        }
        : json(nullptr);
    // K9.2: the values the joined preview takes from its target's supply (the placement's flow gives them).
    const Kit_parameter_values* const join_values = brush_tool->get_kit_join_values();
    if (join.has_value()) {
        result["connector_join"]["flowed"] = (join_values != nullptr);
        if (join_values != nullptr) {
            result["connector_join"]["parameters"] = values_json(*join_values);
            json join_materials = json::object();
            const std::vector<std::string>& names = brush_tool->get_kit_join_slot_materials();
            const Kit_shape_definition* const join_shape = join_values->get_shape();
            for (std::size_t i = 0, end = (join_shape != nullptr) ? std::min(names.size(), join_shape->material_slots.size()) : 0; i < end; ++i) {
                join_materials[join_shape->material_slots[i]] = names[i];
            }
            result["connector_join"]["materials"] = join_materials;
        }
    }
    // The pose of the shown preview: the dragged kit item's while one is over a viewport, else kit mode's.
    const std::optional<Kit_placement_pose> pose = brush_tool->get_kit_pose();
    const bool                              kit_item_preview = brush_tool->is_kit_item_preview();
    const auto pose_json = [&pose](const Kit_shape_definition& shape, const Kit_parameter_values& values, const std::vector<std::string>& slot_materials, Brush& brush, json& out)
    {
        const erhe::math::Aabb bounds = brush.get_bounding_box();
        out["shape"]      = shape.id;
        out["parameters"] = values_json(values);
        out["mesh_key"]   = Level_kits::get_mesh_key(values);
        out["brush_id"]   = brush.get_id();
        out["bounds"]     = aabb_json(bounds);
        json materials = json::object();
        for (std::size_t i = 0, end = std::min(slot_materials.size(), shape.material_slots.size()); i < end; ++i) {
            materials[shape.material_slots[i]] = slot_materials[i];
        }
        out["materials"] = materials;
        out["visible"]   = pose.has_value();
        if (!pose.has_value()) {
            return;
        }
        const glm::vec3 position = pose.value().position;
        const glm::quat rotation = pose.value().rotation;
        erhe::math::Aabb world_bounds{};
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 local{
                ((corner & 1) != 0) ? bounds.max.x : bounds.min.x,
                ((corner & 2) != 0) ? bounds.max.y : bounds.min.y,
                ((corner & 4) != 0) ? bounds.max.z : bounds.min.z
            };
            world_bounds.include(position + (rotation * local));
        }
        out["position"]      = vec3_json(position);
        out["rotation_xyzw"] = quat_json(rotation);
        out["surface_found"] = pose.value().surface_found;
        out["world_aabb"]    = aabb_json(world_bounds);
    };
    if (kit_item_preview) {
        // The item dragged from the Level Kit window (doc/editor/level_tools.md "Dragging a kit item").
        const Kit_parameter_values& item_values = brush_tool->get_kit_item_values();
        json drag = json{{"active", true}};
        pose_json(*item_values.get_shape(), item_values, brush_tool->get_kit_item_slot_materials(), *brush_tool->get_kit_item_brush(), drag);
        result["kit_item_drag"] = drag;
    } else {
        result["kit_item_drag"] = json(nullptr);
    }
    const Kit_shape_definition* const shape = brush_tool->get_kit_shape();
    if (shape == nullptr) {
        result["visible"] = false;
        return make_json_content(result).dump();
    }
    pose_json(*shape, brush_tool->get_kit_values(), brush_tool->get_kit_slot_materials(), *brush_tool->get_kit_brush(), result);
    if (kit_item_preview) {
        // Kit mode's own preview is not shown while the drag's is.
        result["visible"] = false;
        for (const char* key : {"position", "rotation_xyzw", "surface_found", "world_aabb"}) {
            result.erase(key);
        }
    }
    return make_json_content(result).dump();
}

namespace {

// A required 3-vector argument of finite numbers; false with out_error naming it otherwise.
[[nodiscard]] auto read_vec3(const json& object, const char* key, glm::vec3& out_value, std::string& out_error) -> bool
{
    float xyz[3]{};
    if (!object.contains(key) || !read_finite_numbers(object.at(key), 3, xyz)) {
        out_error = std::string{key} + " must be an array of 3 finite numbers";
        return false;
    }
    out_value = glm::vec3{xyz[0], xyz[1], xyz[2]};
    return true;
}

[[nodiscard]] auto read_positive_number(const json& object, const char* key, const float default_value, std::string& out_error) -> float
{
    if (!object.contains(key)) {
        return default_value;
    }
    const json& value = object.at(key);
    if (!value.is_number() || !std::isfinite(value.get<float>()) || !(value.get<float>() > 0.0f)) {
        out_error = std::string{key} + " must be a positive number, not " + value.dump();
        return default_value;
    }
    return value.get<float>();
}

[[nodiscard]] auto region_json(const Level_region& region) -> json
{
    if (region.shape == Level_region_shape::sphere) {
        return json{{"sphere", {{"center", vec3_json(region.center)}, {"radius", region.radius}}}};
    }
    return json{{"box", {{"min", vec3_json(region.min)}, {"max", vec3_json(region.max)}}}};
}

[[nodiscard]] auto text_lines_json(const std::string& text) -> json
{
    json lines = json::array();
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t end = text.find('\n', begin);
        const std::size_t stop = (end == std::string::npos) ? text.size() : end;
        lines.push_back(text.substr(begin, stop - begin));
        begin = stop + 1;
    }
    return lines;
}

// The listed mesh nodes of the scene in the given order; the error names the tool and the id.
[[nodiscard]] auto read_mesh_ids(
    const json&                                      node_ids,
    Scene_root&                                      scene_root,
    const char*                                      tool,
    std::vector<std::shared_ptr<erhe::scene::Mesh>>& out_meshes
) -> std::string
{
    if (!node_ids.is_array() || node_ids.empty()) {
        return std::string{tool} + ": node_ids must be a non-empty array of node ids";
    }
    for (const json& id : node_ids) {
        if (!id.is_number_unsigned()) {
            return std::string{tool} + ": node_ids holds " + id.dump() + ", not a node id";
        }
        const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(scene_root, json{{"node_id", id}}, "node_id", "node_name");
        const std::shared_ptr<erhe::scene::Mesh> mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(node);
        if (!mesh) {
            return std::string{tool} + ": node " + id.dump() + " is not a mesh of scene " + scene_root.get_name();
        }
        if (std::find(out_meshes.begin(), out_meshes.end(), mesh) != out_meshes.end()) {
            return std::string{tool} + ": node " + id.dump() + " is listed twice";
        }
        out_meshes.push_back(mesh);
    }
    return {};
}

// exclude_tags: an array of non-empty strings; absent gives none.
[[nodiscard]] auto read_tags(const json& object, const char* key, std::vector<std::string>& out_tags) -> std::string
{
    if (!object.contains(key)) {
        return {};
    }
    const json& value = object.at(key);
    if (!value.is_array()) {
        return std::string{key} + " must be an array of tag names";
    }
    for (const json& tag : value) {
        if (!tag.is_string() || tag.get<std::string>().empty()) {
            return std::string{key} + " holds " + tag.dump() + ", not a tag name";
        }
        out_tags.push_back(tag.get<std::string>());
    }
    return {};
}

} // anonymous namespace

auto Mcp_server::query_level_foliage(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "center", "radius"});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_foliage: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, center, radius)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    if (args.contains("center") != args.contains("radius")) {
        return make_error_content("level_get_foliage: give center and radius together, or neither");
    }
    const bool sphere = args.contains("center");
    glm::vec3  center{0.0f};
    float      radius{0.0f};
    if (sphere) {
        std::string error;
        if (!read_vec3(args, "center", center, error)) {
            return make_error_content("level_get_foliage: " + error);
        }
        const json& radius_value = args.at("radius");
        if (!radius_value.is_number() || !std::isfinite(to_float_or_infinity(radius_value)) || (to_float_or_infinity(radius_value) < 0.0f)) {
            return make_error_content("level_get_foliage: radius must be a finite number >= 0, not " + radius_value.dump());
        }
        radius = to_float_or_infinity(radius_value);
    }

    // The index reads world positions: ancestors' moves are applied first.
    scene_root->get_scene().update_node_transforms();
    Foliage_system& foliage_system = scene_root->get_foliage_system();
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    if (sphere) {
        foliage_system.find_nodes_within(center, radius, nodes);
    } else {
        foliage_system.get_nodes(nodes);
    }
    std::sort(
        nodes.begin(), nodes.end(),
        [](const std::shared_ptr<erhe::scene::Node>& lhs, const std::shared_ptr<erhe::scene::Node>& rhs) -> bool {
            return lhs->get_id() < rhs->get_id();
        }
    );
    const Foliage_index& index = foliage_system.get_index();
    const std::shared_ptr<erhe::scene::Node> root = scene_root->get_scene().get_root_node();
    json items = json::array();
    for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
        const std::optional<Foliage_index::Handle> handle = foliage_system.find_handle(*node);
        if (!handle.has_value()) {
            continue;
        }
        const Foliage_index_entry&               entry  = index.get_entry(handle.value());
        const std::shared_ptr<erhe::scene::Node> parent = node->get_parent_node();
        items.push_back(
            json{
                {"id",       node->get_id()},
                {"name",     node->get_name()},
                {"shape",    carries_kit_piece(*node) ? node->get_value(Kit_piece::shape_property) : std::string{}},
                {"position", vec3_json(entry.position)},
                {"radius",   entry.radius},
                {"parent",   (parent && (parent != root)) ? json(parent->get_id()) : json(nullptr)}
            }
        );
    }
    json result{
        {"scene_name", scene_root->get_name()},
        {"count",      items.size()},
        {"items",      items},
        {"max_radius", index.get_max_radius()}
    };
    if (sphere) {
        const float area = glm::pi<float>() * radius * radius;
        result["density"] = (area > 0.0f) ? json(static_cast<float>(index.count_within(center, radius)) / area) : json(nullptr);
    } else {
        result["density"] = nullptr;
    }
    return make_json_content(result).dump();
}

namespace {

// One palette entry of level_foliage_paint (FD7.1): parameters as level_place_pieces takes them (or the canonical text),
// materials a name per slot (empty: the slot default), radius the shape's foliage radius when not given.
[[nodiscard]] auto read_foliage_palette_entry(
    const Level_kits&      level_kits,
    const Kit_lengths&     lengths,
    const json&            object,
    Foliage_palette_entry& out_entry
) -> std::string
{
    if (!object.is_object()) {
        return "an entry must be an object {shape, parameters, materials, weight, radius, yaw_min, yaw_max, variants, seed}";
    }
    const std::string unrecognized = unrecognized_keys(object, {"shape", "parameters", "materials", "weight", "radius", "yaw_min", "yaw_max", "variants", "seed"});
    if (!unrecognized.empty()) {
        return "unrecognized key(s): " + unrecognized + " (accepted: shape, parameters, materials, weight, radius, yaw_min, yaw_max, variants, seed)";
    }
    if (!object.contains("shape") || !object.at("shape").is_string()) {
        return "shape must be a shape id";
    }
    const std::string shape_id = object.at("shape").get<std::string>();
    const Kit_shape_definition* const shape = level_kits.get_library().find(shape_id);
    if (shape == nullptr) {
        return "unknown shape '" + shape_id + "' (level_get_kits lists the shapes)";
    }
    Foliage_palette_entry entry{};
    entry.shape = shape_id;
    if (object.contains("parameters")) {
        const json& parameters = object.at("parameters");
        if (parameters.is_string()) {
            entry.parameters = parameters.get<std::string>();
        } else {
            Kit_parameter_values values = editor::make_default_values(*shape, lengths.module, lengths.wall_height);
            const std::string error = apply_parameters_json(parameters, values);
            if (!error.empty()) {
                return error;
            }
            values.resolve_derived(lengths.module, lengths.wall_height);
            entry.parameters = values.to_canonical_text();
        }
    }
    if (object.contains("materials")) {
        const json& materials = object.at("materials");
        if (!materials.is_array()) {
            return "materials must be an array of material names, one per slot (an empty name for the slot default)";
        }
        for (const json& name : materials) {
            if (!name.is_string()) {
                return "materials holds " + name.dump() + ", not a material name";
            }
            entry.materials.push_back(name.get<std::string>());
        }
    }
    if (object.contains("weight")) {
        const json& weight = object.at("weight");
        if (!weight.is_number_unsigned() || (weight.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())) {
            return "weight must be an integer 0 - 1000, not " + weight.dump();
        }
        entry.weight = weight.get<std::uint32_t>();
    }
    if (object.contains("radius")) {
        const json& radius = object.at("radius");
        if (!radius.is_number() || !std::isfinite(to_float_or_infinity(radius))) {
            return "radius must be a finite number, not " + radius.dump();
        }
        entry.radius = to_float_or_infinity(radius);
    } else if (shape->foliage_radius.has_value()) {
        entry.radius = shape->foliage_radius.value();
    } else {
        return shape_id + " is no foliage shape: give the entry a radius";
    }
    if (object.contains("yaw_min") || object.contains("yaw_max")) {
        const json yaw_min = object.value("yaw_min", json(entry.yaw_min_degrees));
        const json yaw_max = object.value("yaw_max", json(entry.yaw_max_degrees));
        if (!yaw_min.is_number() || !yaw_max.is_number() || !std::isfinite(yaw_min.get<float>()) || !std::isfinite(yaw_max.get<float>())) {
            return "yaw_min and yaw_max must be finite numbers of degrees";
        }
        entry.yaw_min_degrees = yaw_min.get<float>();
        entry.yaw_max_degrees = yaw_max.get<float>();
    }
    // F3.1: 8 variants by default; a shape without derived values has one whatever the count.
    entry.variants = 8;
    for (const char* key : {"variants", "seed"}) {
        if (!object.contains(key)) {
            continue;
        }
        const json& value = object.at(key);
        if (!value.is_number_integer() ||
            (value.get<std::int64_t>() < std::numeric_limits<int>::min()) || (value.get<std::int64_t>() > std::numeric_limits<int>::max())) {
            return std::string{key} + " must be an integer, not " + value.dump();
        }
        if (std::string_view{key} == "variants") {
            entry.variants = value.get<int>();
        } else {
            entry.seed = value.get<int>();
        }
    }
    out_entry = std::move(entry);
    return {};
}

} // anonymous namespace

auto Mcp_server::action_level_foliage_paint(const json& args) -> std::string
{
    const std::string tool = "level_foliage_paint";
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "stamps", "radius", "density", "seed", "align_to_surface", "palette"});
    if (!unrecognized.empty()) {
        return make_error_content(tool + ": unrecognized argument(s): " + unrecognized + " (accepted: scene_name, stamps, radius, density, seed, align_to_surface, palette)");
    }
    if (!args.contains("scene_name") || !args.at("scene_name").is_string()) {
        return make_error_content(tool + ": scene_name must be the name of a scene (a string)");
    }
    const std::string scene_name = args.at("scene_name").get<std::string>();
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    Level_kits& level_kits = *m_context.level_kits;
    Foliage_stroke_settings settings{};
    if (args.contains("radius")) {
        if (!args.at("radius").is_number()) {
            return make_error_content(tool + ": radius must be a number of meters, not " + args.at("radius").dump());
        }
        settings.brush_radius = to_float_or_infinity(args.at("radius"));
    }
    if (args.contains("density")) {
        if (!args.at("density").is_number()) {
            return make_error_content(tool + ": density must be a number of items per square meter, not " + args.at("density").dump());
        }
        settings.paint_density = to_float_or_infinity(args.at("density"));
    }
    int seed = 1;
    if (args.contains("seed")) {
        const json& value = args.at("seed");
        if (!value.is_number_integer() || (value.get<std::int64_t>() < std::numeric_limits<int>::min()) || (value.get<std::int64_t>() > std::numeric_limits<int>::max())) {
            return make_error_content(tool + ": seed must be an integer, not " + value.dump());
        }
        seed = value.get<int>();
    }
    if (args.contains("align_to_surface")) {
        if (!args.at("align_to_surface").is_boolean()) {
            return make_error_content(tool + ": align_to_surface must be true or false");
        }
        settings.up = args.at("align_to_surface").get<bool>() ? Foliage_up::surface_normal : Foliage_up::world;
    }
    const json palette_json = args.value("palette", json());
    if (!palette_json.is_array()) {
        return make_error_content(tool + ": palette must be an array of entries");
    }
    std::vector<Foliage_palette_entry> palette;
    for (std::size_t i = 0, end = palette_json.size(); i < end; ++i) {
        Foliage_palette_entry entry;
        const std::string error = read_foliage_palette_entry(level_kits, level_kits.get_lengths(*scene_root), palette_json[i], entry);
        if (!error.empty()) {
            return make_error_content(tool + " palette[" + std::to_string(i) + "]: " + error + "; nothing was painted");
        }
        palette.push_back(std::move(entry));
    }
    const json stamps_json = args.value("stamps", json());
    if (!stamps_json.is_array() || stamps_json.empty()) {
        return make_error_content(tool + ": stamps must be a non-empty array of {center, normal}");
    }
    std::vector<std::pair<glm::vec3, glm::vec3>> stamps;
    for (std::size_t i = 0, end = stamps_json.size(); i < end; ++i) {
        const json& stamp = stamps_json[i];
        std::string error;
        glm::vec3   center{0.0f};
        glm::vec3   normal{0.0f};
        if (!stamp.is_object()) {
            error = "a stamp must be an object {center, normal}";
        } else if (const std::string unknown = unrecognized_keys(stamp, {"center", "normal"}); !unknown.empty()) {
            error = "unrecognized key(s): " + unknown + " (accepted: center, normal)";
        } else if (read_vec3(stamp, "center", center, error)) {
            static_cast<void>(read_vec3(stamp, "normal", normal, error));
        }
        if (!error.empty()) {
            return make_error_content(tool + " stamps[" + std::to_string(i) + "]: " + error + "; nothing was painted");
        }
        stamps.emplace_back(center, normal);
    }

    // One stroke of the stamps (FD9): a refused stamp cancels the stroke, so nothing is painted.
    Foliage_stroke stroke{m_context};
    const std::string refusal = stroke.begin(*scene_root, Foliage_stroke_kind::paint, settings, palette, seed);
    if (!refusal.empty()) {
        return make_error_content(tool + ": " + refusal + "; nothing was painted");
    }
    for (std::size_t i = 0, end = stamps.size(); i < end; ++i) {
        const std::string error = stroke.stamp(stamps[i].first, stamps[i].second);
        if (!error.empty()) {
            stroke.cancel();
            return make_error_content(tool + " stamps[" + std::to_string(i) + "]: " + error + "; the stroke was cancelled, nothing was painted");
        }
    }
    const std::vector<std::shared_ptr<erhe::scene::Node>> placed{stroke.get_placed_nodes().begin(), stroke.get_placed_nodes().end()};
    const Foliage_stroke_result result = stroke.end();

    scene_root->get_scene().update_node_transforms();
    const std::shared_ptr<erhe::scene::Node> root = scene_root->get_scene().get_root_node();
    json items = json::array();
    for (const std::shared_ptr<erhe::scene::Node>& node : placed) {
        const std::shared_ptr<erhe::scene::Node> parent = node->get_parent_node();
        const std::optional<Kit_piece_data>      record = read_kit_piece(*node);
        Kit_parameter_values                     values;
        const bool reads = record.has_value() && read_kit_piece_record(level_kits.get_library(), record->shape, record->parameters, values).empty();
        const erhe::scene::Trs_transform world = node->world_from_node_transform();
        items.push_back(
            json{
                {"id",            node->get_id()},
                {"name",          node->get_name()},
                {"shape",         record.has_value() ? record->shape : std::string{}},
                {"position",      vec3_json(world.get_translation())},
                {"rotation_xyzw", quat_json(world.get_rotation())},
                {"radius",        node->get_value(Kit_piece::foliage_radius_property)},
                {"parent",        (parent && (parent != root)) ? json(parent->get_id()) : json(nullptr)},
                {"attached",      node->has_local_value(Kit_piece::surface_attachment_property.get())},
                {"mesh_key",      reads ? Level_kits::get_mesh_key(values) : std::string{}}
            }
        );
    }
    log_mcp->info("{}: {} stamp(s), {} attempt(s), {} placed in scene '{}'", tool, result.stamps, result.attempts, result.placed, scene_root->get_name());
    return make_json_content(json{
        {"scene_name",         scene_root->get_name()},
        {"stamps",             result.stamps},
        {"attempts",           result.attempts},
        {"placed",             result.placed},
        {"refused_by_spacing", result.refused_by_spacing},
        {"without_surface",    result.without_surface},
        {"on_sealed_support",  result.on_sealed_support},
        {"recorded",           result.recorded},
        {"items",              items}
    }).dump();
}

auto Mcp_server::action_level_foliage_remove(const json& args) -> std::string
{
    const std::string tool = "level_foliage_remove";
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "stamps", "radius", "density", "seed", "only_shapes"});
    if (!unrecognized.empty()) {
        return make_error_content(tool + ": unrecognized argument(s): " + unrecognized + " (accepted: scene_name, stamps, radius, density, seed, only_shapes)");
    }
    if (!args.contains("scene_name") || !args.at("scene_name").is_string()) {
        return make_error_content(tool + ": scene_name must be the name of a scene (a string)");
    }
    const std::string scene_name = args.at("scene_name").get<std::string>();
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    Foliage_stroke_settings settings{};
    if (args.contains("radius")) {
        if (!args.at("radius").is_number()) {
            return make_error_content(tool + ": radius must be a number of meters, not " + args.at("radius").dump());
        }
        settings.brush_radius = to_float_or_infinity(args.at("radius"));
    }
    if (args.contains("density")) {
        if (!args.at("density").is_number()) {
            return make_error_content(tool + ": density must be a number 0 - 1, not " + args.at("density").dump());
        }
        settings.remove_density = to_float_or_infinity(args.at("density"));
    }
    int seed = 1;
    if (args.contains("seed")) {
        const json& value = args.at("seed");
        if (!value.is_number_integer() || (value.get<std::int64_t>() < std::numeric_limits<int>::min()) || (value.get<std::int64_t>() > std::numeric_limits<int>::max())) {
            return make_error_content(tool + ": seed must be an integer, not " + value.dump());
        }
        seed = value.get<int>();
    }
    // F9.3: only_shapes is the palette of Remove Only Palette Shapes; empty or absent removes every foliage item.
    std::vector<Foliage_palette_entry> palette;
    if (args.contains("only_shapes")) {
        const json& only_shapes = args.at("only_shapes");
        if (!only_shapes.is_array()) {
            return make_error_content(tool + ": only_shapes must be an array of shape ids");
        }
        for (const json& shape : only_shapes) {
            if (!shape.is_string()) {
                return make_error_content(tool + ": only_shapes holds " + shape.dump() + ", not a shape id");
            }
            if (m_context.level_kits->get_library().find(shape.get<std::string>()) == nullptr) {
                return make_error_content(tool + ": only_shapes: unknown shape '" + shape.get<std::string>() + "' (level_get_kits lists the shapes); nothing was removed");
            }
            Foliage_palette_entry entry{};
            entry.shape = shape.get<std::string>();
            palette.push_back(std::move(entry));
        }
    }
    settings.remove_filter = palette.empty() ? Foliage_remove_filter::all_shapes : Foliage_remove_filter::palette_shapes;
    const json stamps_json = args.value("stamps", json());
    if (!stamps_json.is_array() || stamps_json.empty()) {
        return make_error_content(tool + ": stamps must be a non-empty array of {center, normal?}");
    }
    std::vector<glm::vec3> centers;
    for (std::size_t i = 0, end = stamps_json.size(); i < end; ++i) {
        const json& stamp = stamps_json[i];
        std::string error;
        glm::vec3   center{0.0f};
        if (!stamp.is_object()) {
            error = "a stamp must be an object {center, normal?}";
        } else if (const std::string unknown = unrecognized_keys(stamp, {"center", "normal"}); !unknown.empty()) {
            error = "unrecognized key(s): " + unknown + " (accepted: center, normal)";
        } else if (!stamp.contains("center") || !stamp.at("center").is_array() || (stamp.at("center").size() != 3) ||
                   !stamp.at("center")[0].is_number() || !stamp.at("center")[1].is_number() || !stamp.at("center")[2].is_number()) {
            error = "center must be an array of 3 numbers";
        } else {
            // A center beyond the float range is not finite: the stroke refuses that stamp and is cancelled.
            // A normal key is accepted in the form of a paint stamp and not read.
            const json& value = stamp.at("center");
            center = glm::vec3{to_float_or_infinity(value[0]), to_float_or_infinity(value[1]), to_float_or_infinity(value[2])};
        }
        if (!error.empty()) {
            return make_error_content(tool + " stamps[" + std::to_string(i) + "]: " + error + "; nothing was removed");
        }
        centers.push_back(center);
    }

    // One stroke of the stamps (FD10): a refused stamp cancels the stroke, so nothing is removed.
    Foliage_stroke stroke{m_context};
    const std::string refusal = stroke.begin(*scene_root, Foliage_stroke_kind::remove, settings, palette, seed);
    if (!refusal.empty()) {
        return make_error_content(tool + ": " + refusal + "; nothing was removed");
    }
    for (std::size_t i = 0, end = centers.size(); i < end; ++i) {
        const std::string error = stroke.stamp(centers[i], glm::vec3{0.0f, 1.0f, 0.0f});
        if (!error.empty()) {
            stroke.cancel();
            return make_error_content(tool + " stamps[" + std::to_string(i) + "]: " + error + "; the stroke was cancelled, nothing was removed");
        }
    }
    json removed_ids = json::array();
    for (const std::shared_ptr<erhe::scene::Node>& node : stroke.get_removed_nodes()) {
        removed_ids.push_back(node->get_id());
    }
    const Foliage_stroke_result result = stroke.end();
    log_mcp->info("{}: {} stamp(s), {} removed in scene '{}'", tool, result.stamps, result.removed, scene_root->get_name());
    return make_json_content(json{
        {"scene_name",  scene_root->get_name()},
        {"stamps",      result.stamps},
        {"removed",     result.removed},
        {"recorded",    result.recorded},
        {"removed_ids", removed_ids}
    }).dump();
}

namespace {

[[nodiscard]] auto foliage_palette_entry_json(const Foliage_palette_entry& entry) -> json
{
    return json{
        {"shape",      entry.shape},
        {"parameters", entry.parameters},
        {"materials",  entry.materials},
        {"weight",     entry.weight},
        {"radius",     entry.radius},
        {"yaw_min",    entry.yaw_min_degrees},
        {"yaw_max",    entry.yaw_max_degrees},
        {"variants",   entry.variants},
        {"seed",       entry.seed}
    };
}

[[nodiscard]] auto foliage_stroke_result_json(const Foliage_stroke_result& result) -> json
{
    return json{
        {"stamps",             result.stamps},
        {"attempts",           result.attempts},
        {"placed",             result.placed},
        {"removed",            result.removed},
        {"refused_by_spacing", result.refused_by_spacing},
        {"without_surface",    result.without_surface},
        {"on_sealed_support",  result.on_sealed_support},
        {"recorded",           result.recorded}
    };
}

[[nodiscard]] auto foliage_kind_name(const Foliage_stroke_kind kind) -> const char*
{
    return (kind == Foliage_stroke_kind::paint) ? "paint" : "remove";
}

// One float argument within its range; absent leaves out_value empty.
[[nodiscard]] auto read_foliage_setting(const json& args, const char* key, const float min_value, const float max_value, std::optional<float>& out_value) -> std::string
{
    if (!args.contains(key)) {
        return {};
    }
    const json& value  = args.at(key);
    const float number = value.is_number() ? to_float_or_infinity(value) : std::numeric_limits<float>::quiet_NaN();
    if (!std::isfinite(number) || (number < min_value) || (number > max_value)) {
        return fmt::format("{} must be a number {} - {}, not {}", key, min_value, max_value, value.dump());
    }
    out_value = number;
    return {};
}

} // anonymous namespace

auto Mcp_server::query_level_foliage_paint_settings(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_foliage_paint_settings: unrecognized argument(s): " + unrecognized + " (it takes none)");
    }
    Foliage_paint_tool* const tool = m_context.foliage_paint_tool;
    if (tool == nullptr) {
        return make_error_content("level_get_foliage_paint_settings: the Foliage Paint tool is not available");
    }
    const Foliage_paint_config& config = tool->get_config();
    json palette = json::array();
    for (const Foliage_palette_entry& entry : tool->get_palette()) {
        palette.push_back(foliage_palette_entry_json(entry));
    }
    const std::string& refusal = tool->get_paint_refusal();
    return make_json_content(json{
        {"brush_radius",               config.brush_radius},
        {"paint_density",              config.paint_density},
        {"remove_density",             config.remove_density},
        {"align_to_surface",           config.align_to_surface},
        {"remove_only_palette_shapes", config.remove_only_palette_shapes},
        {"seed",                       config.seed},
        {"palette",                    palette},
        {"palette_text",               config.palette},
        {"paint_refusal",              refusal.empty() ? json(nullptr) : json(refusal)}
    }).dump();
}

auto Mcp_server::action_level_set_foliage_paint_settings(const json& args) -> std::string
{
    const std::string tool_name = "level_set_foliage_paint_settings";
    const std::string unrecognized = unrecognized_keys(
        args, {"brush_radius", "paint_density", "remove_density", "align_to_surface", "remove_only_palette_shapes", "seed", "palette"}
    );
    if (!unrecognized.empty()) {
        return make_error_content(
            tool_name + ": unrecognized argument(s): " + unrecognized +
            " (accepted: brush_radius, paint_density, remove_density, align_to_surface, remove_only_palette_shapes, seed, palette)"
        );
    }
    Foliage_paint_tool* const tool = m_context.foliage_paint_tool;
    if ((tool == nullptr) || (m_context.level_kits == nullptr)) {
        return make_error_content(tool_name + ": the Foliage Paint tool is not available");
    }
    // Every argument is read and checked first: a refusal changes nothing.
    std::optional<float> brush_radius;
    std::optional<float> paint_density;
    std::optional<float> remove_density;
    std::string error = read_foliage_setting(args, "brush_radius", c_foliage_min_brush_radius, c_foliage_max_brush_radius, brush_radius);
    if (error.empty()) {
        error = read_foliage_setting(args, "paint_density", c_foliage_min_paint_density, c_foliage_max_paint_density, paint_density);
    }
    if (error.empty()) {
        error = read_foliage_setting(args, "remove_density", 0.0f, 1.0f, remove_density);
    }
    if (!error.empty()) {
        return make_error_content(tool_name + ": " + error + "; nothing changed");
    }
    for (const char* key : {"align_to_surface", "remove_only_palette_shapes"}) {
        if (args.contains(key) && !args.at(key).is_boolean()) {
            return make_error_content(tool_name + ": " + key + " must be true or false; nothing changed");
        }
    }
    if (args.contains("seed")) {
        const json& value = args.at("seed");
        if (!value.is_number_integer() || (value.get<std::int64_t>() < 0) || (value.get<std::int64_t>() > kit_seed_max)) {
            return make_error_content(tool_name + ": seed must be an integer 0 - " + std::to_string(kit_seed_max) + ", not " + value.dump() + "; nothing changed");
        }
    }
    std::optional<std::vector<Foliage_palette_entry>> palette;
    if (args.contains("palette")) {
        const json& palette_json = args.at("palette");
        if (!palette_json.is_array()) {
            return make_error_content(tool_name + ": palette must be an array of entries; nothing changed");
        }
        const Level_kits& level_kits = *m_context.level_kits;
        std::vector<Foliage_palette_entry> entries;
        for (std::size_t i = 0, end = palette_json.size(); i < end; ++i) {
            Foliage_palette_entry entry;
            // A parameters object reads at the default level lengths; the canonical text gives exact values.
            const std::string entry_error = read_foliage_palette_entry(level_kits, Kit_lengths{}, palette_json[i], entry);
            if (!entry_error.empty()) {
                return make_error_content(tool_name + " palette[" + std::to_string(i) + "]: " + entry_error + "; nothing changed");
            }
            entries.push_back(std::move(entry));
        }
        const std::string problem = check_foliage_palette(level_kits.get_library(), entries);
        if (!problem.empty()) {
            return make_error_content(tool_name + ": palette " + problem + "; nothing changed");
        }
        palette = std::move(entries);
    }

    if (brush_radius.has_value()) {
        tool->set_brush_radius(brush_radius.value());
    }
    if (paint_density.has_value()) {
        tool->set_paint_density(paint_density.value());
    }
    if (remove_density.has_value()) {
        tool->set_remove_density(remove_density.value());
    }
    if (args.contains("align_to_surface")) {
        tool->set_up_axis(args.at("align_to_surface").get<bool>() ? Foliage_up::surface_normal : Foliage_up::world);
    }
    if (args.contains("remove_only_palette_shapes")) {
        tool->set_remove_filter(args.at("remove_only_palette_shapes").get<bool>() ? Foliage_remove_filter::palette_shapes : Foliage_remove_filter::all_shapes);
    }
    if (args.contains("seed")) {
        tool->set_seed(args.at("seed").get<int>());
    }
    if (palette.has_value()) {
        tool->set_palette(palette.value());
    }
    return query_level_foliage_paint_settings(json::object());
}

auto Mcp_server::query_level_foliage_brush(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"include_stamp_centers"});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_foliage_brush: unrecognized argument(s): " + unrecognized + " (accepted: include_stamp_centers)");
    }
    bool include_stamp_centers = false;
    if (args.contains("include_stamp_centers")) {
        if (!args.at("include_stamp_centers").is_boolean()) {
            return make_error_content("level_get_foliage_brush: include_stamp_centers must be a boolean");
        }
        include_stamp_centers = args.at("include_stamp_centers").get<bool>();
    }
    Foliage_paint_tool* const tool = m_context.foliage_paint_tool;
    if (tool == nullptr) {
        return make_error_content("level_get_foliage_brush: the Foliage Paint tool is not available");
    }
    const std::optional<Foliage_brush>& brush        = tool->get_brush();
    const std::shared_ptr<Scene_root>   brush_scene  = brush.has_value() ? brush->scene_root.lock() : std::shared_ptr<Scene_root>{};
    const bool                          stroke_open  = tool->is_stroke_open();
    const std::shared_ptr<Scene_root>   stroke_scene = tool->get_stroke_scene();
    const Foliage_paint_last_stroke&    last         = tool->get_last_stroke();
    json last_json = nullptr;
    if (last.outcome != Foliage_stroke_outcome::none) {
        last_json = foliage_stroke_result_json(last.result);
        last_json["kind"]       = foliage_kind_name(last.kind);
        last_json["outcome"]    = c_str(last.outcome);
        last_json["scene_name"] = last.scene_name;
        last_json["seed"]       = last.seed;
        last_json["refusal"]    = last.refusal.empty() ? json(nullptr) : json(last.refusal);
    }
    json stroke_json = nullptr;
    if (stroke_open) {
        stroke_json = foliage_stroke_result_json(tool->get_stroke_result());
        stroke_json["kind"]       = foliage_kind_name(tool->get_stroke_kind());
        stroke_json["scene_name"] = stroke_scene ? json(stroke_scene->get_name()) : json(nullptr);
    }
    const Tool* const priority_tool = (m_context.tools != nullptr) ? m_context.tools->get_priority_tool() : nullptr;
    json reply{
        {"tool_active", priority_tool == tool},
        {"has_brush",   brush_scene != nullptr},
        {"scene_name",  brush_scene ? json(brush_scene->get_name()) : json(nullptr)},
        {"center",      brush_scene ? vec3_json(brush->center) : json(nullptr)},
        {"normal",      brush_scene ? vec3_json(brush->normal) : json(nullptr)},
        {"radius",      tool->get_config().brush_radius},
        {"seed",        tool->get_config().seed},
        {"stroke_open", stroke_open},
        {"stroke",      stroke_json},
        {"last_stroke", last_json}
    };
    if (include_stamp_centers) {
        json stamp_centers = json::array();
        for (const glm::vec3& center : tool->get_stamp_centers()) {
            stamp_centers.push_back(vec3_json(center));
        }
        reply["stamp_centers"]           = std::move(stamp_centers);
        reply["stamp_centers_truncated"] = tool->is_stamp_centers_truncated();
    }
    return make_json_content(reply).dump();
}

auto Mcp_server::query_level_region_contents(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "box", "sphere"});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_region_contents: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, box, sphere)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    if (args.contains("box") == args.contains("sphere")) {
        return make_error_content("level_get_region_contents: give box {min, max} or sphere {center, radius}, one of them");
    }
    Level_region region{};
    std::string  error{};
    if (args.contains("box")) {
        const json& box = args.at("box");
        if (!box.is_object() || !unrecognized_keys(box, {"min", "max"}).empty()) {
            return make_error_content("level_get_region_contents: box must be an object {min, max}");
        }
        region.shape = Level_region_shape::box;
        if (!read_vec3(box, "min", region.min, error) || !read_vec3(box, "max", region.max, error)) {
            return make_error_content("level_get_region_contents: box " + error);
        }
        if ((region.min.x > region.max.x) || (region.min.y > region.max.y) || (region.min.z > region.max.z)) {
            return make_error_content("level_get_region_contents: box min " + vec3_json(region.min).dump() + " exceeds max " + vec3_json(region.max).dump());
        }
    } else {
        const json& sphere = args.at("sphere");
        if (!sphere.is_object() || !unrecognized_keys(sphere, {"center", "radius"}).empty()) {
            return make_error_content("level_get_region_contents: sphere must be an object {center, radius}");
        }
        region.shape = Level_region_shape::sphere;
        if (!read_vec3(sphere, "center", region.center, error)) {
            return make_error_content("level_get_region_contents: sphere " + error);
        }
        if (!sphere.contains("radius")) {
            return make_error_content("level_get_region_contents: sphere radius must be a positive number");
        }
        region.radius = read_positive_number(sphere, "radius", 0.0f, error);
        if (!error.empty()) {
            return make_error_content("level_get_region_contents: sphere " + error);
        }
    }

    std::vector<std::shared_ptr<erhe::scene::Mesh>> meshes;
    find_region_contents(*scene_root, region, meshes);
    json nodes = json::array();
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
        const erhe::scene::Trs_transform    world  = mesh->world_from_node_transform();
        const std::optional<Kit_piece_data> record = read_kit_piece(*mesh);
        nodes.push_back({
            {"id",            mesh->get_id()},
            {"name",          mesh->get_name()},
            {"kit_piece",     record.has_value()},
            {"shape",         record.has_value() ? json(record.value().shape) : json(nullptr)},
            {"position",      vec3_json(world.get_translation())},
            {"rotation_xyzw", quat_json(world.get_rotation())},
            {"scale",         vec3_json(world.get_scale())},
            {"world_aabb",    aabb_json(mesh->get_aabb_world())}
        });
    }
    json reply = region_json(region);
    reply["scene_name"] = scene_root->get_name();
    reply["count"]      = nodes.size();
    reply["nodes"]      = nodes;
    return make_json_content(reply).dump();
}

auto Mcp_server::query_level_height_map(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "min", "max", "cell_size", "text"});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_height_map: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, min, max, cell_size, text)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::string error{};
    glm::vec3   min{0.0f};
    glm::vec3   max{0.0f};
    if (!read_vec3(args, "min", min, error) || !read_vec3(args, "max", max, error)) {
        return make_error_content("level_get_height_map: " + error);
    }
    if (!(min.x < max.x) || !(min.z < max.z) || !(min.y <= max.y)) {
        return make_error_content(
            "level_get_height_map: min " + vec3_json(min).dump() + " must be below max " + vec3_json(max).dump() + " in x and z and not above it in y"
        );
    }
    if (!args.contains("cell_size")) {
        return make_error_content("level_get_height_map: cell_size must be a positive number");
    }
    const float cell_size = read_positive_number(args, "cell_size", 1.0f, error);
    const bool  text      = read_bool(args, "text", error).value_or(false);
    if (!error.empty()) {
        return make_error_content("level_get_height_map: " + error);
    }
    const Height_map_grid grid = make_height_map_grid(min, max, cell_size);
    if ((grid.columns * grid.rows) > c_max_height_map_cells) {
        return make_error_content(
            "level_get_height_map: " + std::to_string(grid.columns) + " x " + std::to_string(grid.rows) + " cells exceed the limit of " +
            std::to_string(c_max_height_map_cells) + "; use a larger cell_size or a smaller region"
        );
    }
    Height_map height_map;
    make_height_map(*scene_root, grid, height_map);

    json heights  = json::array();
    json walkable = json::array();
    json islands  = json::array();
    std::size_t hit_count      = 0;
    std::size_t walkable_count = 0;
    for (std::size_t row = 0; row < grid.rows; ++row) {
        json height_row   = json::array();
        json walkable_row = json::array();
        json island_row   = json::array();
        for (std::size_t column = 0; column < grid.columns; ++column) {
            const Height_map_cell& cell = height_map.cells[(row * grid.columns) + column];
            height_row.push_back(cell.hit ? json(cell.height) : json(nullptr));
            walkable_row.push_back(cell.walkable);
            island_row.push_back((cell.island >= 0) ? json(cell.island) : json(nullptr));
            hit_count      += cell.hit ? 1 : 0;
            walkable_count += cell.walkable ? 1 : 0;
        }
        heights.push_back(std::move(height_row));
        walkable.push_back(std::move(walkable_row));
        islands.push_back(std::move(island_row));
    }
    const Height_range range = get_height_range(height_map.cells);
    json reply{
        {"scene_name",           scene_root->get_name()},
        {"min",                  vec3_json(min)},
        {"max",                  vec3_json(max)},
        {"cell_size",            cell_size},
        {"columns",              grid.columns},
        {"rows",                 grid.rows},
        {"first_cell_center",    json::array({get_cell_center(grid, 0, 0).x, get_cell_center(grid, 0, 0).y})},
        {"hit_count",            hit_count},
        {"height_min",           range.valid ? json(range.min) : json(nullptr)},
        {"height_max",           range.valid ? json(range.max) : json(nullptr)},
        {"heights",              heights},
        {"navigation_available", height_map.navigation_available},
        {"text",                 text}
    };
    if (height_map.navigation_available) {
        reply["navigation_stale"] = height_map.navigation_stale;
        reply["walkable_count"]   = walkable_count;
        reply["walkable"]         = walkable;
        reply["islands"]          = islands;
    }
    if (text) {
        std::string picture;
        write_height_text(grid, height_map.cells, picture);
        reply["height_text"] = text_lines_json(picture);
        if (height_map.navigation_available) {
            write_island_text(grid, height_map.cells, picture);
            reply["island_text"] = text_lines_json(picture);
        }
    }
    return make_json_content(reply).dump();
}

auto Mcp_server::query_level_support_report(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "node_ids", "tolerance"});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_support_report: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, node_ids, tolerance)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::string error{};
    const float tolerance = read_positive_number(args, "tolerance", c_default_support_tolerance, error);
    if (!error.empty()) {
        return make_error_content("level_get_support_report: " + error);
    }
    std::vector<std::shared_ptr<erhe::scene::Mesh>> meshes;
    if (args.contains("node_ids")) {
        const std::string ids_error = read_mesh_ids(args.at("node_ids"), *scene_root, "level_get_support_report", meshes);
        if (!ids_error.empty()) {
            return make_error_content(ids_error);
        }
    } else {
        get_kit_piece_meshes(*scene_root, meshes);
    }
    std::vector<Support_result> results;
    find_support(*scene_root, meshes, tolerance, results);
    json nodes       = json::array();
    json unsupported = json::array();
    for (const Support_result& result : results) {
        nodes.push_back({
            {"id",            result.mesh->get_id()},
            {"name",          result.mesh->get_name()},
            {"supported",     result.supported},
            {"surface_found", result.surface_found},
            {"distance",      result.surface_found ? json(result.distance) : json(nullptr)}
        });
        if (!result.supported) {
            unsupported.push_back(result.mesh->get_id());
        }
    }
    return make_json_content({
        {"scene_name",        scene_root->get_name()},
        {"tolerance",         tolerance},
        {"all_kit_pieces",    !args.contains("node_ids")},
        {"count",             nodes.size()},
        {"unsupported_count", unsupported.size()},
        {"unsupported_ids",   unsupported},
        {"nodes",             nodes}
    }).dump();
}

auto Mcp_server::query_level_overlap_report(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "node_ids", "tolerance", "exclude_tags"});
    if (!unrecognized.empty()) {
        return make_error_content(
            "level_get_overlap_report: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, node_ids, tolerance, exclude_tags)"
        );
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::string error{};
    const float tolerance = read_positive_number(args, "tolerance", c_default_overlap_tolerance, error);
    if (!error.empty()) {
        return make_error_content("level_get_overlap_report: " + error);
    }
    std::vector<std::string> exclude_tags;
    error = read_tags(args, "exclude_tags", exclude_tags);
    if (!error.empty()) {
        return make_error_content("level_get_overlap_report: " + error);
    }
    std::vector<std::shared_ptr<erhe::scene::Mesh>> meshes;
    if (args.contains("node_ids")) {
        error = read_mesh_ids(args.at("node_ids"), *scene_root, "level_get_overlap_report", meshes);
        if (!error.empty()) {
            return make_error_content(error);
        }
    } else {
        get_kit_piece_meshes(*scene_root, meshes);
    }
    Overlap_report report;
    find_overlaps(meshes, tolerance, exclude_tags, report);
    json excluded = json::array();
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
        if (has_any_tag(mesh->get_tags(), exclude_tags)) {
            excluded.push_back(mesh->get_id());
        }
    }
    json overlaps = json::array();
    for (const Overlap_result& overlap : report.overlaps) {
        overlaps.push_back({
            {"ids",      json::array({overlap.mesh_a->get_id(), overlap.mesh_b->get_id()})},
            {"names",    json::array({overlap.mesh_a->get_name(), overlap.mesh_b->get_name()})},
            {"depth",    overlap.depth},
            {"measure",  c_str(overlap.measure)},
            {"position", vec3_json(overlap.position)}
        });
    }
    return make_json_content({
        {"scene_name",        scene_root->get_name()},
        {"tolerance",         tolerance},
        {"exclude_tags",      exclude_tags},
        {"all_kit_pieces",    !args.contains("node_ids")},
        {"count",             meshes.size()},
        {"tested_count",      report.tested_count},
        {"excluded_ids",      excluded},
        {"bounds_pair_count", report.bounds_pair_count},
        {"overlap_count",     overlaps.size()},
        {"overlaps",          overlaps}
    }).dump();
}

auto Mcp_server::query_level_check(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "support_tolerance", "overlap_tolerance", "min_island_area", "exclude_tags"});
    if (!unrecognized.empty()) {
        return make_error_content(
            "level_check: unrecognized argument(s): " + unrecognized +
            " (accepted: scene_name, support_tolerance, overlap_tolerance, min_island_area, exclude_tags)"
        );
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::string error{};
    Level_check_options options{};
    options.support_tolerance = read_positive_number(args, "support_tolerance", c_default_support_tolerance, error);
    options.overlap_tolerance = read_positive_number(args, "overlap_tolerance", c_default_overlap_tolerance, error);
    if (args.contains("min_island_area")) {
        const json& value = args.at("min_island_area");
        if (!value.is_number() || !std::isfinite(value.get<float>()) || (value.get<float>() < 0.0f)) {
            error = "min_island_area must be a number of square meters, 0 or more, not " + value.dump();
        } else {
            options.min_island_area = value.get<float>();
        }
    }
    if (!error.empty()) {
        return make_error_content("level_check: " + error);
    }
    std::vector<std::string> exclude_tags;
    error = read_tags(args, "exclude_tags", exclude_tags);
    if (!error.empty()) {
        return make_error_content("level_check: " + error);
    }
    options.exclude_tags = exclude_tags;

    Level_check check;
    run_level_check(*scene_root, m_context.level_kits->get_library(), options, check);
    json findings = json::array();
    json counts   = json::object();
    for (const Level_finding_kind kind : {
        Level_finding_kind::unsupported, Level_finding_kind::overlap, Level_finding_kind::unreachable_island,
        Level_finding_kind::unwalkable_parameters, Level_finding_kind::unreadable_piece
    }) {
        counts[c_str(kind)] = 0;
    }
    for (const Level_finding& finding : check.findings) {
        json ids   = json::array();
        json names = json::array();
        for (std::size_t i = 0; i < finding.mesh_count; ++i) {
            ids.push_back(finding.meshes[i]->get_id());
            names.push_back(finding.meshes[i]->get_name());
        }
        json entry{
            {"kind",     c_str(finding.kind)},
            {"position", vec3_json(finding.position)},
            {"ids",      ids},
            {"names",    names},
            {"detail",   finding.detail}
        };
        switch (finding.kind) {
            case Level_finding_kind::unsupported: {
                entry["distance"] = finding.has_value ? json(finding.value) : json(nullptr);
                break;
            }
            case Level_finding_kind::overlap: {
                entry["depth"]   = finding.value;
                entry["measure"] = c_str(finding.measure);
                break;
            }
            case Level_finding_kind::unreachable_island: {
                entry["area"]   = finding.value;
                entry["island"] = finding.island;
                break;
            }
            default: {
                break;
            }
        }
        counts[c_str(finding.kind)] = counts[c_str(finding.kind)].get<int>() + 1;
        findings.push_back(std::move(entry));
    }
    return make_json_content({
        {"scene_name",           scene_root->get_name()},
        {"support_tolerance",    options.support_tolerance},
        {"overlap_tolerance",    options.overlap_tolerance},
        {"min_island_area",      options.min_island_area},
        {"exclude_tags",         exclude_tags},
        {"piece_count",          check.piece_count},
        {"excluded_count",       check.excluded_count},
        {"agent",                {
            {"height",            check.agent.height},
            {"radius",            check.agent.radius},
            {"max_climb",         check.agent.max_climb},
            {"max_slope_degrees", check.agent.max_slope_degrees}
        }},
        {"navigation_available", check.navigation_available},
        {"navigation_stale",     check.navigation_stale},
        {"island_count",         check.island_count},
        {"finding_count",        findings.size()},
        {"counts",               counts},
        {"findings",             findings}
    }).dump();
}

namespace {

[[nodiscard]] auto surface_settings_json(const Level_kits& level_kits) -> json
{
    const Surface_options options        = level_kits.get_surface_settings();
    const glm::vec3       drag_direction = level_kits.get_surface_drag_direction();
    return json{
        {"snap",                    level_kits.get_surface_snap() == Surface_snap::on},
        {"orient",                  options.orient == Surface_orient::on},
        {"attach",                  options.attach == Surface_attach::on},
        {"max_distance",            options.forward_distance}, // the settings search max_distance either way
        {"rest",                    surface_rest_name(level_kits.get_surface_rest())},
        {"drag_direction_mode",     std::string{to_string(level_kits.get_surface_drag_direction_mode())}},
        {"drag_direction",          {drag_direction.x, drag_direction.y, drag_direction.z}},
        {"connector_snap_distance", level_kits.get_connector_snap_distance()},
        {"connector_snap_axis_mode", std::string{to_string(level_kits.get_connector_snap_axis_mode())}},
        {"connector_snap_orientation", std::string{to_string(level_kits.get_connector_snap_orientation())}},
        {"auto_connect",               level_kits.get_connector_auto_connect()},
        {"flow_mask",                  connector_types_json(level_kits.get_connector_flow_mask())},
        {"connection_step_limit",      level_kits.get_connection_step_limit()}
    };
}

} // anonymous namespace

auto Mcp_server::query_surface_snap_settings(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {});
    if (!unrecognized.empty()) {
        return make_error_content("get_surface_snap_settings: unrecognized argument(s): " + unrecognized + " (accepts none)");
    }
    return make_json_content(surface_settings_json(*m_context.level_kits)).dump();
}

auto Mcp_server::action_set_surface_snap_settings(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(
        args, {"snap", "orient", "attach", "max_distance", "rest", "drag_direction_mode", "drag_direction", "connector_snap_distance", "connector_snap_axis_mode", "connector_snap_orientation",
               "auto_connect", "flow_mask", "connection_step_limit"}
    );
    if (!unrecognized.empty()) {
        return make_error_content(
            "set_surface_snap_settings: unrecognized argument(s): " + unrecognized +
            " (accepted: snap, orient, attach, max_distance, rest, drag_direction_mode, drag_direction, connector_snap_distance, connector_snap_axis_mode, connector_snap_orientation, auto_connect, flow_mask, connection_step_limit)"
        );
    }
    // C3.8, C3.9, C5.5: the connection settings.
    if (args.contains("auto_connect") && !args.at("auto_connect").is_boolean()) {
        return make_error_content("set_surface_snap_settings: auto_connect must be true or false");
    }
    Kit_connector_types flow_mask{.bits = 0};
    if (args.contains("flow_mask")) {
        const json& value = args.at("flow_mask");
        bool ok = value.is_array();
        for (const json& name : ok ? value : json::array()) {
            const std::optional<Kit_connector_type> type = name.is_string() ? parse_connector_type(name.get<std::string>()) : std::nullopt;
            if (!type.has_value()) {
                ok = false;
                break;
            }
            flow_mask.bits |= Kit_connector_types::of(type.value()).bits;
        }
        if (!ok) {
            return make_error_content("set_surface_snap_settings: flow_mask must be an array of connector types (generic, wall, wall-floor, floor, rail), not " + value.dump());
        }
    }
    if (args.contains("connection_step_limit") && (!args.at("connection_step_limit").is_number_integer() || (args.at("connection_step_limit").get<int>() < 1))) {
        return make_error_content("set_surface_snap_settings: connection_step_limit must be an integer of 1 or more");
    }
    Surface_snap_rest rest{Surface_snap_rest::leading_face};
    if (args.contains("rest")) {
        const std::string error = read_surface_rest(args.at("rest"), rest);
        if (!error.empty()) {
            return make_error_content("set_surface_snap_settings: rest " + error);
        }
    }
    Surface_drag_direction drag_direction_mode{Surface_drag_direction::explicit_vector};
    if (args.contains("drag_direction_mode")) {
        const json& value = args.at("drag_direction_mode");
        if (!value.is_string() || !from_string(value.get<std::string>(), drag_direction_mode)) {
            return make_error_content("set_surface_snap_settings: drag_direction_mode must be \"explicit_vector\" or \"camera_ray\", not " + value.dump());
        }
    }
    // Stored as given, as the widget stores it; normalized on use.
    glm::vec3 drag_direction{c_default_surface_direction};
    if (args.contains("drag_direction")) {
        float xyz[3]{};
        if (!read_finite_numbers(args.at("drag_direction"), 3, xyz)) {
            return make_error_content("set_surface_snap_settings: drag_direction must be an array of 3 finite numbers");
        }
        drag_direction = glm::vec3{xyz[0], xyz[1], xyz[2]};
        const std::string problem = check_surface_direction(drag_direction);
        if (!problem.empty()) {
            return make_error_content("set_surface_snap_settings: drag_direction: " + problem);
        }
    }
    Connector_snap_axis_mode axis_mode{Connector_snap_axis_mode::full_offset};
    if (args.contains("connector_snap_axis_mode")) {
        const json& value = args.at("connector_snap_axis_mode");
        if (!value.is_string() || !from_string(value.get<std::string>(), axis_mode)) {
            return make_error_content("set_surface_snap_settings: connector_snap_axis_mode must be \"full_offset\" or \"stay_on_axis\", not " + value.dump());
        }
    }
    // U50: off, rotate or full; the former names keep and align read as off and full.
    Connector_snap_orientation orientation{Connector_snap_orientation::off};
    if (args.contains("connector_snap_orientation")) {
        const json& value = args.at("connector_snap_orientation");
        if (!value.is_string() || !from_string(value.get<std::string>(), orientation)) {
            return make_error_content("set_surface_snap_settings: connector_snap_orientation must be \"off\", \"rotate\" or \"full\", not " + value.dump());
        }
    }
    std::string max_distance_error;
    const float max_distance = read_max_distance(args, "max_distance", 0.0f, max_distance_error);
    if (!max_distance_error.empty()) {
        return make_error_content("set_surface_snap_settings: " + max_distance_error);
    }
    for (const char* key : {"snap", "orient", "attach"}) {
        if (args.contains(key) && !args.at(key).is_boolean()) {
            return make_error_content(std::string{"set_surface_snap_settings: "} + key + " must be a boolean, not " + args.at(key).dump());
        }
    }
    if (args.contains("connector_snap_distance")) {
        const json& value = args.at("connector_snap_distance");
        if (!value.is_number() || !std::isfinite(value.get<double>()) || (value.get<double>() < 0.0)) {
            return make_error_content("set_surface_snap_settings: connector_snap_distance must be a number >= 0, not " + value.dump());
        }
    }
    Level_kits& level_kits = *m_context.level_kits;
    if (args.contains("attach")) {
        level_kits.set_surface_attach(args.at("attach").get<bool>() ? Surface_attach::on : Surface_attach::off);
    }
    if (args.contains("snap")) {
        level_kits.set_surface_snap(args.at("snap").get<bool>() ? Surface_snap::on : Surface_snap::off);
    }
    if (args.contains("orient")) {
        level_kits.set_surface_orient(args.at("orient").get<bool>() ? Surface_orient::on : Surface_orient::off);
    }
    if (args.contains("max_distance")) {
        level_kits.set_surface_max_distance(max_distance);
    }
    if (args.contains("rest")) {
        level_kits.set_surface_rest(rest);
    }
    if (args.contains("drag_direction_mode")) {
        level_kits.set_surface_drag_direction_mode(drag_direction_mode);
    }
    if (args.contains("drag_direction")) {
        level_kits.set_surface_drag_direction(drag_direction);
    }
    if (args.contains("connector_snap_distance")) {
        level_kits.set_connector_snap_distance(args.at("connector_snap_distance").get<float>());
    }
    if (args.contains("connector_snap_axis_mode")) {
        level_kits.set_connector_snap_axis_mode(axis_mode);
    }
    if (args.contains("connector_snap_orientation")) {
        level_kits.set_connector_snap_orientation(orientation);
    }
    if (args.contains("auto_connect")) {
        level_kits.set_connector_auto_connect(args.at("auto_connect").get<bool>() ? Connector_auto_connect::on : Connector_auto_connect::off);
    }
    if (args.contains("flow_mask")) {
        level_kits.set_connector_flow_mask(flow_mask);
    }
    if (args.contains("connection_step_limit")) {
        level_kits.set_connection_step_limit(static_cast<std::size_t>(args.at("connection_step_limit").get<int>()));
    }
    return make_json_content(surface_settings_json(level_kits)).dump();
}

namespace {

// A node of the scene by id under the key; the error names the tool and the id.
[[nodiscard]] auto read_scene_node(
    Scene_root&                         scene_root,
    const json&                         args,
    const char*                         key,
    std::shared_ptr<erhe::scene::Node>& out_node
) -> std::string
{
    if (!args.contains(key) || !args.at(key).is_number_unsigned()) {
        return std::string{key} + " must be a node id";
    }
    const std::shared_ptr<erhe::scene::Node> root = scene_root.get_scene().get_root_node();
    if (root && (root->get_id() == args.at(key).get<std::size_t>())) {
        return "node " + args.at(key).dump() + " is the scene root";
    }
    out_node = find_node_in_scene(scene_root, json{{"node_id", args.at(key)}}, "node_id", "node_name");
    if (!out_node) {
        return "no node " + args.at(key).dump() + " in scene " + scene_root.get_name();
    }
    return {};
}

[[nodiscard]] auto read_optional_name(const json& args, std::string& out_name) -> std::string
{
    if (!args.contains("name")) {
        return {};
    }
    if (!args.at("name").is_string() || args.at("name").get<std::string>().empty()) {
        return "name must be a non-empty string, not " + args.at("name").dump();
    }
    out_name = args.at("name").get<std::string>();
    return {};
}

[[nodiscard]] auto node_kind(const erhe::scene::Node& node) -> const char*
{
    if (carries_kit_piece(node)) {
        return "piece";
    }
    if (dynamic_cast<const erhe::scene::Mesh*>(&node) != nullptr) {
        return "mesh";
    }
    if (dynamic_cast<const erhe::scene::Xform*>(&node) != nullptr) {
        return "group";
    }
    return "node";
}

// The node and its node descendants, parents first: what level_place_pieces reports of a piece, for any node.
void subtree_nodes_json(const Kit_shape_library& library, const erhe::scene::Node& node, json& out_nodes)
{
    const glm::mat4 world    = node.world_from_node();
    const glm::quat rotation = glm::quat_cast(glm::mat3{world});
    const std::shared_ptr<erhe::scene::Node> parent = node.get_parent_node();
    json entry{
        {"id",            node.get_id()},
        {"name",          node.get_name()},
        {"kind",          node_kind(node)},
        {"parent_id",     parent ? json(parent->get_id()) : json(nullptr)},
        {"position",      vec3_json(glm::vec3{world[3]})},
        {"rotation_xyzw", quat_json(glm::normalize(rotation))},
        {"world_aabb",    world_aabb_json(node)}
    };
    const std::optional<Kit_piece_data> record = read_kit_piece(node);
    if (record.has_value()) {
        entry["shape"]           = record.value().shape;
        entry["parameters_text"] = record.value().parameters;
        entry["materials"]       = piece_materials_json(node, library.find(record.value().shape));
    }
    const std::optional<Brush_placement_data> placement = read_brush_placement(node);
    if (placement.has_value() && placement.value().brush) {
        entry["brush_id"] = placement.value().brush->get_id();
    }
    if (carries_node_physics(node)) {
        const std::shared_ptr<erhe::physics::ICollision_shape> collision_shape = get_node_collision_shape(node);
        entry["physics"] = {
            {"collision_shape", collision_shape ? collision_shape->describe() : std::string{}},
            {"has_body",        get_node_rigid_body(node) != nullptr}
        };
    }
    out_nodes.push_back(std::move(entry));
    for (const std::shared_ptr<erhe::Hierarchy>& child : node.get_children()) {
        const erhe::scene::Node* const child_node = dynamic_cast<const erhe::scene::Node*>(child.get());
        if (child_node != nullptr) {
            subtree_nodes_json(library, *child_node, out_nodes);
        }
    }
}

[[nodiscard]] auto subtree_reply(const Kit_shape_library& library, Scene_root& scene_root, const erhe::scene::Node& node) -> json
{
    json nodes = json::array();
    subtree_nodes_json(library, node, nodes);
    std::size_t piece_count = 0;
    for (const json& entry : nodes) {
        if (entry.at("kind") == "piece") {
            ++piece_count;
        }
    }
    const glm::mat4 world = node.world_from_node();
    return json{
        {"scene_name",  scene_root.get_name()},
        {"id",          node.get_id()},
        {"name",        node.get_name()},
        {"position",    vec3_json(glm::vec3{world[3]})},
        {"count",       nodes.size()},
        {"piece_count", piece_count},
        {"nodes",       nodes}
    };
}

} // anonymous namespace

auto Mcp_server::action_level_group_nodes(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "node_ids", "name"});
    if (!unrecognized.empty()) {
        return make_error_content("level_group_nodes: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, node_ids, name)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::string tail = "; nothing was grouped";
    std::string name{"Group"};
    std::string error = read_optional_name(args, name);
    if (!error.empty()) {
        return make_error_content("level_group_nodes: " + error + tail);
    }
    const json node_ids = args.value("node_ids", json());
    if (!node_ids.is_array() || node_ids.empty()) {
        return make_error_content("level_group_nodes: node_ids must be a non-empty array of node ids" + tail);
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    for (const json& id : node_ids) {
        std::shared_ptr<erhe::scene::Node> node;
        error = read_scene_node(*scene_root, json{{"node_id", id}}, "node_id", node);
        if (!error.empty()) {
            return make_error_content("level_group_nodes: node_ids holds " + id.dump() + ": " + error + tail);
        }
        nodes.push_back(std::move(node));
    }
    const Kit_group_result result = m_context.level_kits->group(*scene_root, nodes, name);
    if (!result.error.empty()) {
        return make_error_content("level_group_nodes: " + result.error + tail);
    }
    json reply = subtree_reply(m_context.level_kits->get_library(), *scene_root, *result.node);
    const std::shared_ptr<erhe::scene::Node> parent = result.node->get_parent_node();
    reply["parent_id"] = parent ? json(parent->get_id()) : json(nullptr);
    return make_json_content(reply).dump();
}

auto Mcp_server::action_level_ungroup(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "group_id"});
    if (!unrecognized.empty()) {
        return make_error_content("level_ungroup: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, group_id)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::string tail = "; nothing was changed";
    std::shared_ptr<erhe::scene::Node> group;
    const std::string error = read_scene_node(*scene_root, args, "group_id", group);
    if (!error.empty()) {
        return make_error_content("level_ungroup: " + error + tail);
    }
    const Kit_ungroup_result result = m_context.level_kits->ungroup(*scene_root, group);
    if (!result.error.empty()) {
        return make_error_content("level_ungroup: " + result.error + tail);
    }
    json children = json::array();
    for (const std::shared_ptr<erhe::Hierarchy>& child : result.children) {
        const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(child.get());
        if (node != nullptr) {
            subtree_nodes_json(m_context.level_kits->get_library(), *node, children);
        }
    }
    return make_json_content({
        {"scene_name",  scene_root->get_name()},
        {"group_id",    args.at("group_id")},
        {"parent_id",   result.parent ? json(result.parent->get_id()) : json(nullptr)},
        {"child_count", result.children.size()},
        {"nodes",       children}
    }).dump();
}

auto Mcp_server::action_level_duplicate_group(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "group_id", "offset", "position", "name"});
    if (!unrecognized.empty()) {
        return make_error_content(
            "level_duplicate_group: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, group_id, offset, position, name)"
        );
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::string tail = "; nothing was copied";
    std::shared_ptr<erhe::scene::Node> group;
    std::string error = read_scene_node(*scene_root, args, "group_id", group);
    if (!error.empty()) {
        return make_error_content("level_duplicate_group: " + error + tail);
    }
    if (args.contains("offset") == args.contains("position")) {
        return make_error_content("level_duplicate_group: give exactly one of offset and position" + tail);
    }
    Kit_group_copy copy;
    copy.placement = args.contains("position") ? Kit_copy_placement::position : Kit_copy_placement::offset;
    if (!read_vec3(args, (copy.placement == Kit_copy_placement::position) ? "position" : "offset", copy.value, error)) {
        return make_error_content("level_duplicate_group: " + error + tail);
    }
    error = read_optional_name(args, copy.name);
    if (!error.empty()) {
        return make_error_content("level_duplicate_group: " + error + tail);
    }
    const Kit_group_result result = m_context.level_kits->duplicate(*scene_root, group, copy);
    if (!result.error.empty()) {
        return make_error_content("level_duplicate_group: " + result.error + tail);
    }
    json reply = subtree_reply(m_context.level_kits->get_library(), *scene_root, *result.node);
    reply["source_id"] = args.at("group_id");
    return make_json_content(reply).dump();
}

auto Mcp_server::action_level_mirror_group(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "group_id", "axis"});
    if (!unrecognized.empty()) {
        return make_error_content("level_mirror_group: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, group_id, axis)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::string tail = "; nothing was changed";
    std::shared_ptr<erhe::scene::Node> group;
    const std::string error = read_scene_node(*scene_root, args, "group_id", group);
    if (!error.empty()) {
        return make_error_content("level_mirror_group: " + error + tail);
    }
    const json axis_value = args.value("axis", json());
    Mirror_axis axis{Mirror_axis::x};
    if (axis_value == json("x")) {
        axis = Mirror_axis::x;
    } else if (axis_value == json("y")) {
        axis = Mirror_axis::y;
    } else if (axis_value == json("z")) {
        axis = Mirror_axis::z;
    } else {
        return make_error_content("level_mirror_group: axis is " + axis_value.dump() + " (x, y, z)" + tail);
    }
    const Kit_mirror_result result = m_context.level_kits->mirror(*scene_root, group, axis);
    if (!result.error.empty()) {
        return make_error_content("level_mirror_group: " + result.error + tail);
    }
    json nodes         = json::array();
    json not_exact_ids = json::array();
    for (const Kit_mirrored_node& mirrored : result.nodes) {
        const bool exact = (mirrored.image == Mirror_image::exact);
        nodes.push_back({
            {"id",            mirrored.node->get_id()},
            {"name",          mirrored.node->get_name()},
            {"kind",          node_kind(*mirrored.node)},
            {"position",      vec3_json(mirrored.position)},
            {"rotation_xyzw", quat_json(mirrored.rotation)},
            {"local_plane",   c_str(mirrored.plane)},
            {"exact_mirror",  exact},
            {"world_aabb",    world_aabb_json(*mirrored.node)}
        });
        if (!exact) {
            not_exact_ids.push_back(mirrored.node->get_id());
        }
    }
    return make_json_content({
        {"scene_name",      scene_root->get_name()},
        {"group_id",        args.at("group_id")},
        {"axis",            c_str(axis)},
        {"plane_point",     vec3_json(result.plane_point)},
        {"plane_normal",    vec3_json(result.plane_normal)},
        {"count",           nodes.size()},
        {"not_exact_count", result.not_exact_count},
        {"not_exact_ids",   not_exact_ids},
        {"nodes",           nodes}
    }).dump();
}

namespace {

[[nodiscard]] auto layout_pattern_json(const Kit_layout_pattern& pattern) -> json
{
    return json{
        {"kind",        c_str(pattern.kind)},
        {"count_mode",  c_str(pattern.count_mode)},
        {"count",       pattern.count},
        {"gap",         pattern.gap},
        {"direction",   vec3_json(pattern.direction)},
        {"step",        pattern.step},
        {"length",      pattern.length},
        {"count_x",     pattern.count_x},
        {"count_z",     pattern.count_z},
        {"spacing_x",   pattern.spacing_x},
        {"spacing_z",   pattern.spacing_z},
        {"radius",      pattern.radius},
        {"arc_degrees", pattern.arc_degrees},
        {"facing",      c_str(pattern.facing)},
        {"orientation", c_str(pattern.orientation)},
        {"scatter_mode",  c_str(pattern.scatter_mode)},
        {"density",       pattern.density},
        {"region",        c_str(pattern.region)},
        {"size_x",        pattern.size_x},
        {"size_z",        pattern.size_z},
        {"region_radius", pattern.region_radius},
        {"min_spacing",   pattern.min_spacing},
        {"yaw_min",       pattern.yaw_min},
        {"yaw_max",       pattern.yaw_max},
        {"stairs",        json{
            {"width",          pattern.stairs.width},
            {"step_rise",      pattern.stairs.max_rise},
            {"tread_depth",    pattern.stairs.min_tread},
            {"thickness",      pattern.stairs.thickness},
            {"edge_tolerance", pattern.stairs.tolerance},
            {"material",       pattern.stairs_material}
        }},
        {"floor",         json{
            {"thickness",      pattern.floor.thickness},
            {"wall_thickness", pattern.floor.wall_thickness},
            {"room_height",    pattern.floor.room_height},
            {"rail_inset",     pattern.floor.rail_inset},
            {"material",       pattern.floor_material},
            {"wall_material",  pattern.floor_wall_material}
        }}
    };
}

// A stairs layout's fit and mesh as the curve and the values make them now (R5.12.13); a refusal as its problem.
[[nodiscard]] auto layout_stairs_json(const erhe::scene::Node& node, const Kit_layout_pattern& pattern) -> json
{
    const erhe::scene::Curve* const curve = dynamic_cast<const erhe::scene::Curve*>(&node);
    if (curve == nullptr) {
        return json{{"problem", "the node is not a curve"}};
    }
    Stairs_report     report;
    GEO::Mesh         mesh{3, true};
    const std::string refusal = make_stairs(curve->get_table(), pattern.stairs, report, &mesh);
    if (!refusal.empty()) {
        return json{{"problem", refusal}};
    }
    const Stairs_fit& fit = report.fit;
    json flights = json::array();
    for (const Stairs_flight& flight : fit.flights) {
        flights.push_back(json{{"step_count", flight.step_count}, {"rise", flight.rise}, {"tread", flight.tread}, {"begin", flight.begin}, {"end", flight.end}});
    }
    json landings = json::array();
    for (const Stairs_landing& landing : fit.landings) {
        landings.push_back(json{{"begin", landing.begin}, {"length", landing.length}, {"level", landing.level}});
    }
    json risers = json::array();
    for (const Stairs_riser& riser : fit.risers) {
        risers.push_back(json{{"distance", riser.distance}, {"level_before", riser.level_before}, {"level_after", riser.level_after}, {"flight", riser.flight}});
    }
    const Kit_mesh_report mesh_report = check_kit_mesh(mesh);
    return json{
        {"summary",       fmt::format("{} steps in {} flights, {} landings", fit.step_count, fit.flights.size(), fit.landings.size())},
        {"step_count",    fit.step_count},
        {"flight_count",  fit.flights.size()},
        {"landing_count", fit.landings.size()},
        {"flights",       flights},
        {"landings",      landings},
        {"risers",        risers},
        {"plan_length",   fit.length},
        {"start_level",   fit.start_level},
        {"end_level",     fit.end_level},
        {"total_rise",    fit.total_rise},
        {"max_deviation", fit.max_deviation},
        {"section_count", report.section_count},
        {"mesh", json{
            {"valid",          mesh_report.is_valid()},
            {"problems",       mesh_report.problems},
            {"triangle_count", mesh_report.triangle_count},
            {"vertex_count",   mesh_report.vertex_count},
            {"components",     mesh_report.component_count},
            {"bounds_min",     vec3_json(mesh_report.bounds_min)},
            {"bounds_max",     vec3_json(mesh_report.bounds_max)}
        }}
    };
}

// A path layout's own curve: its points, closure and length; null for a node that is not a curve.
[[nodiscard]] auto layout_curve_json(const erhe::scene::Node& node) -> json
{
    const erhe::scene::Curve* const curve = dynamic_cast<const erhe::scene::Curve*>(&node);
    if (curve == nullptr) {
        return json(nullptr);
    }
    return json{
        {"point_count", curve->get_path().points.size()},
        {"closed",      curve->get_path().closure == erhe::math::Curve_closure::closed},
        {"length",      curve->get_table().get_length()}
    };
}

// The given keys over the pattern; "kind", "count_mode", "facing", "orientation", "scatter_mode" and "region" by name.
[[nodiscard]] auto read_layout_pattern_json(const json& object, Kit_layout_pattern& pattern) -> std::string
{
    if (!object.is_object()) {
        return "pattern must be an object";
    }
    const std::string unrecognized = unrecognized_keys(
        object,
        {
            "kind", "count_mode", "count", "gap", "direction", "step", "length", "count_x", "count_z", "spacing_x", "spacing_z", "radius", "arc_degrees", "facing", "orientation",
            "scatter_mode", "density", "region", "size_x", "size_z", "region_radius", "min_spacing", "yaw_min", "yaw_max", "stairs", "floor"
        }
    );
    if (!unrecognized.empty()) {
        return "pattern: unrecognized key(s): " + unrecognized +
            " (accepted: kind, count_mode, count, gap, direction, step, length, count_x, count_z, spacing_x, spacing_z, radius, arc_degrees, facing, orientation,"
            " scatter_mode, density, region, size_x, size_z, region_radius, min_spacing, yaw_min, yaw_max, stairs;"
            " a path, segments, stairs or floor layout is its own curve node: level_create_layout takes points and closed, or curve_id of a plain curve)";
    }
    if (object.contains("kind")) {
        const json& kind = object.at("kind");
        bool found = false;
        for (const Repeat_pattern_kind candidate : {Repeat_pattern_kind::line, Repeat_pattern_kind::grid, Repeat_pattern_kind::ring, Repeat_pattern_kind::path, Repeat_pattern_kind::scatter, Repeat_pattern_kind::segments, Repeat_pattern_kind::stairs, Repeat_pattern_kind::floor}) {
            if (kind == json(c_str(candidate))) {
                pattern.kind = candidate;
                found = true;
            }
        }
        if (!found) {
            return "pattern: kind is " + kind.dump() + " (line, grid, ring, path, scatter, segments, stairs, floor; a polyline is a path along a curve of corner points)";
        }
    }
    if (object.contains("floor")) {
        const json& floor = object.at("floor");
        if (!floor.is_object()) {
            return "pattern: floor must be an object {thickness, wall_thickness, room_height, rail_inset, material, wall_material}";
        }
        const std::string unrecognized_floor = unrecognized_keys(floor, {"thickness", "wall_thickness", "room_height", "rail_inset", "material", "wall_material"});
        if (!unrecognized_floor.empty()) {
            return "pattern: floor: unrecognized key(s): " + unrecognized_floor + " (accepted: thickness, wall_thickness, room_height, rail_inset, material, wall_material)";
        }
        std::string floor_error;
        if (
            !read_number(floor, "thickness",      pattern.floor.thickness,      floor_error) ||
            !read_number(floor, "wall_thickness", pattern.floor.wall_thickness, floor_error) ||
            !read_number(floor, "room_height",    pattern.floor.room_height,    floor_error) ||
            !read_number(floor, "rail_inset",     pattern.floor.rail_inset,     floor_error)
        ) {
            return "pattern: floor: " + floor_error;
        }
        for (const char* const key : {"material", "wall_material"}) {
            if (floor.contains(key)) {
                if (!floor.at(key).is_string()) {
                    return std::string{"pattern: floor: "} + key + " must be a scene or palette material name (empty: the slot's default)";
                }
                (std::string_view{key} == "material" ? pattern.floor_material : pattern.floor_wall_material) = floor.at(key).get<std::string>();
            }
        }
        floor_error = check_floor_values(pattern.floor);
        if (!floor_error.empty()) {
            return "pattern: " + floor_error;
        }
    }
    if (object.contains("stairs")) {
        const json& stairs = object.at("stairs");
        if (!stairs.is_object()) {
            return "pattern: stairs must be an object {width, step_rise, tread_depth, thickness, edge_tolerance, material}";
        }
        const std::string unrecognized_stairs = unrecognized_keys(stairs, {"width", "step_rise", "tread_depth", "thickness", "edge_tolerance", "material"});
        if (!unrecognized_stairs.empty()) {
            return "pattern: stairs: unrecognized key(s): " + unrecognized_stairs + " (accepted: width, step_rise, tread_depth, thickness, edge_tolerance, material)";
        }
        std::string stairs_error;
        if (
            !read_number(stairs, "width",          pattern.stairs.width,     stairs_error) ||
            !read_number(stairs, "step_rise",      pattern.stairs.max_rise,  stairs_error) ||
            !read_number(stairs, "tread_depth",    pattern.stairs.min_tread, stairs_error) ||
            !read_number(stairs, "thickness",      pattern.stairs.thickness, stairs_error) ||
            !read_number(stairs, "edge_tolerance", pattern.stairs.tolerance, stairs_error)
        ) {
            return "pattern: stairs: " + stairs_error;
        }
        if (stairs.contains("material")) {
            if (!stairs.at("material").is_string()) {
                return "pattern: stairs: material must be a scene or palette material name (empty: the stone slot's default)";
            }
            pattern.stairs_material = stairs.at("material").get<std::string>();
        }
        stairs_error = check_stairs_values(pattern.stairs);
        if (!stairs_error.empty()) {
            return "pattern: " + stairs_error;
        }
    }
    if (object.contains("orientation")) {
        const json& orientation = object.at("orientation");
        if (orientation == json(c_str(Repeat_path_orientation::upright))) {
            pattern.orientation = Repeat_path_orientation::upright;
        } else if (orientation == json(c_str(Repeat_path_orientation::tangent))) {
            pattern.orientation = Repeat_path_orientation::tangent;
        } else {
            return "pattern: orientation is " + orientation.dump() + " (upright, tangent)";
        }
    }
    if (object.contains("count_mode")) {
        const json& mode = object.at("count_mode");
        if (mode == json("count")) {
            pattern.count_mode = Kit_layout_count_mode::count;
        } else if (mode == json("gap")) {
            pattern.count_mode = Kit_layout_count_mode::gap;
        } else {
            return "pattern: count_mode is " + mode.dump() + " (count, gap)";
        }
    }
    if (object.contains("scatter_mode")) {
        const json& mode = object.at("scatter_mode");
        if (mode == json(c_str(Kit_scatter_mode::count))) {
            pattern.scatter_mode = Kit_scatter_mode::count;
        } else if (mode == json(c_str(Kit_scatter_mode::density))) {
            pattern.scatter_mode = Kit_scatter_mode::density;
        } else {
            return "pattern: scatter_mode is " + mode.dump() + " (count, density)";
        }
    }
    if (object.contains("region")) {
        const json& region = object.at("region");
        if (region == json(c_str(Repeat_scatter_region::rectangle))) {
            pattern.region = Repeat_scatter_region::rectangle;
        } else if (region == json(c_str(Repeat_scatter_region::disc))) {
            pattern.region = Repeat_scatter_region::disc;
        } else {
            return "pattern: region is " + region.dump() + " (rectangle, disc)";
        }
    }
    for (const char* const key : {"count", "count_x", "count_z"}) {
        if (!object.contains(key)) {
            continue;
        }
        const json& value = object.at(key);
        if (!value.is_number_integer() || (value.get<long long>() < 1) || (value.get<long long>() > static_cast<long long>(c_max_repeat_count))) {
            return std::string{"pattern: "} + key + " is " + value.dump() + ", it must be an integer in 1 - " + std::to_string(c_max_repeat_count);
        }
        int& target = (std::string_view{key} == "count") ? pattern.count : ((std::string_view{key} == "count_x") ? pattern.count_x : pattern.count_z);
        target = value.get<int>();
    }
    std::string error;
    if (
        !read_number(object, "gap",         pattern.gap,         error) ||
        !read_number(object, "step",        pattern.step,        error) ||
        !read_number(object, "length",      pattern.length,      error) ||
        !read_number(object, "spacing_x",   pattern.spacing_x,   error) ||
        !read_number(object, "spacing_z",   pattern.spacing_z,   error) ||
        !read_number(object, "radius",      pattern.radius,      error) ||
        !read_number(object, "arc_degrees", pattern.arc_degrees, error) ||
        !read_number(object, "density",       pattern.density,       error) ||
        !read_number(object, "size_x",        pattern.size_x,        error) ||
        !read_number(object, "size_z",        pattern.size_z,        error) ||
        !read_number(object, "region_radius", pattern.region_radius, error) ||
        !read_number(object, "min_spacing",   pattern.min_spacing,   error) ||
        !read_number(object, "yaw_min",       pattern.yaw_min,       error) ||
        !read_number(object, "yaw_max",       pattern.yaw_max,       error)
    ) {
        return "pattern: " + error;
    }
    if (object.contains("direction")) {
        float xyz[3]{};
        if (!read_finite_numbers(object.at("direction"), 3, xyz)) {
            return "pattern: direction must be an array of 3 finite numbers";
        }
        pattern.direction = glm::vec3{xyz[0], xyz[1], xyz[2]};
    }
    if (object.contains("facing")) {
        const json& facing = object.at("facing");
        if (facing == json("outward")) {
            pattern.facing = Repeat_ring_facing::outward;
        } else if (facing == json("start")) {
            pattern.facing = Repeat_ring_facing::start_rotation;
        } else {
            return "pattern: facing is " + facing.dump() + " (outward, start)";
        }
    }
    return {};
}

// items: [{shape, parameters, materials, weight}], each read as a level_place_pieces piece reads them; weight defaults to 1.
[[nodiscard]] auto read_layout_items_json(Level_kits& level_kits, Scene_root& scene_root, const json& items_json, std::vector<Kit_layout_item>& out_items) -> std::string
{
    if (!items_json.is_array() || items_json.empty()) {
        return "items must be a non-empty array of {shape, parameters, materials, weight}";
    }
    out_items.clear();
    for (std::size_t i = 0, end = items_json.size(); i < end; ++i) {
        const json& item_json = items_json[i];
        const std::string prefix = "items[" + std::to_string(i) + "]: ";
        if (!item_json.is_object()) {
            return prefix + "an item must be an object";
        }
        const std::string unrecognized = unrecognized_keys(item_json, {"shape", "parameters", "materials", "weight"});
        if (!unrecognized.empty()) {
            return prefix + "unrecognized key(s): " + unrecognized + " (accepted: shape, parameters, materials, weight)";
        }
        uint32_t weight = 1;
        if (item_json.contains("weight")) {
            const json& weight_json = item_json.at("weight");
            if (!weight_json.is_number_integer() || (weight_json.get<long long>() < 0) || (weight_json.get<long long>() > static_cast<long long>(c_max_scatter_weight))) {
                return prefix + "weight is " + weight_json.dump() + ", it must be an integer in 0 - " + std::to_string(c_max_scatter_weight);
            }
            weight = static_cast<uint32_t>(weight_json.get<long long>());
        }
        Kit_placement placement;
        std::string error = read_piece_values_json(level_kits, scene_root, item_json, placement);
        if (error.empty()) {
            error = read_piece_materials(level_kits, scene_root, item_json, placement);
        }
        if (!error.empty()) {
            return prefix + error;
        }
        Kit_layout_item item;
        item.shape      = placement.shape->id;
        item.parameters = placement.values.value().to_canonical_text();
        for (const Kit_slot_material& material : placement.materials) {
            item.materials.push_back(material.material ? material.material->get_name() : material.palette_name);
        }
        item.weight = weight;
        out_items.push_back(std::move(item));
    }
    return {};
}

// The call's snap, surface and seed keys over the data.
[[nodiscard]] auto read_layout_settings_json(const json& args, Kit_layout_data& data) -> std::string
{
    if (args.contains("snap")) {
        const json& snap = args.at("snap");
        if (snap == json("module")) {
            data.snap = Kit_snap::module;
        } else if (snap == json("none")) {
            data.snap = Kit_snap::none;
        } else {
            return "snap is " + snap.dump() + " (none, module)";
        }
    }
    std::string error;
    const std::optional<bool> surface_snap   = read_bool(args, "surface_snap",   error);
    const std::optional<bool> surface_orient = read_bool(args, "surface_orient", error);
    const std::optional<bool> per_piece_seed = read_bool(args, "per_piece_seed", error);
    const std::optional<bool> random_seeds   = read_bool(args, "random_seeds",   error); // the former name of per_piece_seed
    if (!error.empty()) {
        return error;
    }
    if (per_piece_seed.has_value() && random_seeds.has_value()) {
        return "per_piece_seed and random_seeds (its former name) are both given; give one";
    }
    if (surface_snap.has_value())   { data.surface_snap   = surface_snap.value()   ? Surface_snap::on              : Surface_snap::off; }
    if (surface_orient.has_value()) { data.surface_orient = surface_orient.value() ? Surface_orient::on            : Surface_orient::off; }
    if (args.contains("surface_direction")) {
        data.surface_direction = read_direction(args, "surface_direction", error);
    }
    data.surface_forward_distance  = read_max_distance(args, "surface_forward_distance",  data.surface_forward_distance,  error);
    data.surface_backward_distance = read_max_distance(args, "surface_backward_distance", data.surface_backward_distance, error);
    if (args.contains("surface_support")) {
        const json& support = args.at("surface_support");
        if (!support.is_string() || !from_string(support.get<std::string>(), data.surface_support)) {
            return "surface_support is " + support.dump() + " (five_lines, center_blocks, center_fallback)";
        }
    }
    if (!error.empty()) {
        return error;
    }
    const std::optional<bool> seeds = per_piece_seed.has_value() ? per_piece_seed : random_seeds;
    if (seeds.has_value())          { data.seeds          = seeds.value()          ? Kit_layout_seeds::per_element : Kit_layout_seeds::item; }
    if (args.contains("seed")) {
        const json& seed = args.at("seed");
        if (!seed.is_number_integer() || (seed.get<long long>() < 0) || (seed.get<long long>() > kit_seed_max)) {
            return "seed is " + seed.dump() + ", it must be an integer in 0 - " + std::to_string(kit_seed_max);
        }
        data.seed = seed.get<int>();
    }
    if (args.contains("connector_hiding")) {
        return read_connector_hiding_json(args.at("connector_hiding"), data.connector_hiding);
    }
    return {};
}

[[nodiscard]] auto layout_items_reply(Level_kits& level_kits, Scene_root& scene_root, const std::vector<Kit_layout_item>& items) -> json
{
    json result = json::array();
    for (const Kit_layout_item& item : items) {
        json entry{{"shape", item.shape}, {"parameters_text", item.parameters}, {"weight", item.weight}};
        Kit_placement placement;
        const std::string error = level_kits.read_layout_item(scene_root, item, placement);
        if (error.empty()) {
            entry["parameters"] = values_json(placement.values.value());
            json materials = json::object();
            for (std::size_t slot = 0, end = placement.shape->material_slots.size(); slot < end; ++slot) {
                materials[placement.shape->material_slots[slot]] = (slot < item.materials.size()) ? item.materials[slot] : std::string{};
            }
            entry["materials"] = materials;
            const erhe::math::Aabb bounds = level_kits.get_layout_item_bounds(scene_root, item);
            entry["extent"] = bounds.is_valid_3d() ? vec3_json(bounds.max - bounds.min) : json(nullptr);
            entry["bounds"] = bounds.is_valid_3d() ? aabb_json(bounds) : json(nullptr);
            entry["footprint_radius"] = get_scatter_footprint_radius(bounds);
        } else {
            entry["problem"] = error;
        }
        result.push_back(std::move(entry));
    }
    return result;
}

[[nodiscard]] auto layout_reply(Level_kits& level_kits, Scene_root& scene_root, const erhe::scene::Node& node, const Kit_layout_result* const result) -> json
{
    Kit_layout_data   data;
    const std::string read_error = read_kit_layout(node, data);
    const erhe::scene::Trs_transform world = node.world_from_node_transform();
    // R5.12.8: a path layout is a curve node; get_curve and the curve tools read and edit it as any curve.
    json reply{
        {"id",            node.get_id()},
        {"name",          node.get_name()},
        {"type",          std::string{node.get_type_name()}},
        {"position",      vec3_json(world.get_translation())},
        {"rotation_xyzw", quat_json(world.get_rotation())},
        {"curve",         layout_curve_json(node)}
    };
    if (!read_error.empty()) {
        reply["problem"] = read_error;
        return reply;
    }
    reply["locked"]         = (data.lock == Kit_layout_lock::locked);
    reply["pattern"]        = layout_pattern_json(data.pattern);
    reply["pattern_text"]   = node.get_value(Kit_layout::pattern_property);
    reply["items"]          = layout_items_reply(level_kits, scene_root, data.items);
    reply["snap"]           = (data.snap == Kit_snap::module) ? "module" : "none";
    reply["surface_snap"]   = (data.surface_snap == Surface_snap::on);
    reply["surface_orient"] = (data.surface_orient == Surface_orient::on);
    reply["surface_direction"]         = vec3_json(data.surface_direction);
    reply["surface_forward_distance"]  = data.surface_forward_distance;
    reply["surface_backward_distance"] = data.surface_backward_distance;
    reply["surface_support"]           = std::string{to_string(data.surface_support)};
    reply["per_piece_seed"] = (data.seeds == Kit_layout_seeds::per_element);
    reply["seed"]           = data.seed;
    reply["connector_hiding"] = connector_hiding_json(data.connector_hiding);
    // The run the values make now, from the items' extents, weights and footprints.
    Kit_layout_item_sizes sizes;
    level_kits.get_layout_item_sizes(scene_root, data.items, sizes);
    const float       extent = sizes.extent;
    Kit_layout_run    run;
    const std::string run_error = level_kits.resolve_layout_run(node, data, sizes, run);
    reply["run"] = run_error.empty()
        ? json{{"count", run.count}, {"requested", run.requested}, {"step", run.step}, {"space", run.space}, {"item_extent", extent}}
        : json{{"problem", run_error}, {"item_extent", extent}};
    if (data.pattern.kind == Repeat_pattern_kind::stairs) {
        reply["stairs"] = layout_stairs_json(node, data.pattern);
    }
    json children = json::array();
    for (const std::shared_ptr<erhe::Hierarchy>& child : node.get_children()) {
        const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
        if (!child_node) {
            continue;
        }
        const erhe::scene::Trs_transform child_world = child_node->world_from_node_transform();
        json entry{
            {"id",            child_node->get_id()},
            {"name",          child_node->get_name()},
            {"position",      vec3_json(child_world.get_translation())},
            {"rotation_xyzw", quat_json(child_world.get_rotation())},
            {"scale",         vec3_json(child_node->parent_from_node_transform().get_scale())}
        };
        const std::string generated_source = read_layout_generated_source(*child_node);
        if (!generated_source.empty()) {
            // A stairs layout's generated child: no kit piece, its mesh and body made from the curve.
            entry["generated"]  = true;
            entry["world_aabb"] = world_aabb_json(*child_node);
            const std::shared_ptr<erhe::scene::Mesh> child_mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(child_node);
            json materials = json::array();
            if (child_mesh) {
                for (const erhe::scene::Mesh_primitive& primitive : child_mesh->get_primitives()) {
                    materials.push_back(primitive.material ? primitive.material->get_name() : std::string{});
                }
            }
            entry["materials"] = materials;
        }
        const std::optional<Kit_piece_data> record = read_kit_piece(*child_node);
        if (record.has_value()) {
            entry["shape"]           = record.value().shape;
            entry["parameters_text"] = record.value().parameters;
            const Kit_shape_definition* const shape = level_kits.get_library().find(record.value().shape);
            entry["materials"] = piece_materials_json(*child_node, shape);
            if (shape != nullptr) {
                Kit_parameter_values values{*shape};
                if (parse_canonical_text(*shape, record.value().parameters, values).empty()) {
                    entry["parameters"] = values_json(values);
                }
            }
        }
        children.push_back(std::move(entry));
    }
    reply["child_count"] = children.size();
    reply["children"]    = children;
    if (result != nullptr) {
        reply["changed"]       = result->changed;
        reply["kept_count"]    = result->kept_count;
        reply["added_count"]   = result->added_count;
        reply["removed_count"] = result->removed_count;
    }
    return reply;
}

[[nodiscard]] auto find_layout_node(Scene_root& scene_root, const json& args, const char* tool, std::shared_ptr<erhe::scene::Node>& out_node) -> std::string
{
    if (!args.contains("layout_id") || !args.at("layout_id").is_number_unsigned()) {
        return std::string{tool} + ": layout_id must be a node id";
    }
    out_node = find_node_in_scene(scene_root, args, "layout_id", "layout_name");
    if (!out_node) {
        return std::string{tool} + ": no node " + args.at("layout_id").dump() + " in scene " + scene_root.get_name();
    }
    if (!carries_kit_layout(*out_node)) {
        return std::string{tool} + ": node " + args.at("layout_id").dump() + " '" + out_node->get_name() + "' is not a kit layout node";
    }
    return {};
}

} // anonymous namespace

auto Mcp_server::action_level_create_layout(const json& args) -> std::string
{
    const std::string tail = "; nothing was placed";
    const std::string unrecognized = unrecognized_keys(
        args, {"scene_name", "name", "parent_id", "position", "rotation_xyzw", "pattern", "items", "from_piece_id", "curve_id", "points", "closed", "snap", "surface_snap", "surface_orient", "surface_direction", "surface_forward_distance", "surface_backward_distance", "surface_support", "per_piece_seed", "random_seeds", "seed", "connector_hiding", "locked"}
    );
    if (!unrecognized.empty()) {
        return make_error_content(
            "level_create_layout: unrecognized argument(s): " + unrecognized +
            " (accepted: scene_name, name, parent_id, position, rotation_xyzw, pattern, items, from_piece_id, curve_id, points, closed, snap, surface_snap, surface_orient, surface_direction, surface_forward_distance, surface_backward_distance, surface_support, per_piece_seed (random_seeds: its former name), seed, connector_hiding, locked)"
        );
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    Level_kits&       level_kits = *m_context.level_kits;
    Kit_layout_create create;
    std::string       error;
    std::shared_ptr<erhe::scene::Mesh>  piece;
    std::shared_ptr<erhe::scene::Curve> on_curve; // curve_id: this plain curve node becomes the path layout
    // A stairs layout places no item: its staircase is generated from the curve.
    const bool stairs_kind = args.contains("pattern") && args.at("pattern").is_object() &&
                             ((args.at("pattern").value("kind", json()) == json("stairs")) || (args.at("pattern").value("kind", json()) == json("floor")));
    if (args.contains("curve_id")) {
        for (const char* const key : {"from_piece_id", "position", "rotation_xyzw", "parent_id", "name", "points", "closed"}) {
            if (args.contains(key)) {
                return make_error_content(std::string{"level_create_layout: give curve_id or "} + key + ", not both: the curve node states its place, name and points" + tail);
            }
        }
        if (!args.at("curve_id").is_number_unsigned()) {
            return make_error_content("level_create_layout: curve_id must be a curve node id (create_curve, get_curves)" + tail);
        }
        on_curve = std::dynamic_pointer_cast<erhe::scene::Curve>(find_node_in_scene(*scene_root, args, "curve_id", "curve_name"));
        if (!on_curve) {
            return make_error_content("level_create_layout: curve_id " + args.at("curve_id").dump() + " is no curve node of scene " + scene_root->get_name() + tail);
        }
    }
    if (args.contains("from_piece_id")) {
        // The piece is the one item and its place is the node's (the Level Kit window's Make Layout).
        for (const char* const key : {"items", "position", "rotation_xyzw", "parent_id"}) {
            if (args.contains(key)) {
                return make_error_content(std::string{"level_create_layout: give from_piece_id or "} + key + ", not both: the piece states its " + key + tail);
            }
        }
        if (!args.at("from_piece_id").is_number_unsigned()) {
            return make_error_content("level_create_layout: from_piece_id must be a node id" + tail);
        }
        piece = std::dynamic_pointer_cast<erhe::scene::Mesh>(find_node_in_scene(*scene_root, args, "from_piece_id", "from_piece_name"));
        if (!piece) {
            return make_error_content("level_create_layout: no mesh node " + args.at("from_piece_id").dump() + " in scene " + scene_root->get_name() + tail);
        }
        create.data.items.emplace_back();
        error = level_kits.read_piece_layout_item(*piece, create.data.items.front());
    } else if (args.contains("items") || !stairs_kind) {
        error = read_layout_items_json(level_kits, *scene_root, args.value("items", json()), create.data.items);
    }
    if (error.empty()) {
        // Steps and spacings not given make a contiguous run of the largest item.
        glm::vec3 extent{0.0f};
        for (const Kit_layout_item& item : create.data.items) {
            const erhe::math::Aabb bounds = level_kits.get_layout_item_bounds(*scene_root, item);
            if (bounds.is_valid_3d()) {
                extent = glm::max(extent, bounds.max - bounds.min);
            }
        }
        set_layout_spacing_from_extent(create.data.pattern, extent);
        if (!args.contains("pattern")) {
            error = "pattern is required ({kind, ...})";
        } else {
            error = read_layout_pattern_json(args.at("pattern"), create.data.pattern);
        }
    }
    // A path layout is a curve node (R5.12.8): its points and closure, else a straight curve along its x axis.
    if (error.empty() && (args.contains("points") || args.contains("closed"))) {
        if (!is_curve_layout_kind(create.data.pattern.kind)) {
            error = std::string{"points and closed are a path, segments, stairs or floor layout's curve; a "} + c_str(create.data.pattern.kind) + " layout is an Xform node";
        } else if (!args.contains("points")) {
            error = "closed needs points: the default curve is a straight open one";
        } else {
            erhe::math::Curve_path path;
            if (read_curve_path_args(args, path, error)) {
                create.path = path;
            }
        }
    }
    if (error.empty()) { error = read_layout_settings_json(args, create.data); }
    if (error.empty() && !piece && !on_curve) {
        Kit_placement pose;
        error = read_piece_pose(args, pose);
        create.position = pose.position;
        create.rotation = pose.rotation;
    }
    if (error.empty()) {
        Kit_placement named;
        error = read_piece_name(args, "name", named);
        create.name = named.name;
    }
    if (error.empty() && !piece && !on_curve) {
        Kit_placement parented;
        error = read_piece_parent(*scene_root, args, parented);
        create.parent = parented.parent;
    }
    if (error.empty()) {
        std::string lock_error;
        const std::optional<bool> locked = read_bool(args, "locked", lock_error);
        error = lock_error;
        create.data.lock = locked.value_or(true) ? Kit_layout_lock::locked : Kit_layout_lock::unlocked;
    }
    if (!error.empty()) {
        return make_error_content("level_create_layout: " + error + tail);
    }
    const Kit_layout_result result = on_curve
        ? level_kits.make_layout_on_curve(*scene_root, on_curve, create.data)
        : piece
        ? level_kits.make_layout_from_piece(
            *scene_root,
            Kit_layout_from_piece{
                .piece          = piece,
                .pattern        = create.data.pattern,
                .snap           = create.data.snap,
                .surface_snap   = create.data.surface_snap,
                .surface_orient = create.data.surface_orient,
                .surface_direction         = create.data.surface_direction,
                .surface_forward_distance  = create.data.surface_forward_distance,
                .surface_backward_distance = create.data.surface_backward_distance,
                .surface_support           = create.data.surface_support,
                .seeds          = create.data.seeds,
                .seed           = create.data.seed,
                .lock           = create.data.lock,
                .connector_hiding = create.data.connector_hiding,
                .name           = create.name,
                .path           = create.path
            }
        )
        : level_kits.create_layout(*scene_root, create);
    if (!result.error.empty()) {
        return make_error_content("level_create_layout: " + result.error + tail);
    }
    json reply = layout_reply(level_kits, *scene_root, *result.node, &result);
    reply["scene_name"] = scene_root->get_name();
    if (piece) {
        reply["replaced_piece_id"] = piece->get_id();
    }
    return make_json_content(reply).dump();
}

auto Mcp_server::action_level_set_layout(const json& args) -> std::string
{
    const std::size_t serial = get_recording_serial();
    const std::string tail = "; nothing was changed";
    const std::string unrecognized = unrecognized_keys(
        args, {"scene_name", "layout_id", "pattern", "items", "snap", "surface_snap", "surface_orient", "surface_direction", "surface_forward_distance", "surface_backward_distance", "surface_support", "per_piece_seed", "random_seeds", "seed", "connector_hiding"}
    );
    if (!unrecognized.empty()) {
        return make_error_content(
            "level_set_layout: unrecognized argument(s): " + unrecognized +
            " (accepted: scene_name, layout_id, pattern, items, snap, surface_snap, surface_orient, surface_direction, surface_forward_distance, surface_backward_distance, surface_support, per_piece_seed (random_seeds: its former name), seed, connector_hiding; the lock: level_set_layout_lock)"
        );
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::shared_ptr<erhe::scene::Node> node;
    std::string error = find_layout_node(*scene_root, args, "level_set_layout", node);
    if (!error.empty()) {
        return make_error_content(error);
    }
    Level_kits&     level_kits = *m_context.level_kits;
    Kit_layout_data data;
    error = read_kit_layout(*node, data);
    // The values given merge over the node's (D4.9).
    if (error.empty() && args.contains("items")) {
        error = read_layout_items_json(level_kits, *scene_root, args.at("items"), data.items);
    }
    if (error.empty() && args.contains("pattern")) {
        error = read_layout_pattern_json(args.at("pattern"), data.pattern);
    }
    if (error.empty()) {
        error = read_layout_settings_json(args, data);
    }
    if (!error.empty()) {
        return make_error_content("level_set_layout: " + error + tail);
    }
    const Kit_layout_result result = level_kits.set_layout(*scene_root, node, data);
    if (!result.error.empty()) {
        return make_error_content("level_set_layout: " + result.error + tail);
    }
    json reply = layout_reply(level_kits, *scene_root, *node, &result);
    reply["scene_name"] = scene_root->get_name();
    reply["flow"]       = make_flow_reply(serial);
    return make_json_content(reply).dump();
}

auto Mcp_server::action_level_set_layout_lock(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "layout_id", "locked"});
    if (!unrecognized.empty()) {
        return make_error_content("level_set_layout_lock: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, layout_id, locked)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::shared_ptr<erhe::scene::Node> node;
    std::string error = find_layout_node(*scene_root, args, "level_set_layout_lock", node);
    if (!error.empty()) {
        return make_error_content(error);
    }
    const std::optional<bool> locked = read_bool(args, "locked", error);
    if (!locked.has_value()) {
        return make_error_content("level_set_layout_lock: " + (error.empty() ? std::string{"locked (true or false) is required"} : error));
    }
    Level_kits& level_kits = *m_context.level_kits;
    const Kit_layout_result result = level_kits.set_layout_lock(*scene_root, node, locked.value() ? Kit_layout_lock::locked : Kit_layout_lock::unlocked);
    if (!result.error.empty()) {
        return make_error_content("level_set_layout_lock: " + result.error + "; nothing was changed");
    }
    json reply = layout_reply(level_kits, *scene_root, *node, &result);
    reply["scene_name"] = scene_root->get_name();
    return make_json_content(reply).dump();
}

auto Mcp_server::action_level_wrap_with_path(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "node_ids"});
    if (!unrecognized.empty()) {
        return make_error_content("level_wrap_with_path: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, node_ids)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::string tail = "; nothing was changed";
    // node_ids, else the nodes of the scene's selection (the hierarchy window's entry takes the selection the same way).
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    if (args.contains("node_ids")) {
        const json& node_ids = args.at("node_ids");
        if (!node_ids.is_array() || node_ids.empty()) {
            return make_error_content("level_wrap_with_path: node_ids must be a non-empty array of node ids" + tail);
        }
        for (const json& id : node_ids) {
            if (!id.is_number_unsigned()) {
                return make_error_content("level_wrap_with_path: node_ids holds " + id.dump() + ", not a node id" + tail);
            }
            const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*scene_root, json{{"node_id", id}}, "node_id", "node_name");
            if (!node) {
                return make_error_content("level_wrap_with_path: no node " + id.dump() + " in scene " + scene_root->get_name() + tail);
            }
            nodes.push_back(node);
        }
    } else {
        for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
            const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
            if (node && (node->get_item_host() == scene_root)) {
                nodes.push_back(node);
            }
        }
        if (nodes.empty()) {
            return make_error_content("level_wrap_with_path: no node_ids given and no node of scene " + scene_root->get_name() + " is selected" + tail);
        }
    }
    json wrapped_ids = json::array();
    for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
        wrapped_ids.push_back(node->get_id());
    }
    Level_kits& level_kits = *m_context.level_kits;
    const Kit_layout_result result = level_kits.wrap_with_path(*scene_root, nodes);
    if (!result.error.empty()) {
        return make_error_content("level_wrap_with_path: " + result.error + tail);
    }
    json reply = layout_reply(level_kits, *scene_root, *result.node, &result);
    reply["scene_name"]        = scene_root->get_name();
    reply["wrapped_piece_ids"] = wrapped_ids;
    return make_json_content(reply).dump();
}

auto Mcp_server::action_level_refresh_layout(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "layout_id"});
    if (!unrecognized.empty()) {
        return make_error_content("level_refresh_layout: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, layout_id)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::shared_ptr<erhe::scene::Node> node;
    const std::string error = find_layout_node(*scene_root, args, "level_refresh_layout", node);
    if (!error.empty()) {
        return make_error_content(error);
    }
    Level_kits& level_kits = *m_context.level_kits;
    const Kit_layout_result result = level_kits.refresh_layout(*scene_root, node);
    if (!result.error.empty()) {
        return make_error_content("level_refresh_layout: " + result.error + "; nothing was changed");
    }
    json reply = layout_reply(level_kits, *scene_root, *node, &result);
    reply["scene_name"] = scene_root->get_name();
    return make_json_content(reply).dump();
}

namespace {

// The region tool's target, its handles in world space and the selected one (doc/editor/level_tools.md "Layout handles").
[[nodiscard]] auto region_handle_selection_json(Kit_layout_region_tool& tool, Transform_tool* transform_tool) -> json
{
    static_cast<void>(tool.update_point_set());
    const std::shared_ptr<erhe::scene::Node> layout = tool.get_target_layout();
    json reply = json::object();
    if (!layout) {
        reply["layout"]   = nullptr;
        reply["handles"]  = json::array();
        reply["selected"] = nullptr;
        reply["gizmo_on_handle"] = false;
        return reply;
    }
    const std::shared_ptr<Scene_root> scene_root = get_hosting_scene_root(layout.get());
    reply["layout"] = json{
        {"id",         layout->get_id()},
        {"name",       layout->get_name()},
        {"scene_name", scene_root ? scene_root->get_name() : std::string{}}
    };
    std::vector<Kit_region_handle> handles;
    tool.get_handles(handles);
    const glm::mat4 world_from_node = layout->world_from_node();
    json list = json::array();
    for (std::size_t i = 0, end = handles.size(); i < end; ++i) {
        list.push_back({
            {"index",    i},
            {"value",    c_str(handles[i].value)},
            {"position", vec3_json(glm::vec3{world_from_node * glm::vec4{handles[i].position, 1.0f}})}
        });
    }
    reply["handles"] = std::move(list);
    const std::optional<std::size_t> selected = tool.get_selected_handle();
    reply["selected"] = selected.has_value() ? json(selected.value()) : json(nullptr);
    // Whether the selected handle anchors the gizmo (it does from the next idle frame of a viewport).
    reply["gizmo_on_handle"] = (transform_tool != nullptr) && transform_tool->is_component_mode() && selected.has_value();
    return reply;
}

} // anonymous namespace

auto Mcp_server::action_level_select_region_handle(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "layout_id", "index"});
    if (!unrecognized.empty()) {
        return make_error_content("level_select_region_handle: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, layout_id, index)");
    }
    if (m_context.kit_layout_region_tool == nullptr) {
        return make_error_content("level_select_region_handle: no region tool");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::shared_ptr<erhe::scene::Node> node;
    const std::string error = find_layout_node(*scene_root, args, "level_select_region_handle", node);
    if (!error.empty()) {
        return make_error_content(error);
    }
    std::optional<std::size_t> index;
    if (args.contains("index") && !args.at("index").is_null()) {
        if (!args.at("index").is_number_unsigned()) {
            return make_error_content("level_select_region_handle: 'index' must be a handle index (see level_get_region_handles) or null to clear");
        }
        index = args.at("index").get<std::size_t>();
    }
    Kit_layout_region_tool& tool = *m_context.kit_layout_region_tool;
    if (!tool.select_layout_handle(node, index)) {
        std::vector<Kit_region_handle> handles;
        tool.get_handles(handles);
        return make_error_content(fmt::format(
            "level_select_region_handle: '{}' has {} handles (a drawn outline has: line 1 (none for a count mode run of one), grid 3, ring 2, scatter 8 or 4); index {} was not selected",
            node->get_name(), handles.size(), index.has_value() ? fmt::format("{}", index.value()) : std::string{"null"}
        ));
    }
    return make_json_content(region_handle_selection_json(tool, m_context.transform_tool)).dump();
}

auto Mcp_server::query_level_region_handles(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_region_handles: unrecognized argument(s): " + unrecognized + " (it takes none)");
    }
    if (m_context.kit_layout_region_tool == nullptr) {
        return make_error_content("level_get_region_handles: no region tool");
    }
    return make_json_content(region_handle_selection_json(*m_context.kit_layout_region_tool, m_context.transform_tool)).dump();
}

namespace {

// The curve tool's segment handles (doc/editor/level_tools.md "Segment handles"): its target when that is a segments or floor layout.
[[nodiscard]] auto segment_handle_selection_json(Curve_tool& tool, Transform_tool* transform_tool) -> json
{
    static_cast<void>(tool.update_point_set());
    const std::shared_ptr<erhe::scene::Curve> curve = tool.get_target_curve();
    json reply = json::object();
    if (!curve || !tool.has_segment_handles()) {
        reply["layout"]   = nullptr;
        reply["handles"]  = json::array();
        reply["selected"] = nullptr;
        reply["gizmo_on_handle"] = false;
        return reply;
    }
    const std::shared_ptr<Scene_root> scene_root = get_hosting_scene_root(curve.get());
    reply["layout"] = json{
        {"id",         curve->get_id()},
        {"name",       curve->get_name()},
        {"scene_name", scene_root ? scene_root->get_name() : std::string{}}
    };
    const erhe::math::Curve_path& path = curve->get_path();
    const glm::mat4 world_from_node = curve->world_from_node();
    json list = json::array();
    for (std::size_t i = 0, end = tool.get_segment_count(); i < end; ++i) {
        glm::vec3 position{0.0f};
        static_cast<void>(tool.get_point_world_position(tool.get_segment_point_index(i), position));
        glm::vec3 normal{0.0f};
        const bool has_normal = get_segment_normal(path, i, normal).empty();
        list.push_back({
            {"index",    i},
            {"points",   json::array({i, (i + 1) % path.points.size()})},
            {"position", vec3_json(position)},
            {"normal",   has_normal ? vec3_json(glm::normalize(glm::vec3{world_from_node * glm::vec4{normal, 0.0f}})) : json(nullptr)}
        });
    }
    reply["handles"] = std::move(list);
    const std::optional<std::size_t> selected = tool.get_selected_segment();
    reply["selected"] = selected.has_value() ? json(selected.value()) : json(nullptr);
    // Whether the selected handle anchors the gizmo (it does from the next idle frame of a viewport).
    reply["gizmo_on_handle"] = (transform_tool != nullptr) && transform_tool->is_component_mode() && selected.has_value();
    return reply;
}

// The segments or floor layout node of args as a Curve; returns the refusal.
[[nodiscard]] auto find_segments_layout(Scene_root& scene_root, const json& args, const char* tool, std::shared_ptr<erhe::scene::Curve>& out_curve) -> std::string
{
    std::shared_ptr<erhe::scene::Node> node;
    const std::string error = find_layout_node(scene_root, args, tool, node);
    if (!error.empty()) {
        return error;
    }
    out_curve = std::dynamic_pointer_cast<erhe::scene::Curve>(node);
    Kit_layout_data data;
    if (!out_curve || !read_kit_layout(*node, data).empty() ||
        ((data.pattern.kind != Repeat_pattern_kind::segments) && (data.pattern.kind != Repeat_pattern_kind::floor))) {
        return fmt::format("{}: '{}' is not a segments or floor kit layout node (segment handles belong to those kinds)", tool, node->get_name());
    }
    return {};
}

} // anonymous namespace

auto Mcp_server::action_level_select_segment_handle(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "layout_id", "index"});
    if (!unrecognized.empty()) {
        return make_error_content("level_select_segment_handle: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, layout_id, index)");
    }
    if (m_context.curve_tool == nullptr) {
        return make_error_content("level_select_segment_handle: no curve tool");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::shared_ptr<erhe::scene::Curve> curve;
    const std::string error = find_segments_layout(*scene_root, args, "level_select_segment_handle", curve);
    if (!error.empty()) {
        return make_error_content(error);
    }
    std::optional<std::size_t> index;
    if (args.contains("index") && !args.at("index").is_null()) {
        if (!args.at("index").is_number_unsigned()) {
            return make_error_content("level_select_segment_handle: 'index' must be a segment index (see level_get_segment_handles) or null to clear");
        }
        index = args.at("index").get<std::size_t>();
    }
    Curve_tool& tool = *m_context.curve_tool;
    if (!tool.select_curve_segment(curve, index)) {
        return make_error_content(fmt::format(
            "level_select_segment_handle: '{}' has {} segments; index {} was not selected",
            curve->get_name(), erhe::math::get_span_count(curve->get_path()), index.has_value() ? fmt::format("{}", index.value()) : std::string{"null"}
        ));
    }
    return make_json_content(segment_handle_selection_json(tool, m_context.transform_tool)).dump();
}

auto Mcp_server::query_level_segment_handles(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_segment_handles: unrecognized argument(s): " + unrecognized + " (it takes none)");
    }
    if (m_context.curve_tool == nullptr) {
        return make_error_content("level_get_segment_handles: no curve tool");
    }
    return make_json_content(segment_handle_selection_json(*m_context.curve_tool, m_context.transform_tool)).dump();
}

auto Mcp_server::action_level_move_segment(const json& args) -> std::string
{
    const std::size_t serial = get_recording_serial();
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "layout_id", "segment", "offset"});
    if (!unrecognized.empty()) {
        return make_error_content("level_move_segment: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, layout_id, segment, offset)");
    }
    if (m_context.operation_stack == nullptr) {
        return make_error_content("Operation stack not available");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::shared_ptr<erhe::scene::Curve> curve;
    const std::string error = find_segments_layout(*scene_root, args, "level_move_segment", curve);
    if (!error.empty()) {
        return make_error_content(error);
    }
    if (!args.contains("segment") || !args.at("segment").is_number_unsigned()) {
        return make_error_content("level_move_segment: 'segment' must be a segment index (see level_get_segment_handles)");
    }
    if (!args.contains("offset") || !args.at("offset").is_number()) {
        return make_error_content("level_move_segment: 'offset' must be a number (along the segment's normal in the node's frame, meters as a layout node keeps scale 1; negative against it)");
    }
    if (is_instance_structure_protected(*curve)) {
        return make_error_content(fmt::format("level_move_segment: '{}' is inside a reference instance; its path is not an override", curve->get_name()));
    }
    if ((m_context.curve_tool != nullptr) && m_context.curve_tool->is_editing(*curve)) {
        return make_error_content(fmt::format(
            "level_move_segment: a curve tool drag of '{}' is held; release it or cancel it (cancel_component_edit) first; nothing was changed", curve->get_name()
        ));
    }
    const std::size_t segment = args.at("segment").get<std::size_t>();
    const float       offset  = args.at("offset").get<float>();
    const erhe::math::Curve_path& before = curve->get_path();
    Segment_move move;
    const std::string refusal = move_curve_segment(before, segment, offset, move);
    if (!refusal.empty()) {
        return make_error_content("level_move_segment: " + refusal + "; nothing was changed");
    }
    erhe::math::Curve_path after = before;
    apply_segment_move(move, after);
    glm::vec3 normal{0.0f};
    static_cast<void>(get_segment_normal(before, segment, normal));
    const glm::mat4 world_from_node = curve->world_from_node();
    json reply = json::object();
    reply["scene_name"] = scene_root->get_name();
    reply["layout_id"]  = curve->get_id();
    reply["segment"]    = segment;
    reply["offset"]     = offset;
    reply["normal"]     = vec3_json(glm::normalize(glm::vec3{world_from_node * glm::vec4{normal, 0.0f}}));
    reply["moved_points"] = json::array({
        json{{"index", move.first_point},  {"position", vec3_json(move.first)}},
        json{{"index", move.second_point}, {"position", vec3_json(move.second)}}
    });
    const bool changed = (move.first != before.points[move.first_point].position) || (move.second != before.points[move.second_point].position);
    if (changed) {
        const std::shared_ptr<Curve_set_operation> operation = std::make_shared<Curve_set_operation>(curve, before, std::move(after), fmt::format("move segment {}", segment));
        m_context.operation_stack->execute_now(operation);
        if (operation->is_refused()) {
            return make_error_content("level_move_segment: refused: " + operation->get_error() + "; nothing was changed");
        }
    }
    reply["changed"] = changed;
    reply["flow"]    = make_flow_reply(serial);
    return make_json_content(reply).dump();
}

auto Mcp_server::query_level_layouts(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name"});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_layouts: unrecognized argument(s): " + unrecognized + " (accepted: scene_name)");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    scene_root->get_scene().for_each_node(
        [&nodes](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
            if (carries_kit_layout(*node)) {
                nodes.push_back(node);
            }
            return true;
        }
    );
    std::sort(
        nodes.begin(), nodes.end(),
        [](const std::shared_ptr<erhe::scene::Node>& lhs, const std::shared_ptr<erhe::scene::Node>& rhs) -> bool { return lhs->get_id() < rhs->get_id(); }
    );
    json layouts = json::array();
    for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
        layouts.push_back(layout_reply(*m_context.level_kits, *scene_root, *node, nullptr));
    }
    return make_json_content({
        {"scene_name", scene_root->get_name()},
        {"count",      layouts.size()},
        {"layouts",    layouts}
    }).dump();
}

auto Mcp_server::get_recording_serial() const -> std::size_t
{
    return (m_context.operation_stack != nullptr) ? m_context.operation_stack->get_recording_serial() : 0;
}

auto Mcp_server::make_flow_reply(const std::size_t recording_serial) const -> json
{
    if ((m_context.level_kits == nullptr) || (m_context.level_kits->get_last_flow_report_serial() <= recording_serial)) {
        return json(nullptr);
    }
    return flow_report_json(m_context.level_kits->get_last_flow_report());
}

auto Mcp_server::query_level_flow_preview(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_flow_preview: unrecognized argument(s): " + unrecognized + " (it takes none)");
    }
    if (m_context.level_kits == nullptr) {
        return make_error_content("level_get_flow_preview: no level kits");
    }
    const Kit_flow_preview_state& state = m_context.level_kits->get_flow_preview_state();
    json snap = json(nullptr);
    if (state.snap.has_value()) {
        snap = json{
            {"piece_id",         state.snap->piece_id},
            {"piece",            state.snap->piece},
            {"connector",        state.snap->connector},
            {"target_id",        state.snap->target_id},
            {"target",           state.snap->target},
            {"target_connector", state.snap->target_connector}
        };
    }
    return make_json_content({
        {"active",        state.active},
        {"steps",         state.steps},
        {"runs",          state.runs},
        {"held_steps",    state.held_steps},
        {"refused_steps", state.refused_steps},
        {"refusal",       state.refusal.empty() ? json(nullptr) : json(state.refusal)},
        {"snap",          snap}
    }).dump();
}

auto Mcp_server::query_level_flow_report(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {});
    if (!unrecognized.empty()) {
        return make_error_content("level_get_flow_report: unrecognized argument(s): " + unrecognized + " (it takes none)");
    }
    if (m_context.level_kits == nullptr) {
        return make_error_content("level_get_flow_report: no level kits");
    }
    const std::size_t serial = m_context.level_kits->get_last_flow_report_serial();
    return make_json_content({
        {"recording_serial", serial},
        {"latest",           serial == get_recording_serial()},
        {"flow",             (serial == 0) ? json(nullptr) : flow_report_json(m_context.level_kits->get_last_flow_report())}
    }).dump();
}

// A kit slot's item (doc/editor/level_tools.md "Kit slots"): shape, parameters as canonical text or an object over the
// scene's defaults, materials by slot name (palette names).
auto Mcp_server::read_kit_item_json(const json& object, const std::string& scene_name, Kit_item& out_item) -> std::string
{
    if (!object.is_object()) {
        return "kit_item must be an object {shape, parameters, materials}";
    }
    const std::string unrecognized = unrecognized_keys(object, {"shape", "parameters", "materials"});
    if (!unrecognized.empty()) {
        return "kit_item: unrecognized key(s): " + unrecognized + " (accepted: shape, parameters, materials)";
    }
    Level_kits&              level_kits = *m_context.level_kits;
    const Kit_shape_library& library    = level_kits.get_library();
    const std::string        shape_id   = object.value("shape", "");
    const Kit_shape_definition* const shape = library.find(shape_id);
    if (shape == nullptr) {
        return "kit_item: unknown shape '" + shape_id + "' (level_get_kits lists the shapes)";
    }
    Kit_parameter_values values;
    const json parameters = object.value("parameters", json());
    if (parameters.is_string()) {
        const std::string error = read_kit_piece_record(library, shape_id, parameters.get<std::string>(), values);
        if (!error.empty()) {
            return "kit_item: " + error;
        }
    } else {
        Scene_root* const scene_root = find_scene(scene_name);
        if (scene_root == nullptr) {
            return "kit_item: parameters as an object or left out start from the defaults at the lengths of scene_name; no scene '" + scene_name + "'";
        }
        values = level_kits.make_default_values(*scene_root, *shape);
        if (!parameters.is_null()) {
            const std::string error = apply_parameters_json(parameters, values);
            if (!error.empty()) {
                return "kit_item: " + error;
            }
        }
        const Kit_lengths lengths = level_kits.get_lengths(*scene_root);
        values.resolve_derived(lengths.module, lengths.wall_height);
        const std::string range_error = values.validate();
        if (!range_error.empty()) {
            return "kit_item: " + range_error;
        }
    }
    std::vector<std::string> slot_materials;
    level_kits.get_default_slot_materials(*shape, slot_materials);
    if (object.contains("materials")) {
        const json& materials = object.at("materials");
        if (!materials.is_object()) {
            return "kit_item: materials must be an object of slot name to kit palette material name";
        }
        for (const auto& [slot, value] : materials.items()) {
            const std::vector<std::string>::const_iterator i = std::find(shape->material_slots.begin(), shape->material_slots.end(), slot);
            if (i == shape->material_slots.end()) {
                return "kit_item: " + shape->id + " has no material slot '" + slot + "'";
            }
            if (!value.is_string() || (library.find_palette_material(value.get<std::string>()) == nullptr)) {
                return "kit_item: materials['" + slot + "'] must name a kit palette material (level_get_kits: palette), not " + value.dump();
            }
            slot_materials[static_cast<std::size_t>(i - shape->material_slots.begin())] = value.get<std::string>();
        }
    }
    out_item = Kit_item{values, slot_materials};
    return out_item.validate();
}

auto Mcp_server::kit_item_json(const Kit_item& item) const -> json
{
    const Kit_shape_definition* const shape = item.get_shape();
    if (shape == nullptr) {
        return json(nullptr);
    }
    json materials = json::object();
    const std::vector<std::string>& slot_materials = item.get_slot_materials();
    for (std::size_t i = 0, end = std::min(slot_materials.size(), shape->material_slots.size()); i < end; ++i) {
        materials[shape->material_slots[i]] = slot_materials[i];
    }
    return json{
        {"shape",          shape->id},
        {"display_name",   shape->display_name},
        {"parameter_text", item.get_values().to_canonical_text()},
        {"parameters",     values_json(item.get_values())},
        {"materials",      materials}
    };
}

auto Mcp_server::query_inventory_slots(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {});
    if (!unrecognized.empty()) {
        return make_error_content("get_inventory_slots: unrecognized argument(s): " + unrecognized + " (it takes none)");
    }
    if (m_context.inventory_window == nullptr) {
        return make_error_content("get_inventory_slots: Inventory window not available");
    }
    const auto slots_json = [this](const std::vector<Slot_entry>& slots) -> json {
        json result = json::array();
        for (std::size_t i = 0, end = slots.size(); i < end; ++i) {
            const Slot_entry&    slot   = slots[i];
            const Inventory_slot config = Inventory_window::make_config_slot(slot);
            const char* const    kind   =
                slot.is_kit_slot()                  ? "kit"        :
                !slot.brush.get_key().is_empty()    ? "brush"      :
                !slot.material.get_key().is_empty() ? "material"   :
                (slot.command != nullptr)           ? "operation"  :
                !slot.graph_node_type.empty()       ? "graph_node" :
                (slot.tool != nullptr)              ? "tool"       : "empty";
            json entry{
                {"index",     i},
                {"kind",      kind},
                {"tool",      get_mcp_tool_name(m_context, slot.tool)},
                {"tool_name", config.tool_name},
                {"kit_item",  kit_item_json(slot.kit_item)}
            };
            if (slot.is_kit_slot()) {
                entry["config"] = json{
                    {"kit_shape",      config.kit_shape},
                    {"kit_parameters", config.kit_parameters},
                    {"kit_materials",  config.kit_materials}
                };
            }
            result.push_back(entry);
        }
        return result;
    };
    const Inventory_window& inventory = *m_context.inventory_window;
    return make_json_content({
        {"grid",   slots_json(*inventory.get_slots(Slot_drag_payload::Section::grid))},
        {"hotbar", slots_json(*inventory.get_slots(Slot_drag_payload::Section::hotbar))}
    }).dump();
}

auto Mcp_server::read_inventory_section(const json& args, Slot_drag_payload::Section& out_section) -> std::string
{
    const json section = args.value("section", json("grid"));
    if (section == json("grid")) {
        out_section = Slot_drag_payload::Section::grid;
        return {};
    }
    if (section == json("hotbar")) {
        out_section = Slot_drag_payload::Section::hotbar;
        return {};
    }
    return "section must be 'grid' or 'hotbar', not " + section.dump();
}

auto Mcp_server::action_activate_inventory_slot(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"section", "slot_index"});
    if (!unrecognized.empty()) {
        return make_error_content("activate_inventory_slot: unrecognized argument(s): " + unrecognized + " (accepted: section, slot_index)");
    }
    if (m_context.inventory_window == nullptr) {
        return make_error_content("activate_inventory_slot: Inventory window not available");
    }
    Slot_drag_payload::Section section{Slot_drag_payload::Section::grid};
    const std::string section_error = read_inventory_section(args, section);
    if (!section_error.empty()) {
        return make_error_content("activate_inventory_slot: " + section_error);
    }
    if (!args.contains("slot_index") || !args.at("slot_index").is_number_integer()) {
        return make_error_content("activate_inventory_slot: missing required 'slot_index'");
    }
    const int         slot_index = args.at("slot_index").get<int>();
    const std::string error      = m_context.inventory_window->activate_slot(section, slot_index);
    if (!error.empty()) {
        return make_error_content("activate_inventory_slot: " + error);
    }
    return make_json_content({
        {"section",    (section == Slot_drag_payload::Section::hotbar) ? "hotbar" : "grid"},
        {"slot_index", slot_index},
        {"activated",  true}
    }).dump();
}

} // namespace editor
