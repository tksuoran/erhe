// Mcp_server skeleton editing tools (doc/plans/rigging/skeleton_editing.md
// slices A and B): select_bones (R10), flip_bone_names (R13 Flip Names),
// clear_pose (R14), copy_pose and paste_pose (R15). They act on the bones
// their arguments name, never on the selection or on UI state - paste_pose
// takes the pose as an argument and never reads the editor's pose buffer;
// the verbs are the ones the Hierarchy context menu of a bone runs
// (src/editor/rig/bone_commands.hpp).

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

#include "app_context.hpp"
#include "rig/bone_commands.hpp"
#include "rig/bone_pose.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

using namespace mcp_server_detail;

namespace {

[[nodiscard]] auto error_result(const std::string& message) -> std::string
{
    json r = make_text_content(message);
    r["isError"] = true;
    return r.dump();
}

// The bones `bones` names: each entry is a node id (integer) or a node name
// (string, the first bone node of that name in the scene). Fills `out`;
// returns an error text for the first entry that names no bone.
[[nodiscard]] auto resolve_bones(Scene_root& scene_root, const json& bones, std::vector<std::shared_ptr<erhe::scene::Node>>& out) -> std::optional<std::string>
{
    if (!bones.is_array() || bones.empty()) {
        return std::string{"bones must be a non-empty array of bone names or node ids"};
    }
    for (const json& entry : bones) {
        const bool by_id = entry.is_number_unsigned() || entry.is_number_integer();
        if (!by_id && !entry.is_string()) {
            return "bones entries are names (string) or node ids (integer): " + entry.dump();
        }
        const std::size_t id   = by_id ? entry.get<std::size_t>() : std::size_t{0};
        const std::string name = by_id ? std::string{} : entry.get<std::string>();
        std::shared_ptr<erhe::scene::Node> found;
        scene_root.get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) {
            const bool match = by_id ? (node->get_id() == id) : (node->get_name() == name);
            if (match && erhe::scene::is_bone(node.get())) {
                found = node;
                return false;
            }
            return true;
        });
        if (!found) {
            return "No bone node in scene " + scene_root.get_name() + " for " + entry.dump();
        }
        out.push_back(found);
    }
    return std::nullopt;
}

class Mode_name
{
public:
    std::string_view name;
    Bone_select_mode mode;
};

constexpr std::array<Mode_name, 5> c_mode_names{{
    {"parent",             Bone_select_mode::parent},
    {"children",           Bone_select_mode::children},
    {"children_recursive", Bone_select_mode::children_recursive},
    {"chain",              Bone_select_mode::chain},
    {"mirror",             Bone_select_mode::mirror}
}};

[[nodiscard]] auto node_json(const erhe::scene::Node& node) -> json
{
    return json{{"name", node.get_name()}, {"id", node.get_id()}};
}

// A transform plan's result: the changed bones, the lock_edit bones left
// alone and the unmatched pose entries.
[[nodiscard]] auto plan_json(const Bone_pose_plan& plan) -> json
{
    json changed = json::array();
    for (const Bone_pose_change& change : plan.changes) {
        changed.push_back(node_json(*change.bone));
    }
    json sealed = json::array();
    for (const std::shared_ptr<erhe::scene::Node>& bone : plan.sealed) {
        sealed.push_back(node_json(*bone));
    }
    return json{
        {"changed",   changed},
        {"sealed",    sealed},
        {"unmatched", plan.unmatched},
        // One undoable operation, executed on the next editor frame.
        {"queued",    !plan.changes.empty()}
    };
}

// Reads `count` numbers of `value` into `out`; false when `value` is not an
// array of exactly `count` numbers.
[[nodiscard]] auto read_numbers(const json& value, const std::size_t count, float* const out) -> bool
{
    if (!value.is_array() || (value.size() != count)) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!value[i].is_number()) {
            return false;
        }
        out[i] = value[i].get<float>();
    }
    return true;
}

