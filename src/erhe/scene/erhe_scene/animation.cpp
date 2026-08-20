#include "erhe_scene/animation.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::scene {

auto get_component_count(const Animation_path path) -> std::size_t
{
    switch (path) {
        case Animation_path::TRANSLATION: return 3; // T_x, T_y, T_z
        case Animation_path::ROTATION:    return 4; // Q_x, Q_y, Q_z, Q_w
        case Animation_path::SCALE:       return 3; // S_x, S_y, S_z
        default:                          return 0; // TODO
    }
}

// The value that leaves the target untouched, in the packing
// Animation_sampler::evaluate() returns: xyz(+w for a quaternion). Used when a
// sampler carries nothing usable for the channel, so that malformed animation
// data neither reads out of bounds nor collapses the node to a zero scale.
[[nodiscard]] static auto identity_value(const Animation_path path) -> glm::vec4
{
    switch (path) {
        case Animation_path::TRANSLATION: return glm::vec4{0.0f, 0.0f, 0.0f, 0.0f};
        case Animation_path::ROTATION:    return glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}; // x, y, z, w
        case Animation_path::SCALE:       return glm::vec4{1.0f, 1.0f, 1.0f, 0.0f};
        default:                          return glm::vec4{0.0f, 0.0f, 0.0f, 0.0f};
    }
}

[[nodiscard]] auto get_key_value_count(const Animation_interpolation_mode interpolation_mode) -> std::size_t
{
    switch (interpolation_mode) {
        case Animation_interpolation_mode::STEP:        return 1; // value
        case Animation_interpolation_mode::LINEAR:      return 1; // value
        case Animation_interpolation_mode::CUBICSPLINE: return 3; // tangent in, value, tangent out
        default: return 0;
    }
}

auto c_str(const Animation_path path) -> const char*
{
    switch (path) {
        case Animation_path::INVALID:     return "Invalid";
        case Animation_path::TRANSLATION: return "Translation";
        case Animation_path::ROTATION:    return "Rotation";
        case Animation_path::SCALE:       return "Scale";
        case Animation_path::WEIGHTS:     return "Weights";
        default:                          return "";
    }
}

auto c_str(const Animation_interpolation_mode interpolation_mode) -> const char*
{
    switch (interpolation_mode) {
        case Animation_interpolation_mode::STEP:        return "Step";
        case Animation_interpolation_mode::LINEAR:      return "Linear";
        case Animation_interpolation_mode::CUBICSPLINE: return "Cubic Spline";
        default:                                        return "?";
    }
}

Animation_sampler::Animation_sampler() = default;

Animation_sampler::Animation_sampler(
    const Animation_interpolation_mode interpolation_mode
)
    : interpolation_mode{interpolation_mode}
{
}

Animation_sampler::~Animation_sampler() noexcept = default;

void Animation_sampler::set(std::vector<float>&& timestamps_in, std::vector<float>&& values_in)
{
    timestamps = std::move(timestamps_in);
    data       = std::move(values_in);
}

class Cubic_constants
{
public:
    // t is the keyframe-normalized time in [0, 1), t_d the keyframe delta in
    // seconds. glTF 2.0 appendix C scales both tangent terms by t_d:
    //
    //   p(t) = (2t^3 - 3t^2 + 1) v0 + t_d (t^3 - 2t^2 + t) b0 +
    //          (-2t^3 + 3t^2)    v1 + t_d (t^3 - t^2)      a1
    //
    // The stored tangents are per-second derivatives, so leaving t_d out only
    // happens to be correct when keyframes are exactly one second apart. At a
    // 30 fps sampling the tangent contribution comes out 30x too large.
    Cubic_constants(const float t, const float t_d)
    {
        const float t2 = t * t;
        const float t3 = t2 * t;
        s0 =  2.0f * t3 - 3.0f * t2 + 1.0f;
        s1 =  t_d * (t3 - 2.0f * t2 + t);
        s2 = -2.0f * t3 + 3.0f * t2;
        s3 =  t_d * (t3 - t2);
    }

