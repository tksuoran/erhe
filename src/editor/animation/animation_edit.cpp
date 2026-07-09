#include "animation/animation_edit.hpp"

#include "erhe_scene/animation.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace editor {

namespace {

// Minimum separation between neighboring keyframe timestamps. Keeps
// timestamps strictly increasing (Animation_sampler::evaluate VERIFYs
// t_start < t_next).
constexpr float c_min_key_separation = 1.0e-4f;

[[nodiscard]] auto get_key_value_count(const erhe::scene::Animation_interpolation_mode interpolation_mode) -> std::size_t
{
    switch (interpolation_mode) {
        case erhe::scene::Animation_interpolation_mode::STEP:        return 1;
        case erhe::scene::Animation_interpolation_mode::LINEAR:      return 1;
        case erhe::scene::Animation_interpolation_mode::CUBICSPLINE: return 3;
        default:                                                     return 0;
    }
}

} // anonymous namespace

auto capture_sampler_state(const erhe::scene::Animation& animation, const std::size_t sampler_index) -> Animation_sampler_state
{
    const erhe::scene::Animation_sampler& sampler = animation.samplers.at(sampler_index);
    return Animation_sampler_state{
        .sampler_index = sampler_index,
        .timestamps    = sampler.timestamps,
        .data          = sampler.data
    };
}

void restore_sampler_state(erhe::scene::Animation& animation, const Animation_sampler_state& state)
{
    erhe::scene::Animation_sampler& sampler = animation.samplers.at(state.sampler_index);
    sampler.timestamps = state.timestamps;
    sampler.data       = state.data;
    reset_channel_seek_state(animation);
}

auto get_sampler_value_stride(const erhe::scene::Animation_sampler& sampler) -> std::size_t
{
    if (sampler.timestamps.empty()) {
        return 0;
    }
    return sampler.data.size() / sampler.timestamps.size();
}

auto get_key_value_index(
    const erhe::scene::Animation& animation,
    const std::size_t             channel_index,
    const std::size_t             key_index,
    const std::size_t             component
) -> std::size_t
{
    const erhe::scene::Animation_channel& channel = animation.channels.at(channel_index);
    const erhe::scene::Animation_sampler& sampler = animation.samplers.at(channel.sampler_index);
    const std::size_t component_count = erhe::scene::get_component_count(channel.path);
    const std::size_t stride          = component_count * get_key_value_count(sampler.interpolation_mode);
    return (key_index * stride) + channel.value_offset + component;
}

auto get_keyframe_value(
    const erhe::scene::Animation& animation,
    const std::size_t             channel_index,
    const std::size_t             key_index,
    const std::size_t             component
) -> float
{
    const erhe::scene::Animation_channel& channel = animation.channels.at(channel_index);
    const erhe::scene::Animation_sampler& sampler = animation.samplers.at(channel.sampler_index);
    return sampler.data.at(get_key_value_index(animation, channel_index, key_index, component));
}

void set_keyframe_value(
    erhe::scene::Animation& animation,
    const std::size_t       channel_index,
    const std::size_t       key_index,
    const std::size_t       component,
    const float             value
)
{
    const erhe::scene::Animation_channel& channel = animation.channels.at(channel_index);
    erhe::scene::Animation_sampler&       sampler = animation.samplers.at(channel.sampler_index);
    sampler.data.at(get_key_value_index(animation, channel_index, key_index, component)) = value;
}

auto set_keyframe_time(
    erhe::scene::Animation& animation,
    const std::size_t       sampler_index,
    const std::size_t       key_index,
    const float             time
) -> float
{
    erhe::scene::Animation_sampler& sampler = animation.samplers.at(sampler_index);
    ERHE_VERIFY(key_index < sampler.timestamps.size());

    const float low = (key_index > 0)
        ? sampler.timestamps[key_index - 1] + c_min_key_separation
        : std::numeric_limits<float>::lowest();
    const float high = (key_index + 1 < sampler.timestamps.size())
        ? sampler.timestamps[key_index + 1] - c_min_key_separation
        : std::numeric_limits<float>::max();

    const float applied = (low <= high) ? std::clamp(time, low, high) : sampler.timestamps[key_index];
    sampler.timestamps[key_index] = applied;
    reset_channel_seek_state(animation);
    return applied;
}

