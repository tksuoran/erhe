// Definitions of the helpers declared in mcp_server_shared.hpp, shared by the
// Mcp_server translation units (mcp_server*.cpp).

#include "mcp/mcp_server_shared.hpp"

#include "editor_log.hpp"
#include "prefabs/instance_structure.hpp"
#include "scene/collision_shape_from_mesh.hpp"
#include "scene/scene_root.hpp"
#include "tools/mesh_component_selection.hpp"
#include "transform/transform_tool_settings.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_physics/physics_material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <glm/glm.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#   include <process.h> // _getpid
#else
#   include <sys/stat.h>
#   include <unistd.h>  // getpid, getuid
#endif

namespace editor {

namespace mcp_server_detail {

const char* const c_mcp_build_timestamp = __DATE__ " " __TIME__;

auto get_process_id() -> long
{
#if defined(_WIN32)
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

auto make_jsonrpc_response(const json& id, const json& result) -> std::string
{
    json response = {
        {"jsonrpc", "2.0"},
        {"id",      id},
        {"result",  result}
    };
    return response.dump();
}

auto make_jsonrpc_error(const json& id, int code, const std::string& message) -> std::string
{
    json response = {
        {"jsonrpc", "2.0"},
        {"id",      id},
        {"error", {
            {"code",    code},
            {"message", message}
        }}
    };
    return response.dump();
}

auto make_text_content(const std::string& text) -> json
{
    return {
        {"content", {{
            {"type", "text"},
            {"text", text}
        }}}
    };
}

auto make_json_content(const json& data) -> json
{
    return {
        {"content", {{
            {"type", "text"},
            {"text", data.dump(2)}
        }}}
    };
}

auto make_error_content(const std::string& message) -> std::string
{
    json r = make_text_content(message);
    r["isError"] = true;
    return r.dump();
}

auto get_vec3(const json& args, const char* key, const glm::vec3 fallback) -> glm::vec3
{
    const json value = args.value(key, json{});
    if (value.is_array() && (value.size() >= 3)) {
        return glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
    }
    return fallback;
}

auto find_node_in_scene(Scene_root& scene_root, const json& args, const char* id_key, const char* name_key) -> std::shared_ptr<erhe::scene::Node>
{
    const std::size_t node_id   = args.value(id_key, std::size_t{0});
    const std::string node_name = args.value(name_key, "");
    if ((node_id == 0) && node_name.empty()) {
        return {};
    }
    std::shared_ptr<erhe::scene::Node> found;
    scene_root.get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) {
        if ((node_id != 0) ? (node->get_id() == node_id) : (node->get_name() == node_name)) {
            found = node;
            return false;
        }
        return true;
    });
    return found;
}

auto find_prim_in_scene(Scene_root& scene_root, const json& args, const char* id_key, const char* name_key) -> std::shared_ptr<erhe::Hierarchy>
{
    const std::size_t prim_id   = args.value(id_key, std::size_t{0});
    const std::string prim_name = args.value(name_key, "");
    if ((prim_id == 0) && prim_name.empty()) {
        return {};
    }
    const std::shared_ptr<erhe::scene::Node> root_node = scene_root.get_scene().get_root_node();
    if (!root_node) {
        return {};
    }
    if ((prim_id == 0) && (prim_name.find('/') != std::string::npos)) {
        erhe::Hierarchy* const prim = erhe::find_by_path(*root_node, prim_name);
        return (prim != nullptr)
            ? std::static_pointer_cast<erhe::Hierarchy>(prim->shared_from_this())
            : std::shared_ptr<erhe::Hierarchy>{};
    }
    std::shared_ptr<erhe::Hierarchy> found;
    std::function<void(const std::shared_ptr<erhe::Hierarchy>&)> visit =
        [&](const std::shared_ptr<erhe::Hierarchy>& prim) {
            if (found) {
                return;
            }
            if ((prim_id != 0) ? (prim->get_id() == prim_id) : (prim->get_name() == prim_name)) {
                found = prim;
                return;
            }
            for (const std::shared_ptr<erhe::Hierarchy>& child : prim->get_children()) {
                visit(child);
            }
        };
    visit(std::static_pointer_cast<erhe::Hierarchy>(root_node));
    return found;
}

auto find_unique_prim_in_scene(
    Scene_root&       scene_root,
    const json&       args,
    const char* const id_key,
    const char* const name_key,
    const char* const role,
    const Absent_prim absent,
    std::string&      out_error
) -> std::shared_ptr<erhe::Hierarchy>
{
    out_error.clear();
    const std::shared_ptr<erhe::scene::Node> root_node = scene_root.get_scene().get_root_node();
    if (!root_node) {
        out_error = "Scene has no root node";
        return {};
    }
    const bool has_id   = args.contains(id_key) && args.at(id_key).is_number_integer();
    const bool has_name = args.contains(name_key) && args.at(name_key).is_string() && !args.at(name_key).get<std::string>().empty();
    const std::size_t prim_id = has_id ? args.at(id_key).get<std::size_t>() : std::size_t{0};
    if (!has_name && (prim_id == 0)) {
        if (absent == Absent_prim::scene_root) {
            return std::static_pointer_cast<erhe::Hierarchy>(root_node);
        }
        if (absent == Absent_prim::none) {
            return {};
        }
        out_error = std::string{"'"} + id_key + "' or '" + name_key + "' is required";
        return {};
    }

    if (prim_id != 0) {
        const std::shared_ptr<erhe::Hierarchy> prim = find_prim_in_scene(scene_root, args, id_key, name_key);
        if (!prim) {
            out_error = std::string{role} + " not found: id " + std::to_string(prim_id);
        }
        return prim;
    }

    const std::string name = args.at(name_key).get<std::string>();
    if (name.find('/') != std::string::npos) {
        // A leading '/' (the USD spelling) addresses a child of the root by
        // path as well: '/cube' is the prim 'cube' directly below the root.
        const std::string_view path = (name.front() == '/') ? std::string_view{name}.substr(1) : std::string_view{name};
        erhe::Hierarchy* const prim = erhe::find_by_path(*root_node, path);
        if (prim == nullptr) {
            out_error = std::string{role} + " not found: path " + name;
            return {};
        }
        return std::static_pointer_cast<erhe::Hierarchy>(prim->shared_from_this());
    }

    std::shared_ptr<erhe::Hierarchy> found{};
    std::size_t                      match_count{0};
    std::function<void(const std::shared_ptr<erhe::Hierarchy>&)> visit =
        [&](const std::shared_ptr<erhe::Hierarchy>& prim) {
            if (prim->get_name() == name) {
                if (!found) {
                    found = prim;
                }
                ++match_count;
            }
            for (const std::shared_ptr<erhe::Hierarchy>& child : prim->get_children()) {
                visit(child);
            }
        };
    for (const std::shared_ptr<erhe::Hierarchy>& child : root_node->get_children()) {
        visit(child);
    }
    if (!found) {
        out_error = std::string{role} + " not found: " + name;
        return {};
    }
    if (match_count > 1) {
        out_error = "Name '" + name + "' matches " + std::to_string(match_count) + " prims; address the " + role + " by id or by path";
        return {};
    }
    return found;
}

auto prim_move_refusal(const erhe::Hierarchy& prim, const erhe::Hierarchy& new_parent) -> std::optional<std::string>
{
    if (!prim.get_parent().lock()) {
        return std::string{"'"} + prim.get_name() + "' is the scene root and has no parent to change";
    }
    if ((&new_parent == &prim) || new_parent.is_ancestor(&prim)) {
        return "'" + new_parent.get_name() + "' is '" + prim.get_name() + "' or inside it";
    }
    const std::optional<std::string> item_refusal = instance_structure_refusal(prim);
    if (item_refusal.has_value()) {
        return item_refusal;
    }
    return instance_child_refusal(new_parent);
}

auto find_resource_parent(Scene_root& scene_root, const json& args, std::shared_ptr<erhe::Hierarchy>& out_parent) -> std::optional<std::string>
{
    std::string error{};
    out_parent = find_unique_prim_in_scene(scene_root, args, "parent_id", "parent_name", "Parent", Absent_prim::none, error);
    if (!error.empty()) {
        return error;
    }
    if (out_parent) {
        return instance_child_refusal(*out_parent);
    }
    return {};
}

auto find_light_in_scene(Scene_root& scene_root, const json& args, const char* id_key, const char* name_key) -> std::shared_ptr<erhe::scene::Light>
{
    const std::size_t light_id   = args.value(id_key, std::size_t{0});
    const std::string light_name = args.value(name_key, "");
    if ((light_id == 0) && light_name.empty()) {
        return {};
    }
    for (const auto& light_layer : scene_root.get_scene().get_light_layers()) {
        for (const std::shared_ptr<erhe::scene::Light>& light : light_layer->lights) {
            if ((light_id != 0) ? (light->get_id() == light_id) : (light->get_name() == light_name)) {
                return light;
            }
        }
    }
    return {};
}

auto parse_light_type(const std::string& type, const erhe::scene::Light_type fallback) -> erhe::scene::Light_type
{
    if (type == "directional") { return erhe::scene::Light_type::directional; }
    if (type == "point")       { return erhe::scene::Light_type::point; }
    if (type == "spot")        { return erhe::scene::Light_type::spot; }
    return fallback;
}

auto parse_axis(const std::string& axis) -> erhe::physics::Axis
{
    if (axis == "x") { return erhe::physics::Axis::X; }
    if (axis == "z") { return erhe::physics::Axis::Z; }
    return erhe::physics::Axis::Y;
}

auto parse_motion_mode(const std::string& motion_mode, const erhe::physics::Motion_mode fallback) -> erhe::physics::Motion_mode
{
    if (motion_mode == "static")                 { return erhe::physics::Motion_mode::e_static; }
    if (motion_mode == "kinematic")              { return erhe::physics::Motion_mode::e_kinematic_physical; }
    if (motion_mode == "kinematic_physical")     { return erhe::physics::Motion_mode::e_kinematic_physical; }
    if (motion_mode == "kinematic_non_physical") { return erhe::physics::Motion_mode::e_kinematic_non_physical; }
    if (motion_mode == "dynamic")                { return erhe::physics::Motion_mode::e_dynamic; }
    return fallback;
}

auto parse_mesh_component_mode(const std::string& mode, const Mesh_component_mode fallback) -> Mesh_component_mode
{
    if (mode == "object") { return Mesh_component_mode::object; }
    if (mode == "vertex") { return Mesh_component_mode::vertex; }
    if (mode == "edge")   { return Mesh_component_mode::edge;   }
    if (mode == "face")   { return Mesh_component_mode::face;   }
    if (mode == "bone")   { return Mesh_component_mode::bone;   }
    return fallback;
}

auto mesh_component_mode_lc(const Mesh_component_mode mode) -> const char*
{
    switch (mode) {
        case Mesh_component_mode::vertex: return "vertex";
        case Mesh_component_mode::edge:   return "edge";
        case Mesh_component_mode::face:   return "face";
        case Mesh_component_mode::bone:   return "bone";
        case Mesh_component_mode::object:
        default:                          return "object";
    }
}

auto is_valid_mesh_component_mode(const std::string& mode) -> bool
{
    return (mode == "object") || (mode == "vertex") || (mode == "edge") || (mode == "face") || (mode == "bone");
}

auto transform_reference_mode_lc(const Transform_reference_mode mode) -> const char*
{
    switch (mode) {
        case Transform_reference_mode::local:     return "local";
        case Transform_reference_mode::reference: return "reference";
        case Transform_reference_mode::selection: return "selection";
        case Transform_reference_mode::global:
        default:                                  return "global";
    }
}

auto resolve_mesh_geometry(
    const std::shared_ptr<erhe::scene::Node>&  node,
    const std::size_t                          primitive_index,
    std::shared_ptr<erhe::scene::Mesh>&        out_mesh,
    std::shared_ptr<erhe::geometry::Geometry>& out_geometry
) -> bool
{
    out_mesh = erhe::scene::get_mesh(node.get());
    if (!out_mesh) {
        return false;
    }
    const std::vector<erhe::scene::Mesh_primitive>& primitives = out_mesh->get_primitives();
    if (primitive_index >= primitives.size()) {
        return false;
    }
    const erhe::scene::Mesh_primitive& prim = primitives[primitive_index];
    if (!prim.primitive || !prim.primitive->render_shape) {
        return false;
    }
    out_geometry = prim.primitive->render_shape->get_geometry();
    return static_cast<bool>(out_geometry);
}

auto motion_mode_to_string(const erhe::physics::Motion_mode motion_mode) -> const char*
{
    switch (motion_mode) {
        case erhe::physics::Motion_mode::e_static:                 return "static";
        case erhe::physics::Motion_mode::e_kinematic_non_physical: return "kinematic_non_physical";
        case erhe::physics::Motion_mode::e_kinematic_physical:     return "kinematic_physical";
        case erhe::physics::Motion_mode::e_dynamic:                return "dynamic";
        default:                                                   return "invalid";
    }
}

auto parse_combine_mode(const std::string& combine_mode, const erhe::physics::Combine_mode fallback) -> erhe::physics::Combine_mode
{
    if (combine_mode == "average")  { return erhe::physics::Combine_mode::e_average; }
    if (combine_mode == "minimum")  { return erhe::physics::Combine_mode::e_minimum; }
    if (combine_mode == "maximum")  { return erhe::physics::Combine_mode::e_maximum; }
    if (combine_mode == "multiply") { return erhe::physics::Combine_mode::e_multiply; }
    return fallback;
}

auto combine_mode_to_string(const erhe::physics::Combine_mode combine_mode) -> const char*
{
    switch (combine_mode) {
        case erhe::physics::Combine_mode::e_average:  return "average";
        case erhe::physics::Combine_mode::e_minimum:  return "minimum";
        case erhe::physics::Combine_mode::e_maximum:  return "maximum";
        case erhe::physics::Combine_mode::e_multiply: return "multiply";
        default:                                      return "average";
    }
}

auto build_collision_shape_from_args(const json& args, const erhe::scene::Node* node, std::string& error) -> std::shared_ptr<erhe::physics::ICollision_shape>
{
    using erhe::physics::ICollision_shape;
    const std::string shape         = args.value("shape", "auto");
    const glm::vec3   half_extents  = get_vec3(args, "half_extents", glm::vec3{0.5f});
    const float       radius        = args.value("radius", 0.5f);
    const float       bottom_radius = args.value("bottom_radius", 0.5f);
    const float       top_radius    = args.value("top_radius", 0.5f);
    const float       length        = args.value("length", 1.0f);
    const erhe::physics::Axis axis  = parse_axis(args.value("axis", "y"));

    if (shape == "auto") {
        std::shared_ptr<ICollision_shape> hull = build_shape_from_node_mesh(node, true);
        if (hull) {
            return hull;
        }
        return ICollision_shape::create_box_shape_shared(half_extents);
    }
    if (shape == "box") {
        return ICollision_shape::create_box_shape_shared(half_extents);
    }
    if (shape == "sphere") {
        return ICollision_shape::create_sphere_shape_shared(radius);
    }
    if (shape == "capsule") {
        return ICollision_shape::create_capsule_shape_shared(axis, radius, length);
    }
    if (shape == "tapered_capsule") {
        return ICollision_shape::create_tapered_capsule_shape_shared(axis, bottom_radius, top_radius, length);
    }
    if (shape == "cylinder") {
        return ICollision_shape::create_cylinder_shape_shared(axis, half_extents);
    }
    if (shape == "tapered_cylinder") {
        return ICollision_shape::create_tapered_cylinder_shape_shared(axis, bottom_radius, top_radius, length);
    }
    if ((shape == "convex_hull") || (shape == "mesh")) {
        std::shared_ptr<ICollision_shape> mesh_shape = build_shape_from_node_mesh(node, shape == "convex_hull");
        if (!mesh_shape) {
            error = "Node '" + node->get_name() + "' has no usable mesh geometry for shape '" + shape + "'";
        }
        return mesh_shape;
    }
    error = "Unknown shape: " + shape;
    return {};
}

auto parse_joint_limits(const json& limits_json, erhe::physics::Physics_joint_settings& item) -> std::optional<std::string>
{
    std::array<bool, erhe::physics::c_joint_axis_count> axis_limited{};
    std::size_t entry_index = 0;
    for (const json& limit_json : limits_json) {
        const json linear_axes  = limit_json.value("linear_axes", json::array());
        const json angular_axes = limit_json.value("angular_axes", json::array());
        std::vector<std::size_t> axes;
        for (std::size_t i = 0; (i < 3) && (i < linear_axes.size()); ++i) {
            if (linear_axes[i].get<bool>()) {
                axes.push_back(i);
            }
        }
        for (std::size_t i = 0; (i < 3) && (i < angular_axes.size()); ++i) {
            if (angular_axes[i].get<bool>()) {
                axes.push_back(i + 3);
            }
        }
        if (axes.empty()) {
            return "limits[" + std::to_string(entry_index) + "] names no axis";
        }
        for (const std::size_t axis : axes) {
            if (axis_limited[axis]) {
                log_mcp->warn(
                    "joint settings '{}': more than one limit names axis '{}'; the later entry wins",
                    item.get_name(), erhe::physics::joint_axis_token(axis)
                );
            }
            axis_limited[axis] = true;
            item.set_axis_limit(axis, erhe::physics::Joint_axis_limit::limited);
            if (limit_json.contains("min"))       { item.set_axis_limit_min      (axis, limit_json["min"].get<float>()); }
            if (limit_json.contains("max"))       { item.set_axis_limit_max      (axis, limit_json["max"].get<float>()); }
            if (limit_json.contains("stiffness")) { item.set_axis_limit_stiffness(axis, limit_json["stiffness"].get<float>()); }
            item.set_axis_limit_damping(axis, limit_json.value("damping", 0.0f));
        }
        ++entry_index;
    }
    return {};
}

auto parse_joint_drives(const json& drives_json, erhe::physics::Physics_joint_settings& item) -> std::optional<std::string>
{
    std::array<bool, erhe::physics::c_joint_axis_count> axis_driven{};
    std::size_t entry_index = 0;
    for (const json& drive_json : drives_json) {
        const int axis_argument = drive_json.value("axis", 0);
        if ((axis_argument < 0) || (axis_argument > 2)) {
            return "drives[" + std::to_string(entry_index) + "] names axis " + std::to_string(axis_argument) + ", which is outside 0..2";
        }
        const bool        angular = (drive_json.value("type", "linear") == "angular");
        const std::size_t axis    = static_cast<std::size_t>(axis_argument) + (angular ? std::size_t{3} : std::size_t{0});
        if (axis_driven[axis]) {
            log_mcp->warn(
                "joint settings '{}': more than one drive names axis '{}'; the later entry wins",
                item.get_name(), erhe::physics::joint_axis_token(axis)
            );
        }
        axis_driven[axis] = true;
        item.set_axis_drive(
            axis,
            (drive_json.value("mode", "force") == "acceleration")
                ? erhe::physics::Joint_axis_drive::acceleration
                : erhe::physics::Joint_axis_drive::force
        );
        // Zero is the unlimited drive force; an entry without `max_force`
        // leaves the property at that default.
        if (drive_json.contains("max_force")) {
            const float max_force = drive_json["max_force"].get<float>();
            if (std::isfinite(max_force)) {
                item.set_axis_drive_max_force(axis, max_force);
            }
        }
        item.set_axis_drive_position_target(axis, drive_json.value("position_target", 0.0f));
        item.set_axis_drive_velocity_target(axis, drive_json.value("velocity_target", 0.0f));
        item.set_axis_drive_stiffness      (axis, drive_json.value("stiffness", 0.0f));
        item.set_axis_drive_damping        (axis, drive_json.value("damping", 0.0f));
        ++entry_index;
    }
    return {};
}

auto joint_settings_to_json(const erhe::physics::Physics_joint_settings& settings) -> json
{
    // One entry per limited axis and one per driven axis, in the KHR shape a
    // caller writes.
    json limits = json::array();
    json drives = json::array();
    const std::array<erhe::physics::Constraint_axis_limit, erhe::physics::c_joint_axis_count>& axis_limits = settings.get_axis_limits();
    const std::array<erhe::physics::Constraint_axis_drive, erhe::physics::c_joint_axis_count>& axis_drives = settings.get_axis_drives();
    for (std::size_t axis = 0; axis < erhe::physics::c_joint_axis_count; ++axis) {
        const bool rotation   = erhe::physics::is_joint_rotation_axis(axis);
        const std::size_t axis_index = rotation ? (axis - 3) : axis;
        if (axis_limits[axis].limited) {
            json limit_json = {
                {"linear_axes",  {!rotation && (axis_index == 0), !rotation && (axis_index == 1), !rotation && (axis_index == 2)}},
                {"angular_axes", { rotation && (axis_index == 0),  rotation && (axis_index == 1),  rotation && (axis_index == 2)}},
                {"damping",      axis_limits[axis].damping}
            };
            if (settings.get_value_source(erhe::physics::Physics_joint_settings::limit_min_property[axis].get()) != erhe::property::Value_source::default_value) {
                limit_json["min"] = axis_limits[axis].min;
            }
            if (settings.get_value_source(erhe::physics::Physics_joint_settings::limit_max_property[axis].get()) != erhe::property::Value_source::default_value) {
                limit_json["max"] = axis_limits[axis].max;
            }
            if (axis_limits[axis].stiffness.has_value()) {
                limit_json["stiffness"] = axis_limits[axis].stiffness.value();
            }
            limits.push_back(limit_json);
        }
        const erhe::physics::Joint_axis_drive drive_mode = settings.get_value(erhe::physics::Physics_joint_settings::drive_property[axis]);
        if (drive_mode != erhe::physics::Joint_axis_drive::off) {
            json drive_json = {
                {"type",            rotation ? "angular" : "linear"},
                {"mode",            (drive_mode == erhe::physics::Joint_axis_drive::acceleration) ? "acceleration" : "force"},
                {"axis",            static_cast<int>(axis_index)},
                {"position_target", axis_drives[axis].position_target},
                {"velocity_target", axis_drives[axis].velocity_target},
                {"stiffness",       axis_drives[axis].stiffness},
                {"damping",         axis_drives[axis].damping}
            };
            if (std::isfinite(axis_drives[axis].max_force)) {
                drive_json["max_force"] = axis_drives[axis].max_force;
            }
            drives.push_back(drive_json);
        }
    }
    return {
        {"name",   settings.get_name()},
        {"id",     settings.get_id()},
        {"limits", limits},
        {"drives", drives}
    };
}

auto schema_no_args() -> json
{
    return {{"type", "object"}, {"properties", json::object()}};
}

auto schema_scene_name() -> json
{
    return {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}}
        }},
        {"required", json::array({"scene_name"})}
    };
}

