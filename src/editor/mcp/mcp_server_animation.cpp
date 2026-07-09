// Mcp_server animation tools (issue #243): query scene animations, drive the
// Animation window / Animation_player, and edit keyframes (undoable).
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

#include "animation/animation_edit.hpp"
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
        {"end_time",   player.get_end_time()}
    };
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
                    {"keyframes",     sampler.timestamps.size()}
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
