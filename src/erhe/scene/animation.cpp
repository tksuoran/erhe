#include "erhe/scene/animation.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::scene
{

[[nodiscard]] auto get_component_count(const Animation_path path) -> std::size_t
{
    switch (path) {
        case Animation_path::TRANSLATION: return 3; // T_x, T_y, T_z
        case Animation_path::ROTATION:    return 4; // Q_x, Q_y, Q_z, Q_w
        case Animation_path::SCALE:       return 3; // S_x, S_y, S_z
        default:                          return 0; // TODO
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

[[nodiscard]] auto c_str(const Animation_path path) -> const char*
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

[[nodiscard]] auto c_str(const Animation_interpolation_mode interpolation_mode) -> const char*
{
    switch (interpolation_mode) {
        case Animation_interpolation_mode::STEP:        return "Step";
        case Animation_interpolation_mode::LINEAR:      return "Linear";
        case Animation_interpolation_mode::CUBICSPLINE: return "Cubic Spline";
        default:                                        return "?";
    }
}

Animation_sampler::Animation_sampler(
    const Animation_interpolation_mode interpolation_mode
)
    : interpolation_mode{interpolation_mode}
{
}

Animation_sampler::~Animation_sampler() noexcept = default;

void Animation_sampler::set(
    std::vector<float>&& timestamps_in,
    std::vector<float>&& values_in
)
{
    timestamps = std::move(timestamps_in);
    data       = std::move(values_in);
}

class Cubic_constants
{
public:
    Cubic_constants(const float t)
    {
        const float t2 = t * t;
        const float t3 = t2 * t;
#if 1
        s0 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        s1 = t3 - 2.0f * t2 + t;
        s2 = -2.0f * t3 + 3.0f * t2;
        s3 = t3 - t2;
#else
        // Filament
        s2 = -2.0f * t3 + 3.0f * t2;
        s3 = t3 - t2;
        s0 = 1.0f - s2;
        s1 = s3 - t2 + t;
#endif
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

auto Animation_sampler::evaluate(
    Animation_channel& channel,
    const float        time_current
) const -> glm::vec4
{
    seek(channel, time_current);

    using namespace glm;

    const std::size_t k      = get_component_count(channel.path) * get_key_value_count(interpolation_mode);
    const std::size_t offset = channel.start_position * k + channel.value_offset;

    if (
        (time_current < timestamps[0]) ||
        (timestamps[channel.start_position] == time_current) ||
        (timestamps.size() == channel.start_position + 1)
    ) {
        switch (channel.path) {
            case Animation_path::TRANSLATION: return vec4{data[offset], data[offset + 1], data[offset + 2], 0.0f            };
            case Animation_path::ROTATION:    return vec4{data[offset], data[offset + 1], data[offset + 2], data[offset + 3]};
            case Animation_path::SCALE:       return vec4{data[offset], data[offset + 1], data[offset + 2], 0.0f            };
            default:                          return vec4{0.0f, 0.0f, 0.0f, 0.0f}; // TODO
        }
    }

    const float t_start = timestamps[channel.start_position    ];
    const float t_next  = timestamps[channel.start_position + 1];
    ERHE_VERIFY(t_start < t_next);
    ERHE_VERIFY(t_start <= time_current);
    ERHE_VERIFY(time_current < t_next);
    const float t_d     = t_next - t_start;
    ERHE_VERIFY(t_d > 0.0f);
    const float t       = (time_current - t_start) / t_d;
    ERHE_VERIFY(t >= 0.0f);
    ERHE_VERIFY(t < 1.0f);
    const Cubic_constants cubic{t};

    switch (channel.path) {
        case Animation_path::TRANSLATION:
        {
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
                quat start_value{data[offset + 3], data[offset + 0], data[offset +  1], data[offset + 2]};
                quat next_value {data[offset + 7], data[offset + 4], data[offset +  5], data[offset + 6]};
                quat rotation_value = glm::slerp(start_value, next_value, t);
                return vec4{rotation_value.x, rotation_value.y, rotation_value.z, rotation_value.w};
            } else {
                quat start_value      {data[offset +  3], data[offset +  0], data[offset +  1], data[offset +  2]};
                quat start_out_tangent{data[offset +  7], data[offset +  4], data[offset +  5], data[offset +  6]};
                quat next_in_tangent  {data[offset + 11], data[offset +  8], data[offset +  9], data[offset + 10]};
                quat next_value       {data[offset + 15], data[offset + 12], data[offset + 13], data[offset + 14]};
                quat rotation_value = cubic.interpolate(start_value, start_out_tangent, next_in_tangent, next_value);
                quat rotation_value_normalized = glm::normalize(rotation_value);
                return vec4{rotation_value.x, rotation_value.y, rotation_value.z, rotation_value.w};
            }
            break;
        }

        case Animation_path::SCALE:
        {
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

        default: // TODO
        {
        }
    }

    return vec4{0.0f, 0.0f, 0.0f, 0.0f};
}

void Animation_sampler::apply(
    Animation_channel& channel,
    const float        time_current
) const
{
    seek(channel, time_current);

    Trs_transform& target = channel.target->node_data.transforms.parent_from_node;

    const glm::vec4 value = evaluate(channel, time_current);

    switch (channel.path) {
        case Animation_path::TRANSLATION:
        {
            target.set_translation(glm::vec3{value});
            break;
        }
        case Animation_path::ROTATION: {
            target.set_rotation(glm::quat{value.w, value.x, value.y, value.z});
            break;
        }

        case Animation_path::SCALE:
        {
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

Animation::Animation(const std::string_view name)
    : Item{name}
{
}

Animation::~Animation() noexcept = default;

auto Animation::static_type() -> uint64_t
{
    return Item_type::animation;
}

auto Animation::static_type_name() -> const char*
{
    return "Animation";
}

auto Animation::get_type() const -> uint64_t
{
    return static_type();
}

auto Animation::type_name() const -> const char*
{
    return static_type_name();
}

auto Animation::evaluate(
    const float       time_current,
    const std::size_t channel_index,
    const std::size_t component
) -> float
{
    auto& channel = channels.at(channel_index);
    auto& sampler = samplers.at(channel.sampler_index);
    const glm::vec4 value = sampler.evaluate(channel, time_current);
    return value[static_cast<glm::vec4::length_type>(component)];
}

void Animation::apply(float time_current)
{
    for (auto& channel : channels) {
        auto& sampler = samplers.at(channel.sampler_index);
        sampler.apply(channel, time_current);
    }
}


} // namespace erhe::scene