auto schema_scene_and_item(const char* item_key, const char* item_desc) -> json
{
    return {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {item_key,     {{"type", "string"}, {"description", item_desc}}}
        }},
        {"required", json::array({"scene_name", item_key})}
    };
}

auto auth_token_path() -> std::filesystem::path
{
    // ERHE_MCP_TOKEN_FILE names the token file explicitly (the mode and
    // ownership checks in load_auth_token still apply). Lets a second editor
    // instance run with its own token - mcp_server_tests' auth fixture.
    const char* const override_path = std::getenv("ERHE_MCP_TOKEN_FILE");
    if ((override_path != nullptr) && (override_path[0] != '\0')) {
        return std::filesystem::path{override_path};
    }
#if defined(_WIN32)
    const char* base = std::getenv("USERPROFILE");
#else
    const char* base = std::getenv("HOME");
#endif
    if (base == nullptr || base[0] == '\0') {
        return {};
    }
    return std::filesystem::path{base} / ".agents" / "erhe_mcp_token";
}

auto load_auth_token() -> std::string
{
    const std::filesystem::path path = auth_token_path();
    if (path.empty()) {
        return {};
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return {};
    }

#if !defined(_WIN32)
    struct stat st{};
    if (::stat(path.string().c_str(), &st) != 0) {
        log_mcp->warn("MCP server: cannot stat token file {}: {}", path.string(), std::strerror(errno));
        return {};
    }
    const mode_t mode_bits = st.st_mode & 0777;
    if (mode_bits != 0600) {
        log_mcp->warn(
            "MCP server: token file {} has mode {:o}; require 0600 (chmod 600 ~/.agents/erhe_mcp_token)",
            path.string(), mode_bits
        );
        return {};
    }
    if (st.st_uid != ::getuid()) {
        // Refuse to load a token owned by another user. Without this
        // check a symlink swap or a stale file from a different uid
        // (e.g. left over by another tester on a shared box) with
        // mode 0600 would still be accepted as the local user's
        // secret.
        log_mcp->warn(
            "MCP server: token file {} is not owned by uid {}; refusing to load",
            path.string(), static_cast<unsigned long>(::getuid())
        );
        return {};
    }
#endif

    std::ifstream in{path};
    if (!in) {
        log_mcp->warn("MCP server: cannot read token file {}", path.string());
        return {};
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string token = buf.str();
    while (!token.empty() && (token.back() == '\n' || token.back() == '\r' || token.back() == ' ' || token.back() == '\t')) {
        token.pop_back();
    }
    return token;
}

auto constant_time_equal(std::string_view a, std::string_view b) -> bool
{
    if (a.size() != b.size()) {
        return false;
    }
    unsigned int diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned int>(static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]));
    }
    return diff == 0u;
}

auto bearer_token_from(const httplib::Request& req) -> std::optional<std::string>
{
    if (!req.has_header("Authorization")) {
        return std::nullopt;
    }
    const std::string header = req.get_header_value("Authorization");
    static constexpr std::string_view prefix = "Bearer ";
    if (header.size() < prefix.size() ||
        !std::equal(prefix.begin(), prefix.end(), header.begin(),
                    [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }))
    {
        return std::nullopt;
    }
    return header.substr(prefix.size());
}

} // namespace mcp_server_detail

} // namespace editor