// The pose argument of paste_pose, in copy_pose's shape.
[[nodiscard]] auto parse_pose(const json& value, Bone_pose& out) -> std::optional<std::string>
{
    if (!value.is_array()) {
        return std::string{"pose must be an array of {name, translation, rotation_xyzw, scale} (copy_pose's 'pose')"};
    }
    for (const json& entry : value) {
        if (!entry.is_object() || !entry.contains("name") || !entry.at("name").is_string()) {
            return "pose entries need a string 'name': " + entry.dump();
        }
        Bone_pose_entry bone{.name = entry.at("name").get<std::string>()};
        float v[4]{};
        if (entry.contains("translation")) {
            if (!read_numbers(entry.at("translation"), 3, v)) {
                return "pose entry '" + bone.name + "': translation must be [x, y, z]";
            }
            bone.translation = glm::vec3{v[0], v[1], v[2]};
        }
        if (entry.contains("rotation_xyzw")) {
            if (!read_numbers(entry.at("rotation_xyzw"), 4, v)) {
                return "pose entry '" + bone.name + "': rotation_xyzw must be [x, y, z, w]";
            }
            bone.rotation = glm::normalize(glm::quat{v[3], v[0], v[1], v[2]});
        }
        if (entry.contains("scale")) {
            if (!read_numbers(entry.at("scale"), 3, v)) {
                return "pose entry '" + bone.name + "': scale must be [x, y, z]";
            }
            bone.scale = glm::vec3{v[0], v[1], v[2]};
        }
        out.bones.push_back(std::move(bone));
    }
    return std::nullopt;
}

} // anonymous namespace

