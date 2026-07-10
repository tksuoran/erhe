// Mcp_server animation tools (issue #243): query scene animations, drive the
// Animation window / Animation_player, and edit keyframes (undoable).
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

#include "animation/animation_edit.hpp"
#include "animation/animation_keying.hpp"
#include "animation/animation_player.hpp"
#include "animation/animation_window.hpp"
#include "operations/animation_edit_operation.hpp"

#include "erhe_scene/animation.hpp"

namespace editor {

using namespace mcp_server_detail;

namespace {

[[nodiscard]] auto find_animation_in_scenes(
    App_scenes*        app_scenes,
    const std::string& name,
    const std::string& scene_name
) -> std::shared_ptr<erhe::scene::Animation>
{
    if (app_scenes == nullptr) {
        return {};
    }
    for (const std::shared_ptr<Scene_root>& scene_root : app_scenes->get_scene_roots()) {
        if (!scene_name.empty() && (scene_root->get_name() != scene_name)) {
            continue;
        }
        const std::shared_ptr<Content_library>& library = scene_root->get_content_library();
        if (!library || !library->animations) {
            continue;
        }
        std::shared_ptr<erhe::scene::Animation> found = find_library_item<erhe::scene::Animation>(library->animations, name);
        if (found) {
            return found;
        }
    }
    return {};
}

[[nodiscard]] auto autokey_mode_lc(const Autokey_mode mode) -> const char*
{
    switch (mode) {
        case Autokey_mode::off:            return "off";
        case Autokey_mode::modified_paths: return "modified";
        case Autokey_mode::all_paths:      return "all";
        default:                           return "?";
    }
}

[[nodiscard]] auto playback_state_json(const Animation_player& player) -> json
{
    const std::shared_ptr<erhe::scene::Animation>& animation = player.get_animation();
    return json{
        {"animation",  animation ? animation->get_name() : ""},
        {"playing",    player.is_playing()},
        {"looping",    player.is_looping()},
        {"speed",      player.get_speed()},
        {"time",       player.get_time()},
        {"start_time", player.get_start_time()},
        {"end_time",   player.get_end_time()},
        {"autokey",    autokey_mode_lc(player.get_autokey_mode())}
    };
}

// Collects the target nodes for the keying tools: named nodes when 'nodes'
// is given, the current selection otherwise.
[[nodiscard]] auto collect_keying_nodes(
    App_context&       context,
    const json&        args,
    std::string&       out_error
) -> std::vector<std::shared_ptr<erhe::scene::Node>>
{
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    if (args.contains("nodes") && args["nodes"].is_array()) {
        for (const auto& element : args["nodes"]) {
            if (!element.is_string()) {
                continue;
            }
            const std::string node_name = element.get<std::string>();
            std::shared_ptr<erhe::scene::Node> found;
            if (context.app_scenes != nullptr) {
                for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
                    for (const std::shared_ptr<erhe::scene::Node>& node : scene_root->get_scene().get_flat_nodes()) {
                        if (node && (node->get_name() == node_name)) {
                            found = node;
                            break;
                        }
                    }
                    if (found) {
                        break;
                    }
                }
            }
            if (!found) {
                out_error = "Node not found: " + node_name;
                return {};
            }
            nodes.push_back(std::move(found));
        }
    } else if (context.selection != nullptr) {
        for (const std::shared_ptr<erhe::Item_base>& item : context.selection->get_selected_items()) {
            std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
            if (node) {
                nodes.push_back(std::move(node));
            }
        }
    }
    if (nodes.empty()) {
        out_error = "No target nodes: pass 'nodes' or select nodes first";
    }
    return nodes;
}

} // anonymous namespace

