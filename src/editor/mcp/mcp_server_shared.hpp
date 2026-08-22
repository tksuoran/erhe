#pragma once

// Internal header shared by the Mcp_server translation units
// (mcp_server*.cpp). Not for use outside mcp/.
//
// Non-template helpers are declared here and defined in
// mcp_server_shared.cpp; only templates stay in the header.

#include "content_library/content_library.hpp"  // Content_library_node (find_library_item)
#include "tools/mesh_component_selection.hpp"   // Mesh_component_mode
#include "transform/transform_tool_settings.hpp" // Transform_reference_mode

#include "erhe_geometry/geometry.hpp"            // Attribute_present, Mesh_attributes, GEO::vecng
#include "erhe_physics/icollision_shape.hpp"     // Axis, ICollision_shape
#include "erhe_physics/irigid_body.hpp"          // Motion_mode
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_physics/physics_material.hpp"     // Combine_mode
#include "erhe_scene/light.hpp"                  // Light_type

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace httplib {
    struct Request;
}

namespace erhe::scene {
    class Mesh;
    class Node;
}

namespace editor {

class Scene_root;

using json = nlohmann::json;

namespace mcp_server_detail {

// Build timestamp of mcp_server_shared.cpp and the running process id. Both are
// surfaced in the startup log, the initialize response and get_server_info so a
// stale editor.exe holding the MCP port (a second process launched earlier that
// still owns 127.0.0.1:3743) is easy to detect: compare the reported pid/build
// against the editor you just launched.
extern const char* const c_mcp_build_timestamp;

[[nodiscard]] auto get_process_id() -> long;

// The id is echoed verbatim: JSON-RPC 2.0 requires the response id to have
// the same type as the request id (a numeric id must not come back as a
// string). Pass nullptr when no request id is known (parse error, auth).
auto make_jsonrpc_response(const json& id, const json& result) -> std::string;
auto make_jsonrpc_error(const json& id, int code, const std::string& message) -> std::string;
auto make_text_content(const std::string& text) -> json;
auto make_json_content(const json& data) -> json;

// Error result for tools/call: a text content block with isError set.
auto make_error_content(const std::string& message) -> std::string;

auto get_vec3(const json& args, const char* key, const glm::vec3 fallback) -> glm::vec3;

// Finds a node by the integer args[id_key], or by the string args[name_key].
auto find_node_in_scene(Scene_root& scene_root, const json& args, const char* id_key, const char* name_key) -> std::shared_ptr<erhe::scene::Node>;

auto find_light_in_scene(Scene_root& scene_root, const json& args, const char* id_key, const char* name_key) -> std::shared_ptr<erhe::scene::Light>;

auto parse_light_type(const std::string& type, const erhe::scene::Light_type fallback) -> erhe::scene::Light_type;

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

auto parse_axis(const std::string& axis) -> erhe::physics::Axis;

auto parse_motion_mode(const std::string& motion_mode, const erhe::physics::Motion_mode fallback) -> erhe::physics::Motion_mode;

// Mesh component mode <-> lowercase string (matches the MCP tool argument names,
// distinct from the UI-facing c_str() which is capitalized).
auto parse_mesh_component_mode(const std::string& mode, const Mesh_component_mode fallback) -> Mesh_component_mode;

auto mesh_component_mode_lc(const Mesh_component_mode mode) -> const char*;

auto is_valid_mesh_component_mode(const std::string& mode) -> bool;

auto transform_reference_mode_lc(const Transform_reference_mode mode) -> const char*;

// Resolve a node's renderable Geometry for a given primitive, mirroring the path
// the node-details query and the component-selection tool use:
// node -> Mesh attachment -> primitive[primitive_index] -> render_shape geometry.
auto resolve_mesh_geometry(
    const std::shared_ptr<erhe::scene::Node>&  node,
    const std::size_t                          primitive_index,
    std::shared_ptr<erhe::scene::Mesh>&        out_mesh,
    std::shared_ptr<erhe::geometry::Geometry>& out_geometry
) -> bool;

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
    f("vertex_texcoord_2",         a.vertex_texcoord_2);
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
    f("corner_texcoord_2",    a.corner_texcoord_2);
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

auto motion_mode_to_string(const erhe::physics::Motion_mode motion_mode) -> const char*;

auto parse_combine_mode(const std::string& combine_mode, const erhe::physics::Combine_mode fallback) -> erhe::physics::Combine_mode;

auto combine_mode_to_string(const erhe::physics::Combine_mode combine_mode) -> const char*;

// Builds a collision shape from tool arguments. "auto" (the default) builds
// a convex hull from the node's mesh, falling back to a unit box when the
// node has no usable mesh geometry. Returns nullptr with error set on
// failure.
auto build_collision_shape_from_args(const json& args, const erhe::scene::Node* node, std::string& error) -> std::shared_ptr<erhe::physics::ICollision_shape>;

// Replaces out with limits parsed from a JSON array of limit objects.
void parse_joint_limits(const json& limits_json, std::vector<erhe::physics::Joint_limit>& out);

// Replaces out with drives parsed from a JSON array of drive objects.
void parse_joint_drives(const json& drives_json, std::vector<erhe::physics::Joint_drive>& out);

auto joint_settings_to_json(const erhe::physics::Physics_joint_settings& settings) -> json;

auto schema_no_args() -> json;

auto schema_scene_name() -> json;

auto schema_scene_and_item(const char* item_key, const char* item_desc) -> json;

// Returns $HOME/.agents/erhe_mcp_token (or %USERPROFILE%\.agents\... on
// Windows). The directory is not created here; the file is optional.
auto auth_token_path() -> std::filesystem::path;

// Returns the trimmed file contents, or an empty string if the file
// is missing or unreadable. On POSIX the file must be mode 0600
// (owner read/write only); other modes are refused with a warning so
// a world-readable token is not picked up silently.
auto load_auth_token() -> std::string;

// Constant-time comparison so the response timing does not reveal how
// far the first mismatch was. Both inputs are short opaque tokens; the
// constant-time guard is cheap.
auto constant_time_equal(std::string_view a, std::string_view b) -> bool;

// Parses "Authorization: Bearer <token>" out of an httplib::Request.
// Returns the token (possibly empty) or std::nullopt if the header is
// missing or malformed.
auto bearer_token_from(const httplib::Request& req) -> std::optional<std::string>;

} // namespace mcp_server_detail

} // namespace editor
