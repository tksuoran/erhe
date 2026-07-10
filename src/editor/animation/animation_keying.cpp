#include "animation/animation_keying.hpp"

#include "animation/animation_edit.hpp"
#include "operations/animation_edit_operation.hpp"

#include "erhe_scene/animation.hpp"
#include "erhe_scene/node.hpp"

#include <utility>

namespace editor {

auto c_str(const Autokey_mode mode) -> const char*
{
    switch (mode) {
        case Autokey_mode::off:            return "Off";
        case Autokey_mode::modified_paths: return "Modified";
        case Autokey_mode::all_paths:      return "All";
        default:                           return "?";
    }
}

auto key_nodes(
    const std::shared_ptr<erhe::scene::Animation>& animation,
    const std::vector<Keying_request>&             requests,
    const float                                    time
) -> std::shared_ptr<Operation>
{
    if (!animation) {
        return {};
    }

    bool any_requested = false;
    for (const Keying_request& request : requests) {
        if (request.node && (request.key_translation || request.key_rotation || request.key_scale)) {
            any_requested = true;
            break;
        }
    }
    if (!any_requested) {
        return {};
    }

    Animation_state before = capture_animation_state(*animation.get());

    for (const Keying_request& request : requests) {
        if (!request.node) {
            continue;
        }
        const erhe::scene::Animation_path paths[3] = {
            erhe::scene::Animation_path::TRANSLATION,
            erhe::scene::Animation_path::ROTATION,
            erhe::scene::Animation_path::SCALE
        };
        const bool enabled[3] = {request.key_translation, request.key_rotation, request.key_scale};
        for (std::size_t i = 0; i < 3; ++i) {
            if (!enabled[i]) {
                continue;
            }
            const std::size_t channel_index = ensure_channel(*animation.get(), request.node, paths[i], time);
            static_cast<void>(set_key_from_node(*animation.get(), channel_index, time));
        }
    }

    Animation_state after = capture_animation_state(*animation.get());
    return std::make_shared<Animation_structure_operation>(
        "Key nodes",
        animation,
        std::move(before),
        std::move(after)
    );
}

auto delete_keys(
    const std::shared_ptr<erhe::scene::Animation>&         animation,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& nodes,
    const float                                            time
) -> std::shared_ptr<Operation>
{
    if (!animation || nodes.empty()) {
        return {};
    }

    Animation_state before = capture_animation_state(*animation.get());

    bool any_deleted = false;
    for (std::size_t channel_index = 0; channel_index < animation->channels.size(); ++channel_index) {
        const erhe::scene::Animation_channel& channel = animation->channels[channel_index];
        bool targeted = false;
        for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
            if (channel.target == node) {
                targeted = true;
                break;
            }
        }
        if (!targeted) {
            continue;
        }
        any_deleted = delete_key_at_time(*animation.get(), channel_index, time) || any_deleted;
    }

    if (!any_deleted) {
        return {};
    }

    Animation_state after = capture_animation_state(*animation.get());
    return std::make_shared<Animation_structure_operation>(
        "Delete keys",
        animation,
        std::move(before),
        std::move(after)
    );
}

}