auto Mcp_server::query_scene_animations(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    json animations = json::array();
    const auto append_from = [&animations](Scene_root& scene_root) {
        const std::shared_ptr<Content_library>& library = scene_root.get_content_library();
        if (!library || !library->animations) {
            return;
        }
        for (const std::shared_ptr<erhe::scene::Animation>& animation : library->animations->get_all<erhe::scene::Animation>()) {
            json channels = json::array();
            for (std::size_t channel_index = 0; channel_index < animation->channels.size(); ++channel_index) {
                const erhe::scene::Animation_channel& channel = animation->channels[channel_index];
                const erhe::scene::Animation_sampler& sampler = animation->samplers.at(channel.sampler_index);
                channels.push_back({
                    {"index",         channel_index},
                    {"target",        channel.target ? channel.target->get_name() : ""},
                    {"path",          erhe::scene::c_str(channel.path)},
                    {"components",    erhe::scene::get_component_count(channel.path)},
                    {"interpolation", erhe::scene::c_str(sampler.interpolation_mode)},
                    {"sampler_index", channel.sampler_index},
                    {"keyframes",     sampler.timestamps.size()},
                    {"animated",      is_channel_animated(*animation.get(), channel_index)}
                });
            }
            animations.push_back({
                {"name",       animation->get_name()},
                {"id",         animation->get_id()},
                {"scene",      scene_root.get_name()},
                {"start_time", animation->channels.empty() ? 0.0f : animation->get_first_time()},
                {"end_time",   animation->channels.empty() ? 0.0f : animation->get_last_time()},
                {"channels",   channels}
            });
        }
    };
    if (!scene_name.empty()) {
        Scene_root* sr = find_scene(scene_name);
        if (sr == nullptr) {
            return make_error_content("Scene not found: " + scene_name);
        }
        append_from(*sr);
    } else if (m_context.app_scenes != nullptr) {
        for (const std::shared_ptr<Scene_root>& sr : m_context.app_scenes->get_scene_roots()) {
            append_from(*sr);
        }
    }
    json result;
    result["animations"] = animations;
    return make_json_content(result).dump();
}

auto Mcp_server::action_set_animation_target(const json& args) -> std::string
{
    Animation_window* window = m_context.animation_window;
    if (window == nullptr) {
        return make_error_content("Animation window not available");
    }
    const std::string name = args.value("animation", "");
    if (name.empty()) {
        window->set_animation(std::shared_ptr<erhe::scene::Animation>{});
        return make_json_content({{"target_cleared", true}}).dump();
    }
    const std::string scene_name = args.value("scene_name", "");
    const std::shared_ptr<erhe::scene::Animation> found = find_animation_in_scenes(m_context.app_scenes, name, scene_name);
    if (!found) {
        return make_error_content("Animation not found: " + name);
    }
    window->set_animation(found);
    window->show_window();

    if (args.contains("visible_channels") && args["visible_channels"].is_array()) {
        for (std::size_t channel_index = 0; channel_index < found->channels.size(); ++channel_index) {
            window->set_channel_visibility(channel_index, 0u);
        }
        for (const auto& element : args["visible_channels"]) {
            if (element.is_number_integer()) {
                window->set_channel_visibility(static_cast<std::size_t>(element.get<int>()), 0xffffffffu);
            }
        }
        window->frame_all();
    }

    return make_json_content({
        {"target",     name},
        {"id",         found->get_id()},
        {"channels",   found->channels.size()},
        {"start_time", found->channels.empty() ? 0.0f : found->get_first_time()},
        {"end_time",   found->channels.empty() ? 0.0f : found->get_last_time()}
    }).dump();
}

auto Mcp_server::action_animation_playback(const json& args) -> std::string
{
    Animation_player* player = m_context.animation_player;
    if (player == nullptr) {
        return make_error_content("Animation player not available");
    }

    // Optional retarget by name so playback can be driven without the window.
    const std::string name = args.value("animation", "");
    if (!name.empty()) {
        const std::shared_ptr<erhe::scene::Animation> found = find_animation_in_scenes(m_context.app_scenes, name, args.value("scene_name", ""));
        if (!found) {
            return make_error_content("Animation not found: " + name);
        }
        if (m_context.animation_window != nullptr) {
            m_context.animation_window->set_animation(found); // keeps window and player in sync
        } else {
            player->set_animation(found);
        }
    }

    if (!player->get_animation()) {
        return make_error_content("No animation targeted; pass 'animation' or call set_animation_target first");
    }

    if (args.contains("autokey") && args["autokey"].is_string()) {
        const std::string autokey = args["autokey"].get<std::string>();
        if (autokey == "off") {
            player->set_autokey_mode(Autokey_mode::off);
        } else if (autokey == "modified") {
            player->set_autokey_mode(Autokey_mode::modified_paths);
        } else if (autokey == "all") {
            player->set_autokey_mode(Autokey_mode::all_paths);
        } else {
            return make_error_content("Unknown autokey mode: " + autokey + " (use off, modified or all)");
        }
    }
    if (args.contains("speed") && args["speed"].is_number()) {
        player->set_speed(args["speed"].get<float>());
    }
    if (args.contains("looping") && args["looping"].is_boolean()) {
        player->set_looping(args["looping"].get<bool>());
    }
    if (args.contains("time") && args["time"].is_number()) {
        player->seek(args["time"].get<float>());
    }
    const std::string action = args.value("action", "");
    if (action == "play") {
        player->play();
    } else if (action == "pause") {
        player->pause();
    } else if (action == "stop") {
        player->stop();
    } else if (!action.empty()) {
        return make_error_content("Unknown action: " + action + " (use play, pause or stop)");
    }

    // Apply pending seek immediately so the pose is visible to a
    // capture_screenshot on the next frame.
    player->update(0.0f);

    return make_json_content(playback_state_json(*player)).dump();
}

