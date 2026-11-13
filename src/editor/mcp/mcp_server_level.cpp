// Level kit MCP tools (doc/editor/level_tools.md "MCP").

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"
#include "app_context.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_placement.hpp"
#include "brushes/brush_tool.hpp"
#include "content_library/content_library.hpp"
#include "level/kit_piece.hpp"
#include "level/kit_shape_checks.hpp"
#include "level/level_kits.hpp"
#include "level/level_queries.hpp"
#include "prefabs/instance_structure.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_physics_system.hpp"
#include "scene/scene_root.hpp"
#include "tools/tools.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <geogram/mesh/mesh.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
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

[[nodiscard]] auto first_material_name(const erhe::scene::Node& node) -> std::string
{
    const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(&node);
    if (!mesh || mesh->get_primitives().empty() || !mesh->get_primitives().front().material) {
        return {};
    }
    return mesh->get_primitives().front().material->get_name();
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

} // anonymous namespace

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
        const Kit_palette_material*     palette  = library.find_palette_material(shape.material_slot);
        json default_derive = json::array();
        for (const Kit_parameter& parameter : shape.parameters) {
            if ((parameter.derive == Kit_derive::derivable) && (parameter.default_source == Kit_value_source::seed)) {
                default_derive.push_back(parameter.name);
            }
        }
        shapes.push_back({
            {"id",              shape.id},
            {"default_derive",  default_derive},
            {"kit",             shape.kit},
            {"name",            shape.display_name},
            {"kind",            shape.parameters.empty() ? "basic" : "parametric"},
            {"description",     shape.description},
            {"tags",            shape.tags},
            {"parameters",      parameters},
            {"default_size",    vec3_json(bounds.max - bounds.min)},
            {"default_bounds",  aabb_json(bounds)},
            {"material_slots",  json::array({json{{"slot", shape.material_slot}, {"default_material", (palette != nullptr) ? palette->name : std::string{}}}})},
            {"connectors",      json::array()},
            {"normal_style",    c_str(shape.normal_style)},
            {"triangle_budget", shape.triangle_budget},
            {"collision",       c_str(shape.collision)}
        });
    }
    json palette = json::array();
    for (const Kit_palette_material& material : library.get_palette()) {
        palette.push_back({{"slot", material.slot}, {"name", material.name}, {"base_color", vec3_json(material.base_color)}, {"roughness", material.roughness}});
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

auto Mcp_server::action_level_place_pieces(const json& args) -> std::string
{
    const std::vector<std::string_view> top_keys{"scene_name", "snap", "surface_snap", "surface_orient", "surface_direction", "pieces"};
    const std::vector<std::string_view> piece_keys{
        "shape", "parameters", "position", "rotation_xyzw", "name", "material", "parent_id", "parent_index", "surface_snap", "surface_orient"
    };
    {
        const std::string unrecognized = unrecognized_keys(args, top_keys);
        if (!unrecognized.empty()) {
            return make_error_content(
                "level_place_pieces: unrecognized argument(s): " + unrecognized +
                " (accepted: scene_name, snap, surface_snap, surface_orient, surface_direction, pieces)"
            );
        }
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const json snap_value = args.value("snap", json("none"));
    Kit_snap snap = Kit_snap::none;
    if (snap_value == json("module")) {
        snap = Kit_snap::module;
    } else if (snap_value != json("none")) {
        return make_error_content("level_place_pieces: snap is " + snap_value.dump() + " (none, module)");
    }
    std::string surface_error{};
    const bool      surface_snap      = read_bool(args, "surface_snap",   surface_error).value_or(false);
    const bool      surface_orient    = read_bool(args, "surface_orient", surface_error).value_or(false);
    const glm::vec3 surface_direction = read_direction(args, "surface_direction", surface_error);
    if (!surface_error.empty()) {
        return make_error_content("level_place_pieces: " + surface_error);
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
                " (accepted: shape, parameters, position, rotation_xyzw, name, material, parent_id, parent_index, surface_snap, surface_orient)" + tail
            );
        }
        Kit_placement placement;
        {
            std::string piece_surface_error{};
            placement.surface = Surface_options{
                .snap      = (read_bool(piece, "surface_snap",   piece_surface_error).value_or(surface_snap))   ? Surface_snap::on   : Surface_snap::off,
                .orient    = (read_bool(piece, "surface_orient", piece_surface_error).value_or(surface_orient)) ? Surface_orient::on : Surface_orient::off,
                .direction = surface_direction
            };
            if (!piece_surface_error.empty()) {
                return make_error_content(prefix + piece_surface_error + tail);
            }
        }
        const std::string shape_id = piece.value("shape", "");
        placement.shape = level_kits.get_library().find(shape_id);
        if (placement.shape == nullptr) {
            return make_error_content(prefix + "unknown shape '" + shape_id + "' (level_get_kits lists the shapes)" + tail);
        }
        Kit_parameter_values values = level_kits.make_default_values(*scene_root, *placement.shape);
        if (piece.contains("parameters")) {
            const std::string error = apply_parameters_json(piece.at("parameters"), values);
            if (!error.empty()) {
                return make_error_content(prefix + error + tail);
            }
        }
        {
            // The masked values follow the piece's seed and mask (D1.8).
            const Kit_lengths lengths = level_kits.get_lengths(*scene_root);
            values.resolve_derived(lengths.module, lengths.wall_height);
        }
        const std::string range_error = values.validate();
        if (!range_error.empty()) {
            return make_error_content(prefix + range_error + tail);
        }
        placement.values = values;
        if (piece.contains("position")) {
            float xyz[3]{};
            if (!read_finite_numbers(piece.at("position"), 3, xyz)) {
                return make_error_content(prefix + "position must be an array of 3 finite numbers" + tail);
            }
            placement.position = glm::vec3{xyz[0], xyz[1], xyz[2]};
        }
        if (piece.contains("rotation_xyzw")) {
            float xyzw[4]{};
            if (!read_finite_numbers(piece.at("rotation_xyzw"), 4, xyzw)) {
                return make_error_content(prefix + "rotation_xyzw must be an array of 4 numbers" + tail);
            }
            std::string rotation_error{};
            const std::optional<glm::quat> rotation = make_unit_quaternion(xyzw, "rotation_xyzw", rotation_error);
            if (!rotation.has_value()) {
                return make_error_content(prefix + rotation_error + tail);
            }
            placement.rotation = rotation.value();
        }
        if (piece.contains("name")) {
            if (!piece.at("name").is_string()) {
                return make_error_content(prefix + "name must be a string" + tail);
            }
            placement.name = piece.at("name").get<std::string>();
        }
        if (piece.contains("material")) {
            const json& material_name = piece.at("material");
            if (!material_name.is_string()) {
                return make_error_content(prefix + "material must be a material name" + tail);
            }
            placement.material = find_scene_material(*scene_root, material_name.get<std::string>());
            if (!placement.material) {
                return make_error_content(prefix + "no material '" + material_name.get<std::string>() + "' in the scene" + tail);
            }
        }
        if (piece.contains("parent_id") && piece.contains("parent_index")) {
            return make_error_content(prefix + "give parent_id or parent_index, not both" + tail);
        }
        if (piece.contains("parent_index")) {
            const json& parent_index = piece.at("parent_index");
            if (!parent_index.is_number_unsigned() || (parent_index.get<std::size_t>() >= i)) {
                return make_error_content(prefix + "parent_index " + parent_index.dump() + " must name an earlier piece of this call" + tail);
            }
            placement.parent_index = parent_index.get<std::size_t>();
        }
        if (piece.contains("parent_id")) {
            if (!piece.at("parent_id").is_number_unsigned()) {
                return make_error_content(prefix + "parent_id must be a node id" + tail);
            }
            std::string error{};
            placement.parent = find_unique_prim_in_scene(*scene_root, piece, "parent_id", "parent_name", "Parent", Absent_prim::error, error);
            if (!placement.parent) {
                return make_error_content(prefix + error + tail);
            }
            const std::optional<std::string> refusal = instance_child_refusal(*placement.parent);
            if (refusal.has_value()) {
                return make_error_content(prefix + refusal.value() + tail);
            }
        }
        placements.push_back(std::move(placement));
    }

    const Kit_place_result result = level_kits.place(*scene_root, placements, snap);
    if (!result.error.empty()) {
        return make_error_content("level_place_pieces pieces[" + std::to_string(result.error_index) + "]: " + result.error + "; nothing was placed");
    }

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
            {"material",       placed.material ? placed.material->get_name() : std::string{}},
            {"brush_id",       placed.brush->get_id()},
            {"mesh_key",       Level_kits::get_mesh_key(placed.values)}
        });
    }
    const Kit_lengths lengths = level_kits.get_lengths(*scene_root);
    return make_json_content({
        {"scene_name", scene_root->get_name()},
        {"snap",       (snap == Kit_snap::module) ? "module" : "none"},
        {"surface_snap",      surface_snap},
        {"surface_orient",    surface_orient},
        {"surface_direction", vec3_json(surface_direction)},
        {"level",      {{"module", lengths.module}, {"wall_height", lengths.wall_height}}},
        {"count",      pieces.size()},
        {"pieces",     pieces}
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
            {"material",        first_material_name(*node)},
            {"follows_kit",     check.follows_kit},
            {"problem",         check.problem}
        };
        if (shape != nullptr) {
            Kit_parameter_values values{*shape};
            const std::string error = parse_canonical_text(*shape, record.parameters, values);
            if (error.empty()) {
                piece["parameters"] = values_json(values);
                piece["mesh_key"]   = Level_kits::get_mesh_key(values);
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
            piece["shares_brush_primitive"] =
                (mesh != nullptr) && (scaled != nullptr) && !mesh->get_primitives().empty() &&
                (mesh->get_primitives().front().primitive == scaled->primitive);
        }
        const std::optional<Node_physics_data> physics = read_node_physics(*node);
        const std::shared_ptr<erhe::physics::ICollision_shape> collision_shape = get_node_collision_shape(*node);
        piece["physics"] = {
            {"motion_mode",     physics.has_value() ? motion_mode_to_string(physics.value().motion_mode) : std::string{"none"}},
            {"collision_shape", collision_shape ? collision_shape->describe() : std::string{}},
            {"has_body",        get_node_rigid_body(*node) != nullptr}
        };
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
        {"triangle_count", brush_triangle_count(*result.brush)}
    }).dump();
}

