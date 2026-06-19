#include "mcp/mcp_server.hpp"
#include "app_context.hpp"
#include "app_rendering.hpp"
#include "app_scenes.hpp"
#include "app_settings.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_placement.hpp"
#include "content_library/content_library.hpp"
#include "create/create_box.hpp"
#include "create/create_capsule.hpp"
#include "create/create_cone.hpp"
#include "create/create_torus.hpp"
#include "create/create_uv_sphere.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/item_parent_change_operation.hpp"
#include "operations/material_change_operation.hpp"
#include "operations/operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/operations_window.hpp"
#include "parsers/gltf.hpp"
#include "parsers/gltf_physics_export.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/image_writer.hpp"
#include "editor_log.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/collision_shape_from_mesh.hpp"
#include "scene/node_joint.hpp"
#include "scene/node_physics.hpp"
#include "scene/physics_edits.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_serialization.hpp"
#include "scene/shadow_fit_debug.hpp"
#include "tools/mesh_component_selection.hpp"
#include "tools/selection_tool.hpp"
#include "transform/transform_tool.hpp"
#include "transform/transform_tool_settings.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_primitive/primitive.hpp"

#include "erhe_scene_renderer/mesh_memory.hpp"

#include "erhe_scene_renderer/light_buffer.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_file/file.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_physics/physics_material.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>

#if !defined(_WIN32)
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace editor {

using json = nlohmann::json;

namespace {

auto make_jsonrpc_response(const std::string& id, const json& result) -> std::string
{
    json response = {
        {"jsonrpc", "2.0"},
        {"id",      id},
        {"result",  result}
    };
    return response.dump();
}

auto make_jsonrpc_error(const std::string& id, int code, const std::string& message) -> std::string
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

// Error result for tools/call: a text content block with isError set.
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

// Finds a node by the integer args[id_key], or by the string args[name_key].
auto find_node_in_scene(Scene_root& scene_root, const json& args, const char* id_key, const char* name_key) -> std::shared_ptr<erhe::scene::Node>
{
    const std::size_t node_id   = args.value(id_key, std::size_t{0});
    const std::string node_name = args.value(name_key, "");
    if ((node_id == 0) && node_name.empty()) {
        return {};
    }
    for (const std::shared_ptr<erhe::scene::Node>& node : scene_root.get_scene().get_flat_nodes()) {
        if ((node_id != 0) ? (node->get_id() == node_id) : (node->get_name() == node_name)) {
            return node;
        }
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

template <typename T>
auto find_library_item(const std::shared_ptr<Content_library_node>& folder, const std::string& name) -> std::shared_ptr<T>
{
    if (!folder || name.empty()) {
        return {};
    }
    for (const std::shared_ptr<T>& item : folder->get_all<T>()) {
        if (item->get_name() == name) {
            return item;
        }
    }
    return {};
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

// Mesh component mode <-> lowercase string (matches the MCP tool argument names,
// distinct from the UI-facing c_str() which is capitalized).
auto parse_mesh_component_mode(const std::string& mode, const Mesh_component_mode fallback) -> Mesh_component_mode
{
    if (mode == "object") { return Mesh_component_mode::object; }
    if (mode == "vertex") { return Mesh_component_mode::vertex; }
    if (mode == "edge")   { return Mesh_component_mode::edge;   }
    if (mode == "face")   { return Mesh_component_mode::face;   }
    return fallback;
}

auto mesh_component_mode_lc(const Mesh_component_mode mode) -> const char*
{
    switch (mode) {
        case Mesh_component_mode::vertex: return "vertex";
        case Mesh_component_mode::edge:   return "edge";
        case Mesh_component_mode::face:   return "face";
        case Mesh_component_mode::object:
        default:                          return "object";
    }
}

auto is_valid_mesh_component_mode(const std::string& mode) -> bool
{
    return (mode == "object") || (mode == "vertex") || (mode == "edge") || (mode == "face");
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

// Resolve a node's renderable Geometry for a given primitive, mirroring the path
// the node-details query and the component-selection tool use:
// node -> Mesh attachment -> primitive[primitive_index] -> render_shape geometry.
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

// Builds a collision shape from tool arguments. "auto" (the default) builds
// a convex hull from the node's mesh, falling back to a unit box when the
// node has no usable mesh geometry. Returns nullptr with error set on
// failure.
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

// Replaces out with limits parsed from a JSON array of limit objects.
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

// Replaces out with drives parsed from a JSON array of drive objects.
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

// Returns $HOME/.claude/erhe_mcp_token (or %USERPROFILE%\.claude\... on
// Windows). The directory is not created here; the file is optional.
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
    return std::filesystem::path{base} / ".claude" / "erhe_mcp_token";
}

// Returns the trimmed file contents, or an empty string if the file
// is missing or unreadable. On POSIX the file must be mode 0600
// (owner read/write only); other modes are refused with a warning so
// a world-readable token is not picked up silently.
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
            "MCP server: token file {} has mode {:o}; require 0600 (chmod 600 ~/.claude/erhe_mcp_token)",
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

// Constant-time comparison so the response timing does not reveal how
// far the first mismatch was. Both inputs are short opaque tokens; the
// constant-time guard is cheap.
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

// Parses "Authorization: Bearer <token>" out of an httplib::Request.
// Returns the token (possibly empty) or std::nullopt if the header is
// missing or malformed.
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

} // anonymous namespace

Mcp_server::Mcp_server(
    erhe::commands::Commands& commands,
    App_context&              context,
    int                       port
)
    : m_commands{commands}
    , m_context {context}
    , m_port    {port}
{
    m_auth_token = load_auth_token();
    if (m_auth_token.empty()) {
        log_mcp->warn(
            "MCP server: no bearer token loaded (write a secret to ~/.claude/erhe_mcp_token "
            "with mode 0600 to require Authorization: Bearer)"
        );
    } else {
        log_mcp->info("MCP server: bearer-token auth enabled");
    }
}

Mcp_server::~Mcp_server() noexcept
{
    // ~thread on a still-joinable handle calls std::terminate; the
    // join itself can throw (system_error on EDEADLK / EINVAL).
    // Catch and log so destruction is noexcept in practice and any
    // platform-level join failure leaves a breadcrumb instead of
    // crashing the editor at shutdown.
    try {
        stop();
    } catch (const std::system_error& e) {
        log_mcp->error("MCP server: ~Mcp_server caught system_error during stop: {}", e.what());
    } catch (const std::exception& e) {
        log_mcp->error("MCP server: ~Mcp_server caught exception during stop: {}", e.what());
    } catch (...) {
        log_mcp->error("MCP server: ~Mcp_server caught unknown exception during stop");
    }
}

void Mcp_server::start()
{
    std::lock_guard<std::mutex> lock{m_lifecycle_mutex};

    if (m_running.load() || m_server_thread.joinable() || m_http_server) {
        return;
    }

    log_mcp->info("MCP server: starting on port {}", m_port);

    refresh_tool_list();

    m_http_server = std::make_unique<httplib::Server>();
    m_http_server->set_payload_max_length(k_max_payload_bytes);
    setup_routes();

    m_running.store(true);
    m_server_thread = std::thread{&Mcp_server::server_thread_main, this};
}

void Mcp_server::stop()
{
    std::lock_guard<std::mutex> lock{m_lifecycle_mutex};

    // Worker thread flips m_running to false on its way out of
    // httplib::listen() (server_thread_main), so checking m_running
    // alone would miss the post-listen window where the thread is
    // still joinable. Check both the thread and the server pointer.
    if (!m_server_thread.joinable() && !m_http_server) {
        return;
    }

    log_mcp->info("MCP server: stopping");

    m_running.store(false);
    if (m_http_server) {
        m_http_server->stop();
    }
    if (m_server_thread.joinable()) {
        m_server_thread.join();
    }
    m_http_server.reset();

    log_mcp->info("MCP server: stopped");
}

auto Mcp_server::is_running() const -> bool
{
    return m_running.load();
}

void Mcp_server::server_thread_main()
{
    log_mcp->info("MCP server: listening on 127.0.0.1:{}", m_port);

    if (!m_http_server->listen("127.0.0.1", m_port)) {
        if (m_running.load()) {
            log_mcp->error("MCP server: failed to listen on port {}", m_port);
        }
    }

    m_running.store(false);
    log_mcp->info("MCP server: thread exiting");
}

void Mcp_server::setup_routes()
{
    m_http_server->Post("/mcp", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");

        // Auth check first. When m_auth_token is empty (token file not
        // present or unreadable) auth is disabled and any request is
        // accepted; the operator gets a startup warning so disabled
        // auth is loud rather than silent.
        if (!m_auth_token.empty()) {
            const std::optional<std::string> presented = bearer_token_from(req);
            if (!presented.has_value() || !constant_time_equal(*presented, m_auth_token)) {
                res.status = 401;
                res.set_header("WWW-Authenticate", "Bearer realm=\"erhe-mcp\"");
                res.body = make_jsonrpc_error("null", -32001, "Unauthorized");
                return;
            }
        }

        json request;
        try {
            request = json::parse(req.body);
        } catch (const json::parse_error&) {
            res.body = make_jsonrpc_error("null", -32700, "Parse error");
            return;
        }

        // JSON-RPC allows the id to be a string, a number, or null. json::value
        // with a string default throws json::type_error on a numeric id, which
        // httplib turns into an opaque HTTP 500. Normalize any id type to the
        // string form used internally for the response echo.
        std::string id = "null";
        if (request.contains("id")) {
            const json& id_json = request.at("id");
            if (id_json.is_string()) {
                id = id_json.get<std::string>();
            } else if (id_json.is_number_integer()) {
                id = std::to_string(id_json.get<int64_t>());
            } else if (id_json.is_number_unsigned()) {
                id = std::to_string(id_json.get<uint64_t>());
            } else if (id_json.is_number_float()) {
                id = std::to_string(id_json.get<double>());
            }
        }
        const std::string method = request.value("method", "");

        if (method == "initialize") {
            res.body = handle_initialize(id);
        } else if (method == "tools/list") {
            res.body = handle_tools_list(id);
        } else if (method == "tools/call") {
            const auto& params    = request.value("params", json::object());
            const std::string tool_name = params.value("name", "");
            const json arguments = params.value("arguments", json::object());
            if (tool_name.empty()) {
                res.body = make_jsonrpc_error(id, -32602, "Missing tool name in params.name");
            } else {
                res.body = handle_tools_call(id, tool_name, arguments);
            }
        } else {
            res.body = make_jsonrpc_error(id, -32601, "Method not found: " + method);
        }
    });

    m_http_server->Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        res.body = R"({"status":"ok"})";
    });
}

auto Mcp_server::handle_initialize(const std::string& id) -> std::string
{
    json result = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", {
            {"tools", json::object()}
        }},
        {"serverInfo", {
            {"name",    "erhe-editor"},
            {"version", "0.2.0"}
        }}
    };
    return make_jsonrpc_response(id, result);
}

auto Mcp_server::handle_tools_list(const std::string& id) -> std::string
{
    refresh_tool_list();

    json tools = json::array();
    {
        std::lock_guard<std::mutex> lock{m_tools_mutex};
        for (const auto& tool : m_tool_infos) {
            json tool_json = {
                {"name",        tool.name},
                {"description", tool.description},
                {"inputSchema", tool.input_schema}
            };
            tools.push_back(tool_json);
        }
    }

    json result = {{"tools", tools}};
    return make_jsonrpc_response(id, result);
}

auto Mcp_server::handle_tools_call(
    const std::string& id,
    const std::string& tool_name,
    const json&        arguments
) -> std::string
{
    auto queued = std::make_unique<Queued_request>();
    queued->tool_name = tool_name;
    queued->arguments = arguments;
    std::future<std::string> result_future = queued->result_promise.get_future();

    {
        std::lock_guard<std::mutex> lock{m_queue_mutex};
        if (m_request_queue.size() >= k_max_queue_depth) {
            // Drop without enqueuing so process_queued_requests cannot
            // pop a stale entry whose HTTP client has already timed
            // out. -32000 is "server error" in JSON-RPC's reserved
            // range; the message is what differentiates it from other
            // -32000s.
            return make_jsonrpc_error(id, -32000, "Server busy: request queue full");
        }
        m_request_queue.push_back(std::move(queued));
    }

    const auto status = result_future.wait_for(k_request_timeout);
    if (status == std::future_status::timeout) {
        return make_jsonrpc_error(id, -32000, "Request timed out: " + tool_name);
    }

    std::string result_json;
    try {
        result_json = result_future.get();
    } catch (const std::future_error& e) {
        // The queued request's promise was destroyed without being
        // settled. This can happen if Mcp_server is being torn down
        // while HTTP handler threads are still waiting (the queue
        // unique_ptrs are freed before this thread observes ready).
        // Translate the broken_promise into a clean JSON-RPC error
        // so the exception cannot escape into httplib's dispatcher.
        log_mcp->warn("MCP server: future_error in handle_tools_call: {}", e.what());
        return make_jsonrpc_error(id, -32000, "Internal: request abandoned: " + tool_name);
    }
    json result = json::parse(result_json, nullptr, false);
    if (result.is_discarded()) {
        return make_jsonrpc_error(id, -32000, "Internal error processing: " + tool_name);
    }
    return make_jsonrpc_response(id, result);
}

auto Mcp_server::handle_error(const std::string& id, int code, const std::string& message) -> std::string
{
    return make_jsonrpc_error(id, code, message);
}

auto Mcp_server::process_queued_requests() -> int
{
    std::vector<std::unique_ptr<Queued_request>> requests;
    {
        std::lock_guard<std::mutex> lock{m_queue_mutex};
        requests.swap(m_request_queue);
    }

    const auto now = std::chrono::steady_clock::now();
    int count = 0;
    for (auto& req : requests) {
        // Drop entries whose HTTP client has already given up (wait_for
        // returned timeout in handle_tools_call). Without this guard
        // a slow editor frame would still mutate editor state for a
        // request the operator already considers failed. We still
        // settle the promise so the future destructor does not abort.
        if ((now - req->enqueued_at) >= k_request_timeout) {
            req->result_promise.set_value(
                make_jsonrpc_error("dropped", -32000, "Request expired before processing: " + req->tool_name)
            );
            log_mcp->warn("MCP server: dropped expired '{}' before processing", req->tool_name);
            continue;
        }

        std::string result;

        if      (req->tool_name == "list_scenes")         result = query_list_scenes     (req->arguments);
        else if (req->tool_name == "get_scene_nodes")     result = query_scene_nodes     (req->arguments);
        else if (req->tool_name == "get_node_details")    result = query_node_details    (req->arguments);
        else if (req->tool_name == "get_scene_cameras")   result = query_scene_cameras   (req->arguments);
        else if (req->tool_name == "get_scene_lights")    result = query_scene_lights    (req->arguments);
        else if (req->tool_name == "get_scene_materials") result = query_scene_materials (req->arguments);
        else if (req->tool_name == "get_scene_textures") result = query_scene_textures  (req->arguments);
        else if (req->tool_name == "get_scene_brushes")  result = query_scene_brushes  (req->arguments);
        else if (req->tool_name == "get_material_details")result = query_material_details(req->arguments);
        else if (req->tool_name == "get_selection")       result = query_selection       (req->arguments);
        else if (req->tool_name == "get_undo_redo_stack") result = query_undo_redo_stack (req->arguments);
        else if (req->tool_name == "get_async_status")   result = query_async_status    (req->arguments);
        else if (req->tool_name == "get_shadow_fit_debug")result = query_shadow_fit_debug(req->arguments);
        else if (req->tool_name == "select_items")       result = action_select_items   (req->arguments);
        else if (req->tool_name == "transform_selection") result = action_transform_selection(req->arguments);
        else if (req->tool_name == "place_brush")        result = action_place_brush    (req->arguments);
        else if (req->tool_name == "create_shape")       result = action_create_shape   (req->arguments);
        else if (req->tool_name == "create_node")        result = action_create_node    (req->arguments);
        else if (req->tool_name == "create_light")       result = action_create_light   (req->arguments);
        else if (req->tool_name == "edit_light")         result = action_edit_light     (req->arguments);
        else if (req->tool_name == "toggle_physics")     result = action_toggle_physics (req->arguments);
        else if (req->tool_name == "reparent_node")      result = action_reparent_node  (req->arguments);
        else if (req->tool_name == "lock_items")         result = action_lock_items     (req->arguments);
        else if (req->tool_name == "unlock_items")       result = action_unlock_items   (req->arguments);
        else if (req->tool_name == "add_tags")           result = action_add_tags       (req->arguments);
        else if (req->tool_name == "remove_tags")        result = action_remove_tags    (req->arguments);
        else if (req->tool_name == "edit_material")      result = action_edit_material  (req->arguments);
        else if (req->tool_name == "save_scene")         result = action_save_scene     (req->arguments);
        else if (req->tool_name == "export_gltf")        result = action_export_gltf    (req->arguments);
        else if (req->tool_name == "import_gltf")        result = action_import_gltf    (req->arguments);
        else if (req->tool_name == "capture_screenshot") result = action_capture_screenshot(req->arguments);
        else if (req->tool_name == "wake_physics_bodies") result = action_wake_physics_bodies(req->arguments);
        else if (req->tool_name == "get_physics_items")  result = query_physics_items   (req->arguments);
        else if (req->tool_name == "get_physics_state")  result = query_get_physics_state(req->arguments);
        else if (req->tool_name == "create_physics_body") result = action_create_physics_body(req->arguments);
        else if (req->tool_name == "edit_physics_body")  result = action_edit_physics_body(req->arguments);
        else if (req->tool_name == "create_physics_joint") result = action_create_physics_joint(req->arguments);
        else if (req->tool_name == "edit_physics_joint") result = action_edit_physics_joint(req->arguments);
        else if (req->tool_name == "create_physics_material") result = action_create_physics_material(req->arguments);
        else if (req->tool_name == "edit_physics_material")   result = action_edit_physics_material(req->arguments);
        else if (req->tool_name == "create_collision_filter") result = action_create_collision_filter(req->arguments);
        else if (req->tool_name == "edit_collision_filter")   result = action_edit_collision_filter(req->arguments);
        else if (req->tool_name == "create_physics_joint_settings") result = action_create_physics_joint_settings(req->arguments);
        else if (req->tool_name == "edit_physics_joint_settings")   result = action_edit_physics_joint_settings(req->arguments);
        else if (req->tool_name == "set_mesh_component_mode")        result = action_set_mesh_component_mode(req->arguments);
        else if (req->tool_name == "select_mesh_components")         result = action_select_mesh_components(req->arguments);
        else if (req->tool_name == "get_mesh_component_selection")   result = query_mesh_component_selection(req->arguments);
        else if (req->tool_name == "clear_mesh_component_selection") result = action_clear_mesh_component_selection(req->arguments);
        else if (req->tool_name == "align_components")              result = action_align_components(req->arguments);
        else if (req->tool_name == "add_joint")                    result = action_add_joint(req->arguments);
        else if (req->tool_name == "flip_joint")                   result = action_flip_joint(req->arguments);
        else if (req->tool_name == "remesh")                       result = action_remesh(req->arguments);
        else if (req->tool_name == "decimate")                     result = action_decimate(req->arguments);
        else if (req->tool_name == "smooth")                       result = action_smooth(req->arguments);
        else if (req->tool_name == "generate_texture_coordinates") result = action_generate_texture_coordinates(req->arguments);
        else if (req->tool_name == "set_transform_reference_mode") result = action_set_transform_reference_mode(req->arguments);
        else if (req->tool_name == "set_transform_mode")           result = action_set_transform_mode          (req->arguments);
        else                                              result = execute_command       (req->tool_name);

        req->result_promise.set_value(std::move(result));
        ++count;
        log_mcp->info("MCP server: processed '{}'", req->tool_name);
    }

    return count;
}