    [[nodiscard]] auto interpolate(
        const glm::vec3 start_value,
        const glm::vec3 start_tangent_out,
        const glm::vec3 end_tangent_in,
        const glm::vec3 end_value
    ) const -> glm::vec3
    {
        return
            s0 * start_value       +
            s1 * start_tangent_out +
            s2 * end_value         +
            s3 * end_tangent_in;
    }

    [[nodiscard]] auto interpolate(
        const glm::quat start_value,
        const glm::quat start_tangent_out,
        const glm::quat end_tangent_in,
        const glm::quat end_value
    ) const -> glm::quat
    {
        return
            s0 * start_value       +
            s1 * start_tangent_out +
            s2 * end_value         +
            s3 * end_tangent_in;
    }

    float s0; // coefficient for start value
    float s1; // coefficient for start tangent
    float s2; // coefficient for end value
    float s3; // coefficient for end tangent
};

void Animation_sampler::seek(Animation_channel& channel, const float time) const
{
    if (timestamps.empty()) {
        return;
    }

    // start_position is carried across calls and can outlive the sampler data
    // it was found in (Animation_edit rewrites sampler timestamps in place).
    if (channel.start_position >= timestamps.size()) {
        channel.start_position = timestamps.size() - 1;
    }

    if (timestamps[channel.start_position] == time) {
        return;
    }

    while (time < timestamps[channel.start_position]) {
        if (channel.start_position > 0) {
            --channel.start_position;
            continue;
        }
        return;
    }

    if (timestamps[channel.start_position] == time) {
        return;
    }

    std::size_t end = timestamps.size();
    for (;;) {
        std::size_t next = channel.start_position + 1;
        if (next == end) {
            return;
        }
        if (time >= timestamps[next]) {
            channel.start_position = next;
            continue;
        }
        break;
    }
}