auto Mcp_server::action_level_snap_to_surface(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"scene_name", "node_ids", "orient", "direction", "query_only"});
    if (!unrecognized.empty()) {
        return make_error_content(
            "level_snap_to_surface: unrecognized argument(s): " + unrecognized + " (accepted: scene_name, node_ids, orient, direction, query_only)"
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
    const glm::vec3 direction  = read_direction(args, "direction", argument_error);
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
        meshes.push_back(mesh);
        excluded.push_back(mesh.get());
    }

    // The moved nodes are no surfaces for each other (D7.3).
    const Surface_options options{
        .snap      = Surface_snap::on,
        .orient    = orient ? Surface_orient::on : Surface_orient::off,
        .direction = direction
    };
    std::vector<Kit_snap_result> snapped;
    const std::size_t moved_count = m_context.level_kits->snap_to_surface(
        *scene_root, meshes, options, query_only ? Kit_snap_apply::query_only : Kit_snap_apply::move, snapped
    );
    json results = json::array();
    for (const Kit_snap_result& result : snapped) {
        results.push_back({
            {"id",            result.mesh->get_id()},
            {"name",          result.mesh->get_name()},
            {"position",      vec3_json(result.placement.pose.position)},
            {"rotation_xyzw", quat_json(result.placement.pose.rotation)},
            {"distance",      result.placement.distance},
            {"surface_found", result.placement.surface_found}
        });
    }
    return make_json_content({
        {"scene_name",  scene_root->get_name()},
        {"orient",      orient},
        {"direction",   vec3_json(direction)},
        {"query_only",  query_only},
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
        {"yaw_steps",             brush_tool->get_kit_yaw_steps()}
    };
    const std::optional<glm::vec3> hover = brush_tool->get_hover_world_position();
    result["hover_position"] = hover.has_value() ? vec3_json(hover.value()) : json(nullptr);
    const Kit_shape_definition* const shape = brush_tool->get_kit_shape();
    if (shape == nullptr) {
        result["visible"] = false;
        return make_json_content(result).dump();
    }
    const Kit_parameter_values&   values = brush_tool->get_kit_values();
    const std::shared_ptr<Brush>& brush  = brush_tool->get_kit_brush();
    const erhe::math::Aabb        bounds = brush->get_bounding_box();
    result["shape"]      = shape->id;
    result["parameters"] = values_json(values);
    result["mesh_key"]   = Level_kits::get_mesh_key(values);
    result["brush_id"]   = brush->get_id();
    result["bounds"]     = aabb_json(bounds);

    const std::optional<Kit_placement_pose> pose = brush_tool->get_kit_pose();
    result["visible"] = pose.has_value();
    if (pose.has_value()) {
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
        result["position"]      = vec3_json(position);
        result["rotation_xyzw"] = quat_json(rotation);
        result["surface_found"] = pose.value().surface_found;
        result["world_aabb"]    = aabb_json(world_bounds);
    }
    return make_json_content(result).dump();
}

namespace {

[[nodiscard]] auto surface_settings_json(const Surface_options& options) -> json
{
    return json{
        {"snap",   options.snap   == Surface_snap::on},
        {"orient", options.orient == Surface_orient::on}
    };
}

} // anonymous namespace

auto Mcp_server::query_surface_snap_settings(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {});
    if (!unrecognized.empty()) {
        return make_error_content("get_surface_snap_settings: unrecognized argument(s): " + unrecognized + " (accepts none)");
    }
    return make_json_content(surface_settings_json(m_context.level_kits->get_surface_settings())).dump();
}

auto Mcp_server::action_set_surface_snap_settings(const json& args) -> std::string
{
    const std::string unrecognized = unrecognized_keys(args, {"snap", "orient"});
    if (!unrecognized.empty()) {
        return make_error_content("set_surface_snap_settings: unrecognized argument(s): " + unrecognized + " (accepted: snap, orient)");
    }
    for (const char* key : {"snap", "orient"}) {
        if (args.contains(key) && !args.at(key).is_boolean()) {
            return make_error_content(std::string{"set_surface_snap_settings: "} + key + " must be a boolean, not " + args.at(key).dump());
        }
    }
    Level_kits& level_kits = *m_context.level_kits;
    if (args.contains("snap")) {
        level_kits.set_surface_snap(args.at("snap").get<bool>() ? Surface_snap::on : Surface_snap::off);
    }
    if (args.contains("orient")) {
        level_kits.set_surface_orient(args.at("orient").get<bool>() ? Surface_orient::on : Surface_orient::off);
    }
    return make_json_content(surface_settings_json(level_kits.get_surface_settings())).dump();
}

} // namespace editor