auto insert_keyframe(
    erhe::scene::Animation& animation,
    const std::size_t       sampler_index,
    const float             time
) -> std::size_t
{
    erhe::scene::Animation_sampler& sampler = animation.samplers.at(sampler_index);
    ERHE_VERIFY(!sampler.timestamps.empty());

    // Existing key at (or extremely near) the requested time: no insert.
    for (std::size_t i = 0, end = sampler.timestamps.size(); i < end; ++i) {
        if (std::abs(sampler.timestamps[i] - time) < c_min_key_separation) {
            return i;
        }
    }

    const std::size_t stride = get_sampler_value_stride(sampler);
    ERHE_VERIFY(stride > 0);

    const auto        insert_pos_it = std::upper_bound(sampler.timestamps.begin(), sampler.timestamps.end(), time);
    const std::size_t insert_pos    = static_cast<std::size_t>(std::distance(sampler.timestamps.begin(), insert_pos_it));

    // Evaluate every channel that reads this sampler at the insert time
    // BEFORE mutating the storage, and build the new key block.
    std::vector<float> key_block(stride, 0.0f);
    const bool cubic = sampler.interpolation_mode == erhe::scene::Animation_interpolation_mode::CUBICSPLINE;
    for (erhe::scene::Animation_channel& channel : animation.channels) {
        if (channel.sampler_index != sampler_index) {
            continue;
        }
        const std::size_t component_count = erhe::scene::get_component_count(channel.path);
        channel.start_position = 0;
        const glm::vec4 value = sampler.evaluate(channel, time);
        for (std::size_t c = 0; c < component_count; ++c) {
            key_block.at(channel.value_offset + c) = value[static_cast<glm::vec4::length_type>(c)];
        }
        if (cubic) {
            // Estimate tangents from the local derivative so the inserted key
            // approximately preserves the curve shape. glTF cubic spline
            // tangents are expressed in value units per second.
            const float first = sampler.timestamps.front();
            const float last  = sampler.timestamps.back();
            const float h     = std::max(c_min_key_separation, (last - first) * 1.0e-3f);
            const float t0    = std::max(first, time - h);
            const float t1    = std::min(last,  time + h);
            channel.start_position = 0;
            const glm::vec4 v0 = sampler.evaluate(channel, t0);
            channel.start_position = 0;
            const glm::vec4 v1 = sampler.evaluate(channel, t1);
            const float dt = t1 - t0;
            for (std::size_t c = 0; c < component_count; ++c) {
                const auto lc = static_cast<glm::vec4::length_type>(c);
                const float derivative = (dt > 0.0f) ? ((v1[lc] - v0[lc]) / dt) : 0.0f;
                // In-tangent block precedes the value, out-tangent follows it;
                // value_offset == component_count for CUBICSPLINE.
                key_block.at(c)                             = derivative; // in tangent
                key_block.at((2 * component_count) + c)     = derivative; // out tangent
            }
        }
    }

    sampler.timestamps.insert(sampler.timestamps.begin() + static_cast<std::ptrdiff_t>(insert_pos), time);
    sampler.data.insert(
        sampler.data.begin() + static_cast<std::ptrdiff_t>(insert_pos * stride),
        key_block.begin(),
        key_block.end()
    );
    reset_channel_seek_state(animation);
    return insert_pos;
}

auto delete_keyframe(
    erhe::scene::Animation& animation,
    const std::size_t       sampler_index,
    const std::size_t       key_index
) -> bool
{
    erhe::scene::Animation_sampler& sampler = animation.samplers.at(sampler_index);
    if (sampler.timestamps.size() <= 1) {
        return false;
    }
    ERHE_VERIFY(key_index < sampler.timestamps.size());
    const std::size_t stride = get_sampler_value_stride(sampler);

    sampler.timestamps.erase(sampler.timestamps.begin() + static_cast<std::ptrdiff_t>(key_index));
    sampler.data.erase(
        sampler.data.begin() + static_cast<std::ptrdiff_t>(key_index * stride),
        sampler.data.begin() + static_cast<std::ptrdiff_t>((key_index + 1) * stride)
    );
    reset_channel_seek_state(animation);
    return true;
}

void reset_channel_seek_state(erhe::scene::Animation& animation)
{
    for (erhe::scene::Animation_channel& channel : animation.channels) {
        channel.start_position = 0;
    }
}

}