auto Animation_sampler::evaluate(Animation_channel& channel, float time_current) const -> glm::vec4
{
    seek(channel, time_current);

    using namespace glm;

    const std::size_t k      = get_component_count(channel.path) * get_key_value_count(interpolation_mode);
    const std::size_t offset = channel.start_position * k + channel.value_offset;

    // A sampler with no keyframes (or with too little data for the keyframe
    // seek() settled on) carries nothing to evaluate. Animation data comes
    // from files, so this is a malformed-input path, not an invariant: return
    // the identity value for the channel instead of indexing out of bounds.
    const std::size_t component_count = get_component_count(channel.path);
    if (timestamps.empty() || (component_count == 0) || (offset + component_count > data.size())) {
        return identity_value(channel.path);
    }

    // Hold: the value of the keyframe start_position sits on.
    const auto hold = [&]() -> glm::vec4 {
        switch (channel.path) {
            case Animation_path::TRANSLATION: return vec4{data[offset], data[offset + 1], data[offset + 2], 0.0f            };
            case Animation_path::ROTATION:    return vec4{data[offset], data[offset + 1], data[offset + 2], data[offset + 3]};
            case Animation_path::SCALE:       return vec4{data[offset], data[offset + 1], data[offset + 2], 0.0f            };
            default:                          return identity_value(channel.path); // TODO WEIGHTS
        }
    };

    // Hold the keyframe value when the requested time is at or outside the
    // sampler's range, and for STEP, whose "interpolation" is exactly that
    // hold - without this case STEP would fall into the LINEAR branches below.
    if (
        (interpolation_mode == Animation_interpolation_mode::STEP) ||
        (time_current < timestamps[0]) ||
        (timestamps[channel.start_position] == time_current) ||
        (timestamps.size() == channel.start_position + 1)
    ) {
        return hold();
    }

    // Interpolating reaches into the next keyframe, up to the end of its value
    // block: offset + k (the next keyframe's matching field) + one value. A
    // file whose output accessor is shorter than its input accessor claims can
    // reach this point, so bound it before reading.
    if (offset + k + component_count > data.size()) {
        return hold();
    }

    const float t_start = timestamps[channel.start_position    ];
    const float t_next  = timestamps[channel.start_position + 1];
    const float t_d     = t_next - t_start;
    // glTF requires strictly increasing timestamps. A file that violates it
    // must not take down the process: hold the start keyframe instead.
    if (!(t_d > 0.0f)) {
        return hold();
    }
    // seek() leaves start_position on the keyframe at or before time_current,
    // and the holds above already took the out-of-range cases.
    ERHE_VERIFY(t_start <= time_current);
    ERHE_VERIFY(time_current < t_next);
    const float t = (time_current - t_start) / t_d;
    ERHE_VERIFY(t >= 0.0f);
    ERHE_VERIFY(t < 1.0f);
    const Cubic_constants cubic{t, t_d};

    switch (channel.path) {
        case Animation_path::TRANSLATION: {
            if (interpolation_mode != Animation_interpolation_mode::CUBICSPLINE) {
                vec3 start_value{data[offset    ], data[offset + 1], data[offset + 2]};
                vec3 next_value {data[offset + 3], data[offset + 4], data[offset + 5]};
                vec3 translation_value = glm::mix(start_value, next_value, t);
                return vec4{translation_value, 0.0f};
            } else {
                vec3 start_value      {data[offset    ], data[offset +  1], data[offset +  2] };
                vec3 start_out_tangent{data[offset + 3], data[offset +  4], data[offset +  5] };
                vec3 next_in_tangent  {data[offset + 6], data[offset +  7], data[offset +  8] };
                vec3 next_value       {data[offset + 9], data[offset + 10], data[offset + 11] };
                vec3 translation_value = cubic.interpolate(start_value, start_out_tangent, next_in_tangent, next_value);
                return vec4{translation_value, 0.0f};
            }
            break;
        }
        case Animation_path::ROTATION: {
            if (interpolation_mode != Animation_interpolation_mode::CUBICSPLINE) {
                // Normalize the endpoints: rotation output accessors may be
                // normalized byte / short, which round-trips to a not-quite
                // unit quaternion, and slerp of non-unit inputs is not a
                // rotation.
                quat start_value{data[offset + 3], data[offset + 0], data[offset +  1], data[offset + 2]};
                quat next_value {data[offset + 7], data[offset + 4], data[offset +  5], data[offset + 6]};
                quat rotation_value = glm::slerp(glm::normalize(start_value), glm::normalize(next_value), t);
                return vec4{rotation_value.x, rotation_value.y, rotation_value.z, rotation_value.w};
            } else {
                quat start_value      {data[offset +  3], data[offset +  0], data[offset +  1], data[offset +  2]};
                quat start_out_tangent{data[offset +  7], data[offset +  4], data[offset +  5], data[offset +  6]};
                quat next_in_tangent  {data[offset + 11], data[offset +  8], data[offset +  9], data[offset + 10]};
                quat next_value       {data[offset + 15], data[offset + 12], data[offset + 13], data[offset + 14]};
                // The cubic spline is evaluated component-wise, so the result
                // is not a unit quaternion - glTF requires normalizing it.
                quat rotation_value = glm::normalize(
                    cubic.interpolate(start_value, start_out_tangent, next_in_tangent, next_value)
                );
                return vec4{rotation_value.x, rotation_value.y, rotation_value.z, rotation_value.w};
            }
            break;
        }

        case Animation_path::SCALE: {
            if (interpolation_mode != Animation_interpolation_mode::CUBICSPLINE) {
                vec3 start_value{data[offset    ], data[offset + 1], data[offset + 2]};
                vec3 next_value {data[offset + 3], data[offset + 4], data[offset + 5]};
                vec3 scale_value = glm::mix(start_value, next_value, t);
                return vec4{scale_value, 0.0f};
            } else {
                vec3 start_value      {data[offset    ], data[offset +  1], data[offset +  2] };
                vec3 start_out_tangent{data[offset + 3], data[offset +  4], data[offset +  5] };
                vec3 next_in_tangent  {data[offset + 6], data[offset +  7], data[offset +  8] };
                vec3 next_value       {data[offset + 9], data[offset + 10], data[offset + 11] };
                vec3 scale_value = cubic.interpolate(start_value, start_out_tangent, next_in_tangent, next_value);
                return vec4{scale_value, 0.0f};
            }
            break;
        }

        default: { // TODO
        }
    }

    return vec4{0.0f, 0.0f, 0.0f, 0.0f};
}