auto Mcp_server::action_animation_create_key(const json& args) -> std::string
{
    Animation_window* window = m_context.animation_window;
    Animation_player* player = m_context.animation_player;
    if ((window == nullptr) || (player == nullptr) || (m_context.operation_stack == nullptr)) {
        return make_error_content("Animation window / player not available");
    }

    std::string error;
    const std::vector<std::shared_ptr<erhe::scene::Node>> nodes = collect_keying_nodes(m_context, args, error);
    if (nodes.empty()) {
        return make_error_content(error);
    }

    const std::shared_ptr<erhe::scene::Animation> animation = window->get_animation();
    if (!animation) {
        return make_error_content("No animation targeted; call set_animation_target first");
    }

    bool key_translation = true;
    bool key_rotation    = true;
    bool key_scale       = true;
    if (args.contains("paths") && args["paths"].is_array()) {
        key_translation = false;
        key_rotation    = false;
        key_scale       = false;
        for (const auto& element : args["paths"]) {
            if (!element.is_string()) {
                continue;
            }
            const std::string path = element.get<std::string>();
            if      (path == "translation") key_translation = true;
            else if (path == "rotation")    key_rotation    = true;
            else if (path == "scale")       key_scale       = true;
            else {
                return make_error_content("Unknown path: " + path + " (use translation, rotation or scale)");
            }
        }
    }

    const float time = args.contains("time") && args["time"].is_number()
        ? args["time"].get<float>()
        : player->get_time();

    std::vector<Keying_request> requests;
    requests.reserve(nodes.size());
    for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
        requests.push_back(
            Keying_request{
                .node            = node,
                .key_translation = key_translation,
                .key_rotation    = key_rotation,
                .key_scale       = key_scale
            }
        );
    }

    const std::size_t channels_before = animation->channels.size();
    std::shared_ptr<Operation> operation = key_nodes(animation, requests, time);
    if (!operation) {
        return make_error_content("Nothing to key");
    }
    m_context.operation_stack->execute_now(std::move(operation));
    player->on_animation_edited(animation);

    return make_json_content({
        {"keyed_nodes",      nodes.size()},
        {"time",             time},
        {"channels_created", animation->channels.size() - channels_before},
        {"channels",         animation->channels.size()}
    }).dump();
}

auto Mcp_server::action_animation_delete_key(const json& args) -> std::string
{
    Animation_window* window = m_context.animation_window;
    Animation_player* player = m_context.animation_player;
    if ((window == nullptr) || (player == nullptr) || (m_context.operation_stack == nullptr)) {
        return make_error_content("Animation window / player not available");
    }
    const std::shared_ptr<erhe::scene::Animation> animation = window->get_animation();
    if (!animation) {
        return make_error_content("No animation targeted; call set_animation_target first");
    }

    std::string error;
    const std::vector<std::shared_ptr<erhe::scene::Node>> nodes = collect_keying_nodes(m_context, args, error);
    if (nodes.empty()) {
        return make_error_content(error);
    }

    const float time = args.contains("time") && args["time"].is_number()
        ? args["time"].get<float>()
        : player->get_time();

    std::shared_ptr<Operation> operation = delete_keys(animation, nodes, time);
    if (!operation) {
        return make_error_content("No keys at the given time for the given nodes");
    }
    m_context.operation_stack->execute_now(std::move(operation));
    player->on_animation_edited(animation);

    return make_json_content({{"deleted", true}, {"time", time}}).dump();
}

