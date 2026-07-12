#pragma once

// Internal header shared by the Mcp_server translation units
// (mcp_server*.cpp). Not for use outside mcp/.

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
#include "geometry_graph/geometry_graph.hpp"
#include "geometry_graph/geometry_graph_mesh.hpp"
#include "geometry_graph/geometry_graph_node.hpp"
#include "geometry_graph/geometry_graph_window.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "texture_graph/graph_texture.hpp"
#include "texture_graph/texture_graph.hpp"
#include "texture_graph/texture_graph_compose.hpp"
#include "texture_graph/texture_graph_node.hpp"
#include "texture_graph/texture_graph_window.hpp"
#include "texture_graph/texture_payload.hpp"
#include "texture_graph/texture_renderer.hpp"
#include "texture_graph/nodes/texture_node_descriptors.hpp"
#include "texture_graph/nodes/texture_material_output_node.hpp"
#include "windows/editor_windows.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/item_parent_change_operation.hpp"
#include "operations/material_change_operation.hpp"
#include "operations/operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/operations_window.hpp"
#include "operations/set_edge_sharpness_operation.hpp"
#include "parsers/gltf.hpp"
#include "parsers/gltf_physics_export.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/image_writer.hpp"
#include "editor_log.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/attachment_types.hpp"
#include "scene/collision_shape_from_mesh.hpp"
#include "scene/node_joint.hpp"
#include "scene/node_physics.hpp"
#include "scene/physics_edits.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "renderers/id_renderer.hpp"
#include "scene/shadow_fit_debug.hpp"
#include "tools/mesh_component_selection.hpp"
#include "tools/mesh_component_selection_tool.hpp"
#include "tools/selection_tool.hpp"
#include "transform/transform_tool.hpp"
#include "transform/transform_tool_settings.hpp"

#include "erhe_imgui/imgui_windows.hpp"

#include <imgui/imgui.h>

#include "erhe_geometry/geometry.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_texgen/composer.hpp"
#include "erhe_texgen/node_descriptor.hpp"
#include "erhe_texgen/shader_code.hpp"
#include "erhe_texgen/value_type.hpp"

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
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#   include <process.h> // _getpid
#else
#   include <unistd.h>  // getpid
#endif

