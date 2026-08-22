// Definitions of the helpers declared in mcp_server_shared.hpp, shared by the
// Mcp_server translation units (mcp_server*.cpp).

#include "mcp/mcp_server_shared.hpp"

#include "editor_log.hpp"
#include "scene/collision_shape_from_mesh.hpp"
#include "scene/scene_root.hpp"
#include "tools/mesh_component_selection.hpp"
#include "transform/transform_tool_settings.hpp"

#include "erhe_geometry/geometry.hpp"
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
    out_mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
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

void parse_joint_limits(const json& limits_json, std::vector<erhe::physics::Joint_limit>& out)
{
    out.clear();
    for (const json& limit_json : limits_json) {
        erhe::physics::Joint_limit limit{};
        const json linear_axes  = limit_json.value("linear_axes", json::array());
        const json angular_axes = limit_json.value("angular_axes", json::array());
        for (std::size_t i = 0; (i < 3) && (i < linear_axes.size()); ++i) {
            limit.linear_axes[i] = linear_axes[i].get<bool>();
        }
        for (std::size_t i = 0; (i < 3) && (i < angular_axes.size()); ++i) {
            limit.angular_axes[i] = angular_axes[i].get<bool>();
        }
        if (limit_json.contains("min"))       { limit.min       = limit_json["min"].get<float>(); }
        if (limit_json.contains("max"))       { limit.max       = limit_json["max"].get<float>(); }
        if (limit_json.contains("stiffness")) { limit.stiffness = limit_json["stiffness"].get<float>(); }
        limit.damping = limit_json.value("damping", 0.0f);
        out.push_back(limit);
    }
}

void parse_joint_drives(const json& drives_json, std::vector<erhe::physics::Joint_drive>& out)
{
    out.clear();
    for (const json& drive_json : drives_json) {
        erhe::physics::Joint_drive drive{};
        drive.type = (drive_json.value("type", "linear") == "angular")
            ? erhe::physics::Drive_type::e_angular
            : erhe::physics::Drive_type::e_linear;
        drive.mode = (drive_json.value("mode", "force") == "acceleration")
            ? erhe::physics::Drive_mode::e_acceleration
            : erhe::physics::Drive_mode::e_force;
        drive.axis = drive_json.value("axis", 0);
        if (drive_json.contains("max_force")) {
            drive.max_force = drive_json["max_force"].get<float>();
        }
        drive.position_target = drive_json.value("position_target", 0.0f);
        drive.velocity_target = drive_json.value("velocity_target", 0.0f);
        drive.stiffness       = drive_json.value("stiffness", 0.0f);
        drive.damping         = drive_json.value("damping", 0.0f);
        out.push_back(drive);
    }
}

auto joint_settings_to_json(const erhe::physics::Physics_joint_settings& settings) -> json
{
    json limits = json::array();
    for (const erhe::physics::Joint_limit& limit : settings.limits) {
        json limit_json = {
            {"linear_axes",  {limit.linear_axes[0], limit.linear_axes[1], limit.linear_axes[2]}},
            {"angular_axes", {limit.angular_axes[0], limit.angular_axes[1], limit.angular_axes[2]}},
            {"damping",      limit.damping}
        };
        if (limit.min.has_value())       { limit_json["min"]       = limit.min.value(); }
        if (limit.max.has_value())       { limit_json["max"]       = limit.max.value(); }
        if (limit.stiffness.has_value()) { limit_json["stiffness"] = limit.stiffness.value(); }
        limits.push_back(limit_json);
    }
    json drives = json::array();
    for (const erhe::physics::Joint_drive& drive : settings.drives) {
        json drive_json = {
            {"type",            (drive.type == erhe::physics::Drive_type::e_angular) ? "angular" : "linear"},
            {"mode",            (drive.mode == erhe::physics::Drive_mode::e_acceleration) ? "acceleration" : "force"},
            {"axis",            drive.axis},
            {"position_target", drive.position_target},
            {"velocity_target", drive.velocity_target},
            {"stiffness",       drive.stiffness},
            {"damping",         drive.damping}
        };
        if (std::isfinite(drive.max_force)) {
            drive_json["max_force"] = drive.max_force;
        }
        drives.push_back(drive_json);
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