auto Mcp_server::action_select_bones(const json& args) -> std::string
{
    if (m_context.selection == nullptr) {
        return error_result("Selection system not available");
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return error_result("Scene not found: " + scene_name);
    }
    const std::string mode_text = args.value("mode", "");
    std::optional<Bone_select_mode> mode;
    for (const Mode_name& entry : c_mode_names) {
        if (entry.name == mode_text) {
            mode = entry.mode;
        }
    }
    if (!mode.has_value()) {
        return error_result("mode must be one of parent, children, children_recursive, chain, mirror; got '" + mode_text + "'");
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> targets;
    const std::optional<std::string> error = resolve_bones(*sr, args.value("bones", json{}), targets);
    if (error.has_value()) {
        return error_result(error.value());
    }

    const std::vector<std::shared_ptr<erhe::scene::Node>> selected = select_bones(m_context, targets, mode.value());
    if (!selected.empty()) {
        // As select_items: the selection change makes the target scene active.
        m_context.selection->set_active_scene_root(sr->shared_from_this());
    }

    json selected_json = json::array();
    for (const std::shared_ptr<erhe::scene::Node>& bone : selected) {
        selected_json.push_back(node_json(*bone));
    }
    json result = {
        {"mode",           mode_text},
        {"selected_count", selected.size()},
        {"selected",       selected_json},
        // An empty result leaves the selection as it was.
        {"changed",        !selected.empty()}
    };
    const std::shared_ptr<erhe::Item_base> active = m_context.selection->get_active_item();
    if (active) {
        result["active_item"] = json{{"name", active->get_name()}, {"id", active->get_id()}};
    }
    return make_json_content(result).dump();
}

auto Mcp_server::action_flip_bone_names(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return error_result("Scene not found: " + scene_name);
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> targets;
    const std::optional<std::string> error = resolve_bones(*sr, args.value("bones", json{}), targets);
    if (error.has_value()) {
        return error_result(error.value());
    }

    const Flip_bone_names_result flipped = flip_bone_names(m_context, targets);
    json renamed = json::array();
    for (const Bone_rename& rename : flipped.renamed) {
        renamed.push_back(json{{"id", rename.bone->get_id()}, {"from", rename.from}, {"to", rename.to}});
    }
    json skipped = json::array();
    for (const Bone_rename_skip& skip : flipped.skipped) {
        skipped.push_back(json{{"id", skip.bone->get_id()}, {"name", skip.bone->get_name()}, {"reason", skip.reason}});
    }
    return make_json_content({
        {"renamed", renamed},
        {"skipped", skipped},
        // One undoable operation, executed on the next editor frame.
        {"queued",  !flipped.renamed.empty()}
    }).dump();
}

auto Mcp_server::action_clear_pose(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return error_result("Scene not found: " + scene_name);
    }
    const json channels_arg = args.value("channels", json{});
    Pose_channels channels;
    const auto add_channel = [&channels](const std::string& name) -> bool {
        if      (name == "location") { channels.location = true; }
        else if (name == "rotation") { channels.rotation = true; }
        else if (name == "scale"   ) { channels.scale    = true; }
        else if (name == "all"     ) { channels.location = true; channels.rotation = true; channels.scale = true; }
        else {
            return false;
        }
        return true;
    };
    bool channels_valid = false;
    if (channels_arg.is_string()) {
        channels_valid = add_channel(channels_arg.get<std::string>());
    } else if (channels_arg.is_array() && !channels_arg.empty()) {
        channels_valid = true;
        for (const json& entry : channels_arg) {
            channels_valid = channels_valid && entry.is_string() && add_channel(entry.get<std::string>());
        }
    }
    if (!channels_valid) {
        return error_result("channels must be 'all' or a non-empty array of 'location', 'rotation', 'scale'; got " + channels_arg.dump());
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> targets;
    const std::optional<std::string> error = resolve_bones(*sr, args.value("bones", json{}), targets);
    if (error.has_value()) {
        return error_result(error.value());
    }

    const Bone_pose_plan plan = clear_bone_pose(m_context, targets, channels);
    json result = plan_json(plan);
    result["channels"] = json{{"location", channels.location}, {"rotation", channels.rotation}, {"scale", channels.scale}};
    return make_json_content(result).dump();
}

auto Mcp_server::action_copy_pose(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return error_result("Scene not found: " + scene_name);
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> targets;
    const std::optional<std::string> error = resolve_bones(*sr, args.value("bones", json{}), targets);
    if (error.has_value()) {
        return error_result(error.value());
    }
    const Bone_pose pose = copy_bone_pose(targets);
    json entries = json::array();
    for (const Bone_pose_entry& entry : pose.bones) {
        entries.push_back(
            json{
                {"name",          entry.name},
                {"translation",   {entry.translation.x, entry.translation.y, entry.translation.z}},
                {"rotation_xyzw", {entry.rotation.x, entry.rotation.y, entry.rotation.z, entry.rotation.w}},
                {"scale",         {entry.scale.x, entry.scale.y, entry.scale.z}}
            }
        );
    }
    return make_json_content({{"pose", entries}}).dump();
}

auto Mcp_server::action_paste_pose(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return error_result("Scene not found: " + scene_name);
    }
    const std::string mode_text = args.value("mode", "normal");
    Paste_pose_mode   mode      = Paste_pose_mode::normal;
    if (mode_text == "flipped") {
        mode = Paste_pose_mode::flipped;
    } else if (mode_text != "normal") {
        return error_result("mode must be 'normal' or 'flipped'; got '" + mode_text + "'");
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> skeleton;
    const std::optional<std::string> skeleton_error = resolve_bones(*sr, json::array({args.value("skeleton", json{})}), skeleton);
    if (skeleton_error.has_value()) {
        return error_result("skeleton: " + skeleton_error.value());
    }
    Bone_pose pose;
    const std::optional<std::string> pose_error = parse_pose(args.value("pose", json{}), pose);
    if (pose_error.has_value()) {
        return error_result(pose_error.value());
    }

    const Bone_pose_plan plan = paste_bone_pose(m_context, skeleton.front(), pose, mode);
    json result = plan_json(plan);
    result["mode"] = mode_text;
    return make_json_content(result).dump();
}

} // namespace editor
