// Mcp_server skeleton editing tools (doc/plans/rigging/skeleton_editing.md
// slice A): select_bones (R10) and flip_bone_names (R13 Flip Names). Both
// act on the bones their `bones` argument names, never on the selection or
// on UI state; the verbs are the ones the Hierarchy context menu of a bone
// runs (src/editor/rig/bone_commands.hpp).

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

#include "app_context.hpp"
#include "rig/bone_commands.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"

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

} // namespace editor