void Mcp_server::refresh_tool_list()
{
    std::lock_guard<std::mutex> lock{m_tools_mutex};
    m_tool_infos.clear();

    // Query tools
    m_tool_infos.push_back({"list_scenes",         "List all scenes in the editor",                          schema_no_args()});
    m_tool_infos.push_back({"get_scene_nodes",     "List all nodes in a scene",                              schema_scene_name()});
    m_tool_infos.push_back({"get_node_details",    "Get detailed info for a node (transform, attachments)",  schema_scene_and_item("node_name", "Name of the node")});
    m_tool_infos.push_back({"get_scene_cameras",   "List all cameras in a scene",                            schema_scene_name()});
    m_tool_infos.push_back({"get_scene_lights",    "List all lights in a scene",                             schema_scene_name()});
    m_tool_infos.push_back({"get_scene_materials", "List all materials in a scene's content library",        schema_scene_name()});
    m_tool_infos.push_back({"get_material_details","Get detailed material properties",                       schema_scene_and_item("material_name", "Name of the material")});
    m_tool_infos.push_back({"get_scene_textures", "List all textures in a scene's content library",         schema_scene_name()});
    m_tool_infos.push_back({"get_scene_brushes",  "List all brushes in a scene's content library",         schema_scene_name()});
    m_tool_infos.push_back({"get_selection",        "Get currently selected items",                          schema_no_args()});
    m_tool_infos.push_back({"get_undo_redo_stack", "Get undo/redo operation stacks",                       schema_no_args()});
    m_tool_infos.push_back({"get_async_status",   "Get pending/running async operation counts",          schema_no_args()});
    m_tool_infos.push_back({"get_shadow_fit_debug","Dump directional shadow frustum fit debug geometry per shadow node: F_shadow planes, their bounded face quads (the truncated view-frustum faces caster AABBs are tested against), and receiver frustum corners. Needs the Shadow Fit 'Collect Debug' setting enabled.", schema_no_args()});
    m_tool_infos.push_back({"select_items",        "Select items by ID (scene nodes, materials, etc.)",   {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene to search for items"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Array of item IDs to select"}}}
        }},
        {"required", json::array({"scene_name", "ids"})}
    }});
    m_tool_infos.push_back({"transform_selection", "Apply a Transform tool edit (translation / rotation / scale / skew) to the currently selected node(s), through the same code path as the Transform window numeric entry. space=local applies values in parent space (requires exactly one selected node); space=global applies in world (anchor) space. end_edit=true (default) records an undo operation and refreshes the edit baselines; end_edit=false keeps the edit session open like an active drag, so repeated calls re-apply against the same initial state.", {
        {"type", "object"},
        {"properties", {
            {"space",         {{"type", "string"},  {"description", "Edit space: 'local' or 'global' (default 'global')"}}},
            {"translation",   {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Translation [x, y, z]"}}},
            {"rotation_xyzw", {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 4}, {"maxItems", 4}, {"description", "Rotation quaternion [x, y, z, w]"}}},
            {"scale",         {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Scale [x, y, z]"}}},
            {"skew",          {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Skew [x, y, z]"}}},
            {"end_edit",      {{"type", "boolean"}, {"description", "Record an undo operation and refresh edit baselines (default true)"}}}
        }}
    }});
    m_tool_infos.push_back({"place_brush",         "Place a brush instance in a scene at a given position", {
        {"type", "object"},
        {"properties", {
            {"scene_name",    {{"type", "string"},  {"description", "Name of the scene"}}},
            {"brush_id",      {{"type", "integer"}, {"description", "Brush ID (from get_scene_brushes)"}}},
            {"position",      {{"type", "array"},   {"items", {{"type", "number"}}}, {"description", "World position [x, y, z]"}}},
            {"material_name", {{"type", "string"},  {"description", "Material name (optional, uses first available if omitted)"}}},
            {"scale",         {{"type", "number"},  {"description", "Scale factor (optional, default 1.0)"}}},
            {"motion_mode",   {{"type", "string"},  {"description", "Physics mode: static, dynamic (optional, default dynamic)"}}}
        }},
        {"required", json::array({"scene_name", "brush_id", "position"})}
    }});

    m_tool_infos.push_back({"create_shape",        "Create a parametric shape using the same generators as the Create window, then place an instance in the scene and/or add the brush to the content library. Shape parameters: box (size [x,y,z], steps [x,y,z], power), uv_sphere (radius, slice_count, stack_count), cone (height, bottom_radius, top_radius, use_top, use_bottom, slice_count, stack_count), capsule (length, bottom_radius, top_radius, slice_count, stack_count; tapered when the radii differ, which requires length > |bottom_radius - top_radius|), torus (major_radius, minor_radius, major_steps, minor_steps).", {
        {"type", "object"},
        {"properties", {
            {"scene_name",     {{"type", "string"},  {"description", "Name of the scene"}}},
            {"shape",          {{"type", "string"},  {"description", "Shape type: box, uv_sphere, cone, capsule or torus"}}},
            {"name",           {{"type", "string"},  {"description", "Brush / instance name (default: shape type)"}}},
            {"instance",       {{"type", "boolean"}, {"description", "Place an instance node in the scene (default true)"}}},
            {"add_brush",      {{"type", "boolean"}, {"description", "Add the brush to the content library for later place_brush use (default false)"}}},
            {"position",       {{"type", "array"},   {"items", {{"type", "number"}}},  {"minItems", 3}, {"maxItems", 3}, {"description", "World position [x, y, z] for the instance (default [0, 0, 0])"}}},
            {"parent_node_id", {{"type", "integer"}, {"description", "Parent node ID for the instance (default: scene root); the world position is preserved"}}},
            {"material_name",  {{"type", "string"},  {"description", "Material name (default: first available)"}}},
            {"scale",          {{"type", "number"},  {"description", "Uniform scale factor for the instance (default 1.0)"}}},
            {"motion_mode",    {{"type", "string"},  {"description", "Physics motion mode for the instance: static, kinematic, dynamic (default dynamic)"}}},
            {"size",           {{"type", "array"},   {"items", {{"type", "number"}}},  {"minItems", 3}, {"maxItems", 3}, {"description", "box: size [x, y, z]"}}},
            {"steps",          {{"type", "array"},   {"items", {{"type", "integer"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "box: subdivision steps [x, y, z]"}}},
            {"power",          {{"type", "number"},  {"description", "box: power"}}},
            {"radius",         {{"type", "number"},  {"description", "uv_sphere: radius"}}},
            {"height",         {{"type", "number"},  {"description", "cone: height"}}},
            {"length",         {{"type", "number"},  {"description", "capsule: cylinder section length"}}},
            {"bottom_radius",  {{"type", "number"},  {"description", "cone / capsule: bottom radius"}}},
            {"top_radius",     {{"type", "number"},  {"description", "cone / capsule: top radius"}}},
            {"use_top",        {{"type", "boolean"}, {"description", "cone: generate top cap (default true)"}}},
            {"use_bottom",     {{"type", "boolean"}, {"description", "cone: generate bottom cap (default true)"}}},
            {"slice_count",    {{"type", "integer"}, {"description", "uv_sphere / cone / capsule: slice count"}}},
            {"stack_count",    {{"type", "integer"}, {"description", "uv_sphere / cone: stack count; capsule: hemisphere stack count"}}},
            {"major_radius",   {{"type", "number"},  {"description", "torus: major radius"}}},
            {"minor_radius",   {{"type", "number"},  {"description", "torus: minor radius"}}},
            {"major_steps",    {{"type", "integer"}, {"description", "torus: major steps"}}},
            {"minor_steps",    {{"type", "integer"}, {"description", "torus: minor steps"}}}
        }},
        {"required", json::array({"scene_name", "shape"})}
    }});
    m_tool_infos.push_back({"create_node",         "Create an empty scene node (undoable), optionally parented and positioned. Useful as a physics joint anchor: create_physics_joint captures its joint frames from the joint / connected node world transforms, so coincident anchor child nodes on the two bodies give a clean joint pivot.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",       {{"type", "string"},  {"description", "Name of the scene"}}},
            {"name",             {{"type", "string"},  {"description", "Name for the new node (default 'new empty node')"}}},
            {"parent_node_id",   {{"type", "integer"}, {"description", "Parent node ID (default: scene root)"}}},
            {"parent_node_name", {{"type", "string"},  {"description", "Parent node name (alternative to parent_node_id)"}}},
            {"position",         {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "World position [x, y, z] (default [0, 0, 0])"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"create_light",        "Create a light (directional, point or spot) attached to a new scene-root node (undoable, inserted on the next frame). Point/spot lights default to range 25; directional to range 0.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",       {{"type", "string"},  {"description", "Name of the scene"}}},
            {"type",             {{"type", "string"},  {"enum", json::array({"directional", "point", "spot"})}, {"description", "Light type (default directional)"}}},
            {"name",             {{"type", "string"},  {"description", "Name for the new light / node (default 'MCP light')"}}},
            {"position",         {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "World position [x, y, z] (default [0, 0, 0])"}}},
            {"color",            {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Linear RGB color [r, g, b] (default [1, 1, 1])"}}},
            {"intensity",        {{"type", "number"},  {"description", "Light intensity (default 1.0)"}}},
            {"range",            {{"type", "number"},  {"description", "Light range / far distance (default 25 for point/spot, 0 for directional)"}}},
            {"cast_shadow",      {{"type", "boolean"}, {"description", "Whether the light casts shadows (default true)"}}},
            {"inner_spot_angle", {{"type", "number"}, {"description", "Spot inner cone angle in radians (spot only)"}}},
            {"outer_spot_angle", {{"type", "number"}, {"description", "Spot outer cone angle in radians (spot only)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"edit_light",          "Edit an existing light in place (by light_id or light_name). Changing 'type' re-buckets the light for shadow rendering. 'position' moves the light's node. Only the provided fields change.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",       {{"type", "string"},  {"description", "Name of the scene"}}},
            {"light_id",         {{"type", "integer"}, {"description", "ID of the light to edit (from get_scene_lights)"}}},
            {"light_name",       {{"type", "string"},  {"description", "Name of the light to edit (alternative to light_id)"}}},
            {"type",             {{"type", "string"},  {"enum", json::array({"directional", "point", "spot"})}, {"description", "New light type"}}},
            {"position",         {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "New world position [x, y, z] for the light's node"}}},
            {"color",            {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "New linear RGB color [r, g, b]"}}},
            {"intensity",        {{"type", "number"},  {"description", "New light intensity"}}},
            {"range",            {{"type", "number"},  {"description", "New light range / far distance"}}},
            {"cast_shadow",      {{"type", "boolean"}, {"description", "Whether the light casts shadows"}}},
            {"inner_spot_angle", {{"type", "number"}, {"description", "Spot inner cone angle in radians"}}},
            {"outer_spot_angle", {{"type", "number"}, {"description", "Spot outer cone angle in radians"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"toggle_physics",     "Toggle dynamic physics simulation on/off",              schema_no_args()});
    m_tool_infos.push_back({"reparent_node",     "Set a node's parent (by node IDs)",                    {
        {"type", "object"},
        {"properties", {
            {"scene_name",    {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",       {{"type", "integer"}, {"description", "ID of the node to reparent"}}},
            {"parent_node_id",{{"type", "integer"}, {"description", "ID of the new parent node (0 for scene root)"}}}
        }},
        {"required", json::array({"scene_name", "node_id"})}
    }});
    m_tool_infos.push_back({"lock_items",         "Lock items by ID (prevents deletion/modification)",   {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Item IDs to lock"}}}
        }},
        {"required", json::array({"scene_name", "ids"})}
    }});
    m_tool_infos.push_back({"unlock_items",       "Unlock items by ID",                                  {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Item IDs to unlock"}}}
        }},
        {"required", json::array({"scene_name", "ids"})}
    }});
    m_tool_infos.push_back({"add_tags",           "Add tags to items by ID",                             {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Item IDs to tag"}}},
            {"tags",       {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Tags to add"}}}
        }},
        {"required", json::array({"scene_name", "ids", "tags"})}
    }});
    m_tool_infos.push_back({"remove_tags",        "Remove tags from items by ID",                        {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Item IDs"}}},
            {"tags",       {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Tags to remove"}}}
        }},
        {"required", json::array({"scene_name", "ids", "tags"})}
    }});
    json texture_slot_schema = {
        {"type", "object"},
        {"properties", {
            {"texture",   {{"description", "Texture: string (name in content library), integer (texture id), or null to clear. Omit to keep current."}}},
            {"tex_coord", {{"type", "integer"}, {"minimum", 0},  {"description", "UV channel index (e.g. 0 for TEXCOORD_0)"}}},
            {"rotation",  {{"type", "number"}, {"description", "UV rotation in radians"}}},
            {"offset",    {{"type", "array"},  {"items", {{"type", "number"}}}, {"minItems", 2}, {"maxItems", 2}, {"description", "UV offset [u, v]"}}},
            {"scale",     {{"type", "array"},  {"items", {{"type", "number"}}}, {"minItems", 2}, {"maxItems", 2}, {"description", "UV scale [u, v]"}}}
        }}
    };
    m_tool_infos.push_back({"edit_material",      "Edit material properties including texture assignments (undoable). Only fields supplied are changed.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",                 {{"type", "string"},  {"description", "Name of the scene"}}},
            {"material_name",              {{"type", "string"},  {"description", "Name of the material to edit"}}},
            {"base_color",                 {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Linear RGB base color [r, g, b]"}}},
            {"opacity",                    {{"type", "number"},  {"description", "Opacity in [0, 1]"}}},
            {"roughness",                  {{"description", "Roughness; either [x, y] for anisotropic or a single number applied to both."}}},
            {"metallic",                   {{"type", "number"},  {"description", "Metallic factor in [0, 1]"}}},
            {"reflectance",                {{"type", "number"},  {"description", "Dielectric reflectance in [0, 1]"}}},
            {"emissive",                   {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Linear RGB emissive [r, g, b]"}}},
            {"normal_texture_scale",       {{"type", "number"},  {"description", "Normal map scale"}}},
            {"occlusion_texture_strength", {{"type", "number"},  {"description", "Occlusion map strength"}}},
            {"bxdf_model",                 {{"type", "string"},  {"enum", json::array({"unlit", "isotropic_brdf", "anisotropic_brdf", "anisotropic_slope", "anisotropic_engine_ready"})}, {"description", "Selects which BxDF the standard shader applies"}}},
            {"use_circular_brushed_metal", {{"type", "boolean"}, {"description", "Enable circular brushed metal shading variant"}}},
            {"use_aniso_control",          {{"type", "boolean"}, {"description", "Enable anisotropic shading control"}}},
            {"texture_samplers",           {
                {"type", "object"},
                {"description", "Per-slot texture assignments. Textures must come from the scene's content library (use get_scene_textures to list)."},
                {"properties", {
                    {"base_color",         texture_slot_schema},
                    {"metallic_roughness", texture_slot_schema},
                    {"normal",             texture_slot_schema},
                    {"occlusion",          texture_slot_schema},
                    {"emissive",           texture_slot_schema}
                }}
            }}
        }},
        {"required", json::array({"scene_name", "material_name"})}
    }});

    m_tool_infos.push_back({"save_scene",         "Save a scene to an editor scene .json file (plus companion .glb), without a file dialog", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"path",       {{"type", "string"}, {"description", "Destination .json file path"}}}
        }},
        {"required", json::array({"scene_name", "path"})}
    }});
    m_tool_infos.push_back({"export_gltf",        "Export a scene to a glTF file, without a file dialog", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"},  {"description", "Name of the scene"}}},
            {"path",       {{"type", "string"},  {"description", "Destination file path"}}},
            {"binary",     {{"type", "boolean"}, {"description", "Write binary .glb instead of text .gltf (default true)"}}}
        }},
        {"required", json::array({"scene_name", "path"})}
    }});
    m_tool_infos.push_back({"import_gltf",        "Import a glTF file into an existing scene", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the destination scene"}}},
            {"path",       {{"type", "string"}, {"description", "Source .gltf/.glb file path"}}}
        }},
        {"required", json::array({"scene_name", "path"})}
    }});
    m_tool_infos.push_back({"capture_screenshot",  "Capture the current rendered frame to a PNG file and return its path. Currently supported only in the headless Vulkan configuration (emulated swapchain).", {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Output PNG path (default logs/mcp_screenshot.png)"}}}
        }}
    }});
    m_tool_infos.push_back({"wake_physics_bodies", "Activate all dynamic rigid bodies in a scene (bodies enter the world deactivated)", schema_scene_name()});

    m_tool_infos.push_back({"get_physics_items", "List the shared physics content-library items (physics materials, collision filters, joint settings) with full properties", schema_scene_name()});

    const json node_ref_properties = {
        {"node_id",   {{"type", "integer"}, {"description", "Node ID (preferred over node_name when both given)"}}},
        {"node_name", {{"type", "string"},  {"description", "Node name (used when node_id is absent)"}}}
    };
    const json shape_properties = {
        {"shape",         {{"type", "string"}, {"enum", json::array({"auto", "box", "sphere", "capsule", "tapered_capsule", "cylinder", "tapered_cylinder", "convex_hull", "mesh"})}, {"description", "Collision shape; auto (default) = convex hull from the node's mesh, unit box when the node has no mesh. mesh shapes are static/kinematic only."}}},
        {"half_extents",  {{"type", "array"},  {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Box / cylinder half extents [x, y, z] (default [0.5, 0.5, 0.5])"}}},
        {"radius",        {{"type", "number"}, {"description", "Sphere / capsule radius (default 0.5)"}}},
        {"bottom_radius", {{"type", "number"}, {"description", "Tapered capsule / cylinder bottom radius (default 0.5)"}}},
        {"top_radius",    {{"type", "number"}, {"description", "Tapered capsule / cylinder top radius (default 0.5)"}}},
        {"length",        {{"type", "number"}, {"description", "Capsule / tapered shape axial length (default 1.0)"}}},
        {"axis",          {{"type", "string"}, {"enum", json::array({"x", "y", "z"})}, {"description", "Shape axis (default y)"}}}
    };
    const json body_properties = {
        {"motion_mode",      {{"type", "string"}, {"enum", json::array({"static", "kinematic", "kinematic_non_physical", "dynamic"})}, {"description", "Motion mode (default dynamic; kinematic = kinematic physical)"}}},
        {"mass",             {{"type", "number"}, {"description", "Mass; 0 = spec infinite-mass convention; omitted = derived from the shape"}}},
        {"friction",         {{"type", "number"}, {"description", "Scalar friction (overridden by a physics material)"}}},
        {"restitution",      {{"type", "number"}, {"description", "Scalar restitution (overridden by a physics material)"}}},
        {"linear_damping",   {{"type", "number"}, {"description", "Linear damping"}}},
        {"angular_damping",  {{"type", "number"}, {"description", "Angular damping"}}},
        {"gravity_factor",   {{"type", "number"}, {"description", "Gravity factor (default 1.0)"}}},
        {"is_trigger",       {{"type", "boolean"}, {"description", "Create as a sensor / trigger volume"}}},
        {"linear_velocity",  {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Initial linear velocity [x, y, z] (world space, applied at body creation)"}}},
        {"angular_velocity", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Initial angular velocity [x, y, z] (world space, applied at body creation)"}}},
        {"center_of_mass",   {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Center of mass offset [x, y, z]"}}},
        {"material_name",    {{"type", "string"}, {"description", "Physics material name from the content library (empty string clears)"}}},
        {"filter_name",      {{"type", "string"}, {"description", "Collision filter name from the content library (empty string clears)"}}}
    };
    json create_body_properties = {{"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}}};
    create_body_properties.update(node_ref_properties);
    create_body_properties.update(shape_properties);
    create_body_properties.update(body_properties);
    m_tool_infos.push_back({"create_physics_body", "Attach a new rigid body (Node_physics) to a scene node (undoable). One rigid body per node.", {
        {"type", "object"},
        {"properties", create_body_properties},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"edit_physics_body", "Edit the rigid body attached to a scene node. Only fields supplied are changed; shape fields replace the collision shape (recreates the body).", {
        {"type", "object"},
        {"properties", create_body_properties},
        {"required", json::array({"scene_name"})}
    }});

    json joint_properties = {
        {"scene_name",          {{"type", "string"},  {"description", "Name of the scene"}}},
        {"connected_node_id",   {{"type", "integer"}, {"description", "Connected node ID (no connected node = constrain to the world)"}}},
        {"connected_node_name", {{"type", "string"},  {"description", "Connected node name"}}},
        {"settings_name",       {{"type", "string"},  {"description", "Physics joint settings name from the content library (empty = free six-dof joint)"}}},
        {"enable_collision",    {{"type", "boolean"}, {"description", "Keep collision enabled between the joined bodies (default false)"}}}
    };
    joint_properties.update(node_ref_properties);
    m_tool_infos.push_back({"create_physics_joint", "Attach a new joint (Node_joint) to a scene node (undoable): joins the nearest self-or-ancestor rigid body of the node to that of the connected node (or the world)", {
        {"type", "object"},
        {"properties", joint_properties},
        {"required", json::array({"scene_name"})}
    }});
    json edit_joint_properties = joint_properties;
    edit_joint_properties.update(json{
        {"joint_index",      {{"type", "integer"}, {"description", "Index among the node's joint attachments (default 0)"}}},
        {"connect_to_world", {{"type", "boolean"}, {"description", "Clear the connected node (constrain to the world)"}}},
        {"rebuild",          {{"type", "boolean"}, {"description", "Rebuild the constraint, re-capturing joint frames from current node transforms"}}}
    });
    m_tool_infos.push_back({"edit_physics_joint", "Edit a joint attached to a scene node. Only fields supplied are changed; changes rebuild the constraint.", {
        {"type", "object"},
        {"properties", edit_joint_properties},
        {"required", json::array({"scene_name"})}
    }});

    const json physics_material_value_properties = {
        {"static_friction",     {{"type", "number"}, {"description", "Static friction (default 0.6)"}}},
        {"dynamic_friction",    {{"type", "number"}, {"description", "Dynamic friction (default 0.6)"}}},
        {"restitution",         {{"type", "number"}, {"description", "Restitution (default 0.0)"}}},
        {"friction_combine",    {{"type", "string"}, {"enum", json::array({"average", "minimum", "maximum", "multiply"})}, {"description", "Friction combine mode"}}},
        {"restitution_combine", {{"type", "string"}, {"enum", json::array({"average", "minimum", "maximum", "multiply"})}, {"description", "Restitution combine mode"}}}
    };
    json create_physics_material_properties = {
        {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
        {"name",       {{"type", "string"}, {"description", "Name for the new physics material (must not already exist)"}}}
    };
    create_physics_material_properties.update(physics_material_value_properties);
    m_tool_infos.push_back({"create_physics_material", "Create a shared physics material in the scene's content library (undoable)", {
        {"type", "object"},
        {"properties", create_physics_material_properties},
        {"required", json::array({"scene_name", "name"})}
    }});
    json edit_physics_material_properties = create_physics_material_properties;
    edit_physics_material_properties["name"] = {{"type", "string"}, {"description", "Name of the physics material to edit"}};
    edit_physics_material_properties["new_name"] = {{"type", "string"}, {"description", "Rename the physics material"}};
    m_tool_infos.push_back({"edit_physics_material", "Edit a shared physics material; changes re-apply to all bodies using it. Only fields supplied are changed.", {
        {"type", "object"},
        {"properties", edit_physics_material_properties},
        {"required", json::array({"scene_name", "name"})}
    }});

    const json collision_filter_value_properties = {
        {"collision_systems",        {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Systems the body belongs to"}}},
        {"collide_with_systems",     {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Non-empty = collide only with these systems (allowlist)"}}},
        {"not_collide_with_systems", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Used when collide_with_systems is empty: never collide with these"}}}
    };
    json create_collision_filter_properties = {
        {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
        {"name",       {{"type", "string"}, {"description", "Name for the new collision filter (must not already exist)"}}}
    };
    create_collision_filter_properties.update(collision_filter_value_properties);
    m_tool_infos.push_back({"create_collision_filter", "Create a shared collision filter in the scene's content library (undoable)", {
        {"type", "object"},
        {"properties", create_collision_filter_properties},
        {"required", json::array({"scene_name", "name"})}
    }});
    json edit_collision_filter_properties = create_collision_filter_properties;
    edit_collision_filter_properties["name"] = {{"type", "string"}, {"description", "Name of the collision filter to edit"}};
    edit_collision_filter_properties["new_name"] = {{"type", "string"}, {"description", "Rename the collision filter"}};
    m_tool_infos.push_back({"edit_collision_filter", "Edit a shared collision filter; system lists supplied replace the existing lists and re-apply to all bodies using the filter", {
        {"type", "object"},
        {"properties", edit_collision_filter_properties},
        {"required", json::array({"scene_name", "name"})}
    }});

    const json joint_settings_value_properties = {
        {"limits", {
            {"type", "array"},
            {"description", "Per-axis limit entries"},
            {"items", {
                {"type", "object"},
                {"properties", {
                    {"linear_axes",  {{"type", "array"}, {"items", {{"type", "boolean"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Translation axes [x, y, z] this limit applies to"}}},
                    {"angular_axes", {{"type", "array"}, {"items", {{"type", "boolean"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Rotation axes [x, y, z] this limit applies to"}}},
                    {"min",          {{"type", "number"}, {"description", "Lower bound (absent = unbounded)"}}},
                    {"max",          {{"type", "number"}, {"description", "Upper bound (absent = unbounded; min == max fixes the axis)"}}},
                    {"stiffness",    {{"type", "number"}, {"description", "Soft limit spring stiffness (absent = hard limit)"}}},
                    {"damping",      {{"type", "number"}, {"description", "Soft limit damping"}}}
                }}
            }}
        }},
        {"drives", {
            {"type", "array"},
            {"description", "Drive (motor) entries"},
            {"items", {
                {"type", "object"},
                {"properties", {
                    {"type",            {{"type", "string"},  {"enum", json::array({"linear", "angular"})}}},
                    {"mode",            {{"type", "string"},  {"enum", json::array({"force", "acceleration"})}}},
                    {"axis",            {{"type", "integer"}, {"minimum", 0}, {"maximum", 2}}},
                    {"max_force",       {{"type", "number"},  {"description", "Maximum force (absent = unlimited)"}}},
                    {"position_target", {{"type", "number"}}},
                    {"velocity_target", {{"type", "number"}}},
                    {"stiffness",       {{"type", "number"},  {"description", "> 0 selects a position motor, else a velocity motor"}}},
                    {"damping",         {{"type", "number"}}}
                }}
            }}
        }}
    };
    json create_joint_settings_properties = {
        {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
        {"name",       {{"type", "string"}, {"description", "Name for the new joint settings (must not already exist)"}}}
    };
    create_joint_settings_properties.update(joint_settings_value_properties);
    m_tool_infos.push_back({"create_physics_joint_settings", "Create shared physics joint settings (limits + drives) in the scene's content library (undoable)", {
        {"type", "object"},
        {"properties", create_joint_settings_properties},
        {"required", json::array({"scene_name", "name"})}
    }});
    json edit_joint_settings_properties = create_joint_settings_properties;
    edit_joint_settings_properties["name"] = {{"type", "string"}, {"description", "Name of the joint settings to edit"}};
    edit_joint_settings_properties["new_name"] = {{"type", "string"}, {"description", "Rename the joint settings"}};
    m_tool_infos.push_back({"edit_physics_joint_settings", "Edit shared physics joint settings; limits / drives arrays supplied replace the existing ones and all joints using the settings are rebuilt", {
        {"type", "object"},
        {"properties", edit_joint_settings_properties},
        {"required", json::array({"scene_name", "name"})}
    }});

    // Mesh component (vertex / edge / face) selection, used by Align and Add Joint.
    const json component_mode_property = {
        {"mode", {{"type", "string"}, {"enum", json::array({"object", "vertex", "edge", "face"})}, {"description", "Component granularity: object, vertex, edge or face"}}}
    };
    m_tool_infos.push_back({"set_mesh_component_mode", "Set the mesh-component selection granularity (object / vertex / edge / face). vertex/edge/face are required before select_mesh_components and Align / Add Joint.", {
        {"type", "object"},
        {"properties", component_mode_property},
        {"required", json::array({"mode"})}
    }});
    m_tool_infos.push_back({"select_mesh_components", "Select sub-components (vertices / edges / faces) of a node's mesh, addressed by Geogram indices into its render geometry. extend=false (default) replaces the whole component selection first; extend=true accumulates (use it to add a second node's component for Align / Add Joint). Indices are validated against the geometry. Edges are [v0, v1] vertex-index pairs.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",      {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",         {{"type", "integer"}, {"description", "Node ID (preferred over node_name when both given)"}}},
            {"node_name",       {{"type", "string"},  {"description", "Node name (used when node_id is absent)"}}},
            {"primitive_index", {{"type", "integer"}, {"description", "Mesh primitive index (default 0)"}}},
            {"mode",            {{"type", "string"},  {"enum", json::array({"object", "vertex", "edge", "face"})}, {"description", "Set the component mode before selecting (default: keep current)"}}},
            {"extend",          {{"type", "boolean"}, {"description", "Accumulate instead of replacing the whole selection (default false)"}}},
            {"vertices",        {{"type", "array"},   {"items", {{"type", "integer"}}}, {"description", "Vertex indices to select"}}},
            {"edges",           {{"type", "array"},   {"items", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"minItems", 2}, {"maxItems", 2}}}, {"description", "Edges as [v0, v1] vertex-index pairs"}}},
            {"facets",          {{"type", "array"},   {"items", {{"type", "integer"}}}, {"description", "Facet (polygon) indices to select"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"get_mesh_component_selection", "Get the current mesh-component selection: mode plus each entry's node, primitive index, selected vertices / edges / facets, and whether it is live.", schema_no_args()});
    m_tool_infos.push_back({"clear_mesh_component_selection", "Clear the entire mesh-component selection", schema_no_args()});
    m_tool_infos.push_back({"align_components", "Align the two selected mesh components (of the active vertex/edge/face mode) on two distinct nodes: colocate vertices, align edges, or glue faces. apply_scale also matches scale (edge/face only). Requires exactly two components selected on two distinct nodes. Undoable.", {
        {"type", "object"},
        {"properties", {
            {"apply_scale", {{"type", "boolean"}, {"description", "Also apply uniform scale to match the components (edge/face modes; default false)"}}}
        }}
    }});
    m_tool_infos.push_back({"add_joint", "Align the two selected mesh components, then create a physics joint between the two nodes' rigid bodies (ball for vertex, hinge for edge/face). Both nodes must already have a rigid body. Searches the joint's free rotational DOF for a non-intersecting placement; fails if none is found. Undoable.", {
        {"type", "object"},
        {"properties", {
            {"avoidance", {{"type", "string"}, {"enum", json::array({"joint_pair", "whole_world"})}, {"description", "What the initial-orientation search avoids intersecting: just the joined pair (default) or every body in the physics world"}}}
        }}
    }});
    m_tool_infos.push_back({"flip_joint", "Flip the hinge joint that the selected node is a rigid-body party of: reorients that body by a 180-degree edge-endpoints-swapped flip plus a non-intersecting roll, re-pins its joint frame, and rebuilds the constraint. Select a rigid-body party of a hinge joint first. Undoable.", {
        {"type", "object"},
        {"properties", {
            {"avoidance", {{"type", "string"}, {"enum", json::array({"joint_pair", "whole_world"})}, {"description", "What the roll search avoids intersecting: just the joined pair (default) or every body in the physics world"}}}
        }}
    }});
    m_tool_infos.push_back({"get_physics_state", "Get the live physics motion state of a node's rigid body: motion mode, active flag, world position, and linear / angular velocity (with magnitudes). Poll across frames to observe how a body reacts after an operation.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",    {{"type", "integer"}, {"description", "Node ID (preferred over node_name when both given)"}}},
            {"node_name",  {{"type", "string"},  {"description", "Node name (used when node_id is absent)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});

    // Geogram mesh operations - act on the object selection (set via select_items).
    m_tool_infos.push_back({"remesh", "Geogram isotropic / anisotropic remesh of the selected mesh node(s) to a target vertex count (queued, runs over subsequent frames - poll get_async_status). anisotropy=0 (default) is isotropic; anisotropy>0 (e.g. 0.04) is anisotropic. Acts on the current object selection.", {
        {"type", "object"},
        {"properties", {
            {"target_vertex_count",   {{"type", "integer"}, {"description", "Target vertex count (Geogram nb_points, default 2000)"}}},
            {"anisotropy",            {{"type", "number"},  {"description", "0 = isotropic (default); >0 = anisotropic strength (e.g. 0.04)"}}},
            {"regenerate_attributes", {{"type", "boolean"}, {"description", "Regenerate smooth normals and texture coordinates (default true)"}}}
        }}
    }});
    m_tool_infos.push_back({"decimate", "Geogram vertex-clustering decimation of the selected mesh node(s) (queued). bins is the clustering grid resolution (higher = more detail kept). Acts on the current object selection.", {
        {"type", "object"},
        {"properties", {
            {"bins",                  {{"type", "integer"}, {"description", "Vertex-clustering grid resolution (default 50)"}}},
            {"regenerate_attributes", {{"type", "boolean"}, {"description", "Regenerate smooth normals and texture coordinates (default true)"}}}
        }}
    }});
    m_tool_infos.push_back({"smooth", "Geogram Laplacian smoothing of the selected mesh node(s) (queued). Vertex count is preserved. Acts on the current object selection.", {
        {"type", "object"},
        {"properties", {
            {"iterations",            {{"type", "integer"}, {"description", "Smoothing iterations (default 5)"}}},
            {"strength",              {{"type", "number"},  {"description", "Smoothing strength [0,1] (default 0.5)"}}},
            {"regenerate_attributes", {{"type", "boolean"}, {"description", "Regenerate smooth normals and texture coordinates (default true)"}}}
        }}
    }});
    m_tool_infos.push_back({"generate_texture_coordinates", "Generate texture coordinates for the selected mesh node(s) via Geogram mesh_make_atlas (queued). Writes UVs into the given corner texcoord channel. Acts on the current object selection.", {
        {"type", "object"},
        {"properties", {
            {"texcoord_slot",   {{"type", "integer"}, {"description", "Target corner texcoord channel 0 or 1 (default 0)"}}},
            {"hard_angles_deg", {{"type", "number"},  {"description", "Hard-angle threshold in degrees for chart seams (default 45)"}}},
            {"parameterizer",   {{"type", "integer"}, {"description", "Atlas_parameterizer enum index (default 3 = ABF)"}}},
            {"packer",          {{"type", "integer"}, {"description", "Atlas_packer enum index (default 2 = XAtlas)"}}}
        }}
    }});

    // Transform reference frame for the transform gizmo / numeric edits.
    m_tool_infos.push_back({"set_transform_reference_mode", "Set the orientation reference frame of the transform tool: global (world axes), local (selection's own orientation), reference (a chosen reference node's orientation), or selection (a frame derived from the active mesh-component selection). For reference mode, give reference_node_id or reference_node_name (searched across scenes, or within scene_name when given).", {
        {"type", "object"},
        {"properties", {
            {"mode",                {{"type", "string"}, {"enum", json::array({"global", "local", "reference", "selection"})}, {"description", "Reference mode"}}},
            {"scene_name",          {{"type", "string"},  {"description", "Scene to resolve reference_node in (optional; otherwise all scenes are searched)"}}},
            {"reference_node_id",   {{"type", "integer"}, {"description", "Reference-mode node ID"}}},
            {"reference_node_name", {{"type", "string"},  {"description", "Reference-mode node name"}}},
            {"edge_normal_blend",   {{"type", "number"},  {"description", "Selection mode: [0,1] blend between the two faces sharing a selected edge"}}}
        }},
        {"required", json::array({"mode"})}
    }});
    m_tool_infos.push_back({"set_transform_mode", "Set what the transform tool does to a mesh-component (vertex/edge/face) selection when the gizmo is dragged: move (translate/rotate/scale in place), extrude (duplicate the selection boundary, bridge it with new faces, then move along the gizmo delta), extrude_group_normal (same topology, but each disjoint selection subset slides along its own average normal by an amount derived from the drag), or extrude_vertex_normal (same topology, but each vertex slides along its own normal). Persisted in editor settings; applies to subsequent component edits.", {
        {"type", "object"},
        {"properties", {
            {"mode", {{"type", "string"}, {"enum", json::array({"move", "extrude", "extrude_group_normal", "extrude_vertex_normal"})}, {"description", "Mesh transform mode"}}}
        }},
        {"required", json::array({"mode"})}
    }});

    // Editor commands
    const auto& registered_commands = m_commands.get_commands();
    for (const auto* command : registered_commands) {
        const char* name = command->get_name();
        if (name == nullptr || name[0] == '\0') {
            continue;
        }
        m_tool_infos.push_back({name, std::string{"Editor command: "} + name, schema_no_args()});
    }
}

auto Mcp_server::find_scene(const std::string& name) -> Scene_root*
{
    if (!m_context.app_scenes) {
        return nullptr;
    }
    for (const auto& sr : m_context.app_scenes->get_scene_roots()) {
        if (sr->get_name() == name) {
            return sr.get();
        }
    }
    return nullptr;
}

auto Mcp_server::execute_command(const std::string& tool_name) -> std::string
{
    const auto& registered_commands = m_commands.get_commands();
    for (auto* command : registered_commands) {
        if (tool_name == command->get_name()) {
            const bool success = command->try_call();
            if (success) {
                return make_text_content("Command executed successfully: " + tool_name).dump();
            } else {
                json r = make_text_content("Command failed: " + tool_name);
                r["isError"] = true;
                return r.dump();
            }
        }
    }
    json r = make_text_content("Command not found: " + tool_name);
    r["isError"] = true;
    return r.dump();
}

// --- Query implementations ---

auto Mcp_server::query_list_scenes(const json& args) -> std::string
{
    static_cast<void>(args);

    if (!m_context.app_scenes) {
        return make_text_content("No scenes available").dump();
    }

    json scenes = json::array();
    for (const auto& sr : m_context.app_scenes->get_scene_roots()) {
        const auto& scene = sr->get_scene();
        const auto  library = sr->get_content_library();

        int material_count = 0;
        if (library && library->materials) {
            material_count = static_cast<int>(library->materials->get_all<erhe::primitive::Material>().size());
        }

        int light_count = 0;
        for (const auto& ll : scene.get_light_layers()) {
            light_count += static_cast<int>(ll->lights.size());
        }

        scenes.push_back({
            {"name",                sr->get_name()},
            {"node_count",          static_cast<int>(scene.get_flat_nodes().size())},
            {"camera_count",        static_cast<int>(scene.get_cameras().size())},
            {"light_count",         light_count},
            {"material_count",      material_count},
            {"trigger_event_count", sr->get_trigger_event_count()}
        });
    }

    return make_json_content({{"scenes", scenes}}).dump();
}

auto Mcp_server::query_scene_nodes(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const auto& scene = sr->get_scene();
    json nodes = json::array();
    for (const auto& node : scene.get_flat_nodes()) {
        const auto& trs = node->parent_from_node_transform();
        const glm::vec3 t = trs.get_translation();
        const glm::quat r = trs.get_rotation();
        const glm::vec3 s = trs.get_scale();

        json attachment_types = json::array();
        for (const auto& att : node->get_attachments()) {
            attachment_types.push_back(std::string{att->get_type_name()});
        }

        auto parent_node = node->get_parent_node();

        json tags_arr = json::array();
        for (const auto& tag : node->get_tags()) {
            tags_arr.push_back(tag);
        }

        nodes.push_back({
            {"name",             node->get_name()},
            {"id",               node->get_id()},
            {"parent",           parent_node ? parent_node->get_name() : ""},
            {"position",         {t.x, t.y, t.z}},
            {"rotation_xyzw",    {r.x, r.y, r.z, r.w}},
            {"scale",            {s.x, s.y, s.z}},
            {"attachment_types", attachment_types},
            {"locked",           node->is_lock_edit()},
            {"tags",             tags_arr}
        });
    }

    return make_json_content({{"nodes", nodes}}).dump();
}

auto Mcp_server::query_node_details(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    const std::string node_name  = args.value("node_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const auto& scene = sr->get_scene();
    std::shared_ptr<erhe::scene::Node> found_node;
    for (const auto& node : scene.get_flat_nodes()) {
        if (node->get_name() == node_name) {
            found_node = node;
            break;
        }
    }
    if (!found_node) {
        json r = make_text_content("Node not found: " + node_name);
        r["isError"] = true;
        return r.dump();
    }

    const auto& trs = found_node->parent_from_node_transform();
    const glm::vec3 t = trs.get_translation();
    const glm::quat r = trs.get_rotation();
    const glm::vec3 s = trs.get_scale();
    const glm::vec3 k = trs.get_skew();
    const auto& world_trs = found_node->world_from_node_transform();
    const glm::vec3 wt = world_trs.get_translation();
    const glm::quat wr = world_trs.get_rotation();
    const glm::vec3 ws = world_trs.get_scale();
    const glm::vec3 wk = world_trs.get_skew();
    const glm::vec4 wp = found_node->position_in_world();

    json attachments = json::array();
    for (const auto& att : found_node->get_attachments()) {
        json att_json = {
            {"type", std::string{att->get_type_name()}},
            {"name", att->get_name()}
        };

        auto mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(att);
        if (mesh) {
            att_json["primitive_count"] = static_cast<int>(mesh->get_primitives().size());
            json mat_names = json::array();
            int total_vertices = 0;
            int total_facets = 0;
            for (const auto& prim : mesh->get_primitives()) {
                mat_names.push_back(prim.material ? prim.material->get_name() : "(none)");
                if (prim.primitive && prim.primitive->render_shape) {
                    const auto& geom = prim.primitive->render_shape->get_geometry_const();
                    if (geom) {
                        total_vertices += static_cast<int>(geom->get_mesh().vertices.nb());
                        total_facets   += static_cast<int>(geom->get_mesh().facets.nb());
                    }
                }
            }
            att_json["materials"]     = mat_names;
            att_json["vertex_count"]  = total_vertices;
            att_json["facet_count"]   = total_facets;
        }

        auto camera = std::dynamic_pointer_cast<erhe::scene::Camera>(att);
        if (camera) {
            att_json["exposure"]     = camera->get_exposure();
            att_json["shadow_range"] = camera->get_shadow_range();
        }

        auto light = std::dynamic_pointer_cast<erhe::scene::Light>(att);
        if (light) {
            const char* type_str = (light->type == erhe::scene::Light_type::directional) ? "directional"
                                 : (light->type == erhe::scene::Light_type::point)       ? "point"
                                 : (light->type == erhe::scene::Light_type::spot)         ? "spot"
                                 : "unknown";
            att_json["light_type"] = type_str;
            att_json["color"]      = {light->color.x, light->color.y, light->color.z};
            att_json["intensity"]  = light->intensity;
            att_json["range"]      = light->range;
        }

        auto bp = std::dynamic_pointer_cast<Brush_placement>(att);
        if (bp) {
            auto brush = bp->get_brush();
            if (brush) {
                att_json["brush_name"] = brush->get_name();
                att_json["brush_id"]   = brush->get_id();
            }
        }

        auto node_physics = std::dynamic_pointer_cast<Node_physics>(att);
        if (node_physics) {
            att_json["motion_mode"]    = motion_mode_to_string(node_physics->get_motion_mode());
            att_json["is_trigger"]     = node_physics->is_trigger();
            att_json["gravity_factor"] = node_physics->get_gravity_factor();
            const std::shared_ptr<erhe::physics::ICollision_shape>& shape = node_physics->get_collision_shape();
            att_json["collision_shape"] = shape ? shape->describe() : "";
            const std::shared_ptr<erhe::physics::Physics_material>& physics_material = node_physics->get_physics_material();
            att_json["physics_material"] = physics_material ? physics_material->get_name() : "";
            const std::shared_ptr<erhe::physics::Collision_filter>& collision_filter = node_physics->get_collision_filter();
            att_json["collision_filter"] = collision_filter ? collision_filter->get_name() : "";
            const erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
            if (rigid_body != nullptr) {
                att_json["mass"]        = rigid_body->get_mass();
                att_json["friction"]    = rigid_body->get_friction();
                att_json["restitution"] = rigid_body->get_restitution();
                att_json["is_active"]   = rigid_body->is_active();
            }
        }

        auto node_joint = std::dynamic_pointer_cast<Node_joint>(att);
        if (node_joint) {
            const std::shared_ptr<erhe::scene::Node> connected = node_joint->get_connected_node();
            att_json["connected_node"]   = connected ? connected->get_name() : "(world)";
            const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings = node_joint->get_settings();
            att_json["joint_settings"]   = settings ? settings->get_name() : "";
            att_json["enable_collision"] = node_joint->get_enable_collision();
            att_json["constraint"]       = (node_joint->get_constraint() != nullptr) ? "created" : "pending";
        }

        attachments.push_back(att_json);
    }

    json children = json::array();
    for (const auto& child : found_node->get_children()) {
        children.push_back(child->get_name());
    }

    auto parent_node = found_node->get_parent_node();

    json result = {
        {"name",           found_node->get_name()},
        {"id",             found_node->get_id()},
        {"parent",         parent_node ? parent_node->get_name() : ""},
        {"world_position", {wp.x, wp.y, wp.z}},
        {"local_transform", {
            {"translation",   {t.x, t.y, t.z}},
            {"rotation_xyzw", {r.x, r.y, r.z, r.w}},
            {"scale",         {s.x, s.y, s.z}},
            {"skew",          {k.x, k.y, k.z}}
        }},
        {"world_transform", {
            {"translation",   {wt.x, wt.y, wt.z}},
            {"rotation_xyzw", {wr.x, wr.y, wr.z, wr.w}},
            {"scale",         {ws.x, ws.y, ws.z}},
            {"skew",          {wk.x, wk.y, wk.z}}
        }},
        {"attachments",    attachments},
        {"children",       children},
        {"visible",        found_node->is_visible()},
        {"selected",       found_node->is_selected()},
        {"locked",         found_node->is_lock_edit()},
        {"tags",           [&]() { json t = json::array(); for (const auto& tag : found_node->get_tags()) t.push_back(tag); return t; }()}
    };

    return make_json_content(result).dump();
}

auto Mcp_server::query_scene_cameras(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    json cameras = json::array();
    for (const auto& camera : sr->get_scene().get_cameras()) {
        const auto* node = camera->get_node();
        cameras.push_back({
            {"name",         camera->get_name()},
            {"id",           camera->get_id()},
            {"node",         node ? node->get_name() : ""},
            {"exposure",     camera->get_exposure()},
            {"shadow_range", camera->get_shadow_range()}
        });
    }

    return make_json_content({{"cameras", cameras}}).dump();
}

auto Mcp_server::query_scene_lights(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    json lights = json::array();
    for (const auto& ll : sr->get_scene().get_light_layers()) {
        for (const auto& light : ll->lights) {
            const auto* node = light->get_node();
            const char* type_str = (light->type == erhe::scene::Light_type::directional) ? "directional"
                                 : (light->type == erhe::scene::Light_type::point)       ? "point"
                                 : (light->type == erhe::scene::Light_type::spot)         ? "spot"
                                 : "unknown";
            lights.push_back({
                {"name",      light->get_name()},
                {"id",        light->get_id()},
                {"node",      node ? node->get_name() : ""},
                {"type",      type_str},
                {"color",     {light->color.x, light->color.y, light->color.z}},
                {"intensity", light->intensity},
                {"range",     light->range}
            });
        }
    }

    return make_json_content({{"lights", lights}}).dump();
}

auto Mcp_server::query_shadow_fit_debug(const json& args) -> std::string
{
    static_cast<void>(args);
    if (m_context.app_rendering == nullptr) {
        json r = make_text_content("App_rendering not available");
        r["isError"] = true;
        return r.dump();
    }

    const std::vector<std::shared_ptr<Shadow_render_node>>& shadow_nodes = m_context.app_rendering->get_all_shadow_nodes();
    json nodes = json::array();
    for (std::size_t i = 0; i < shadow_nodes.size(); ++i) {
        const std::shared_ptr<Shadow_render_node>& node = shadow_nodes[i];
        if (!node) {
            continue;
        }
        json node_json = dump_shadow_fit_debug(node->get_light_projections());
        node_json["shadow_node_index"] = i;
        nodes.push_back(node_json);
    }

    return make_json_content({{"shadow_nodes", nodes}}).dump();
}

auto Mcp_server::query_scene_materials(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    auto library = sr->get_content_library();
    if (!library || !library->materials) {
        return make_json_content({{"materials", json::array()}}).dump();
    }

    json materials = json::array();
    const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
    for (const auto& mat : mat_list) {
        materials.push_back({
            {"name",       mat->get_name()},
            {"id",         mat->get_id()},
            {"base_color", {mat->data.base_color.x, mat->data.base_color.y, mat->data.base_color.z}},
            {"metallic",   mat->data.metallic},
            {"roughness",  mat->data.roughness.x},
            {"emissive",   {mat->data.emissive.x, mat->data.emissive.y, mat->data.emissive.z}}
        });
    }

    return make_json_content({{"materials", materials}}).dump();
}

auto Mcp_server::query_material_details(const json& args) -> std::string
{
    const std::string scene_name    = args.value("scene_name", "");
    const std::string material_name = args.value("material_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    auto library = sr->get_content_library();
    if (!library || !library->materials) {
        json r = make_text_content("No materials in scene: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
    for (const auto& mat : mat_list) {
        if (mat->get_name() == material_name) {
            const auto& d = mat->data;
            auto sampler_to_json = [](const erhe::primitive::Material_texture_sampler& s) -> json {
                json entry = {
                    {"tex_coord", s.tex_coord},
                    {"rotation",  s.rotation},
                    {"offset",    {s.offset.x, s.offset.y}},
                    {"scale",     {s.scale.x,  s.scale.y}}
                };
                if (s.texture) {
                    entry["texture_id"]   = s.texture->get_id();
                    entry["texture_name"] = s.texture->get_name();
                } else {
                    entry["texture_id"]   = nullptr;
                    entry["texture_name"] = nullptr;
                }
                return entry;
            };
            json result = {
                {"name",                       mat->get_name()},
                {"id",                         mat->get_id()},
                {"base_color",                 {d.base_color.x, d.base_color.y, d.base_color.z}},
                {"opacity",                    d.opacity},
                {"roughness",                  {d.roughness.x, d.roughness.y}},
                {"metallic",                   d.metallic},
                {"reflectance",                d.reflectance},
                {"emissive",                   {d.emissive.x, d.emissive.y, d.emissive.z}},
                {"normal_texture_scale",       d.normal_texture_scale},
                {"occlusion_texture_strength", d.occlusion_texture_strength},
                {"bxdf_model",
                    (d.bxdf_model == erhe::primitive::Bxdf_model::unlit)                    ? "unlit" :
                    (d.bxdf_model == erhe::primitive::Bxdf_model::anisotropic_brdf)         ? "anisotropic_brdf" :
                    (d.bxdf_model == erhe::primitive::Bxdf_model::anisotropic_slope)        ? "anisotropic_slope" :
                    (d.bxdf_model == erhe::primitive::Bxdf_model::anisotropic_engine_ready) ? "anisotropic_engine_ready" :
                                                                                              "isotropic_brdf"},
                {"use_circular_brushed_metal", d.use_circular_brushed_metal},
                {"use_aniso_control",          d.use_aniso_control},
                {"texture_samplers", {
                    {"base_color",         sampler_to_json(d.texture_samplers.base_color)},
                    {"metallic_roughness", sampler_to_json(d.texture_samplers.metallic_roughness)},
                    {"normal",             sampler_to_json(d.texture_samplers.normal)},
                    {"occlusion",          sampler_to_json(d.texture_samplers.occlusion)},
                    {"emissive",           sampler_to_json(d.texture_samplers.emissive)}
                }}
            };
            return make_json_content(result).dump();
        }
    }

    json r = make_text_content("Material not found: " + material_name);
    r["isError"] = true;
    return r.dump();
}

auto Mcp_server::query_scene_textures(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    auto library = sr->get_content_library();
    if (!library || !library->textures) {
        return make_json_content({{"textures", json::array()}}).dump();
    }

    json textures = json::array();
    const auto& tex_list = library->textures->get_all<erhe::graphics::Texture>();
    for (const auto& tex : tex_list) {
        textures.push_back({
            {"name",   tex->get_name()},
            {"id",     tex->get_id()},
            {"width",  tex->get_width()},
            {"height", tex->get_height()},
            {"format", erhe::dataformat::c_str(tex->get_pixelformat())}
        });
    }

    return make_json_content({{"textures", textures}}).dump();
}

auto Mcp_server::query_scene_brushes(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    auto library = sr->get_content_library();
    if (!library || !library->brushes) {
        return make_json_content({{"brushes", json::array()}}).dump();
    }

    json brushes = json::array();
    const auto& brush_list = library->brushes->get_all<Brush>();
    for (const auto& brush : brush_list) {
        auto geometry = brush->get_geometry();
        const GEO::index_t vertex_count = geometry ? geometry->get_mesh().vertices.nb() : 0;
        const GEO::index_t facet_count  = geometry ? geometry->get_mesh().facets.nb()   : 0;
        brushes.push_back({
            {"name",         brush->get_name()},
            {"id",           brush->get_id()},
            {"vertex_count", vertex_count},
            {"facet_count",  facet_count}
        });
    }

    return make_json_content({{"brushes", brushes}}).dump();
}

auto Mcp_server::query_selection(const json& args) -> std::string
{
    static_cast<void>(args);

    if (!m_context.selection) {
        return make_json_content({{"items", json::array()}}).dump();
    }

    json items = json::array();
    for (const auto& item : m_context.selection->get_selected_items()) {
        items.push_back({
            {"name",      item->get_name()},
            {"type",      std::string{item->get_type_name()}},
            {"id",        item->get_id()}
        });
    }

    json result = {{"items", items}};
    if (m_context.transform_tool != nullptr) {
        result["transform_reference_mode"] = transform_reference_mode_lc(m_context.transform_tool->shared.settings.reference_mode);
    }
    if (m_context.mesh_component_selection != nullptr) {
        result["mesh_component_mode"] = mesh_component_mode_lc(m_context.mesh_component_selection->get_mode());
    }
    if (m_context.editor_settings != nullptr) {
        result["transform_mode"] = std::string{::to_string(m_context.editor_settings->transform_mode)};
    }
    return make_json_content(result).dump();
}

auto Mcp_server::query_undo_redo_stack(const json& args) -> std::string
{
    static_cast<void>(args);

    if (!m_context.operation_stack) {
        return make_json_content({{"undo", json::array()}, {"redo", json::array()}, {"can_undo", false}, {"can_redo", false}}).dump();
    }

    auto make_stack = [](const std::vector<std::shared_ptr<Operation>>& ops) -> json {
        json arr = json::array();
        for (const auto& op : ops) {
            json entry = {{"description", op->describe()}};
            if (op->has_error()) {
                entry["error"] = op->get_error();
            }
            arr.push_back(entry);
        }
        return arr;
    };

    return make_json_content({
        {"undo",     make_stack(m_context.operation_stack->get_undo_stack())},
        {"redo",     make_stack(m_context.operation_stack->get_redo_stack())},
        {"can_undo", m_context.operation_stack->can_undo()},
        {"can_redo", m_context.operation_stack->can_redo()}
    }).dump();
}

auto Mcp_server::query_async_status(const json& args) -> std::string
{
    static_cast<void>(args);

    return make_json_content({
        {"pending", m_context.pending_async_ops.load()},
        {"running", m_context.running_async_ops.load()}
    }).dump();
}

auto Mcp_server::find_items_by_ids(Scene_root& sr, const std::set<std::size_t>& target_ids) -> std::vector<std::shared_ptr<erhe::Item_base>>
{
    std::vector<std::shared_ptr<erhe::Item_base>> result;

    const auto& scene = sr.get_scene();
    for (const auto& node : scene.get_flat_nodes()) {
        if (target_ids.contains(node->get_id())) {
            result.push_back(node);
        }
    }
    for (const auto& camera : scene.get_cameras()) {
        if (target_ids.contains(camera->get_id())) {
            result.push_back(camera);
        }
    }
    for (const auto& ll : scene.get_light_layers()) {
        for (const auto& light : ll->lights) {
            if (target_ids.contains(light->get_id())) {
                result.push_back(light);
            }
        }
    }
    auto library = sr.get_content_library();
    if (library) {
        if (library->materials) {
            for (const auto& mat : library->materials->get_all<erhe::primitive::Material>()) {
                if (target_ids.contains(mat->get_id())) {
                    result.push_back(mat);
                }
            }
        }
        if (library->brushes) {
            for (const auto& brush : library->brushes->get_all<Brush>()) {
                if (target_ids.contains(brush->get_id())) {
                    result.push_back(brush);
                }
            }
        }
    }
    return result;
}

auto Mcp_server::action_select_items(const json& args) -> std::string
{
    if (!m_context.selection) {
        json r = make_text_content("Selection system not available");
        r["isError"] = true;
        return r.dump();
    }

    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const json ids_json = args.value("ids", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& id_val : ids_json) {
        if (id_val.is_number_unsigned() || id_val.is_number_integer()) {
            target_ids.insert(id_val.get<std::size_t>());
        }
    }

    if (target_ids.empty()) {
        m_context.selection->clear_selection();
        return make_text_content("Selection cleared").dump();
    }

    auto items_to_select = find_items_by_ids(*sr, target_ids);
    m_context.selection->set_selection(items_to_select);

    json selected = json::array();
    for (const auto& item : items_to_select) {
        selected.push_back({
            {"name", item->get_name()},
            {"type", std::string{item->get_type_name()}},
            {"id",   item->get_id()}
        });
    }

    return make_json_content({
        {"selected_count", static_cast<int>(items_to_select.size())},
        {"items",          selected}
    }).dump();
}

auto Mcp_server::action_transform_selection(const json& args) -> std::string
{
    Transform_tool* transform_tool = m_context.transform_tool;
    if (transform_tool == nullptr) {
        json r = make_text_content("Transform tool not available");
        r["isError"] = true;
        return r.dump();
    }

    Transform_tool_shared& shared = transform_tool->shared;
    // A node selection populates shared.entries; an active mesh-component selection
    // instead sets shared.component_mode (entries stays empty). Either is a valid target,
    // matching the gizmo's own "nothing to transform" condition used elsewhere.
    if (shared.entries.empty() && !shared.component_mode) {
        json r = make_text_content("Nothing to transform - select node(s) with select_items, or a mesh-component selection with select_mesh_components");
        r["isError"] = true;
        return r.dump();
    }

    const std::string space = args.value("space", "global");
    if ((space != "local") && (space != "global")) {
        json r = make_text_content("Invalid space '" + space + "' (expected 'local' or 'global')");
        r["isError"] = true;
        return r.dump();
    }
    const bool local = (space == "local");
    if (local && (shared.entries.size() != 1)) {
        json r = make_text_content("Local space edit requires exactly one selected node");
        r["isError"] = true;
        return r.dump();
    }

    std::string parse_error;
    auto read_floats = [&args, &parse_error](const char* key, float* out_values, std::size_t count) -> bool {
        if (!args.contains(key)) {
            return false;
        }
        const json& value = args.at(key);
        const bool shape_ok = value.is_array() && (value.size() == count);
        if (shape_ok) {
            for (std::size_t i = 0; i < count; ++i) {
                if (!value[i].is_number()) {
                    parse_error = std::string{key} + " must be an array of " + std::to_string(count) + " numbers";
                    return false;
                }
                out_values[i] = value[i].get<float>();
            }
            return true;
        }
        parse_error = std::string{key} + " must be an array of " + std::to_string(count) + " numbers";
        return false;
    };

    std::optional<glm::vec3> translation;
    std::optional<glm::quat> rotation;
    std::optional<glm::vec3> scale;
    std::optional<glm::vec3> skew;
    float v[4];
    if (read_floats("translation",   v, 3)) { translation = glm::vec3{v[0], v[1], v[2]};       }
    if (read_floats("rotation_xyzw", v, 4)) { rotation    = glm::quat{v[3], v[0], v[1], v[2]}; }
    if (read_floats("scale",         v, 3)) { scale       = glm::vec3{v[0], v[1], v[2]};       }
    if (read_floats("skew",          v, 3)) { skew        = glm::vec3{v[0], v[1], v[2]};       }
    if (!parse_error.empty()) {
        json r = make_text_content(parse_error);
        r["isError"] = true;
        return r.dump();
    }
    if (!translation.has_value() && !rotation.has_value() && !scale.has_value() && !skew.has_value()) {
        json r = make_text_content("Nothing to apply - provide translation, rotation_xyzw, scale and/or skew");
        r["isError"] = true;
        return r.dump();
    }

    json applied = json::object();
    if (translation.has_value()) {
        transform_tool->apply_translation_edit(translation.value(), local);
        applied["translation"] = {translation->x, translation->y, translation->z};
    }
    if (rotation.has_value()) {
        transform_tool->apply_rotation_edit(rotation.value(), local);
        applied["rotation_xyzw"] = {rotation->x, rotation->y, rotation->z, rotation->w};
    }
    if (scale.has_value()) {
        transform_tool->apply_scale_edit(scale.value(), local);
        applied["scale"] = {scale->x, scale->y, scale->z};
    }
    if (skew.has_value()) {
        transform_tool->apply_skew_edit(skew.value(), local);
        applied["skew"] = {skew->x, skew->y, skew->z};
    }

    const bool end_edit = args.value("end_edit", true);
    if (end_edit) {
        // Mirror the gizmo drag-release: the node record path is a no-op in component
        // mode (shared.entries is empty there), so a mesh-component edit (move / extrude /
        // extrude_group_normal / extrude_vertex_normal) must be finalized via
        // commit_component_edit() to queue its undoable operation. Without this, an MCP
        // component edit would deform the live geometry but never commit (and never finalize
        // extrude normals).
        transform_tool->record_transform_operation();
        if (shared.component_mode && transform_tool->is_component_edit_active()) {
            transform_tool->commit_component_edit();
        }
    }

    auto trs_to_json = [](const erhe::scene::Trs_transform& trs) -> json {
        const glm::vec3 t = trs.get_translation();
        const glm::quat r = trs.get_rotation();
        const glm::vec3 s = trs.get_scale();
        const glm::vec3 k = trs.get_skew();
        return json{
            {"translation",   {t.x, t.y, t.z}},
            {"rotation_xyzw", {r.x, r.y, r.z, r.w}},
            {"scale",         {s.x, s.y, s.z}},
            {"skew",          {k.x, k.y, k.z}}
        };
    };

    json nodes = json::array();
    for (const Transform_entry& entry : shared.entries) {
        if (!entry.node) {
            continue;
        }
        nodes.push_back({
            {"name",            entry.node->get_name()},
            {"id",              entry.node->get_id()},
            {"local_transform", trs_to_json(entry.node->parent_from_node_transform())},
            {"world_transform", trs_to_json(entry.node->world_from_node_transform())}
        });
    }

    return make_json_content({
        {"space",    space},
        {"applied",  applied},
        {"end_edit", end_edit},
        {"nodes",    nodes}
    }).dump();
}

auto Mcp_server::action_place_brush(const json& args) -> std::string
{
    const std::string scene_name    = args.value("scene_name", "");
    const std::size_t brush_id      = args.value("brush_id", std::size_t{0});
    const json        pos_json      = args.value("position", json::array());
    const std::string material_name = args.value("material_name", "");
    const double      scale         = args.value("scale", 1.0);
    const std::string motion_str    = args.value("motion_mode", "dynamic");

    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    // Find brush by ID
    auto library = sr->get_content_library();
    if (!library || !library->brushes) {
        json r = make_text_content("No brushes in scene");
        r["isError"] = true;
        return r.dump();
    }

    std::shared_ptr<Brush> brush;
    const auto& brush_list = library->brushes->get_all<Brush>();
    for (const auto& b : brush_list) {
        if (b->get_id() == brush_id) {
            brush = b;
            break;
        }
    }
    if (!brush) {
        json r = make_text_content("Brush not found with id: " + std::to_string(brush_id));
        r["isError"] = true;
        return r.dump();
    }

    // Parse position
    glm::vec3 position{0.0f};
    if (pos_json.is_array() && pos_json.size() >= 3) {
        position.x = pos_json[0].get<float>();
        position.y = pos_json[1].get<float>();
        position.z = pos_json[2].get<float>();
    }

    // Find material
    std::shared_ptr<erhe::primitive::Material> material;
    if (!material_name.empty() && library->materials) {
        const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
        for (const auto& mat : mat_list) {
            if (mat->get_name() == material_name) {
                material = mat;
                break;
            }
        }
    }
    if (!material && library->materials) {
        const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
        if (!mat_list.empty()) {
            material = mat_list.front();
        }
    }
    if (!material) {
        json r = make_text_content("No materials available");
        r["isError"] = true;
        return r.dump();
    }

    // Motion mode
    erhe::physics::Motion_mode motion_mode = erhe::physics::Motion_mode::e_dynamic;
    if (motion_str == "static") {
        motion_mode = erhe::physics::Motion_mode::e_static;
    }

    const glm::mat4 world_from_node = erhe::math::create_translation<float>(position);

    auto node = place_brush_in_scene(
        m_context,
        *brush,
        *sr,
        world_from_node,
        material,
        scale,
        motion_mode
    );

    if (!node) {
        json r = make_text_content("Failed to place brush");
        r["isError"] = true;
        return r.dump();
    }

    json result = {
        {"node_name", node->get_name()},
        {"node_id",   node->get_id()},
        {"brush",     brush->get_name()},
        {"material",  material->get_name()},
        {"position",  {position.x, position.y, position.z}},
        {"scale",     scale}
    };
    return make_json_content(result).dump();
}

auto Mcp_server::action_create_shape(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const std::string shape = args.value("shape", "");
    if ((shape != "box") && (shape != "uv_sphere") && (shape != "cone") && (shape != "capsule") && (shape != "torus")) {
        json r = make_text_content("Invalid shape '" + shape + "' (expected box, uv_sphere, cone, capsule or torus)");
        r["isError"] = true;
        return r.dump();
    }

    const bool make_instance = args.value("instance", true);
    const bool add_brush     = args.value("add_brush", false);
    if (!make_instance && !add_brush) {
        json r = make_text_content("Nothing to do - enable instance and/or add_brush");
        r["isError"] = true;
        return r.dump();
    }

    auto read_vec3 = [&args](const char* key, glm::vec3& out_value) {
        const json value = args.value(key, json());
        if (value.is_array() && (value.size() == 3)) {
            out_value = glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        }
    };
    auto read_ivec3 = [&args](const char* key, glm::ivec3& out_value) {
        const json value = args.value(key, json());
        if (value.is_array() && (value.size() == 3)) {
            out_value = glm::ivec3{value[0].get<int>(), value[1].get<int>(), value[2].get<int>()};
        }
    };

    Brush_data brush_create_info{
        .context      = m_context,
        .app_settings = *m_context.app_settings,
        .name         = args.value("name", shape),
        .build_info   = erhe::primitive::Build_info{
            .primitive_types = {
                .fill_triangles  = true,
                .edge_lines      = true,
                .corner_points   = true,
                .centroid_points = true
            },
            .buffer_info     = m_context.mesh_memory->make_primitive_buffer_info()
        },
        .normal_style = erhe::primitive::Normal_style::point_normals,
        .density      = 1.0f
    };

    std::shared_ptr<Brush> brush;
    json parameters_echo = json::object();
    if (shape == "box") {
        Box_parameters parameters;
        read_vec3 ("size",  parameters.size);
        read_ivec3("steps", parameters.steps);
        parameters.power = args.value("power", parameters.power);
        brush = Create_box::create_brush(brush_create_info, parameters);
        parameters_echo = {
            {"size",  {parameters.size.x,  parameters.size.y,  parameters.size.z}},
            {"steps", {parameters.steps.x, parameters.steps.y, parameters.steps.z}},
            {"power", parameters.power}
        };
    } else if (shape == "uv_sphere") {
        Uv_sphere_parameters parameters;
        parameters.radius      = args.value("radius",      parameters.radius);
        parameters.slice_count = args.value("slice_count", parameters.slice_count);
        parameters.stack_count = args.value("stack_count", parameters.stack_count);
        brush = Create_uv_sphere::create_brush(brush_create_info, parameters);
        parameters_echo = {
            {"radius",      parameters.radius},
            {"slice_count", parameters.slice_count},
            {"stack_count", parameters.stack_count}
        };
    } else if (shape == "cone") {
        Cone_parameters parameters;
        parameters.height        = args.value("height",        parameters.height);
        parameters.bottom_radius = args.value("bottom_radius", parameters.bottom_radius);
        parameters.top_radius    = args.value("top_radius",    parameters.top_radius);
        parameters.use_top       = args.value("use_top",       parameters.use_top);
        parameters.use_bottom    = args.value("use_bottom",    parameters.use_bottom);
        parameters.slice_count   = args.value("slice_count",   parameters.slice_count);
        parameters.stack_count   = args.value("stack_count",   parameters.stack_count);
        brush = Create_cone::create_brush(brush_create_info, parameters);
        parameters_echo = {
            {"height",        parameters.height},
            {"bottom_radius", parameters.bottom_radius},
            {"top_radius",    parameters.top_radius},
            {"use_top",       parameters.use_top},
            {"use_bottom",    parameters.use_bottom},
            {"slice_count",   parameters.slice_count},
            {"stack_count",   parameters.stack_count}
        };
    } else if (shape == "capsule") {
        Capsule_parameters parameters;
        parameters.length                 = args.value("length",        parameters.length);
        parameters.bottom_radius          = args.value("bottom_radius", parameters.bottom_radius);
        parameters.top_radius             = args.value("top_radius",    parameters.top_radius);
        parameters.slice_count            = args.value("slice_count",   parameters.slice_count);
        parameters.hemisphere_stack_count = args.value("stack_count",   parameters.hemisphere_stack_count);
        // make_capsule() requires |bottom_radius - top_radius| < length when the
        // radii differ: the tangent cone between the cap spheres exists only
        // while neither sphere contains the other.
        if (
            (parameters.bottom_radius != parameters.top_radius) &&
            (parameters.length <= std::abs(parameters.bottom_radius - parameters.top_radius))
        ) {
            json r = make_text_content("Tapered capsule requires length > |bottom_radius - top_radius|");
            r["isError"] = true;
            return r.dump();
        }
        brush = Create_capsule::create_brush(brush_create_info, parameters);
        parameters_echo = {
            {"length",        parameters.length},
            {"bottom_radius", parameters.bottom_radius},
            {"top_radius",    parameters.top_radius},
            {"slice_count",   parameters.slice_count},
            {"stack_count",   parameters.hemisphere_stack_count},
            {"tapered",       parameters.bottom_radius != parameters.top_radius}
        };
    } else { // torus
        Torus_parameters parameters;
        parameters.major_radius = args.value("major_radius", parameters.major_radius);
        parameters.minor_radius = args.value("minor_radius", parameters.minor_radius);
        parameters.major_steps  = args.value("major_steps",  parameters.major_steps);
        parameters.minor_steps  = args.value("minor_steps",  parameters.minor_steps);
        brush = Create_torus::create_brush(brush_create_info, parameters);
        parameters_echo = {
            {"major_radius", parameters.major_radius},
            {"minor_radius", parameters.minor_radius},
            {"major_steps",  parameters.major_steps},
            {"minor_steps",  parameters.minor_steps}
        };
    }

    if (!brush) {
        json r = make_text_content("Failed to create shape: " + shape);
        r["isError"] = true;
        return r.dump();
    }

    json result = {
        {"shape",      shape},
        {"name",       brush->get_name()},
        {"parameters", parameters_echo}
    };

    auto library = sr->get_content_library();
    if (add_brush) {
        if (!library || !library->brushes) {
            json r = make_text_content("No brush library in scene");
            r["isError"] = true;
            return r.dump();
        }
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{library->mutex};
        library->brushes->add(brush);
        result["brush_id"] = brush->get_id();
    }

    if (make_instance) {
        std::shared_ptr<erhe::primitive::Material> material;
        const std::string material_name = args.value("material_name", "");
        if (!material_name.empty() && library && library->materials) {
            const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
            for (const auto& mat : mat_list) {
                if (mat->get_name() == material_name) {
                    material = mat;
                    break;
                }
            }
        }
        if (!material && library && library->materials) {
            const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
            if (!mat_list.empty()) {
                material = mat_list.front();
            }
        }
        if (!material) {
            json r = make_text_content("No materials available");
            r["isError"] = true;
            return r.dump();
        }

        glm::vec3 position{0.0f};
        read_vec3("position", position);

        std::shared_ptr<erhe::scene::Node> parent;
        if (args.contains("parent_node_id")) {
            const std::size_t parent_node_id = args.value("parent_node_id", std::size_t{0});
            for (const auto& node : sr->get_scene().get_flat_nodes()) {
                if (node->get_id() == parent_node_id) {
                    parent = node;
                    break;
                }
            }
            if (!parent) {
                json r = make_text_content("Parent node not found with id: " + std::to_string(parent_node_id));
                r["isError"] = true;
                return r.dump();
            }
        }

        const double scale = args.value("scale", 1.0);
        const erhe::physics::Motion_mode motion_mode = parse_motion_mode(
            args.value("motion_mode", "dynamic"),
            erhe::physics::Motion_mode::e_dynamic
        );

        const glm::mat4 world_from_node = erhe::math::create_translation<float>(position);
        auto instance_node = place_brush_in_scene(m_context, *brush, *sr, world_from_node, material, scale, motion_mode, parent);
        if (!instance_node) {
            json r = make_text_content("Failed to create shape instance");
            r["isError"] = true;
            return r.dump();
        }
        result["node_name"]   = instance_node->get_name();
        result["node_id"]     = instance_node->get_id();
        result["material"]    = material->get_name();
        result["position"]    = {position.x, position.y, position.z};
        result["motion_mode"] = motion_mode_to_string(motion_mode);
        result["parent"]      = parent ? parent->get_name() : "(scene root)";
    }

    return make_json_content(result).dump();
}

auto Mcp_server::action_create_node(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    std::shared_ptr<erhe::scene::Node> parent{};
    if (args.contains("parent_node_id") || args.contains("parent_node_name")) {
        parent = find_node_in_scene(*sr, args, "parent_node_id", "parent_node_name");
        if (!parent) {
            json r = make_text_content("Parent node not found");
            r["isError"] = true;
            return r.dump();
        }
    } else {
        parent = sr->get_hosted_scene()->get_root_node();
    }

    const std::shared_ptr<erhe::scene::Node> node = m_context.scene_commands->create_new_empty_node(parent.get());
    if (!node) {
        json r = make_text_content("Failed to create node");
        r["isError"] = true;
        return r.dump();
    }

    const std::string name = args.value("name", "");
    if (!name.empty()) {
        node->set_name(name);
    }

    glm::vec3 position{0.0f};
    const json pos_json = args.value("position", json());
    if (pos_json.is_array() && (pos_json.size() == 3)) {
        position = glm::vec3{pos_json[0].get<float>(), pos_json[1].get<float>(), pos_json[2].get<float>()};
    }
    // The node is not yet attached (the insert executes on the next editor
    // frame); setting the world transform now is preserved by Node::set_parent.
    node->set_world_from_node(erhe::math::create_translation<float>(position));

    return make_json_content({
        {"node_name", node->get_name()},
        {"node_id",   node->get_id()},
        {"parent",    parent->get_name()},
        {"position",  {position.x, position.y, position.z}},
        {"queued",    true} // the insert operation executes on the next editor frame
    }).dump();
}

auto Mcp_server::action_create_light(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const std::string type_str = args.value("type", "directional");
    if ((type_str != "directional") && (type_str != "point") && (type_str != "spot")) {
        json r = make_text_content("Invalid light type '" + type_str + "' (expected directional, point or spot)");
        r["isError"] = true;
        return r.dump();
    }
    const erhe::scene::Light_type type = parse_light_type(type_str, erhe::scene::Light_type::directional);

    auto read_vec3 = [&args](const char* key, glm::vec3& out_value) {
        const json value = args.value(key, json());
        if (value.is_array() && (value.size() == 3)) {
            out_value = glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        }
    };

    glm::vec3 position{0.0f};
    read_vec3("position", position);
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    read_vec3("color", color);
    const float       intensity   = args.value("intensity",   1.0f);
    const bool        cast_shadow = args.value("cast_shadow", true);
    // Directional lights have no meaningful range (parallel rays); point / spot
    // default to the same 25.0 the editor's Scene_builder uses.
    const float       range       = args.value("range", (type == erhe::scene::Light_type::directional) ? 0.0f : 25.0f);
    const std::string name        = args.value("name", "MCP light");

    std::shared_ptr<erhe::scene::Node>  node;
    std::shared_ptr<erhe::scene::Light> light;
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{sr->item_host_mutex};
        node  = std::make_shared<erhe::scene::Node>(name);
        light = std::make_shared<erhe::scene::Light>(name);
        light->type        = type;
        light->color       = color;
        light->intensity   = intensity;
        light->range       = range;
        light->cast_shadow = cast_shadow;
        light->layer_id    = sr->layers().light()->id;
        if (args.contains("inner_spot_angle")) { light->inner_spot_angle = args.value("inner_spot_angle", light->inner_spot_angle); }
        if (args.contains("outer_spot_angle")) { light->outer_spot_angle = args.value("outer_spot_angle", light->outer_spot_angle); }
        light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui | erhe::Item_flags::show_debug_visualizations);
        node->attach(light);
        node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
        // The node is attached to the scene via the queued insert operation below;
        // set the world transform now (preserved by Node::set_parent).
        node->set_world_from_node(erhe::math::create_translation<float>(position));
    }

    // Insert the light node into the scene root via an undoable operation that
    // runs on the next editor frame (mirrors Scene_builder::add_lights).
    const std::shared_ptr<erhe::scene::Node>& root_node = sr->get_scene().get_root_node();
    m_context.operation_stack->queue(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = node,
                .parent  = root_node,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );

    return make_json_content({
        {"light_name",  light->get_name()},
        {"light_id",    light->get_id()},
        {"node_name",   node->get_name()},
        {"node_id",     node->get_id()},
        {"type",        type_str},
        {"color",       {color.x, color.y, color.z}},
        {"intensity",   intensity},
        {"range",       range},
        {"cast_shadow", cast_shadow},
        {"position",    {position.x, position.y, position.z}},
        {"queued",      true} // the insert operation executes on the next editor frame
    }).dump();
}

auto Mcp_server::action_edit_light(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    // Accept light_id / light_name, and also bare id / name for convenience.
    std::shared_ptr<erhe::scene::Light> light = find_light_in_scene(*sr, args, "light_id", "light_name");
    if (!light) {
        light = find_light_in_scene(*sr, args, "id", "name");
    }
    if (!light) {
        json r = make_text_content("Light not found (specify light_id or light_name)");
        r["isError"] = true;
        return r.dump();
    }

    auto read_vec3 = [&args](const char* key, glm::vec3& out_value) -> bool {
        const json value = args.value(key, json());
        if (value.is_array() && (value.size() == 3)) {
            out_value = glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
            return true;
        }
        return false;
    };

    json changed = json::object();
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{sr->item_host_mutex};

        if (args.contains("type")) {
            const std::string type_str = args.value("type", "");
            if ((type_str != "directional") && (type_str != "point") && (type_str != "spot")) {
                json r = make_text_content("Invalid light type '" + type_str + "' (expected directional, point or spot)");
                r["isError"] = true;
                return r.dump();
            }
            // Assigning Light::type re-buckets the light for rendering (forward
            // variant + shadow technique). Other type-dependent fields (e.g.
            // range) are left exactly as provided by the caller.
            light->type = parse_light_type(type_str, light->type);
            changed["type"] = type_str;
        }
        glm::vec3 color{};
        if (read_vec3("color", color)) {
            light->color = color;
            changed["color"] = {color.x, color.y, color.z};
        }
        if (args.contains("intensity")) {
            light->intensity = args.value("intensity", light->intensity);
            changed["intensity"] = light->intensity;
        }
        if (args.contains("range")) {
            light->range = args.value("range", light->range);
            changed["range"] = light->range;
        }
        if (args.contains("cast_shadow")) {
            light->cast_shadow = args.value("cast_shadow", light->cast_shadow);
            changed["cast_shadow"] = light->cast_shadow;
        }
        if (args.contains("inner_spot_angle")) {
            light->inner_spot_angle = args.value("inner_spot_angle", light->inner_spot_angle);
            changed["inner_spot_angle"] = light->inner_spot_angle;
        }
        if (args.contains("outer_spot_angle")) {
            light->outer_spot_angle = args.value("outer_spot_angle", light->outer_spot_angle);
            changed["outer_spot_angle"] = light->outer_spot_angle;
        }
        glm::vec3 position{};
        if (read_vec3("position", position)) {
            erhe::scene::Node* node = light->get_node();
            if (node != nullptr) {
                node->set_world_from_node(erhe::math::create_translation<float>(position));
                changed["position"] = {position.x, position.y, position.z};
            } else {
                changed["position_error"] = "light has no node";
            }
        }
    }

    return make_json_content({
        {"light_id",   light->get_id()},
        {"light_name", light->get_name()},
        {"changed",    changed}
    }).dump();
}

auto Mcp_server::action_toggle_physics(const json& args) -> std::string
{
    static_cast<void>(args);

    if (!m_context.app_settings) {
        json r = make_text_content("Settings not available");
        r["isError"] = true;
        return r.dump();
    }

    m_context.editor_settings->physics.dynamic_enable = !m_context.editor_settings->physics.dynamic_enable;
    const bool enabled = m_context.editor_settings->physics.dynamic_enable;

    return make_json_content({
        {"dynamic_physics_enabled", enabled}
    }).dump();
}

auto Mcp_server::action_reparent_node(const json& args) -> std::string
{
    const std::string scene_name    = args.value("scene_name", "");
    const std::size_t node_id       = args.value("node_id", std::size_t{0});
    const std::size_t parent_node_id = args.value("parent_node_id", std::size_t{0});

    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    erhe::scene::Scene& scene = sr->get_scene();

    // Find child node
    std::shared_ptr<erhe::scene::Node> child_node;
    for (const std::shared_ptr<erhe::scene::Node>& node : scene.get_flat_nodes()) {
        if (node->get_id() == node_id) {
            child_node = node;
            break;
        }
    }
    if (!child_node) {
        json r = make_text_content("Node not found: " + std::to_string(node_id));
        r["isError"] = true;
        return r.dump();
    }

    // Find parent node (0 means scene root)
    std::shared_ptr<erhe::scene::Node> new_parent;
    if (parent_node_id == 0) {
        new_parent = scene.get_root_node();
    } else {
        for (const std::shared_ptr<erhe::scene::Node>& node : scene.get_flat_nodes()) {
            if (node->get_id() == parent_node_id) {
                new_parent = node;
                break;
            }
        }
    }
    if (!new_parent) {
        json r = make_text_content("Parent node not found: " + std::to_string(parent_node_id));
        r["isError"] = true;
        return r.dump();
    }

    std::shared_ptr<Operation> op = std::make_shared<Item_parent_change_operation>(
        new_parent,
        child_node,
        std::shared_ptr<erhe::Hierarchy>{},
        std::shared_ptr<erhe::Hierarchy>{}
    );
    m_context.operation_stack->queue(op);

    return make_json_content({
        {"node",   child_node->get_name()},
        {"parent", new_parent->get_name()}
    }).dump();
}

auto Mcp_server::action_lock_items(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json = args.value("ids", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) target_ids.insert(v.get<std::size_t>());
    }
    auto items = find_items_by_ids(*sr, target_ids);
    for (auto& item : items) {
        item->set_lock_edit(true);
    }
    return make_json_content({{"locked_count", static_cast<int>(items.size())}}).dump();
}

auto Mcp_server::action_unlock_items(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json = args.value("ids", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) target_ids.insert(v.get<std::size_t>());
    }
    auto items = find_items_by_ids(*sr, target_ids);
    for (auto& item : items) {
        item->set_lock_edit(false);
    }
    return make_json_content({{"unlocked_count", static_cast<int>(items.size())}}).dump();
}

auto Mcp_server::action_add_tags(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json  = args.value("ids", json::array());
    const json tags_json = args.value("tags", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) target_ids.insert(v.get<std::size_t>());
    }
    std::vector<std::string> tags;
    for (const auto& v : tags_json) {
        if (v.is_string()) tags.push_back(v.get<std::string>());
    }
    auto items = find_items_by_ids(*sr, target_ids);
    for (auto& item : items) {
        for (const auto& tag : tags) {
            item->add_tag(tag);
        }
    }
    return make_json_content({{"tagged_count", static_cast<int>(items.size())}}).dump();
}

auto Mcp_server::action_remove_tags(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json  = args.value("ids", json::array());
    const json tags_json = args.value("tags", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) target_ids.insert(v.get<std::size_t>());
    }
    std::vector<std::string> tags;
    for (const auto& v : tags_json) {
        if (v.is_string()) tags.push_back(v.get<std::string>());
    }
    auto items = find_items_by_ids(*sr, target_ids);
    for (auto& item : items) {
        for (const auto& tag : tags) {
            item->remove_tag(tag);
        }
    }
    return make_json_content({{"untagged_count", static_cast<int>(items.size())}}).dump();
}

namespace {

// Read helpers return a tri-state so the caller can distinguish "the
// field was not in the JSON" from "the field was present but invalid
// (wrong type, NaN, Inf)". The Invalid branch carries a human-readable
// error in out_error so it can flow directly into a JSON-RPC error
// response.
enum class Field_status
{
    NotPresent,
    Ok,
    Invalid
};

[[nodiscard]] auto try_read_vec3(const json& obj, const char* key, glm::vec3& out, std::string& out_error) -> Field_status
{
    const auto it = obj.find(key);
    if (it == obj.end()) {
        return Field_status::NotPresent;
    }
    if (!it->is_array() || it->size() < 3) {
        out_error = std::string{key} + " must be an array of 3 finite numbers";
        return Field_status::Invalid;
    }
    const json& a = *it;
    if (!a[0].is_number() || !a[1].is_number() || !a[2].is_number()) {
        out_error = std::string{key} + " entries must be numbers";
        return Field_status::Invalid;
    }
    const float x = a[0].get<float>();
    const float y = a[1].get<float>();
    const float z = a[2].get<float>();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        out_error = std::string{key} + " entries must be finite (got NaN or Inf)";
        return Field_status::Invalid;
    }
    out.x = x;
    out.y = y;
    out.z = z;
    return Field_status::Ok;
}

[[nodiscard]] auto try_read_vec2(const json& obj, const char* key, glm::vec2& out, std::string& out_error) -> Field_status
{
    const auto it = obj.find(key);
    if (it == obj.end()) {
        return Field_status::NotPresent;
    }
    if (it->is_number()) {
        const float v = it->get<float>();
        if (!std::isfinite(v)) {
            out_error = std::string{key} + " must be finite (got NaN or Inf)";
            return Field_status::Invalid;
        }
        out.x = v;
        out.y = v;
        return Field_status::Ok;
    }
    if (it->is_array() && it->size() >= 2 && (*it)[0].is_number() && (*it)[1].is_number()) {
        const float x = (*it)[0].get<float>();
        const float y = (*it)[1].get<float>();
        if (!std::isfinite(x) || !std::isfinite(y)) {
            out_error = std::string{key} + " entries must be finite (got NaN or Inf)";
            return Field_status::Invalid;
        }
        out.x = x;
        out.y = y;
        return Field_status::Ok;
    }
    out_error = std::string{key} + " must be a number or an array of 2 finite numbers";
    return Field_status::Invalid;
}

[[nodiscard]] auto try_read_float(const json& obj, const char* key, float& out, std::string& out_error) -> Field_status
{
    const auto it = obj.find(key);
    if (it == obj.end()) {
        return Field_status::NotPresent;
    }
    if (!it->is_number()) {
        out_error = std::string{key} + " must be a number";
        return Field_status::Invalid;
    }
    const float v = it->get<float>();
    if (!std::isfinite(v)) {
        out_error = std::string{key} + " must be finite (got NaN or Inf)";
        return Field_status::Invalid;
    }
    out = v;
    return Field_status::Ok;
}

[[nodiscard]] auto try_read_bool(const json& obj, const char* key, bool& out) -> bool
{
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_boolean()) {
        return false;
    }
    out = it->get<bool>();
    return true;
}

class Slot_edit
{
public:
    bool                                     has_texture{false};
    std::shared_ptr<erhe::graphics::Texture> texture{};
    std::optional<uint32_t>                  tex_coord{};
    std::optional<float>                     rotation{};
    std::optional<glm::vec2>                 offset{};
    std::optional<glm::vec2>                 scale{};
};

[[nodiscard]] auto find_texture_in_library(
    const std::vector<std::shared_ptr<erhe::graphics::Texture>>& tex_list,
    const json&                                                  ref,
    std::shared_ptr<erhe::graphics::Texture>&                    out_texture,
    std::string&                                                 out_error
) -> bool
{
    if (ref.is_number_integer() || ref.is_number_unsigned()) {
        const std::size_t target_id = ref.get<std::size_t>();
        for (const auto& tex : tex_list) {
            if (tex->get_id() == target_id) {
                out_texture = tex;
                return true;
            }
        }
        out_error = "Texture id not found in content library: " + std::to_string(target_id);
        return false;
    }
    if (ref.is_string()) {
        const std::string target_name = ref.get<std::string>();
        std::shared_ptr<erhe::graphics::Texture> first_match;
        std::vector<std::size_t> matching_ids;
        for (const auto& tex : tex_list) {
            if (tex->get_name() == target_name) {
                if (!first_match) {
                    first_match = tex;
                }
                matching_ids.push_back(tex->get_id());
            }
        }
        if (!first_match) {
            out_error = "Texture name not found in content library: " + target_name;
            return false;
        }
        if (matching_ids.size() > 1) {
            out_error = "Texture name '" + target_name + "' matches " +
                        std::to_string(matching_ids.size()) + " textures (ids:";
            for (const std::size_t id : matching_ids) {
                out_error += " " + std::to_string(id);
            }
            out_error += "); reference by id to disambiguate";
            return false;
        }
        out_texture = first_match;
        return true;
    }
    out_error = "Texture reference must be a string (name), integer (id), or null (clear)";
    return false;
}

[[nodiscard]] auto parse_slot_edit(
    const json&                                                  slot_json,
    const std::vector<std::shared_ptr<erhe::graphics::Texture>>& tex_list,
    Slot_edit&                                                   out_edit,
    std::string&                                                 out_error
) -> bool
{
    if (!slot_json.is_object()) {
        out_error = "Texture slot entry must be an object";
        return false;
    }

    const auto tex_it = slot_json.find("texture");
    if (tex_it != slot_json.end()) {
        out_edit.has_texture = true;
        if (tex_it->is_null()) {
            out_edit.texture.reset();
        } else if (!find_texture_in_library(tex_list, *tex_it, out_edit.texture, out_error)) {
            return false;
        }
    }

    const auto tc_it = slot_json.find("tex_coord");
    if (tc_it != slot_json.end()) {
        if (!tc_it->is_number_integer() && !tc_it->is_number_unsigned()) {
            out_error = "tex_coord must be an integer";
            return false;
        }
        // Forward-looking field: Material_texture_sampler::tex_coord
        // is stored on the material, but the current standard shader
        // reads v_texcoord (vertex stream a_texcoord_0 only). Accept
        // 0 or 1 so a future multi-UV shader does not need a config
        // bump, and reject larger values explicitly instead of
        // wrapping uint32_t silently.
        constexpr std::int64_t k_max_tex_coord = 1;
        const std::int64_t raw = tc_it->get<std::int64_t>();
        if (raw < 0 || raw > k_max_tex_coord) {
            out_error = "tex_coord must be in [0, " + std::to_string(k_max_tex_coord) + "]";
            return false;
        }
        out_edit.tex_coord = static_cast<uint32_t>(raw);
    }

    float f_tmp{};
    switch (try_read_float(slot_json, "rotation", f_tmp, out_error)) {
        case Field_status::Ok:         out_edit.rotation = f_tmp; break;
        case Field_status::Invalid:    return false;
        case Field_status::NotPresent: break;
    }
    glm::vec2 v2_tmp{};
    switch (try_read_vec2(slot_json, "offset", v2_tmp, out_error)) {
        case Field_status::Ok:         out_edit.offset = v2_tmp; break;
        case Field_status::Invalid:    return false;
        case Field_status::NotPresent: break;
    }
    switch (try_read_vec2(slot_json, "scale", v2_tmp, out_error)) {
        case Field_status::Ok:         out_edit.scale = v2_tmp; break;
        case Field_status::Invalid:    return false;
        case Field_status::NotPresent: break;
    }
    return true;
}

void apply_slot_edit(const Slot_edit& edit, erhe::primitive::Material_texture_sampler& target)
{
    if (edit.has_texture)         target.texture   = edit.texture;
    if (edit.tex_coord.has_value()) target.tex_coord = edit.tex_coord.value();
    if (edit.rotation.has_value()) target.rotation  = edit.rotation.value();
    if (edit.offset.has_value())   target.offset    = edit.offset.value();
    if (edit.scale.has_value())    target.scale     = edit.scale.value();
}

[[nodiscard]] auto slot_edit_summary(const Slot_edit& edit) -> json
{
    json entry = json::object();
    if (edit.has_texture) {
        if (edit.texture) {
            entry["texture_id"]   = edit.texture->get_id();
            entry["texture_name"] = edit.texture->get_name();
        } else {
            entry["texture_id"]   = nullptr;
            entry["texture_name"] = nullptr;
        }
    }
    if (edit.tex_coord.has_value()) entry["tex_coord"] = edit.tex_coord.value();
    if (edit.rotation.has_value())  entry["rotation"]  = edit.rotation.value();
    if (edit.offset.has_value())    entry["offset"]    = {edit.offset->x, edit.offset->y};
    if (edit.scale.has_value())     entry["scale"]     = {edit.scale->x,  edit.scale->y};
    return entry;
}

} // anonymous namespace

auto Mcp_server::action_edit_material(const json& args) -> std::string
{
    if (m_context.operation_stack == nullptr) {
        json r = make_text_content("Operation stack not available");
        r["isError"] = true;
        return r.dump();
    }

    const std::string scene_name    = args.value("scene_name", "");
    const std::string material_name = args.value("material_name", "");

    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    auto library = sr->get_content_library();
    if (!library || !library->materials) {
        json r = make_text_content("No materials in scene: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    std::shared_ptr<erhe::primitive::Material> material;
    const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
    std::vector<std::size_t> matching_ids;
    for (const auto& mat : mat_list) {
        if (mat->get_name() == material_name) {
            if (!material) {
                material = mat;
            }
            matching_ids.push_back(mat->get_id());
        }
    }
    if (!material) {
        json r = make_text_content("Material not found: " + material_name);
        r["isError"] = true;
        return r.dump();
    }
    if (matching_ids.size() > 1) {
        // Ambiguous: refuse to mutate. Return the candidate ids so the
        // caller can re-issue with a disambiguating id (once the id
        // path is added).
        json r = make_text_content(
            "Material name '" + material_name + "' matches " +
            std::to_string(matching_ids.size()) + " materials"
        );
        r["isError"]      = true;
        r["candidate_ids"] = matching_ids;
        return r.dump();
    }

    if (material->is_lock_edit()) {
        json r = make_text_content("Material is locked: " + material_name);
        r["isError"] = true;
        return r.dump();
    }

    const erhe::primitive::Material_data before = material->data;
    erhe::primitive::Material_data       after  = before;

    json applied = json::object();

    glm::vec3   v3{};
    glm::vec2   v2{};
    float       f{};
    bool        b{};
    std::string field_err;

    auto reject = [](const std::string& msg) -> std::string {
        json r = make_text_content(msg);
        r["isError"] = true;
        return r.dump();
    };

    auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
    auto clamp01_vec3 = [&](glm::vec3 v) { return glm::vec3{clamp01(v.x), clamp01(v.y), clamp01(v.z)}; };
    auto clamp01_vec2 = [&](glm::vec2 v) { return glm::vec2{clamp01(v.x), clamp01(v.y)}; };

    switch (try_read_vec3(args, "base_color", v3, field_err)) {
        case Field_status::Ok: {
            const glm::vec3 clamped = clamp01_vec3(v3);
            after.base_color = clamped;
            applied["base_color"] = {clamped.x, clamped.y, clamped.z};
            break;
        }
        case Field_status::Invalid:    return reject(field_err);
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "opacity", f, field_err)) {
        case Field_status::Ok:         after.opacity = clamp01(f); applied["opacity"] = after.opacity; break;
        case Field_status::Invalid:    return reject(field_err);
        case Field_status::NotPresent: break;
    }
    switch (try_read_vec2(args, "roughness", v2, field_err)) {
        case Field_status::Ok: {
            const glm::vec2 clamped = clamp01_vec2(v2);
            after.roughness = clamped;
            applied["roughness"] = {clamped.x, clamped.y};
            break;
        }
        case Field_status::Invalid:    return reject(field_err);
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "metallic", f, field_err)) {
        case Field_status::Ok:         after.metallic = clamp01(f); applied["metallic"] = after.metallic; break;
        case Field_status::Invalid:    return reject(field_err);
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "reflectance", f, field_err)) {
        case Field_status::Ok:         after.reflectance = clamp01(f); applied["reflectance"] = after.reflectance; break;
        case Field_status::Invalid:    return reject(field_err);
        case Field_status::NotPresent: break;
    }
    switch (try_read_vec3(args, "emissive", v3, field_err)) {
        case Field_status::Ok: {
            // Emissive is HDR; floor at 0 but no upper clamp.
            const glm::vec3 clamped{std::max(0.0f, v3.x), std::max(0.0f, v3.y), std::max(0.0f, v3.z)};
            after.emissive = clamped;
            applied["emissive"] = {clamped.x, clamped.y, clamped.z};
            break;
        }
        case Field_status::Invalid:    return reject(field_err);
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "normal_texture_scale", f, field_err)) {
        case Field_status::Ok:         after.normal_texture_scale = f; applied["normal_texture_scale"] = f; break;
        case Field_status::Invalid:    return reject(field_err);
        case Field_status::NotPresent: break;
    }
    switch (try_read_float(args, "occlusion_texture_strength", f, field_err)) {
        case Field_status::Ok:         after.occlusion_texture_strength = clamp01(f); applied["occlusion_texture_strength"] = after.occlusion_texture_strength; break;
        case Field_status::Invalid:    return reject(field_err);
        case Field_status::NotPresent: break;
    }
    {
        const auto bxdf_it = args.find("bxdf_model");
        if (bxdf_it != args.end()) {
            if (!bxdf_it->is_string()) {
                json r = make_text_content("bxdf_model must be a string");
                r["isError"] = true;
                return r.dump();
            }
            const std::string s = bxdf_it->get<std::string>();
            if (s == "unlit") {
                after.bxdf_model = erhe::primitive::Bxdf_model::unlit;
            } else if (s == "isotropic_brdf") {
                after.bxdf_model = erhe::primitive::Bxdf_model::isotropic_brdf;
            } else if (s == "anisotropic_brdf") {
                after.bxdf_model = erhe::primitive::Bxdf_model::anisotropic_brdf;
            } else if (s == "anisotropic_slope") {
                after.bxdf_model = erhe::primitive::Bxdf_model::anisotropic_slope;
            } else if (s == "anisotropic_engine_ready") {
                after.bxdf_model = erhe::primitive::Bxdf_model::anisotropic_engine_ready;
            } else {
                json r = make_text_content("bxdf_model must be one of 'unlit', 'isotropic_brdf', 'anisotropic_brdf', 'anisotropic_slope', 'anisotropic_engine_ready'");
                r["isError"] = true;
                return r.dump();
            }
            applied["bxdf_model"] = s;
        }
    }
    if (try_read_bool(args, "use_circular_brushed_metal", b)) {
        after.use_circular_brushed_metal = b;
        applied["use_circular_brushed_metal"] = b;
    }
    if (try_read_bool(args, "use_aniso_control", b)) {
        after.use_aniso_control = b;
        applied["use_aniso_control"] = b;
    }

    const auto ts_it = args.find("texture_samplers");
    if (ts_it != args.end()) {
        if (!ts_it->is_object()) {
            json r = make_text_content("texture_samplers must be an object");
            r["isError"] = true;
            return r.dump();
        }

        if (!library->textures) {
            json r = make_text_content("Content library has no textures node");
            r["isError"] = true;
            return r.dump();
        }
        const auto& tex_list = library->textures->get_all<erhe::graphics::Texture>();

        struct Named_slot
        {
            const char*                                slot_name;
            erhe::primitive::Material_texture_sampler* target;
        };
        const Named_slot slots[] = {
            {"base_color",         &after.texture_samplers.base_color},
            {"metallic_roughness", &after.texture_samplers.metallic_roughness},
            {"normal",             &after.texture_samplers.normal},
            {"occlusion",          &after.texture_samplers.occlusion},
            {"emissive",           &after.texture_samplers.emissive}
        };

        std::vector<std::pair<const Named_slot*, Slot_edit>> parsed_edits;
        parsed_edits.reserve(std::size(slots));

        for (const Named_slot& slot : slots) {
            const auto slot_it = ts_it->find(slot.slot_name);
            if (slot_it == ts_it->end()) {
                continue;
            }
            Slot_edit  edit{};
            std::string slot_error{};
            if (!parse_slot_edit(*slot_it, tex_list, edit, slot_error)) {
                json r = make_text_content(std::string{slot.slot_name} + ": " + slot_error);
                r["isError"] = true;
                return r.dump();
            }
            parsed_edits.emplace_back(&slot, std::move(edit));
        }

        json applied_textures = json::object();
        for (auto& [slot, edit] : parsed_edits) {
            apply_slot_edit(edit, *slot->target);
            applied_textures[slot->slot_name] = slot_edit_summary(edit);
        }
        if (!applied_textures.empty()) {
            applied["texture_samplers"] = applied_textures;
        }
    }

    if (applied.empty()) {
        json r = make_text_content("No editable material fields supplied");
        r["isError"] = true;
        return r.dump();
    }

    if (after == before) {
        return make_json_content({
            {"name",     material->get_name()},
            {"id",       material->get_id()},
            {"applied",  applied},
            {"changed",  false}
        }).dump();
    }

    m_context.operation_stack->queue(
        std::make_shared<Material_change_operation>(material, before, after)
    );

    return make_json_content({
        {"name",    material->get_name()},
        {"id",      material->get_id()},
        {"applied", applied},
        {"changed", true}
    }).dump();
}

auto Mcp_server::action_save_scene(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    const std::string path_str   = args.value("path", "");
    if (path_str.empty()) {
        json r = make_text_content("Missing required argument: path");
        r["isError"] = true;
        return r.dump();
    }
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const std::filesystem::path path{path_str};
    const bool ok = editor::save_scene(*sr, path);
    if (!ok) {
        json r = make_text_content("save_scene failed: " + path_str);
        r["isError"] = true;
        return r.dump();
    }
    return make_json_content({
        {"saved", true},
        {"path",  path_str}
    }).dump();
}

auto Mcp_server::action_export_gltf(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    const std::string path_str   = args.value("path", "");
    const bool        binary     = args.value("binary", true);
    if (path_str.empty()) {
        json r = make_text_content("Missing required argument: path");
        r["isError"] = true;
        return r.dump();
    }
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    std::shared_ptr<erhe::scene::Node> root_node = sr->get_scene().get_root_node();
    if (!root_node) {
        json r = make_text_content("Scene has no root node: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const erhe::gltf::Gltf_physics_data physics_data = build_gltf_physics_data(sr->get_scene());
    const std::string gltf = erhe::gltf::export_gltf(*root_node, binary, &physics_data);
    if (!erhe::file::write_file(std::filesystem::path{path_str}, gltf)) {
        json r = make_text_content("Failed to write file: " + path_str);
        r["isError"] = true;
        return r.dump();
    }
    return make_json_content({
        {"exported", true},
        {"path",     path_str},
        {"bytes",    gltf.size()}
    }).dump();
}

auto Mcp_server::action_import_gltf(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    const std::string path_str   = args.value("path", "");
    if (path_str.empty()) {
        json r = make_text_content("Missing required argument: path");
        r["isError"] = true;
        return r.dump();
    }

    std::shared_ptr<Scene_root> scene_root;
    if (m_context.app_scenes != nullptr) {
        for (const std::shared_ptr<Scene_root>& candidate : m_context.app_scenes->get_scene_roots()) {
            if (candidate->get_name() == scene_name) {
                scene_root = candidate;
                break;
            }
        }
    }
    if (!scene_root) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const std::filesystem::path path{path_str};
    std::error_code error_code;
    if (!std::filesystem::exists(path, error_code)) {
        json r = make_text_content("File not found: " + path_str);
        r["isError"] = true;
        return r.dump();
    }

    editor::import_gltf(
        m_context,
        erhe::primitive::Build_info{
            .primitive_types = {
                .fill_triangles  = true,
                .edge_lines      = true,
                .corner_points   = true,
                .centroid_points = true
            },
            .buffer_info = m_context.mesh_memory->make_primitive_buffer_info()
        },
        scene_root,
        path
    );
    return make_json_content({
        {"imported", true},
        {"path",     path_str}
    }).dump();
}

auto Mcp_server::action_capture_screenshot(const json& args) -> std::string
{
    if (m_context.graphics_device == nullptr) {
        json r = make_text_content("Graphics device not available");
        r["isError"] = true;
        return r.dump();
    }

    const std::string path_str = args.value("path", std::string{"logs/mcp_screenshot.png"});

    int                      width  = 0;
    int                      height = 0;
    erhe::dataformat::Format format = erhe::dataformat::Format::format_8_vec4_srgb;
    std::vector<std::byte>   pixels;
    if (!m_context.graphics_device->capture_last_frame(width, height, format, pixels)) {
        json r = make_text_content(
            "Frame capture not available. Screenshots are currently only supported in the headless "
            "Vulkan configuration (emulated swapchain), and require at least one rendered frame."
        );
        r["isError"] = true;
        return r.dump();
    }

    std::unique_ptr<erhe::graphics::Image_writer> writer = erhe::graphics::Image_writer::create();
    const int row_stride = width * 4;
    if (!writer->write_png(std::filesystem::path{path_str}, width, height, row_stride, format, pixels)) {
        json r = make_text_content("Failed to write PNG '" + path_str + "' (image writer backend may be disabled)");
        r["isError"] = true;
        return r.dump();
    }

    return make_json_content({
        {"path",   path_str},
        {"width",  width},
        {"height", height}
    }).dump();
}

auto Mcp_server::action_wake_physics_bodies(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    // Bodies enter the world deactivated (quiet scene loading); wake the
    // dynamic ones the same way Node_joint does after constraint creation.
    std::size_t woken = 0;
    for (const std::shared_ptr<erhe::scene::Node>& node : sr->get_scene().get_flat_nodes()) {
        const std::shared_ptr<Node_physics> node_physics = erhe::scene::get_attachment<Node_physics>(node.get());
        if (!node_physics) {
            continue;
        }
        erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
        if (rigid_body == nullptr) {
            continue;
        }
        if (rigid_body->get_motion_mode() != erhe::physics::Motion_mode::e_dynamic) {
            continue;
        }
        rigid_body->begin_move();
        rigid_body->end_move();
        ++woken;
    }
    return make_json_content({
        {"woken", woken}
    }).dump();
}

auto Mcp_server::query_physics_items(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }

    json materials = json::array();
    for (const std::shared_ptr<erhe::physics::Physics_material>& material : library->physics_materials->get_all<erhe::physics::Physics_material>()) {
        materials.push_back({
            {"name",                material->get_name()},
            {"id",                  material->get_id()},
            {"static_friction",     material->static_friction},
            {"dynamic_friction",    material->dynamic_friction},
            {"restitution",         material->restitution},
            {"friction_combine",    combine_mode_to_string(material->friction_combine)},
            {"restitution_combine", combine_mode_to_string(material->restitution_combine)}
        });
    }
    json filters = json::array();
    for (const std::shared_ptr<erhe::physics::Collision_filter>& filter : library->collision_filters->get_all<erhe::physics::Collision_filter>()) {
        filters.push_back({
            {"name",                      filter->get_name()},
            {"id",                        filter->get_id()},
            {"collision_systems",         filter->collision_systems},
            {"collide_with_systems",      filter->collide_with_systems},
            {"not_collide_with_systems",  filter->not_collide_with_systems}
        });
    }
    json joint_settings = json::array();
    for (const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings : library->physics_joints->get_all<erhe::physics::Physics_joint_settings>()) {
        joint_settings.push_back(joint_settings_to_json(*settings));
    }
    return make_json_content({
        {"physics_materials",      materials},
        {"collision_filters",      filters},
        {"physics_joint_settings", joint_settings}
    }).dump();
}

auto Mcp_server::action_create_physics_body(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*sr, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("Node not found (give node_id or node_name)");
    }
    if (erhe::scene::get_attachment<Node_physics>(node.get())) {
        return make_error_content("Node already has a rigid body: " + node->get_name());
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }

    erhe::physics::IRigid_body_create_info create_info{};
    create_info.motion_mode = parse_motion_mode(args.value("motion_mode", "dynamic"), erhe::physics::Motion_mode::e_dynamic);

    std::string shape_error;
    create_info.collision_shape = build_collision_shape_from_args(args, node.get(), shape_error);
    if (!create_info.collision_shape) {
        return make_error_content(shape_error);
    }
    if ((args.value("shape", "auto") == "mesh") && (create_info.motion_mode == erhe::physics::Motion_mode::e_dynamic)) {
        return make_error_content("Triangle mesh shapes are static / kinematic only (Jolt restriction); use convex_hull for dynamic bodies");
    }

    if (args.contains("mass")) {
        create_info.mass = args["mass"].get<float>();
    }
    create_info.friction         = args.value("friction",        create_info.friction);
    create_info.restitution      = args.value("restitution",     create_info.restitution);
    create_info.linear_damping   = args.value("linear_damping",  create_info.linear_damping);
    create_info.angular_damping  = args.value("angular_damping", create_info.angular_damping);
    create_info.gravity_factor   = args.value("gravity_factor",  create_info.gravity_factor);
    create_info.is_sensor        = args.value("is_trigger",      false);
    create_info.linear_velocity  = get_vec3(args, "linear_velocity",  create_info.linear_velocity);
    create_info.angular_velocity = get_vec3(args, "angular_velocity", create_info.angular_velocity);

    const std::string material_name = args.value("material_name", "");
    if (!material_name.empty()) {
        create_info.physics_material = find_library_item<erhe::physics::Physics_material>(library->physics_materials, material_name);
        if (!create_info.physics_material) {
            return make_error_content("Physics material not found: " + material_name);
        }
    }
    const std::string filter_name = args.value("filter_name", "");
    if (!filter_name.empty()) {
        create_info.collision_filter = find_library_item<erhe::physics::Collision_filter>(library->collision_filters, filter_name);
        if (!create_info.collision_filter) {
            return make_error_content("Collision filter not found: " + filter_name);
        }
    }

    const glm::vec3 center_of_mass = get_vec3(args, "center_of_mass", glm::vec3{0.0f});
    if (center_of_mass != glm::vec3{0.0f}) {
        create_info.collision_shape = erhe::physics::ICollision_shape::create_offset_center_of_mass_shape_shared(create_info.collision_shape, center_of_mass);
    }
    create_info.debug_label = node->get_name();

    const std::shared_ptr<Node_physics> node_physics = m_context.scene_commands->create_new_rigid_body(node.get(), create_info);
    if (!node_physics) {
        return make_error_content("Failed to create rigid body on node: " + node->get_name());
    }
    return make_json_content({
        {"created",     true},
        {"queued",      true}, // the attach operation executes on the next editor frame
        {"node",        node->get_name()},
        {"node_id",     node->get_id()},
        {"shape",       create_info.collision_shape->describe()},
        {"motion_mode", motion_mode_to_string(create_info.motion_mode)}
    }).dump();
}

auto Mcp_server::action_edit_physics_body(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*sr, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("Node not found (give node_id or node_name)");
    }
    const std::shared_ptr<Node_physics> node_physics = erhe::scene::get_attachment<Node_physics>(node.get());
    if (!node_physics) {
        return make_error_content("Node has no rigid body: " + node->get_name());
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }

    // Validate everything before applying anything, so an error leaves the
    // body unchanged.
    const erhe::physics::Motion_mode motion_mode = args.contains("motion_mode")
        ? parse_motion_mode(args["motion_mode"].get<std::string>(), node_physics->get_motion_mode())
        : node_physics->get_motion_mode();
    std::shared_ptr<erhe::physics::ICollision_shape> new_shape{};
    if (args.contains("shape")) {
        std::string shape_error;
        new_shape = build_collision_shape_from_args(args, node.get(), shape_error);
        if (!new_shape) {
            return make_error_content(shape_error);
        }
        if ((args["shape"].get<std::string>() == "mesh") && (motion_mode == erhe::physics::Motion_mode::e_dynamic)) {
            return make_error_content("Triangle mesh shapes are static / kinematic only (Jolt restriction); use convex_hull for dynamic bodies");
        }
    }
    std::shared_ptr<erhe::physics::Physics_material> material{};
    if (args.contains("material_name")) {
        const std::string material_name = args["material_name"].get<std::string>();
        if (!material_name.empty()) {
            material = find_library_item<erhe::physics::Physics_material>(library->physics_materials, material_name);
            if (!material) {
                return make_error_content("Physics material not found: " + material_name);
            }
        }
    }
    std::shared_ptr<erhe::physics::Collision_filter> filter{};
    if (args.contains("filter_name")) {
        const std::string filter_name = args["filter_name"].get<std::string>();
        if (!filter_name.empty()) {
            filter = find_library_item<erhe::physics::Collision_filter>(library->collision_filters, filter_name);
            if (!filter) {
                return make_error_content("Collision filter not found: " + filter_name);
            }
        }
    }

    json applied = json::array();
    if (args.contains("motion_mode")) {
        node_physics->set_motion_mode(motion_mode);
        applied.push_back("motion_mode");
    }
    // Body-recreating edits first so live scalar edits below land on the
    // final rigid body.
    if (new_shape) {
        node_physics->set_collision_shape(new_shape);
        applied.push_back("shape");
    }
    if (args.contains("is_trigger")) {
        node_physics->set_trigger(args["is_trigger"].get<bool>());
        applied.push_back("is_trigger");
    }
    if (args.contains("center_of_mass")) {
        node_physics->set_center_of_mass_offset(get_vec3(args, "center_of_mass", glm::vec3{0.0f}));
        applied.push_back("center_of_mass");
    }
    if (args.contains("gravity_factor")) {
        node_physics->set_gravity_factor(args["gravity_factor"].get<float>());
        applied.push_back("gravity_factor");
    }
    if (args.contains("linear_velocity")) {
        node_physics->set_initial_linear_velocity(get_vec3(args, "linear_velocity", glm::vec3{0.0f}));
        applied.push_back("linear_velocity");
    }
    if (args.contains("angular_velocity")) {
        node_physics->set_initial_angular_velocity(get_vec3(args, "angular_velocity", glm::vec3{0.0f}));
        applied.push_back("angular_velocity");
    }
    if (args.contains("material_name")) {
        node_physics->set_physics_material(material);
        applied.push_back("material_name");
    }
    if (args.contains("filter_name")) {
        node_physics->set_collision_filter(filter);
        applied.push_back("filter_name");
    }

    erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
    const bool wants_live_edit =
        args.contains("mass")        || args.contains("friction") || args.contains("restitution") ||
        args.contains("linear_damping") || args.contains("angular_damping");
    if (wants_live_edit && (rigid_body == nullptr)) {
        return make_error_content("Rigid body is not live (node not attached to a scene); mass / friction / restitution / damping not applied");
    }
    if (rigid_body != nullptr) {
        if (args.contains("mass")) {
            rigid_body->set_mass_properties(args["mass"].get<float>(), rigid_body->get_local_inertia());
            applied.push_back("mass");
        }
        if (args.contains("friction")) {
            rigid_body->set_friction(args["friction"].get<float>());
            applied.push_back("friction");
        }
        if (args.contains("restitution")) {
            rigid_body->set_restitution(args["restitution"].get<float>());
            applied.push_back("restitution");
        }
        if (args.contains("linear_damping") || args.contains("angular_damping")) {
            rigid_body->set_damping(
                args.value("linear_damping",  rigid_body->get_linear_damping()),
                args.value("angular_damping", rigid_body->get_angular_damping())
            );
            if (args.contains("linear_damping"))  { applied.push_back("linear_damping"); }
            if (args.contains("angular_damping")) { applied.push_back("angular_damping"); }
        }
    }

    const std::shared_ptr<erhe::physics::ICollision_shape>& shape = node_physics->get_collision_shape();
    return make_json_content({
        {"node",        node->get_name()},
        {"applied",     applied},
        {"motion_mode", motion_mode_to_string(node_physics->get_motion_mode())},
        {"shape",       shape ? shape->describe() : ""},
        {"is_trigger",  node_physics->is_trigger()}
    }).dump();
}

auto Mcp_server::action_create_physics_joint(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*sr, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("Node not found (give node_id or node_name)");
    }

    std::shared_ptr<erhe::scene::Node> connected{};
    if (args.contains("connected_node_id") || args.contains("connected_node_name")) {
        connected = find_node_in_scene(*sr, args, "connected_node_id", "connected_node_name");
        if (!connected) {
            return make_error_content("Connected node not found");
        }
        if (connected == node) {
            return make_error_content("Connected node must differ from the joint node");
        }
    }

    std::shared_ptr<erhe::physics::Physics_joint_settings> settings{};
    const std::string settings_name = args.value("settings_name", "");
    if (!settings_name.empty()) {
        const std::shared_ptr<Content_library> library = sr->get_content_library();
        settings = find_library_item<erhe::physics::Physics_joint_settings>(library ? library->physics_joints : std::shared_ptr<Content_library_node>{}, settings_name);
        if (!settings) {
            return make_error_content("Joint settings not found: " + settings_name);
        }
    }
    const bool enable_collision = args.value("enable_collision", false);

    const std::shared_ptr<Node_joint> node_joint = m_context.scene_commands->create_new_joint(node.get(), connected, settings, enable_collision);
    if (!node_joint) {
        return make_error_content("Failed to create joint on node: " + node->get_name());
    }
    return make_json_content({
        {"created",          true},
        {"queued",           true}, // the attach operation executes on the next editor frame
        {"node",             node->get_name()},
        {"node_id",          node->get_id()},
        {"connected_node",   connected ? connected->get_name() : "(world)"},
        {"settings",         settings ? settings->get_name() : ""},
        {"enable_collision", enable_collision}
    }).dump();
}

auto Mcp_server::action_edit_physics_joint(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*sr, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("Node not found (give node_id or node_name)");
    }
    std::vector<std::shared_ptr<Node_joint>> joints;
    for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : node->get_attachments()) {
        const std::shared_ptr<Node_joint> node_joint = std::dynamic_pointer_cast<Node_joint>(attachment);
        if (node_joint) {
            joints.push_back(node_joint);
        }
    }
    if (joints.empty()) {
        return make_error_content("Node has no joint: " + node->get_name());
    }
    const std::size_t joint_index = args.value("joint_index", std::size_t{0});
    if (joint_index >= joints.size()) {
        return make_error_content("joint_index out of range: node has " + std::to_string(joints.size()) + " joint(s)");
    }
    const std::shared_ptr<Node_joint> node_joint = joints[joint_index];

    json applied = json::array();
    if (args.value("connect_to_world", false)) {
        node_joint->set_connected_node({});
        applied.push_back("connect_to_world");
    } else if (args.contains("connected_node_id") || args.contains("connected_node_name")) {
        const std::shared_ptr<erhe::scene::Node> connected = find_node_in_scene(*sr, args, "connected_node_id", "connected_node_name");
        if (!connected) {
            return make_error_content("Connected node not found");
        }
        if (connected == node) {
            return make_error_content("Connected node must differ from the joint node");
        }
        node_joint->set_connected_node(connected);
        applied.push_back("connected_node");
    }
    if (args.contains("settings_name")) {
        const std::string settings_name = args["settings_name"].get<std::string>();
        if (settings_name.empty()) {
            node_joint->set_settings({});
        } else {
            const std::shared_ptr<Content_library> library = sr->get_content_library();
            const std::shared_ptr<erhe::physics::Physics_joint_settings> settings =
                find_library_item<erhe::physics::Physics_joint_settings>(library ? library->physics_joints : std::shared_ptr<Content_library_node>{}, settings_name);
            if (!settings) {
                return make_error_content("Joint settings not found: " + settings_name);
            }
            node_joint->set_settings(settings);
        }
        applied.push_back("settings");
    }
    if (args.contains("enable_collision")) {
        node_joint->set_enable_collision(args["enable_collision"].get<bool>());
        applied.push_back("enable_collision");
    }
    if (args.value("rebuild", false)) {
        node_joint->rebuild();
        applied.push_back("rebuild");
    }

    const std::shared_ptr<erhe::scene::Node> connected = node_joint->get_connected_node();
    const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings = node_joint->get_settings();
    return make_json_content({
        {"node",             node->get_name()},
        {"applied",          applied},
        {"connected_node",   connected ? connected->get_name() : "(world)"},
        {"settings",         settings ? settings->get_name() : ""},
        {"enable_collision", node_joint->get_enable_collision()},
        {"constraint",       (node_joint->get_constraint() != nullptr) ? "created" : "pending"}
    }).dump();
}

auto Mcp_server::action_create_physics_material(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }
    const std::string name = args.value("name", "");
    if (name.empty()) {
        return make_error_content("name is required");
    }
    if (find_library_item<erhe::physics::Physics_material>(library->physics_materials, name)) {
        return make_error_content("Physics material already exists: " + name);
    }

    auto item = std::make_shared<erhe::physics::Physics_material>(name);
    item->static_friction     = args.value("static_friction",  item->static_friction);
    item->dynamic_friction    = args.value("dynamic_friction", item->dynamic_friction);
    item->restitution         = args.value("restitution",      item->restitution);
    item->friction_combine    = parse_combine_mode(args.value("friction_combine",    ""), item->friction_combine);
    item->restitution_combine = parse_combine_mode(args.value("restitution_combine", ""), item->restitution_combine);

    m_context.operation_stack->queue(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = std::make_shared<Content_library_node>(item),
                .parent  = library->physics_materials,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );
    return make_json_content({
        {"created", true},
        {"queued",  true}, // the insert operation executes on the next editor frame
        {"name",    name},
        {"id",      item->get_id()}
    }).dump();
}

auto Mcp_server::action_edit_physics_material(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }
    const std::string name = args.value("name", "");
    const std::shared_ptr<erhe::physics::Physics_material> item = find_library_item<erhe::physics::Physics_material>(library->physics_materials, name);
    if (!item) {
        return make_error_content("Physics material not found: " + name);
    }

    json applied = json::array();
    if (args.contains("new_name")) {
        item->set_name(args["new_name"].get<std::string>());
        applied.push_back("new_name");
    }
    if (args.contains("static_friction"))  { item->static_friction  = args["static_friction"].get<float>();  applied.push_back("static_friction"); }
    if (args.contains("dynamic_friction")) { item->dynamic_friction = args["dynamic_friction"].get<float>(); applied.push_back("dynamic_friction"); }
    if (args.contains("restitution"))      { item->restitution      = args["restitution"].get<float>();      applied.push_back("restitution"); }
    if (args.contains("friction_combine")) {
        item->friction_combine = parse_combine_mode(args["friction_combine"].get<std::string>(), item->friction_combine);
        applied.push_back("friction_combine");
    }
    if (args.contains("restitution_combine")) {
        item->restitution_combine = parse_combine_mode(args["restitution_combine"].get<std::string>(), item->restitution_combine);
        applied.push_back("restitution_combine");
    }
    reapply_physics_material(m_context, item);

    return make_json_content({
        {"name",                item->get_name()},
        {"applied",             applied},
        {"static_friction",     item->static_friction},
        {"dynamic_friction",    item->dynamic_friction},
        {"restitution",         item->restitution},
        {"friction_combine",    combine_mode_to_string(item->friction_combine)},
        {"restitution_combine", combine_mode_to_string(item->restitution_combine)}
    }).dump();
}

auto Mcp_server::action_create_collision_filter(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }
    const std::string name = args.value("name", "");
    if (name.empty()) {
        return make_error_content("name is required");
    }
    if (find_library_item<erhe::physics::Collision_filter>(library->collision_filters, name)) {
        return make_error_content("Collision filter already exists: " + name);
    }

    auto item = std::make_shared<erhe::physics::Collision_filter>(name);
    if (args.contains("collision_systems"))        { item->collision_systems        = args["collision_systems"].get<std::vector<std::string>>(); }
    if (args.contains("collide_with_systems"))     { item->collide_with_systems     = args["collide_with_systems"].get<std::vector<std::string>>(); }
    if (args.contains("not_collide_with_systems")) { item->not_collide_with_systems = args["not_collide_with_systems"].get<std::vector<std::string>>(); }

    m_context.operation_stack->queue(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = std::make_shared<Content_library_node>(item),
                .parent  = library->collision_filters,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );
    return make_json_content({
        {"created", true},
        {"queued",  true}, // the insert operation executes on the next editor frame
        {"name",    name},
        {"id",      item->get_id()}
    }).dump();
}

auto Mcp_server::action_edit_collision_filter(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }
    const std::string name = args.value("name", "");
    const std::shared_ptr<erhe::physics::Collision_filter> item = find_library_item<erhe::physics::Collision_filter>(library->collision_filters, name);
    if (!item) {
        return make_error_content("Collision filter not found: " + name);
    }

    json applied = json::array();
    if (args.contains("new_name")) {
        item->set_name(args["new_name"].get<std::string>());
        applied.push_back("new_name");
    }
    if (args.contains("collision_systems")) {
        item->collision_systems = args["collision_systems"].get<std::vector<std::string>>();
        applied.push_back("collision_systems");
    }
    if (args.contains("collide_with_systems")) {
        item->collide_with_systems = args["collide_with_systems"].get<std::vector<std::string>>();
        applied.push_back("collide_with_systems");
    }
    if (args.contains("not_collide_with_systems")) {
        item->not_collide_with_systems = args["not_collide_with_systems"].get<std::vector<std::string>>();
        applied.push_back("not_collide_with_systems");
    }
    reapply_collision_filter(m_context, item);

    return make_json_content({
        {"name",                     item->get_name()},
        {"applied",                  applied},
        {"collision_systems",        item->collision_systems},
        {"collide_with_systems",     item->collide_with_systems},
        {"not_collide_with_systems", item->not_collide_with_systems}
    }).dump();
}

auto Mcp_server::action_create_physics_joint_settings(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }
    const std::string name = args.value("name", "");
    if (name.empty()) {
        return make_error_content("name is required");
    }
    if (find_library_item<erhe::physics::Physics_joint_settings>(library->physics_joints, name)) {
        return make_error_content("Joint settings already exist: " + name);
    }

    auto item = std::make_shared<erhe::physics::Physics_joint_settings>(name);
    if (args.contains("limits")) {
        parse_joint_limits(args["limits"], item->limits);
    }
    if (args.contains("drives")) {
        parse_joint_drives(args["drives"], item->drives);
    }

    m_context.operation_stack->queue(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = std::make_shared<Content_library_node>(item),
                .parent  = library->physics_joints,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );
    json result = joint_settings_to_json(*item);
    result["created"] = true;
    result["queued"]  = true; // the insert operation executes on the next editor frame
    return make_json_content(result).dump();
}

auto Mcp_server::action_edit_physics_joint_settings(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<Content_library> library = sr->get_content_library();
    if (!library) {
        return make_error_content("Scene has no content library: " + scene_name);
    }
    const std::string name = args.value("name", "");
    const std::shared_ptr<erhe::physics::Physics_joint_settings> item = find_library_item<erhe::physics::Physics_joint_settings>(library->physics_joints, name);
    if (!item) {
        return make_error_content("Joint settings not found: " + name);
    }

    json applied = json::array();
    if (args.contains("new_name")) {
        item->set_name(args["new_name"].get<std::string>());
        applied.push_back("new_name");
    }
    if (args.contains("limits")) {
        parse_joint_limits(args["limits"], item->limits);
        applied.push_back("limits");
    }
    if (args.contains("drives")) {
        parse_joint_drives(args["drives"], item->drives);
        applied.push_back("drives");
    }
    // Joints using these settings only pick up changes when their constraint
    // is recreated; rebuild them all.
    rebuild_joints_using_settings(m_context, item);

    json result = joint_settings_to_json(*item);
    result["applied"] = applied;
    return make_json_content(result).dump();
}

auto Mcp_server::action_set_mesh_component_mode(const json& args) -> std::string
{
    if (m_context.mesh_component_selection == nullptr) {
        return make_error_content("Mesh component selection not available");
    }
    const std::string mode_str = args.value("mode", "");
    if (!is_valid_mesh_component_mode(mode_str)) {
        return make_error_content("mode is required (object, vertex, edge, face)");
    }
    m_context.mesh_component_selection->set_mode(parse_mesh_component_mode(mode_str, Mesh_component_mode::object));
    return make_json_content({{"mode", mode_str}}).dump();
}

auto Mcp_server::action_select_mesh_components(const json& args) -> std::string
{
    Mesh_component_selection* selection = m_context.mesh_component_selection;
    if (selection == nullptr) {
        return make_error_content("Mesh component selection not available");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*sr, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("Node not found (give node_id or node_name)");
    }
    const std::size_t primitive_index = args.value("primitive_index", std::size_t{0});
    std::shared_ptr<erhe::scene::Mesh>        mesh;
    std::shared_ptr<erhe::geometry::Geometry> geometry;
    if (!resolve_mesh_geometry(node, primitive_index, mesh, geometry)) {
        return make_error_content("Node has no mesh geometry at primitive_index " + std::to_string(primitive_index) + ": " + node->get_name());
    }

    if (args.contains("mode")) {
        const std::string mode_str = args.value("mode", "");
        if (!is_valid_mesh_component_mode(mode_str)) {
            return make_error_content("Invalid mode: " + mode_str + " (object, vertex, edge, face)");
        }
        selection->set_mode(parse_mesh_component_mode(mode_str, Mesh_component_mode::object));
    }

    const bool extend = args.value("extend", false);
    if (!extend) {
        selection->clear_all();
    }

    const GEO::Mesh&   geo_mesh     = geometry->get_mesh();
    const GEO::index_t vertex_count = geo_mesh.vertices.nb();
    const GEO::index_t facet_count  = geo_mesh.facets.nb();

    Mesh_component_entry& entry = selection->find_or_create_entry(mesh, primitive_index, geometry);

    if (args.contains("vertices") && args["vertices"].is_array()) {
        for (const auto& v : args["vertices"]) {
            const GEO::index_t vertex = v.get<GEO::index_t>();
            if (vertex >= vertex_count) {
                return make_error_content("Vertex index out of range: " + std::to_string(vertex) + " >= " + std::to_string(vertex_count));
            }
            entry.add_vertex(vertex);
        }
    }
    if (args.contains("edges") && args["edges"].is_array()) {
        for (const auto& e : args["edges"]) {
            if (!e.is_array() || (e.size() != 2)) {
                return make_error_content("Each edge must be a [v0, v1] vertex-index pair");
            }
            const GEO::index_t v0 = e[0].get<GEO::index_t>();
            const GEO::index_t v1 = e[1].get<GEO::index_t>();
            if ((v0 >= vertex_count) || (v1 >= vertex_count)) {
                return make_error_content("Edge vertex index out of range (vertex_count " + std::to_string(vertex_count) + ")");
            }
            entry.add_edge(v0, v1);
        }
    }
    if (args.contains("facets") && args["facets"].is_array()) {
        for (const auto& f : args["facets"]) {
            const GEO::index_t facet = f.get<GEO::index_t>();
            if (facet >= facet_count) {
                return make_error_content("Facet index out of range: " + std::to_string(facet) + " >= " + std::to_string(facet_count));
            }
            entry.add_facet(facet);
        }
    }

    return make_json_content({
        {"node",            node->get_name()},
        {"node_id",         node->get_id()},
        {"primitive_index", primitive_index},
        {"mode",            mesh_component_mode_lc(selection->get_mode())},
        {"vertices",        entry.vertices.size()},
        {"edges",           entry.edges.size()},
        {"facets",          entry.facets.size()}
    }).dump();
}

auto Mcp_server::query_mesh_component_selection(const json& args) -> std::string
{
    static_cast<void>(args);
    Mesh_component_selection* selection = m_context.mesh_component_selection;
    if (selection == nullptr) {
        return make_json_content({{"mode", "object"}, {"entries", json::array()}}).dump();
    }
    json entries = json::array();
    for (const Mesh_component_entry& entry : selection->get_entries()) {
        json vertices = json::array();
        for (const GEO::index_t v : entry.vertices) {
            vertices.push_back(v);
        }
        json facets = json::array();
        for (const GEO::index_t f : entry.facets) {
            facets.push_back(f);
        }
        json edges = json::array();
        for (const Mesh_edge_key& e : entry.edges) {
            edges.push_back(json::array({e.first, e.second}));
        }
        json entry_json = {
            {"primitive_index", entry.primitive_index},
            {"live",            selection->is_live(entry)},
            {"vertices",        vertices},
            {"edges",           edges},
            {"facets",          facets}
        };
        const std::shared_ptr<erhe::scene::Mesh> mesh = entry.mesh.lock();
        if (mesh) {
            entry_json["mesh_name"] = mesh->get_name();
            const erhe::scene::Node* node = mesh->get_node();
            if (node != nullptr) {
                entry_json["node_name"] = node->get_name();
                entry_json["node_id"]   = node->get_id();
            }
        }
        entries.push_back(entry_json);
    }
    return make_json_content({
        {"mode",    mesh_component_mode_lc(selection->get_mode())},
        {"entries", entries}
    }).dump();
}

auto Mcp_server::action_clear_mesh_component_selection(const json& args) -> std::string
{
    static_cast<void>(args);
    if (m_context.mesh_component_selection == nullptr) {
        return make_error_content("Mesh component selection not available");
    }
    m_context.mesh_component_selection->clear_all();
    return make_json_content({{"cleared", true}}).dump();
}

auto Mcp_server::action_align_components(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const bool apply_scale = args.value("apply_scale", false);
    const bool aligned = m_context.operations->align_selection(apply_scale);
    if (!aligned) {
        return make_error_content("Align failed: requires exactly two components of the active mode (vertex/edge/face) selected on two distinct nodes");
    }
    return make_json_content({{"aligned", true}, {"apply_scale", apply_scale}}).dump();
}

auto Mcp_server::action_add_joint(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const std::string avoidance_str = args.value("avoidance", "joint_pair");
    Add_joint_avoidance avoidance = Add_joint_avoidance::joint_pair;
    if (avoidance_str == "whole_world") {
        avoidance = Add_joint_avoidance::whole_world;
    } else if (avoidance_str != "joint_pair") {
        return make_error_content("Invalid avoidance: " + avoidance_str + " (joint_pair, whole_world)");
    }
    const bool created = m_context.operations->add_joint(avoidance);
    if (!created) {
        return make_error_content("Add Joint failed: needs two aligned components on distinct rigid bodies and a non-intersecting orientation (see editor log for the specific reason)");
    }
    return make_json_content({{"created", true}, {"avoidance", avoidance_str}}).dump();
}

auto Mcp_server::action_flip_joint(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const std::string avoidance_str = args.value("avoidance", "joint_pair");
    Add_joint_avoidance avoidance = Add_joint_avoidance::joint_pair;
    if (avoidance_str == "whole_world") {
        avoidance = Add_joint_avoidance::whole_world;
    } else if (avoidance_str != "joint_pair") {
        return make_error_content("Invalid avoidance: " + avoidance_str + " (joint_pair, whole_world)");
    }
    const bool flipped = m_context.operations->flip_joint(avoidance);
    if (!flipped) {
        return make_error_content("Flip Joint failed: select a rigid-body party of a hinge joint (see editor log for the specific reason)");
    }
    return make_json_content({{"flipped", true}, {"avoidance", avoidance_str}}).dump();
}

auto Mcp_server::query_get_physics_state(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*sr, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("Node not found (give node_id or node_name)");
    }
    const std::shared_ptr<Node_physics> node_physics = erhe::scene::get_attachment<Node_physics>(node.get());
    if (!node_physics) {
        return make_error_content("Node has no rigid body: " + node->get_name());
    }
    const erhe::physics::IRigid_body* rigid_body = node_physics->get_rigid_body();
    if (rigid_body == nullptr) {
        return make_error_content("Rigid body is not live (node not attached to a scene): " + node->get_name());
    }
    const glm::vec3 lin = rigid_body->get_linear_velocity();
    const glm::vec3 ang = rigid_body->get_angular_velocity();
    const glm::vec3 pos = glm::vec3{rigid_body->get_world_transform()[3]};
    return make_json_content({
        {"node",             node->get_name()},
        {"motion_mode",      motion_mode_to_string(rigid_body->get_motion_mode())},
        {"is_active",        rigid_body->is_active()},
        {"position",         {pos.x, pos.y, pos.z}},
        {"linear_velocity",  {lin.x, lin.y, lin.z}},
        {"angular_velocity", {ang.x, ang.y, ang.z}},
        {"linear_speed",     glm::length(lin)},
        {"angular_speed",    glm::length(ang)}
    }).dump();
}

auto Mcp_server::action_remesh(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const unsigned int target     = static_cast<unsigned int>(args.value("target_vertex_count", 2000));
    const float        anisotropy = args.value("anisotropy", 0.0f);
    const bool         regen      = args.value("regenerate_attributes", true);
    if (anisotropy > 0.0f) {
        m_context.operations->anisotropic_remesh(target, anisotropy, regen);
    } else {
        m_context.operations->remesh(target, regen);
    }
    return make_json_content({
        {"queued",                true},
        {"target_vertex_count",   target},
        {"anisotropy",            anisotropy},
        {"regenerate_attributes", regen}
    }).dump();
}

auto Mcp_server::action_decimate(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const unsigned int bins  = static_cast<unsigned int>(args.value("bins", 50));
    const bool         regen = args.value("regenerate_attributes", true);
    m_context.operations->decimate(bins, regen);
    return make_json_content({{"queued", true}, {"bins", bins}, {"regenerate_attributes", regen}}).dump();
}

auto Mcp_server::action_smooth(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const unsigned int iterations = static_cast<unsigned int>(args.value("iterations", 5));
    const float        strength   = args.value("strength", 0.5f);
    const bool         regen      = args.value("regenerate_attributes", true);
    m_context.operations->smooth(iterations, strength, regen);
    return make_json_content({{"queued", true}, {"iterations", iterations}, {"strength", strength}, {"regenerate_attributes", regen}}).dump();
}

auto Mcp_server::action_generate_texture_coordinates(const json& args) -> std::string
{
    if (m_context.operations == nullptr) {
        return make_error_content("Operations not available");
    }
    const std::size_t texcoord_slot   = args.value("texcoord_slot", std::size_t{0});
    const float       hard_angles_deg = args.value("hard_angles_deg", 45.0f);
    const int         parameterizer   = args.value("parameterizer", 3);
    const int         packer          = args.value("packer", 2);
    m_context.operations->make_atlas(texcoord_slot, hard_angles_deg, parameterizer, packer);
    return make_json_content({
        {"queued",          true},
        {"texcoord_slot",   texcoord_slot},
        {"hard_angles_deg", hard_angles_deg},
        {"parameterizer",   parameterizer},
        {"packer",          packer}
    }).dump();
}

auto Mcp_server::action_set_transform_reference_mode(const json& args) -> std::string
{
    if (m_context.transform_tool == nullptr) {
        return make_error_content("Transform tool not available");
    }
    const std::string mode_str = args.value("mode", "");
    Transform_reference_mode mode = Transform_reference_mode::global;
    if      (mode_str == "global")    { mode = Transform_reference_mode::global;    }
    else if (mode_str == "local")     { mode = Transform_reference_mode::local;     }
    else if (mode_str == "reference") { mode = Transform_reference_mode::reference; }
    else if (mode_str == "selection") { mode = Transform_reference_mode::selection; }
    else {
        return make_error_content("Invalid mode: " + mode_str + " (global, local, reference, selection)");
    }

    Transform_tool_shared& shared = m_context.transform_tool->shared;
    shared.settings.reference_mode = mode;

    json result = {{"mode", mode_str}};

    if (mode == Transform_reference_mode::reference) {
        const std::string scene_name = args.value("scene_name", "");
        std::shared_ptr<erhe::scene::Node> ref_node;
        if (!scene_name.empty()) {
            Scene_root* sr = find_scene(scene_name);
            if (sr != nullptr) {
                ref_node = find_node_in_scene(*sr, args, "reference_node_id", "reference_node_name");
            }
        } else if (m_context.app_scenes != nullptr) {
            for (const auto& sr : m_context.app_scenes->get_scene_roots()) {
                ref_node = find_node_in_scene(*sr, args, "reference_node_id", "reference_node_name");
                if (ref_node) {
                    break;
                }
            }
        }
        if (ref_node) {
            shared.reference_node = ref_node;
            result["reference_node"] = ref_node->get_name();
        } else if (args.contains("reference_node_id") || args.contains("reference_node_name")) {
            return make_error_content("Reference node not found (give reference_node_id or reference_node_name)");
        }
    }

    if (args.contains("edge_normal_blend")) {
        shared.settings.edge_normal_blend = args.value("edge_normal_blend", 0.5f);
    }

    m_context.transform_tool->on_reference_settings_changed();
    return make_json_content(result).dump();
}

auto Mcp_server::action_set_transform_mode(const json& args) -> std::string
{
    if (m_context.editor_settings == nullptr) {
        return make_error_content("Editor settings not available");
    }
    const std::string   mode_str = args.value("mode", "");
    Mesh_transform_mode mode     = Mesh_transform_mode::move;
    if (!::from_string(mode_str, mode)) {
        return make_error_content("Invalid mode: " + mode_str + " (move, extrude, extrude_group_normal, extrude_vertex_normal)");
    }
    m_context.editor_settings->transform_mode = mode;
    return make_json_content(json{{"mode", std::string{::to_string(mode)}}}).dump();
}

} // namespace editor