#if !defined(_WIN32)
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace editor {

using json = nlohmann::json;

namespace mcp_server_detail {

// Compile timestamp (expanded in each including translation unit) and the running
// process id. Both are
// surfaced in the startup log, the initialize response and get_server_info so a
// stale editor.exe holding the MCP port (a second process launched earlier that
// still owns 127.0.0.1:8080) is easy to detect: compare the reported pid/build
// against the editor you just launched.
constexpr const char* c_mcp_build_timestamp = __DATE__ " " __TIME__;

[[nodiscard]] inline auto get_process_id() -> long
{
#if defined(_WIN32)
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

inline auto make_jsonrpc_response(const std::string& id, const json& result) -> std::string
{
    json response = {
        {"jsonrpc", "2.0"},
        {"id",      id},
        {"result",  result}
    };
    return response.dump();
}

inline auto make_jsonrpc_error(const std::string& id, int code, const std::string& message) -> std::string
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

inline auto make_text_content(const std::string& text) -> json
{
    return {
        {"content", {{
            {"type", "text"},
            {"text", text}
        }}}
    };
}

inline auto make_json_content(const json& data) -> json
{
    return {
        {"content", {{
            {"type", "text"},
            {"text", data.dump(2)}
        }}}
    };
}

// Error result for tools/call: a text content block with isError set.
inline auto make_error_content(const std::string& message) -> std::string
{
    json r = make_text_content(message);
    r["isError"] = true;
    return r.dump();
}

inline auto get_vec3(const json& args, const char* key, const glm::vec3 fallback) -> glm::vec3
{
    const json value = args.value(key, json{});
    if (value.is_array() && (value.size() >= 3)) {
        return glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
    }
    return fallback;
}

// Finds a node by the integer args[id_key], or by the string args[name_key].
inline auto find_node_in_scene(Scene_root& scene_root, const json& args, const char* id_key, const char* name_key) -> std::shared_ptr<erhe::scene::Node>
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

inline auto find_light_in_scene(Scene_root& scene_root, const json& args, const char* id_key, const char* name_key) -> std::shared_ptr<erhe::scene::Light>
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

inline auto parse_light_type(const std::string& type, const erhe::scene::Light_type fallback) -> erhe::scene::Light_type
{
    if (type == "directional") { return erhe::scene::Light_type::directional; }
    if (type == "point")       { return erhe::scene::Light_type::point; }
    if (type == "spot")        { return erhe::scene::Light_type::spot; }
    return fallback;
}

template <typename T>
inline auto find_library_item(const std::shared_ptr<Content_library_node>& folder, const std::string& name) -> std::shared_ptr<T>
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

inline auto parse_axis(const std::string& axis) -> erhe::physics::Axis
{
    if (axis == "x") { return erhe::physics::Axis::X; }
    if (axis == "z") { return erhe::physics::Axis::Z; }
    return erhe::physics::Axis::Y;
}

inline auto parse_motion_mode(const std::string& motion_mode, const erhe::physics::Motion_mode fallback) -> erhe::physics::Motion_mode
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
inline auto parse_mesh_component_mode(const std::string& mode, const Mesh_component_mode fallback) -> Mesh_component_mode
{
    if (mode == "object") { return Mesh_component_mode::object; }
    if (mode == "vertex") { return Mesh_component_mode::vertex; }
    if (mode == "edge")   { return Mesh_component_mode::edge;   }
    if (mode == "face")   { return Mesh_component_mode::face;   }
    return fallback;
}

inline auto mesh_component_mode_lc(const Mesh_component_mode mode) -> const char*
{
    switch (mode) {
        case Mesh_component_mode::vertex: return "vertex";
        case Mesh_component_mode::edge:   return "edge";
        case Mesh_component_mode::face:   return "face";
        case Mesh_component_mode::object:
        default:                          return "object";
    }
}

inline auto is_valid_mesh_component_mode(const std::string& mode) -> bool
{
    return (mode == "object") || (mode == "vertex") || (mode == "edge") || (mode == "face");
}

inline auto transform_reference_mode_lc(const Transform_reference_mode mode) -> const char*
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
inline auto resolve_mesh_geometry(
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

// ---------------------------------------------------------------------------
// Mesh attribute introspection helpers (for get_mesh_geometry_info /
// get_mesh_attribute_values). The Geometry's per-domain attributes are visited
// generically so a value of any GEO::vecng type serializes uniformly.
// ---------------------------------------------------------------------------

template <typename T>
inline auto geo_type_name() -> const char*
{
    if constexpr (std::is_same_v<T, GEO::vec2f>) { return "vec2f"; }
    else if constexpr (std::is_same_v<T, GEO::vec3f>) { return "vec3f"; }
    else if constexpr (std::is_same_v<T, GEO::vec4f>) { return "vec4f"; }
    else if constexpr (std::is_same_v<T, GEO::vec4u>) { return "vec4u"; }
    else if constexpr (std::is_same_v<T, GEO::vec2i>) { return "vec2i"; }
    else { return "vec"; }
}

template <typename T>
inline auto geo_type_name_of(const erhe::geometry::Attribute_present<T>&) -> const char*
{
    return geo_type_name<T>();
}

template <typename T>
inline auto geo_vec_to_json(const T& v) -> json
{
    json arr = json::array();
    for (GEO::index_t i = 0; i < T::dim; ++i) {
        arr.push_back(v[i]);
    }
    return arr;
}

// {"present": bool, "value": [...]} for one attribute at one element key.
template <typename T>
inline auto attribute_value_json(const erhe::geometry::Attribute_present<T>& ap, const GEO::index_t key) -> json
{
    json e;
    const bool present = ap.has(key);
    e["present"] = present;
    if (present) {
        e["value"] = geo_vec_to_json(ap.get(key));
    }
    return e;
}

template <typename F>
inline void for_each_facet_attribute(erhe::geometry::Mesh_attributes& a, F&& f)
{
    f("facet_id",            a.facet_id);
    f("facet_centroid",      a.facet_centroid);
    f("facet_normal",        a.facet_normal);
    f("facet_tangent",       a.facet_tangent);
    f("facet_bitangent",     a.facet_bitangent);
    f("facet_color_0",       a.facet_color_0);
    f("facet_color_1",       a.facet_color_1);
    f("facet_aniso_control", a.facet_aniso_control);
}

template <typename F>
inline void for_each_vertex_attribute(erhe::geometry::Mesh_attributes& a, F&& f)
{
    f("vertex_normal",             a.vertex_normal);
    f("vertex_normal_smooth",      a.vertex_normal_smooth);
    f("vertex_texcoord_0",         a.vertex_texcoord_0);
    f("vertex_texcoord_1",         a.vertex_texcoord_1);
    f("vertex_tangent",            a.vertex_tangent);
    f("vertex_bitangent",          a.vertex_bitangent);
    f("vertex_color_0",            a.vertex_color_0);
    f("vertex_color_1",            a.vertex_color_1);
    f("vertex_joint_indices_0",    a.vertex_joint_indices_0);
    f("vertex_joint_indices_1",    a.vertex_joint_indices_1);
    f("vertex_joint_weights_0",    a.vertex_joint_weights_0);
    f("vertex_joint_weights_1",    a.vertex_joint_weights_1);
    f("vertex_aniso_control",      a.vertex_aniso_control);
    f("vertex_valency_edge_count", a.vertex_valency_edge_count);
}

template <typename F>
inline void for_each_corner_attribute(erhe::geometry::Mesh_attributes& a, F&& f)
{
    f("corner_normal",        a.corner_normal);
    f("corner_texcoord_0",    a.corner_texcoord_0);
    f("corner_texcoord_1",    a.corner_texcoord_1);
    f("corner_tangent",       a.corner_tangent);
    f("corner_bitangent",     a.corner_bitangent);
    f("corner_color_0",       a.corner_color_0);
    f("corner_color_1",       a.corner_color_1);
    f("corner_aniso_control", a.corner_aniso_control);
}

// Per-domain summary: one entry per attribute that is present on at least one
// element, with its type and how many elements carry it.
template <typename ForEach>
inline auto attribute_presence_summary(erhe::geometry::Mesh_attributes& attributes, const GEO::index_t element_count, ForEach&& for_each) -> json
{
    json arr = json::array();
    for_each(attributes, [&](const char* name, auto& ap) {
        GEO::index_t present_count = 0;
        for (GEO::index_t key = 0; key < element_count; ++key) {
            if (ap.has(key)) {
                ++present_count;
            }
        }
        if (present_count > 0) {
            arr.push_back({
                {"name",          name},
                {"type",          geo_type_name_of(ap)},
                {"present_count", present_count}
            });
        }
    });
    return arr;
}

inline auto motion_mode_to_string(const erhe::physics::Motion_mode motion_mode) -> const char*
{
    switch (motion_mode) {
        case erhe::physics::Motion_mode::e_static:                 return "static";
        case erhe::physics::Motion_mode::e_kinematic_non_physical: return "kinematic_non_physical";
        case erhe::physics::Motion_mode::e_kinematic_physical:     return "kinematic_physical";
        case erhe::physics::Motion_mode::e_dynamic:                return "dynamic";
        default:                                                   return "invalid";
    }
}

inline auto parse_combine_mode(const std::string& combine_mode, const erhe::physics::Combine_mode fallback) -> erhe::physics::Combine_mode
{
    if (combine_mode == "average")  { return erhe::physics::Combine_mode::e_average; }
    if (combine_mode == "minimum")  { return erhe::physics::Combine_mode::e_minimum; }
    if (combine_mode == "maximum")  { return erhe::physics::Combine_mode::e_maximum; }
    if (combine_mode == "multiply") { return erhe::physics::Combine_mode::e_multiply; }
    return fallback;
}

inline auto combine_mode_to_string(const erhe::physics::Combine_mode combine_mode) -> const char*
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
inline auto build_collision_shape_from_args(const json& args, const erhe::scene::Node* node, std::string& error) -> std::shared_ptr<erhe::physics::ICollision_shape>
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
inline void parse_joint_limits(const json& limits_json, std::vector<erhe::physics::Joint_limit>& out)
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
inline void parse_joint_drives(const json& drives_json, std::vector<erhe::physics::Joint_drive>& out)
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

inline auto joint_settings_to_json(const erhe::physics::Physics_joint_settings& settings) -> json
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

inline auto schema_no_args() -> json
{
    return {{"type", "object"}, {"properties", json::object()}};
}

inline auto schema_scene_name() -> json
{
    return {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}}
        }},
        {"required", json::array({"scene_name"})}
    };
}

inline auto schema_scene_and_item(const char* item_key, const char* item_desc) -> json
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
inline auto auth_token_path() -> std::filesystem::path
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
inline auto load_auth_token() -> std::string
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
inline auto constant_time_equal(std::string_view a, std::string_view b) -> bool
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
inline auto bearer_token_from(const httplib::Request& req) -> std::optional<std::string>
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
