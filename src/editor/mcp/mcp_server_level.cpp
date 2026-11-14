// Level kit MCP tools (doc/editor/level_tools.md "MCP").

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"
#include "app_context.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_placement.hpp"
#include "brushes/brush_tool.hpp"
#include "content_library/content_library.hpp"
#include "level/kit_layout.hpp"
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
#include "erhe_math/curve_path.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/curve.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform.hpp"

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

[[nodiscard]] auto mirror_planes_json(const std::vector<Kit_mirror_plane>& planes) -> json
{
    json result = json::array();
    for (const Kit_mirror_plane plane : planes) {
        result.push_back(c_str(plane));
    }
    return result;
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
        json material_slots = json::array();
        for (const std::string& slot : shape.material_slots) {
            const Kit_slot_palette* const slot_palette = library.find_slot_palette(slot);
            material_slots.push_back({
                {"slot",             slot},
                {"default_material", (slot_palette != nullptr) ? slot_palette->default_material : std::string{}},
                {"alternatives",     (slot_palette != nullptr) ? json(slot_palette->alternatives) : json::array()}
            });
        }
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
            {"material_slots",  material_slots},
            {"connectors",      json::array()},
            {"normal_style",    c_str(shape.normal_style)},
            {"triangle_budget", shape.triangle_budget},
            {"collision",       c_str(shape.collision)},
            {"mirror_planes",   mirror_planes_json(shape.mirror_planes)}
        });
    }
    json palette = json::array();
    for (const Kit_palette_material& material : library.get_palette()) {
        palette.push_back({{"name", material.name}, {"base_color", vec3_json(material.base_color)}, {"roughness", material.roughness}});
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
    return surface_error;
}

// A piece's own surface_snap and surface_orient over the call's.
[[nodiscard]] auto read_piece_surface(const json& piece, const Placement_options& options, Kit_placement& placement) -> std::string
{
    std::string error{};
    placement.surface = Surface_options{
        .snap      = (read_bool(piece, "surface_snap",   error).value_or(options.surface_snap))   ? Surface_snap::on   : Surface_snap::off,
        .orient    = (read_bool(piece, "surface_orient", error).value_or(options.surface_orient)) ? Surface_orient::on : Surface_orient::off,
        .direction = options.surface_direction
    };
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
            {"mesh_key",       Level_kits::get_mesh_key(placed.values)}
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
        {"level",             {{"module", lengths.module}, {"wall_height", lengths.wall_height}}},
        {"count",             pieces.size()},
        {"pieces",            pieces}
    };
}

} // anonymous namespace

auto Mcp_server::action_level_place_pieces(const json& args) -> std::string
{
    const std::vector<std::string_view> top_keys{"scene_name", "snap", "surface_snap", "surface_orient", "surface_direction", "pieces"};
    const std::vector<std::string_view> piece_keys{
        "shape", "parameters", "position", "rotation_xyzw", "name", "materials", "parent_id", "parent_index", "surface_snap", "surface_orient"
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
    Placement_options options;
    {
        const std::string error = read_placement_options(args, options);
        if (!error.empty()) {
            return make_error_content("level_place_pieces: " + error);
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
                " (accepted: shape, parameters, position, rotation_xyzw, name, materials, parent_id, parent_index, surface_snap, surface_orient)" + tail
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
        if (piece.contains("parent_index")) {
            const json& parent_index = piece.at("parent_index");
            if (!parent_index.is_number_unsigned() || (parent_index.get<std::size_t>() >= i)) {
                return make_error_content(prefix + "parent_index " + parent_index.dump() + " must name an earlier piece of this call" + tail);
            }
            placement.parent_index = parent_index.get<std::size_t>();
        }
        placements.push_back(std::move(placement));
    }

    const Kit_place_result result = level_kits.place(*scene_root, placements, options.snap);
    if (!result.error.empty()) {
        return make_error_content("level_place_pieces pieces[" + std::to_string(result.error_index) + "]: " + result.error + "; nothing was placed");
    }
    return make_json_content(placement_reply(level_kits, *scene_root, options, result)).dump();
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
        {"triangle_count", brush_triangle_count(*result.brush)},
        {"materials",      piece_materials_json(*node, shape)}
    }).dump();
}