void Animation_sampler::apply(Animation_channel& channel, const float time_current) const
{
    seek(channel, time_current);

    // The component write goes straight into the node data: a channel carries only
    // one of translation / rotation / scale, and several channels can target the
    // same node, so notifying per channel would be redundant. Animation::apply()
    // does the world-transform update and the notification, once per target node.
    Trs_transform& target = channel.target->node_data.transforms.parent_from_node;

    const glm::vec4 value = evaluate(channel, time_current);

    switch (channel.path) {
        case Animation_path::TRANSLATION: {
            target.set_translation(glm::vec3{value});
            break;
        }
        case Animation_path::ROTATION: {
            target.set_rotation(glm::quat{value.w, value.x, value.y, value.z});
            break;
        }

        case Animation_path::SCALE: {
            target.set_scale(glm::vec3{value});
            break;
        }

        default: {
            // TODO
            break;
        }
    }
}

//
//

Animation::Animation(const Animation&)            = default;
Animation& Animation::operator=(const Animation&) = default;
Animation::~Animation() noexcept                  = default;

Animation::Animation(const std::string_view name)
    : Item<Item_base, Item_base, Animation>{name}
{
}

// A sampler with no keyframes contributes no time range (front() / back() on
// it would be undefined behavior). An animation whose samplers are all empty
// keeps the identity range these start from.
auto Animation::get_first_time() const -> float
{
    float first_time = std::numeric_limits<float>::max();
    for (auto& channel : channels) {
        const Animation_sampler& sampler = samplers.at(channel.sampler_index);
        if (sampler.timestamps.empty()) {
            continue;
        }
        first_time = std::min(first_time, sampler.timestamps.front());
    }
    return first_time;
}

auto Animation::get_last_time() const -> float
{
    float last_time = std::numeric_limits<float>::lowest();
    for (auto& channel : channels) {
        const Animation_sampler& sampler = samplers.at(channel.sampler_index);
        if (sampler.timestamps.empty()) {
            continue;
        }
        last_time = std::max(last_time, sampler.timestamps.back());
    }
    return last_time;
}

auto Animation::evaluate(const float time_current, const std::size_t channel_index, const std::size_t component) -> float
{
    auto& channel = channels.at(channel_index);
    auto& sampler = samplers.at(channel.sampler_index);
    const glm::vec4 value = sampler.evaluate(channel, time_current);
    return value[static_cast<glm::vec4::length_type>(component)];
}

void Animation::apply(float time_current)
{
    // Animation_sampler::apply() writes the sampled component directly into the
    // target's parent_from_node, bypassing the Node transform setters. Collect the
    // touched nodes so that each one gets exactly one world-transform update and
    // one handle_transform_update() after all of its channels have been applied.
    // Without that notification the attachments never see the new pose and the
    // node is never marked dirty, so Scene::update_node_transforms() - dirty-list
    // driven since 1d2375d6a - has nothing to propagate and the viewport keeps
    // rendering the old pose.
    m_applied_nodes.clear();
    for (auto& channel : channels) {
        // A channel can lose its target node (the editor resets targets
        // pointing into a closing scene); the sampler data stays, the
        // channel just no longer applies anywhere.
        if (!channel.target) {
            continue;
        }
        auto& sampler = samplers.at(channel.sampler_index);
        sampler.apply(channel, time_current);
        m_applied_nodes.push_back(channel.target.get());
    }

    std::sort(m_applied_nodes.begin(), m_applied_nodes.end());
    m_applied_nodes.erase(std::unique(m_applied_nodes.begin(), m_applied_nodes.end()), m_applied_nodes.end());

    // One serial for the whole pose: all of these nodes moved at the same time.
    // A node whose animated parent is updated after it briefly holds a world
    // transform computed from the parent's previous pose; the scene's next
    // update_node_transforms() pass walks dirty nodes ancestors-first and
    // recomputes those descendants from the final parent transform.
    const uint64_t serial = Node_transforms::get_next_serial();
    for (Node* node : m_applied_nodes) {
        node->update_world_from_node();
        node->handle_transform_update(serial);
    }
}

} // namespace erhe::scene