auto Mcp_server::action_animation_edit_keyframe(const json& args) -> std::string
{
    if (m_context.operation_stack == nullptr) {
        return make_error_content("Operation stack not available");
    }
    const std::string name = args.value("animation", "");
    std::shared_ptr<erhe::scene::Animation> animation;
    if (!name.empty()) {
        animation = find_animation_in_scenes(m_context.app_scenes, name, args.value("scene_name", ""));
        if (!animation) {
            return make_error_content("Animation not found: " + name);
        }
    } else if (m_context.animation_window != nullptr) {
        animation = m_context.animation_window->get_animation();
    }
    if (!animation) {
        return make_error_content("No animation targeted; pass 'animation' or call set_animation_target first");
    }

    const std::string edit = args.value("edit", "");
    if (!args.contains("channel_index") || !args["channel_index"].is_number_integer()) {
        return make_error_content("Missing required 'channel_index'");
    }
    const std::size_t channel_index = static_cast<std::size_t>(args["channel_index"].get<int>());
    if (channel_index >= animation->channels.size()) {
        return make_error_content("channel_index out of range");
    }
    const erhe::scene::Animation_channel& channel       = animation->channels.at(channel_index);
    const std::size_t                     sampler_index = channel.sampler_index;
    erhe::scene::Animation_sampler&       sampler       = animation->samplers.at(sampler_index);
    const std::size_t                     key_count     = sampler.timestamps.size();

    std::vector<Animation_sampler_state> before;
    before.push_back(capture_sampler_state(*animation.get(), sampler_index));

    json result;
    if (edit == "move") {
        if (!args.contains("key_index") || !args["key_index"].is_number_integer()) {
            return make_error_content("move requires 'key_index'");
        }
        const std::size_t key_index = static_cast<std::size_t>(args["key_index"].get<int>());
        if (key_index >= key_count) {
            return make_error_content("key_index out of range");
        }
        if (args.contains("time") && args["time"].is_number()) {
            const float applied_time = set_keyframe_time(*animation.get(), sampler_index, key_index, args["time"].get<float>());
            result["applied_time"] = applied_time;
        }
        if (args.contains("value") && args["value"].is_number()) {
            if (!args.contains("component") || !args["component"].is_number_integer()) {
                return make_error_content("move with 'value' requires 'component'");
            }
            const std::size_t component = static_cast<std::size_t>(args["component"].get<int>());
            if (component >= erhe::scene::get_component_count(channel.path)) {
                return make_error_content("component out of range for channel path");
            }
            set_keyframe_value(*animation.get(), channel_index, key_index, component, args["value"].get<float>());
            result["applied_value"] = args["value"].get<float>();
        }
        result["key_index"] = key_index;
    } else if (edit == "insert") {
        if (!args.contains("time") || !args["time"].is_number()) {
            return make_error_content("insert requires 'time'");
        }
        const std::size_t key_index = insert_keyframe(*animation.get(), sampler_index, args["time"].get<float>());
        result["key_index"] = key_index;
    } else if (edit == "delete") {
        if (!args.contains("key_index") || !args["key_index"].is_number_integer()) {
            return make_error_content("delete requires 'key_index'");
        }
        const std::size_t key_index = static_cast<std::size_t>(args["key_index"].get<int>());
        if (key_index >= key_count) {
            return make_error_content("key_index out of range");
        }
        if (!delete_keyframe(*animation.get(), sampler_index, key_index)) {
            return make_error_content("Cannot delete the last remaining keyframe of a sampler");
        }
    } else {
        return make_error_content("Unknown edit: '" + edit + "' (use move, insert or delete)");
    }

    std::vector<Animation_sampler_state> after;
    after.push_back(capture_sampler_state(*animation.get(), sampler_index));
    m_context.operation_stack->execute_now(
        std::make_shared<Animation_edit_operation>(
            "MCP " + edit + " keyframe",
            animation,
            std::move(before),
            std::move(after)
        )
    );

    result["edit"]      = edit;
    result["keyframes"] = sampler.timestamps.size();
    return make_json_content(result).dump();
}

}