auto Mcp_server::action_level_set_piece_material(const json& args) -> std::string
{
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
        {"materials",         piece_materials_json(*node, shape)}
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
        const std::optional<std::string> refusal = locked_layout_refusal(*mesh);
        if (refusal.has_value() && !query_only) {
            return make_error_content("level_snap_to_surface: " + refusal.value() + "; nothing was moved");
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
    json materials = json::object();
    const std::vector<std::string>& slot_materials = brush_tool->get_kit_slot_materials();
    for (std::size_t i = 0, end = std::min(slot_materials.size(), shape->material_slots.size()); i < end; ++i) {
        materials[shape->material_slots[i]] = slot_materials[i];
    }
    result["materials"]  = materials;

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
        {"orientation", c_str(pattern.orientation)}
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

// The given keys over the pattern; "kind", "count_mode", "facing" and "orientation" by name.
[[nodiscard]] auto read_layout_pattern_json(const json& object, Kit_layout_pattern& pattern) -> std::string
{
    if (!object.is_object()) {
        return "pattern must be an object";
    }
    const std::string unrecognized = unrecognized_keys(
        object,
        {"kind", "count_mode", "count", "gap", "direction", "step", "length", "count_x", "count_z", "spacing_x", "spacing_z", "radius", "arc_degrees", "facing", "orientation"}
    );
    if (!unrecognized.empty()) {
        return "pattern: unrecognized key(s): " + unrecognized +
            " (accepted: kind, count_mode, count, gap, direction, step, length, count_x, count_z, spacing_x, spacing_z, radius, arc_degrees, facing, orientation;"
            " a path layout is its own curve node: level_create_layout takes points and closed, or curve_id of a plain curve)";
    }
    if (object.contains("kind")) {
        const json& kind = object.at("kind");
        bool found = false;
        for (const Repeat_pattern_kind candidate : {Repeat_pattern_kind::line, Repeat_pattern_kind::grid, Repeat_pattern_kind::ring, Repeat_pattern_kind::path}) {
            if (kind == json(c_str(candidate))) {
                pattern.kind = candidate;
                found = true;
            }
        }
        if (!found) {
            return "pattern: kind is " + kind.dump() + " (line, grid, ring, path; a polyline is a path along a curve of corner points)";
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
        !read_number(object, "arc_degrees", pattern.arc_degrees, error)
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

// items: [{shape, parameters, materials}], each read as a level_place_pieces piece reads them.
[[nodiscard]] auto read_layout_items_json(Level_kits& level_kits, Scene_root& scene_root, const json& items_json, std::vector<Kit_layout_item>& out_items) -> std::string
{
    if (!items_json.is_array() || items_json.empty()) {
        return "items must be a non-empty array of {shape, parameters, materials}";
    }
    out_items.clear();
    for (std::size_t i = 0, end = items_json.size(); i < end; ++i) {
        const json& item_json = items_json[i];
        const std::string prefix = "items[" + std::to_string(i) + "]: ";
        if (!item_json.is_object()) {
            return prefix + "an item must be an object";
        }
        const std::string unrecognized = unrecognized_keys(item_json, {"shape", "parameters", "materials"});
        if (!unrecognized.empty()) {
            return prefix + "unrecognized key(s): " + unrecognized + " (accepted: shape, parameters, materials)";
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
    const std::optional<bool> random_seeds   = read_bool(args, "random_seeds",   error);
    if (!error.empty()) {
        return error;
    }
    if (surface_snap.has_value())   { data.surface_snap   = surface_snap.value()   ? Surface_snap::on              : Surface_snap::off; }
    if (surface_orient.has_value()) { data.surface_orient = surface_orient.value() ? Surface_orient::on            : Surface_orient::off; }
    if (random_seeds.has_value())   { data.seeds          = random_seeds.value()   ? Kit_layout_seeds::per_element : Kit_layout_seeds::item; }
    if (args.contains("seed")) {
        const json& seed = args.at("seed");
        if (!seed.is_number_integer() || (seed.get<long long>() < 0) || (seed.get<long long>() > kit_seed_max)) {
            return "seed is " + seed.dump() + ", it must be an integer in 0 - " + std::to_string(kit_seed_max);
        }
        data.seed = seed.get<int>();
    }
    return {};
}

[[nodiscard]] auto layout_items_reply(Level_kits& level_kits, Scene_root& scene_root, const std::vector<Kit_layout_item>& items) -> json
{
    json result = json::array();
    for (const Kit_layout_item& item : items) {
        json entry{{"shape", item.shape}, {"parameters_text", item.parameters}};
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
    reply["random_seeds"]   = (data.seeds == Kit_layout_seeds::per_element);
    reply["seed"]           = data.seed;
    // The run the values make now, from the items' extents.
    float extent = 0.0f;
    for (const Kit_layout_item& item : data.items) {
        const erhe::math::Aabb bounds = level_kits.get_layout_item_bounds(scene_root, item);
        if (bounds.is_valid_3d()) {
            extent = std::max(extent, bounds.max.x - bounds.min.x);
        }
    }
    Kit_layout_run    run;
    const std::string run_error = level_kits.resolve_layout_run(node, data, extent, run);
    reply["run"] = run_error.empty()
        ? json{{"count", run.count}, {"step", run.step}, {"space", run.space}, {"item_extent", extent}}
        : json{{"problem", run_error}, {"item_extent", extent}};
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
            {"rotation_xyzw", quat_json(child_world.get_rotation())}
        };
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
        args, {"scene_name", "name", "parent_id", "position", "rotation_xyzw", "pattern", "items", "from_piece_id", "curve_id", "points", "closed", "snap", "surface_snap", "surface_orient", "random_seeds", "seed", "locked"}
    );
    if (!unrecognized.empty()) {
        return make_error_content(
            "level_create_layout: unrecognized argument(s): " + unrecognized +
            " (accepted: scene_name, name, parent_id, position, rotation_xyzw, pattern, items, from_piece_id, curve_id, points, closed, snap, surface_snap, surface_orient, random_seeds, seed, locked)"
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
    } else {
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
        if (create.data.pattern.kind != Repeat_pattern_kind::path) {
            error = std::string{"points and closed are a path layout's curve; a "} + c_str(create.data.pattern.kind) + " layout is an Xform node";
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
                .seeds          = create.data.seeds,
                .seed           = create.data.seed,
                .lock           = create.data.lock,
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
    const std::string tail = "; nothing was changed";
    const std::string unrecognized = unrecognized_keys(
        args, {"scene_name", "layout_id", "pattern", "items", "snap", "surface_snap", "surface_orient", "random_seeds", "seed"}
    );
    if (!unrecognized.empty()) {
        return make_error_content(
            "level_set_layout: unrecognized argument(s): " + unrecognized +
            " (accepted: scene_name, layout_id, pattern, items, snap, surface_snap, surface_orient, random_seeds, seed; the lock: level_set_layout_lock)"
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

} // namespace editor
