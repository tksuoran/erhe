// Curve prim MCP tools (doc/editor/curves.md "MCP"): create_curve,
// get_curves, get_curve, set_curve, curve_set_point_kind, curve_add_point,
// curve_remove_points, evaluate_curve and the curve tool's point selection.

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"
#include "app_context.hpp"
#include "editor_log.hpp"
#include "operations/curve_set_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"
#include "prefabs/instance_structure.hpp"
#include "scene/scene_root.hpp"
#include "tools/curve_tool.hpp"
#include "transform/transform_tool.hpp"

#include "erhe_item/item.hpp"
#include "erhe_math/curve_path.hpp"
#include "erhe_scene/curve.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/scene.hpp"

#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

using json = nlohmann::json;
using namespace mcp_server_detail;

namespace {

[[nodiscard]] auto to_json(const glm::vec3& v) -> json
{
    return json::array({v.x, v.y, v.z});
}

// Unknown keys are refused, so a misspelled argument is not silently ignored.
[[nodiscard]] auto find_unknown_key(const json& object, std::initializer_list<std::string_view> known) -> std::optional<std::string>
{
    for (const auto& [key, value] : object.items()) {
        bool found = false;
        for (const std::string_view k : known) {
            if (key == k) {
                found = true;
                break;
            }
        }
        if (!found) {
            return key;
        }
    }
    return std::nullopt;
}

[[nodiscard]] auto read_vec3(const json& value, const std::string& what, glm::vec3& out_value, std::string& out_error) -> bool
{
    if (!value.is_array() || (value.size() != 3)) {
        out_error = fmt::format("{} must be an array of 3 numbers", what);
        return false;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (!value[i].is_number()) {
            out_error = fmt::format("{} must be an array of 3 numbers", what);
            return false;
        }
        const float component = value[i].get<float>();
        if (!std::isfinite(component)) {
            out_error = fmt::format("{}[{}] is not finite", what, i);
            return false;
        }
        out_value[static_cast<glm::length_t>(i)] = component;
    }
    return true;
}

// A point object {position, in?, out?, kind?}; a missing handle is zero, a missing kind corner.
[[nodiscard]] auto read_point(const json& point_json, const std::string& what, erhe::math::Curve_point& out_point, std::string& out_error) -> bool
{
    if (!point_json.is_object()) {
        out_error = fmt::format("{} must be an object", what);
        return false;
    }
    const std::optional<std::string> unknown = find_unknown_key(point_json, {"position", "in", "out", "kind"});
    if (unknown.has_value()) {
        out_error = fmt::format("{}: unknown key '{}' (expected position, in, out, kind)", what, unknown.value());
        return false;
    }
    erhe::math::Curve_point point{};
    if (!point_json.contains("position")) {
        out_error = fmt::format("{}: 'position' is required", what);
        return false;
    }
    if (!read_vec3(point_json.at("position"), fmt::format("{}.position", what), point.position, out_error)) {
        return false;
    }
    if (point_json.contains("in") && !read_vec3(point_json.at("in"), fmt::format("{}.in", what), point.in_handle, out_error)) {
        return false;
    }
    if (point_json.contains("out") && !read_vec3(point_json.at("out"), fmt::format("{}.out", what), point.out_handle, out_error)) {
        return false;
    }
    if (point_json.contains("kind")) {
        const json& kind = point_json.at("kind");
        if (!kind.is_string() || !erhe::scene::curve_point_kind_from_string(kind.get<std::string>(), point.kind)) {
            out_error = fmt::format("{}.kind must be \"corner\", \"smooth\" or \"free\"; got {}", what, kind.dump());
            return false;
        }
    }
    out_point = point;
    return true;
}

// points: [{position, in?, out?, kind?}], at least two.
[[nodiscard]] auto read_points(const json& points, std::vector<erhe::math::Curve_point>& out_points, std::string& out_error) -> bool
{
    if (!points.is_array()) {
        out_error = "'points' must be an array of point objects";
        return false;
    }
    if (points.size() < 2) {
        out_error = fmt::format("'points' must hold at least 2 points; got {}", points.size());
        return false;
    }
    out_points.clear();
    for (std::size_t i = 0, end = points.size(); i < end; ++i) {
        erhe::math::Curve_point point{};
        if (!read_point(points[i], fmt::format("points[{}]", i), point, out_error)) {
            return false;
        }
        out_points.push_back(point);
    }
    return true;
}

[[nodiscard]] auto read_closed(const json& args, erhe::math::Curve_closure& out_closure, std::string& out_error) -> bool
{
    const json& closed = args.at("closed");
    if (!closed.is_boolean()) {
        out_error = fmt::format("'closed' must be a boolean; got {}", closed.dump());
        return false;
    }
    out_closure = closed.get<bool>() ? erhe::math::Curve_closure::closed : erhe::math::Curve_closure::open;
    return true;
}

} // anonymous namespace

auto mcp_server_detail::read_curve_path_args(const json& args, erhe::math::Curve_path& io_path, std::string& out_error) -> bool
{
    if (args.contains("points") && !read_points(args.at("points"), io_path.points, out_error)) {
        return false;
    }
    return !args.contains("closed") || read_closed(args, io_path.closure, out_error);
}

namespace {

[[nodiscard]] auto paths_equal(const erhe::math::Curve_path& a, const erhe::math::Curve_path& b) -> bool
{
    if ((a.closure != b.closure) || (a.points.size() != b.points.size())) {
        return false;
    }
    for (std::size_t i = 0, end = a.points.size(); i < end; ++i) {
        const erhe::math::Curve_point& p = a.points[i];
        const erhe::math::Curve_point& q = b.points[i];
        if ((p.position != q.position) || (p.in_handle != q.in_handle) || (p.out_handle != q.out_handle) || (p.kind != q.kind)) {
            return false;
        }
    }
    return true;
}

enum class Curve_reply_points : unsigned int
{
    local = 0,
    local_and_world
};

[[nodiscard]] auto curve_to_json(const erhe::scene::Curve& curve, Scene_root& scene_root, const Curve_reply_points reply_points) -> json
{
    const bool include_world = (reply_points == Curve_reply_points::local_and_world);
    const erhe::math::Curve_path&       path  = curve.get_path();
    const erhe::math::Curve_path_table& table = curve.get_table();
    const glm::mat4 world_from_node = curve.world_from_node();
    json points = json::array();
    for (const erhe::math::Curve_point& point : path.points) {
        json point_json{
            {"position", to_json(point.position)},
            {"in",       to_json(point.in_handle)},
            {"out",      to_json(point.out_handle)},
            {"kind",     erhe::scene::c_str(point.kind)}
        };
        if (include_world) {
            point_json["world_position"] = to_json(glm::vec3{world_from_node * glm::vec4{point.position, 1.0f}});
            point_json["world_in"]       = to_json(glm::vec3{world_from_node * glm::vec4{point.position + point.in_handle,  1.0f}});
            point_json["world_out"]      = to_json(glm::vec3{world_from_node * glm::vec4{point.position + point.out_handle, 1.0f}});
        }
        points.push_back(std::move(point_json));
    }
    json distances = json::array();
    for (const float distance : table.get_point_distances()) {
        distances.push_back(distance);
    }
    const std::shared_ptr<erhe::scene::Node> parent = curve.get_parent_node();
    const glm::vec3 world_position{world_from_node[3]};
    const glm::quat world_rotation = curve.world_from_node_transform().get_rotation();
    return json{
        {"id",                   curve.get_id()},
        {"name",                 curve.get_name()},
        {"type",                 std::string{curve.get_type_name()}},
        {"parent_id",            parent ? parent->get_id() : std::size_t{0}},
        {"closed",               path.closure == erhe::math::Curve_closure::closed},
        {"point_count",          path.points.size()},
        {"span_count",           table.get_span_count()},
        {"length",               table.get_length()},
        {"point_distances",      std::move(distances)},
        {"points",               std::move(points)},
        {"world_position",       to_json(world_position)},
        {"world_rotation_xyzw",  json::array({world_rotation.x, world_rotation.y, world_rotation.z, world_rotation.w})},
        {"revision",             curve.get_revision()},
        {"change_message_count", scene_root.get_curve_change_message_count()}
    };
}

// The curve args name by curve_id / curve_name; an error text when there is none.
[[nodiscard]] auto find_curve(Scene_root& scene_root, const json& args, std::shared_ptr<erhe::scene::Curve>& out_curve) -> std::string
{
    if (!args.contains("curve_id") && !args.contains("curve_name")) {
        return "give curve_id or curve_name";
    }
    const std::shared_ptr<erhe::Hierarchy> prim = find_prim_in_scene(scene_root, args, "curve_id", "curve_name");
    out_curve = std::dynamic_pointer_cast<erhe::scene::Curve>(prim);
    if (!out_curve) {
        return prim
            ? fmt::format("'{}' is a {}, not a Curve", prim->get_name(), prim->get_type_name())
            : std::string{"Curve not found"};
    }
    return {};
}

[[nodiscard]] auto curve_point_selection_json(Curve_tool& curve_tool, Transform_tool* transform_tool) -> json
{
    static_cast<void>(curve_tool.update_point_set());
    const std::shared_ptr<erhe::scene::Curve> curve = curve_tool.get_target_curve();
    json reply = json::object();
    if (!curve) {
        reply["curve"]  = nullptr;
        reply["points"] = json::array();
        return reply;
    }
    const std::shared_ptr<Scene_root> scene_root = get_hosting_scene_root(curve.get());
    reply["curve"] = json{
        {"id",         curve->get_id()},
        {"name",       curve->get_name()},
        {"scene_name", scene_root ? scene_root->get_name() : std::string{}}
    };
    std::vector<Curve_point_ref> refs;
    curve_tool.get_selected_points(refs);
    json points = json::array();
    for (const Curve_point_ref& ref : refs) {
        glm::vec3 world{0.0f};
        static_cast<void>(curve_tool.get_point_world_position((ref.point * Curve_tool::parts_per_point) + static_cast<std::size_t>(ref.part), world));
        points.push_back(json{{"index", ref.point}, {"part", c_str(ref.part)}, {"world_position", to_json(world)}});
    }
    reply["points"] = std::move(points);
    // Whether the selection anchors the gizmo (it does from the next idle frame of a viewport).
    reply["gizmo_on_points"] = (transform_tool != nullptr) && transform_tool->is_component_mode() && !refs.empty();
    return reply;
}

} // anonymous namespace

auto Mcp_server::action_curve_add_point(const json& args) -> std::string
{
    const std::optional<std::string> unknown = find_unknown_key(args, {"scene_name", "curve_id", "curve_name", "at", "point", "position", "distance"});
    if (unknown.has_value()) {
        return make_error_content(fmt::format("curve_add_point: unknown argument '{}'", unknown.value()));
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::shared_ptr<erhe::scene::Curve> curve;
    std::string error = find_curve(*scene_root, args, curve);
    if (!error.empty()) {
        return make_error_content("curve_add_point: " + error);
    }
    if (!args.contains("at") || !args.at("at").is_string()) {
        return make_error_content("curve_add_point: 'at' must be \"start\", \"end\" or \"curve\"");
    }
    const std::string at = args.at("at").get<std::string>();
    std::optional<glm::vec3> position;
    if (args.contains("position")) {
        glm::vec3 value{0.0f};
        if (!read_vec3(args.at("position"), "position", value, error)) {
            return make_error_content("curve_add_point: " + error);
        }
        position = value;
    }
    std::shared_ptr<Curve_set_operation> operation;
    std::size_t index = 0;
    json        location_json;
    if ((at == "start") || (at == "end")) {
        if (args.contains("distance")) {
            return make_error_content("curve_add_point: 'distance' is for at \"curve\"");
        }
        if (args.contains("point") && position.has_value()) {
            return make_error_content("curve_add_point: give 'point' or 'position', not both");
        }
        const erhe::math::Curve_end end = (at == "start") ? erhe::math::Curve_end::start : erhe::math::Curve_end::end;
        std::optional<erhe::math::Curve_point> point;
        if (args.contains("point")) {
            erhe::math::Curve_point given{};
            if (!read_point(args.at("point"), "point", given, error)) {
                return make_error_content("curve_add_point: " + error);
            }
            point = given;
        } else if (position.has_value() && (curve->get_path().points.size() >= 2)) {
            // At the given place (in the prim's frame): the kind of the computed end point, Catmull-Rom handles.
            erhe::math::Curve_point given = erhe::math::make_curve_end_point(curve->get_path(), end);
            given.position = position.value();
            erhe::math::Curve_path probe = curve->get_path();
            const std::size_t probe_index = erhe::math::add_curve_point(probe, end, given);
            if (given.kind != erhe::math::Curve_point_kind::corner) {
                erhe::math::get_catmull_rom_handles(probe.points, probe_index, probe.closure, given.in_handle, given.out_handle);
            }
            point = given;
        }
        operation = make_curve_add_point_operation(curve, end, point, index, error);
    } else if (at == "curve") {
        if (args.contains("point")) {
            return make_error_content("curve_add_point: 'point' is for at \"start\" / \"end\"; a point on the curve follows from the split");
        }
        const bool has_distance = args.contains("distance");
        if (has_distance == position.has_value()) {
            return make_error_content("curve_add_point: at \"curve\" takes 'distance' or 'position' (world space)");
        }
        const erhe::math::Curve_path_table& table = curve->get_table();
        erhe::math::Curve_location location{};
        if (has_distance) {
            if (!args.at("distance").is_number() || !std::isfinite(args.at("distance").get<float>())) {
                return make_error_content("curve_add_point: 'distance' must be a finite number");
            }
            location = table.locate(args.at("distance").get<float>());
        } else {
            const glm::vec3 local = glm::vec3{curve->node_from_world() * glm::vec4{position.value(), 1.0f}};
            location = table.get_nearest_location(local);
        }
        location_json = json{{"span", location.span}, {"t", location.t}, {"distance", table.get_distance(location)}};
        operation = make_curve_insert_point_operation(curve, location, index, error);
    } else {
        return make_error_content(fmt::format("curve_add_point: 'at' must be \"start\", \"end\" or \"curve\"; got \"{}\"", at));
    }
    if (!operation) {
        return make_error_content("curve_add_point: " + error);
    }
    m_context.operation_stack->execute_now(operation);
    json reply = curve_to_json(*curve, *scene_root, Curve_reply_points::local);
    reply["changed"] = true;
    reply["index"]   = index;
    if (!location_json.is_null()) {
        reply["location"] = std::move(location_json);
    }
    return make_json_content(reply).dump();
}

auto Mcp_server::action_curve_remove_points(const json& args) -> std::string
{
    const std::optional<std::string> unknown = find_unknown_key(args, {"scene_name", "curve_id", "curve_name", "indices"});
    if (unknown.has_value()) {
        return make_error_content(fmt::format("curve_remove_points: unknown argument '{}'", unknown.value()));
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::shared_ptr<erhe::scene::Curve> curve;
    std::string error = find_curve(*scene_root, args, curve);
    if (!error.empty()) {
        return make_error_content("curve_remove_points: " + error);
    }
    if (!args.contains("indices") || !args.at("indices").is_array() || args.at("indices").empty()) {
        return make_error_content("curve_remove_points: 'indices' must be a non-empty array of point indices");
    }
    std::vector<std::size_t> indices;
    for (const json& index : args.at("indices")) {
        if (!index.is_number_integer() || (index.get<long long>() < 0)) {
            return make_error_content(fmt::format("curve_remove_points: index {} is not a non-negative integer", index.dump()));
        }
        indices.push_back(static_cast<std::size_t>(index.get<long long>()));
    }
    const std::shared_ptr<Curve_set_operation> operation = make_curve_remove_points_operation(curve, indices, error);
    if (!operation) {
        return make_error_content("curve_remove_points: " + error);
    }
    m_context.operation_stack->execute_now(operation);
    json reply = curve_to_json(*curve, *scene_root, Curve_reply_points::local);
    reply["changed"] = true;
    return make_json_content(reply).dump();
}

auto Mcp_server::query_get_curve_point_selection(const json& args) -> std::string
{
    static_cast<void>(args);
    if (m_context.curve_tool == nullptr) {
        return make_error_content("Curve tool not available");
    }
    return make_json_content(curve_point_selection_json(*m_context.curve_tool, m_context.transform_tool)).dump();
}

auto Mcp_server::action_select_curve_points(const json& args) -> std::string
{
    const std::optional<std::string> unknown = find_unknown_key(args, {"scene_name", "curve_id", "curve_name", "points", "extend"});
    if (unknown.has_value()) {
        return make_error_content(fmt::format("select_curve_points: unknown argument '{}'", unknown.value()));
    }
    if (m_context.curve_tool == nullptr) {
        return make_error_content("Curve tool not available");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::shared_ptr<erhe::scene::Curve> curve;
    const std::string error = find_curve(*scene_root, args, curve);
    if (!error.empty()) {
        return make_error_content("select_curve_points: " + error);
    }
    if (!args.contains("points") || !args.at("points").is_array()) {
        return make_error_content("select_curve_points: 'points' must be an array of {index, part} (an empty array clears)");
    }
    if (args.contains("extend") && !args.at("extend").is_boolean()) {
        return make_error_content("select_curve_points: 'extend' must be a boolean");
    }
    const erhe::math::Curve_path& path = curve->get_path();
    std::vector<Curve_point_ref> refs;
    for (const json& entry : args.at("points")) {
        if (!entry.is_object()) {
            return make_error_content("select_curve_points: each point is an object {index, part}");
        }
        const std::optional<std::string> unknown_key = find_unknown_key(entry, {"index", "part"});
        if (unknown_key.has_value()) {
            return make_error_content(fmt::format("select_curve_points: unknown point key '{}' (expected index, part)", unknown_key.value()));
        }
        if (!entry.contains("index") || !entry.at("index").is_number_integer()) {
            return make_error_content("select_curve_points: each point needs an integer 'index'");
        }
        const long long index = entry.at("index").get<long long>();
        if ((index < 0) || (static_cast<std::size_t>(index) >= path.points.size())) {
            return make_error_content(fmt::format("select_curve_points: index {} is outside [0, {}]", index, path.points.size() - 1));
        }
        Curve_point_ref ref{.point = static_cast<std::size_t>(index), .part = Curve_point_part::position};
        if (entry.contains("part") && (!entry.at("part").is_string() || !curve_point_part_from_string(entry.at("part").get<std::string>(), ref.part))) {
            return make_error_content(fmt::format("select_curve_points: part must be \"point\", \"in\" or \"out\"; got {}", entry.at("part").dump()));
        }
        if ((ref.part != Curve_point_part::position) && (path.points[ref.point].kind == erhe::math::Curve_point_kind::corner)) {
            return make_error_content(fmt::format("select_curve_points: point {} is a corner; it has no handles", ref.point));
        }
        refs.push_back(ref);
    }
    const Point_select select = args.value("extend", false) ? Point_select::toggle : Point_select::replace;
    if (!m_context.curve_tool->select_curve_points(curve, refs, select)) {
        return make_error_content("select_curve_points: the curve could not be made the curve tool's target");
    }
    return make_json_content(curve_point_selection_json(*m_context.curve_tool, m_context.transform_tool)).dump();
}

auto Mcp_server::action_create_curve(const json& args) -> std::string
{
    const std::optional<std::string> unknown = find_unknown_key(
        args, {"scene_name", "name", "parent_id", "parent_name", "position", "rotation_xyzw", "points", "closed"}
    );
    if (unknown.has_value()) {
        return make_error_content(fmt::format("create_curve: unknown argument '{}'", unknown.value()));
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::shared_ptr<erhe::Hierarchy> parent{};
    if (args.contains("parent_id") || args.contains("parent_name")) {
        parent = find_prim_in_scene(*scene_root, args, "parent_id", "parent_name");
        if (!parent) {
            return make_error_content("Parent not found");
        }
        const std::optional<std::string> child_refusal = instance_child_refusal(*parent);
        if (child_refusal.has_value()) {
            return make_error_content(child_refusal.value());
        }
    } else {
        parent = scene_root->get_hosted_scene()->get_root_node();
    }

    erhe::math::Curve_path path = erhe::scene::make_default_curve_path();
    std::string error;
    if (!read_curve_path_args(args, path, error)) {
        return make_error_content("create_curve: " + error);
    }
    glm::vec3 position{0.0f};
    if (args.contains("position") && !read_vec3(args.at("position"), "position", position, error)) {
        return make_error_content("create_curve: " + error);
    }
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    if (args.contains("rotation_xyzw")) {
        const json& r = args.at("rotation_xyzw");
        if (!r.is_array() || (r.size() != 4) || !r[0].is_number() || !r[1].is_number() || !r[2].is_number() || !r[3].is_number()) {
            return make_error_content("create_curve: rotation_xyzw must be an array of 4 numbers");
        }
        const float xyzw[4] = {r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>()};
        const std::optional<glm::quat> unit = make_unit_quaternion(xyzw, "rotation_xyzw", error);
        if (!unit.has_value()) {
            return make_error_content("create_curve: " + error);
        }
        rotation = unit.value();
    }

    const std::string name = args.value("name", std::string{"new curve"});
    std::shared_ptr<erhe::scene::Curve> curve = std::make_shared<erhe::scene::Curve>(name);
    curve->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    curve->set_path(path);
    // World pose: the insert keeps the world transform the prim has before it (Node::set_parent).
    curve->set_world_from_node(erhe::scene::Trs_transform{position, rotation, glm::vec3{1.0f}});
    m_context.operation_stack->execute_now(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context         = m_context,
                .item            = curve,
                .parent          = parent,
                .mode            = Item_insert_remove_operation::Mode::insert,
                .index_in_parent = std::numeric_limits<std::size_t>::max()
            }
        )
    );
    log_mcp->info("create_curve '{}' ({} points)", curve->get_name(), path.points.size());
    json reply = curve_to_json(*curve, *scene_root, Curve_reply_points::local_and_world);
    reply["scene_name"] = scene_name;
    return make_json_content(reply).dump();
}

auto Mcp_server::query_get_curves(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::vector<std::shared_ptr<erhe::scene::Curve>> curves;
    scene_root->get_curves(curves);
    json list = json::array();
    for (const std::shared_ptr<erhe::scene::Curve>& curve : curves) {
        json entry = curve_to_json(*curve, *scene_root, Curve_reply_points::local);
        entry.erase("points");
        list.push_back(std::move(entry));
    }
    return make_json_content(
        json{
            {"scene_name",           scene_name},
            {"count",                curves.size()},
            {"curves",               std::move(list)},
            {"change_message_count", scene_root->get_curve_change_message_count()}
        }
    ).dump();
}

auto Mcp_server::query_get_curve(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::shared_ptr<erhe::scene::Curve> curve;
    const std::string error = find_curve(*scene_root, args, curve);
    if (!error.empty()) {
        return make_error_content("get_curve: " + error);
    }
    const Curve_reply_points reply_points = args.value("world", false) ? Curve_reply_points::local_and_world : Curve_reply_points::local;
    return make_json_content(curve_to_json(*curve, *scene_root, reply_points)).dump();
}

auto Mcp_server::action_set_curve(const json& args) -> std::string
{
    const std::optional<std::string> unknown = find_unknown_key(args, {"scene_name", "curve_id", "curve_name", "points", "closed"});
    if (unknown.has_value()) {
        return make_error_content(fmt::format("set_curve: unknown argument '{}'", unknown.value()));
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::shared_ptr<erhe::scene::Curve> curve;
    std::string error = find_curve(*scene_root, args, curve);
    if (!error.empty()) {
        return make_error_content("set_curve: " + error);
    }
    if (!args.contains("points") && !args.contains("closed")) {
        return make_error_content("set_curve: give 'points', 'closed' or both");
    }
    if (is_instance_structure_protected(*curve)) {
        return make_error_content(fmt::format("set_curve: curve '{}' is inside a reference instance; its path is not an override", curve->get_name()));
    }
    erhe::math::Curve_path path = curve->get_path();
    if (args.contains("points") && !read_points(args.at("points"), path.points, error)) {
        return make_error_content("set_curve: " + error);
    }
    if (args.contains("closed") && !read_closed(args, path.closure, error)) {
        return make_error_content("set_curve: " + error);
    }
    const bool changed = !paths_equal(path, curve->get_path());
    if (changed) {
        m_context.operation_stack->execute_now(
            std::make_shared<Curve_set_operation>(curve, curve->get_path(), path, "set")
        );
    }
    json reply = curve_to_json(*curve, *scene_root, Curve_reply_points::local);
    reply["changed"] = changed;
    return make_json_content(reply).dump();
}

auto Mcp_server::action_curve_set_point_kind(const json& args) -> std::string
{
    const std::optional<std::string> unknown = find_unknown_key(args, {"scene_name", "curve_id", "curve_name", "index", "kind"});
    if (unknown.has_value()) {
        return make_error_content(fmt::format("curve_set_point_kind: unknown argument '{}'", unknown.value()));
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::shared_ptr<erhe::scene::Curve> curve;
    const std::string error = find_curve(*scene_root, args, curve);
    if (!error.empty()) {
        return make_error_content("curve_set_point_kind: " + error);
    }
    if (is_instance_structure_protected(*curve)) {
        return make_error_content(fmt::format("curve_set_point_kind: curve '{}' is inside a reference instance; its path is not an override", curve->get_name()));
    }
    const erhe::math::Curve_path& before = curve->get_path();
    if (!args.contains("index") || !args.at("index").is_number_integer()) {
        return make_error_content("curve_set_point_kind: 'index' (integer) is required");
    }
    const long long index = args.at("index").get<long long>();
    if ((index < 0) || (static_cast<std::size_t>(index) >= before.points.size())) {
        return make_error_content(fmt::format("curve_set_point_kind: index {} is outside [0, {}]", index, before.points.size() - 1));
    }
    erhe::math::Curve_point_kind kind{erhe::math::Curve_point_kind::corner};
    if (!args.contains("kind") || !args.at("kind").is_string() || !erhe::scene::curve_point_kind_from_string(args.at("kind").get<std::string>(), kind)) {
        return make_error_content("curve_set_point_kind: 'kind' must be \"corner\", \"smooth\" or \"free\"");
    }
    erhe::math::Curve_path after = before;
    erhe::math::set_curve_point_kind(after.points, static_cast<std::size_t>(index), after.closure, kind);
    const bool changed = !paths_equal(after, before);
    if (changed) {
        m_context.operation_stack->execute_now(
            std::make_shared<Curve_set_operation>(curve, before, after, fmt::format("point {} kind = {}", index, erhe::scene::c_str(kind)))
        );
    }
    json reply = curve_to_json(*curve, *scene_root, Curve_reply_points::local);
    reply["changed"] = changed;
    return make_json_content(reply).dump();
}

auto Mcp_server::query_evaluate_curve(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    std::shared_ptr<erhe::scene::Curve> curve;
    std::string error = find_curve(*scene_root, args, curve);
    if (!error.empty()) {
        return make_error_content("evaluate_curve: " + error);
    }
    if (!args.contains("distances") || !args.at("distances").is_array() || args.at("distances").empty()) {
        return make_error_content("evaluate_curve: 'distances' must be a non-empty array of numbers");
    }
    glm::vec3 reference_up{0.0f, 1.0f, 0.0f};
    if (args.contains("reference_up")) {
        if (!read_vec3(args.at("reference_up"), "reference_up", reference_up, error)) {
            return make_error_content("evaluate_curve: " + error);
        }
        if (glm::length(reference_up) < 1.0e-6f) {
            return make_error_content("evaluate_curve: reference_up must not be zero");
        }
        reference_up = glm::normalize(reference_up);
    }

    const erhe::math::Curve_path_table& table = curve->get_table();
    const glm::mat4 world_from_node = curve->world_from_node();
    const glm::mat3 world_from_node_3{world_from_node};
    json samples = json::array();
    glm::vec3 side{0.0f, 0.0f, -1.0f};
    for (const json& distance_json : args.at("distances")) {
        if (!distance_json.is_number()) {
            return make_error_content(fmt::format("evaluate_curve: distance {} is not a number", distance_json.dump()));
        }
        const float distance = distance_json.get<float>();
        if (!std::isfinite(distance)) {
            return make_error_content("evaluate_curve: a distance is not finite");
        }
        const erhe::math::Curve_sample local = table.sample_at_distance(distance);
        erhe::math::Curve_sample world{};
        world.position = glm::vec3{world_from_node * glm::vec4{local.position, 1.0f}};
        const glm::vec3 world_tangent = world_from_node_3 * local.tangent;
        world.tangent = (glm::length(world_tangent) > 0.0f) ? glm::normalize(world_tangent) : local.tangent;
        const erhe::math::Curve_frame frame = erhe::math::make_up_frame(world, reference_up, side);
        side = frame.side;
        samples.push_back(
            json{
                {"distance",            distance},
                {"normalized_distance", table.normalize_distance(distance)},
                {"position",            to_json(frame.position)},
                {"tangent",             to_json(frame.tangent)},
                {"side",                to_json(frame.side)},
                {"up",                  to_json(frame.up)},
                {"local_position",      to_json(local.position)},
                {"local_tangent",       to_json(local.tangent)}
            }
        );
    }
    return make_json_content(
        json{
            {"id",           curve->get_id()},
            {"name",         curve->get_name()},
            {"closed",       table.get_closure() == erhe::math::Curve_closure::closed},
            {"length",       table.get_length()},
            {"reference_up", to_json(reference_up)},
            {"samples",      std::move(samples)}
        }
    ).dump();
}

} // namespace editor
