#include "erhe_scene/animation.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>

namespace erhe::scene {

auto get_component_count(const Animation_path path) -> std::size_t
{
    switch (path) {
        case Animation_path::TRANSLATION: return 3; // T_x, T_y, T_z
        case Animation_path::ROTATION:    return 4; // Q_x, Q_y, Q_z, Q_w
        case Animation_path::SCALE:       return 3; // S_x, S_y, S_z
        default:                          return 0;
    }
}

auto get_component_count(const erhe::property::Dependency_property& property) -> std::size_t
{
    switch (property.get_type()) {
        case erhe::property::Property_type::boolean:         return 1;
        case erhe::property::Property_type::integer:         return 1;
        case erhe::property::Property_type::floating:        return 1;
        case erhe::property::Property_type::double_floating: return 1;
        case erhe::property::Property_type::enumeration:     return 1;
        case erhe::property::Property_type::vec2:            return 2;
        case erhe::property::Property_type::ivec2:           return 2;
        case erhe::property::Property_type::vec3:            return 3;
        case erhe::property::Property_type::ivec3:           return 3;
        case erhe::property::Property_type::vec4:            return 4;
        case erhe::property::Property_type::ivec4:           return 4;
        case erhe::property::Property_type::quat:            return 4;
        default:                                             return 0;
    }
}

auto get_component_count(const Animation_channel& channel) -> std::size_t
{
    return (channel.property != nullptr) ? get_component_count(*channel.property) : 0;
}

auto is_animatable(const erhe::property::Dependency_property& property) -> bool
{
    return get_component_count(property) > 0;
}

auto get_transform_property(const Animation_path path) -> const erhe::property::Dependency_property*
{
    switch (path) {
        case Animation_path::TRANSLATION: return Xformable::translation_property.get_ptr();
        case Animation_path::ROTATION:    return Xformable::rotation_property.get_ptr();
        case Animation_path::SCALE:       return Xformable::scale_property.get_ptr();
        default:                          return nullptr;
    }
}

auto get_animation_path(const Animation_channel& channel) -> Animation_path
{
    if (channel.property == nullptr) {
        return Animation_path::INVALID;
    }
    if (channel.property == Xformable::translation_property.get_ptr()) {
        return Animation_path::TRANSLATION;
    }
    if (channel.property == Xformable::rotation_property.get_ptr()) {
        return Animation_path::ROTATION;
    }
    if (channel.property == Xformable::scale_property.get_ptr()) {
        return Animation_path::SCALE;
    }
    return Animation_path::INVALID;
}

auto get_target_node(const Animation_channel& channel) -> std::shared_ptr<Xformable>
{
    if (!erhe::is<Xformable>(channel.target)) {
        return {};
    }
    return std::static_pointer_cast<Xformable>(channel.target);
}

auto make_transform_channel(
    std::shared_ptr<Xformable> target,
    const Animation_path       path,
    const std::size_t          sampler_index,
    const std::size_t          value_offset
) -> Animation_channel
{
    return Animation_channel{
        .property       = get_transform_property(path),
        .sampler_index  = sampler_index,
        .target         = std::move(target),
        .start_position = 0,
        .value_offset   = value_offset
    };
}

// Whether the channel's value is held from key to key rather than
// interpolated: a boolean, an integer and an enumeration have no value
// between two keys, so they take STEP semantics whatever the sampler says.
[[nodiscard]] static auto is_step_valued(const erhe::property::Dependency_property& property) -> bool
{
    switch (property.get_type()) {
        case erhe::property::Property_type::boolean:
        case erhe::property::Property_type::integer:
        case erhe::property::Property_type::enumeration:
        case erhe::property::Property_type::ivec2:
        case erhe::property::Property_type::ivec3:
        case erhe::property::Property_type::ivec4:
            return true;
        default:
            return false;
    }
}

// A property value in the packing Animation_sampler::evaluate() returns:
// a quaternion as (x, y, z, w), anything else in its own component order.
[[nodiscard]] static auto pack_value(const erhe::property::Property_value& value) -> glm::vec4
{
    using namespace erhe::property;
    switch (type_of(value)) {
        case Property_type::boolean:         return glm::vec4{std::get<bool>(value) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
        case Property_type::integer:         return glm::vec4{static_cast<float>(std::get<int>(value)), 0.0f, 0.0f, 0.0f};
        case Property_type::floating:        return glm::vec4{std::get<float>(value), 0.0f, 0.0f, 0.0f};
        case Property_type::double_floating: return glm::vec4{static_cast<float>(std::get<double>(value)), 0.0f, 0.0f, 0.0f};
        case Property_type::enumeration:     return glm::vec4{static_cast<float>(std::get<Enum_value>(value).value), 0.0f, 0.0f, 0.0f};
        case Property_type::vec2:            return glm::vec4{std::get<glm::vec2>(value), 0.0f, 0.0f};
        case Property_type::vec3:            return glm::vec4{std::get<glm::vec3>(value), 0.0f};
        case Property_type::vec4:            return std::get<glm::vec4>(value);
        case Property_type::ivec2:           return glm::vec4{glm::vec2{std::get<glm::ivec2>(value)}, 0.0f, 0.0f};
        case Property_type::ivec3:           return glm::vec4{glm::vec3{std::get<glm::ivec3>(value)}, 0.0f};
        case Property_type::ivec4:           return glm::vec4{std::get<glm::ivec4>(value)};
        case Property_type::quat: {
            const glm::quat q = std::get<glm::quat>(value);
            return glm::vec4{q.x, q.y, q.z, q.w};
        }
        default: return glm::vec4{0.0f};
    }
}

// The inverse of pack_value(): the property value a sampled vec4 carries.
// A quaternion is normalized - a sampled spline and a normalized-integer
// accessor both come back not quite unit long.
[[nodiscard]] static auto unpack_value(
    const erhe::property::Property_type type,
    const glm::vec4                     value
) -> erhe::property::Property_value
{
    using namespace erhe::property;
    switch (type) {
        case Property_type::boolean:         return value.x != 0.0f;
        case Property_type::integer:         return static_cast<int>(value.x);
        case Property_type::floating:        return value.x;
        case Property_type::double_floating: return static_cast<double>(value.x);
        case Property_type::enumeration:     return Enum_value{static_cast<int32_t>(value.x)};
        case Property_type::vec2:            return glm::vec2{value};
        case Property_type::vec3:            return glm::vec3{value};
        case Property_type::vec4:            return value;
        case Property_type::ivec2:           return glm::ivec2{value.x, value.y};
        case Property_type::ivec3:           return glm::ivec3{value.x, value.y, value.z};
        case Property_type::ivec4:           return glm::ivec4{value.x, value.y, value.z, value.w};
        case Property_type::quat:            return glm::normalize(glm::quat{value.w, value.x, value.y, value.z});
        default:                             return zero_value(type);
    }
}

// The value that leaves the target untouched, in the packing
// Animation_sampler::evaluate() returns: the driven property's default (zero
// translation, identity rotation, unit scale, ...). Used when a sampler
// carries nothing usable for the channel, so that malformed animation data
// neither reads out of bounds nor collapses the target to a zero value.
[[nodiscard]] static auto identity_value(const Animation_channel& channel) -> glm::vec4
{
    if (channel.property == nullptr) {
        return glm::vec4{0.0f};
    }
    const erhe::property::Property_metadata& metadata =
        (channel.target != nullptr)
            ? channel.property->get_metadata(channel.target->get_property_owner_type())
            : channel.property->get_default_metadata();
    if (!metadata.default_value.has_value()) {
        return pack_value(erhe::property::zero_value(channel.property->get_type()));
    }
    return pack_value(metadata.default_value.value());
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
        const glm::vec4 start_value,
        const glm::vec4 start_tangent_out,
        const glm::vec4 end_tangent_in,
        const glm::vec4 end_value
    ) const -> glm::vec4
    {
        return
            s0 * start_value       +
            s1 * start_tangent_out +
            s2 * end_value         +
            s3 * end_tangent_in;
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

    // A sampler with no keyframes (or with too little data for the keyframe
    // seek() settled on) carries nothing to evaluate. Animation data comes
    // from files, so this is a malformed-input path, not an invariant: return
    // the identity value for the channel instead of indexing out of bounds.
    const std::size_t component_count = get_component_count(channel);
    const std::size_t k               = component_count * get_key_value_count(interpolation_mode);
    const std::size_t offset          = channel.start_position * k + channel.value_offset;
    if (timestamps.empty() || (component_count == 0) || (offset + component_count > data.size())) {
        return identity_value(channel);
    }

    // The channel's components as the sampler stores them, from `base`.
    const auto read = [this, component_count](const std::size_t base) -> glm::vec4 {
        glm::vec4 value{0.0f};
        for (std::size_t component = 0; component < component_count; ++component) {
            value[static_cast<glm::vec4::length_type>(component)] = data[base + component];
        }
        return value;
    };

    // Hold the keyframe value when the requested time is at or outside the
    // sampler's range, for STEP, whose "interpolation" is exactly that hold,
    // and for a property whose values have nothing in between them.
    if (
        (interpolation_mode == Animation_interpolation_mode::STEP) ||
        is_step_valued(*channel.property) ||
        (time_current < timestamps[0]) ||
        (timestamps[channel.start_position] == time_current) ||
        (timestamps.size() == channel.start_position + 1)
    ) {
        return read(offset);
    }

    // Interpolating reaches into the next keyframe, up to the end of its value
    // block: offset + k (the next keyframe's matching field) + one value. A
    // file whose output accessor is shorter than its input accessor claims can
    // reach this point, so bound it before reading.
    if (offset + k + component_count > data.size()) {
        return read(offset);
    }

    const float t_start = timestamps[channel.start_position    ];
    const float t_next  = timestamps[channel.start_position + 1];
    const float t_d     = t_next - t_start;
    // glTF requires strictly increasing timestamps. A file that violates it
    // must not take down the process: hold the start keyframe instead.
    if (!(t_d > 0.0f)) {
        return read(offset);
    }
    // seek() leaves start_position on the keyframe at or before time_current,
    // and the holds above already took the out-of-range cases.
    ERHE_VERIFY(t_start <= time_current);
    ERHE_VERIFY(time_current < t_next);
    const float t = (time_current - t_start) / t_d;
    ERHE_VERIFY(t >= 0.0f);
    ERHE_VERIFY(t < 1.0f);
    const Cubic_constants cubic{t, t_d};

    const bool is_rotation = (channel.property->get_type() == erhe::property::Property_type::quat);
    const auto as_quat = [](const glm::vec4 value) -> glm::quat {
        return glm::quat{value.w, value.x, value.y, value.z};
    };

    if (interpolation_mode != Animation_interpolation_mode::CUBICSPLINE) {
        const glm::vec4 start_value = read(offset);
        const glm::vec4 next_value  = read(offset + k);
        if (is_rotation) {
            // Normalize the endpoints: rotation output accessors may be
            // normalized byte / short, which round-trips to a not-quite
            // unit quaternion, and slerp of non-unit inputs is not a
            // rotation.
            const glm::quat rotation_value = glm::slerp(
                glm::normalize(as_quat(start_value)),
                glm::normalize(as_quat(next_value)),
                t
            );
            return vec4{rotation_value.x, rotation_value.y, rotation_value.z, rotation_value.w};
        }
        return glm::mix(start_value, next_value, t);
    }

    const glm::vec4 start_value       = read(offset);
    const glm::vec4 start_out_tangent = read(offset + component_count);
    const glm::vec4 next_in_tangent   = read(offset + (2 * component_count));
    const glm::vec4 next_value        = read(offset + (3 * component_count));
    if (is_rotation) {
        // The cubic spline is evaluated component-wise, so the result is not
        // a unit quaternion - glTF requires normalizing it.
        const glm::quat rotation_value = glm::normalize(
            cubic.interpolate(
                as_quat(start_value),
                as_quat(start_out_tangent),
                as_quat(next_in_tangent),
                as_quat(next_value)
            )
        );
        return vec4{rotation_value.x, rotation_value.y, rotation_value.z, rotation_value.w};
    }
    return cubic.interpolate(start_value, start_out_tangent, next_in_tangent, next_value);
}

void Animation_sampler::apply(Animation_channel& channel, const float time_current) const
{
    seek(channel, time_current);

    // The sampled value goes into the target's animated layer
    // (doc/property_system.md D5): the value the prim authored is the base
    // under it, so a save writes the authored state whatever the playhead
    // says and stopping the animation puts the prim back on it. The write
    // notifies the property readers but not the scene - a transform channel
    // carries only one of translation / rotation / scale, and several
    // channels can target the same prim, so Animation::apply() does the
    // world-transform update and the scene notification once per target.
    if (!channel.target || (channel.property == nullptr)) {
        return;
    }
    const erhe::property::Dependency_property& property = *channel.property;
    if (!is_animatable(property)) {
        if (!channel.refusal_logged) {
            channel.refusal_logged = true;
            log->warn(
                "animation channel on property '{}' of '{}': type {} carries no animatable value - the channel does nothing",
                property.get_name(), channel.target->get_name(), erhe::property::c_str(property.get_type())
            );
        }
        return;
    }

    const glm::vec4 value = evaluate(channel, time_current);
    channel.target->set_animated_value(property, unpack_value(property.get_type(), value));
}

//
//

Animation::Animation(const Animation&)            = default;
Animation& Animation::operator=(const Animation&) = default;
Animation::~Animation() noexcept                  = default;

namespace {

constexpr std::string_view c_animation_group = "Animation";

} // anonymous namespace

const erhe::property::Property<float> Animation::first_time_property = erhe::property::Property<float>::register_computed(
    "first_time", Animation::property_owner_type(),
    [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value { return static_cast<const Animation&>(object).get_first_time(); },
    erhe::property::Property_metadata{.flags = erhe::property::Property_flags::none, .ui = erhe::property::Property_ui{.group = c_animation_group, .tooltip = "Earliest keyframe time over the channels, in seconds", .label = "Start Time"}}
);
const erhe::property::Property<float> Animation::last_time_property = erhe::property::Property<float>::register_computed(
    "last_time", Animation::property_owner_type(),
    [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value { return static_cast<const Animation&>(object).get_last_time(); },
    erhe::property::Property_metadata{.flags = erhe::property::Property_flags::none, .ui = erhe::property::Property_ui{.group = c_animation_group, .tooltip = "Latest keyframe time over the channels, in seconds", .label = "End Time"}}
);
const erhe::property::Property<int> Animation::sampler_count_property = erhe::property::Property<int>::register_computed(
    "sampler_count", Animation::property_owner_type(),
    [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value { return static_cast<int>(static_cast<const Animation&>(object).samplers.size()); },
    erhe::property::Property_metadata{.flags = erhe::property::Property_flags::none, .ui = erhe::property::Property_ui{.group = c_animation_group, .label = "Samplers"}}
);
const erhe::property::Property<int> Animation::channel_count_property = erhe::property::Property<int>::register_computed(
    "channel_count", Animation::property_owner_type(),
    [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value { return static_cast<int>(static_cast<const Animation&>(object).channels.size()); },
    erhe::property::Property_metadata{.flags = erhe::property::Property_flags::none, .ui = erhe::property::Property_ui{.group = c_animation_group, .label = "Channels"}}
);

void Animation::notify_keyframes_changed()
{
    invalidate_dependents(first_time_property.get());
    invalidate_dependents(last_time_property.get());
    invalidate_dependents(sampler_count_property.get());
    invalidate_dependents(channel_count_property.get());
}

Animation::Animation(const std::string_view name)
    : Item{name}
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
    // Animation_sampler::apply() writes the sampled component into the target's
    // animated layer, which stores it without touching the world transform.
    // Collect the touched nodes so that each one gets exactly one
    // world-transform update and one handle_transform_update() after all of its
    // channels have been applied.
    // Without that notification the attachments never see the new pose and the
    // node is never marked dirty, so Scene::update_node_transforms() - dirty-list
    // driven since 1d2375d6a - has nothing to propagate and the viewport keeps
    // rendering the old pose.
    // A channel driving any other property of any other kind of item needs
    // nothing beyond the write: set_animated_value() notifies its readers.
    m_applied_nodes.clear();
    for (auto& channel : channels) {
        // A channel can lose its target (the editor resets targets pointing
        // into a closing scene); the sampler data stays, the channel just no
        // longer applies anywhere.
        if (!channel.target) {
            continue;
        }
        auto& sampler = samplers.at(channel.sampler_index);
        sampler.apply(channel, time_current);
        if (get_animation_path(channel) == Animation_path::INVALID) {
            continue;
        }
        Xformable* node = erhe::is<Xformable>(channel.target) ? static_cast<Xformable*>(channel.target.get()) : nullptr;
        if (node != nullptr) {
            m_applied_nodes.push_back(node);
        }
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

void Animation::clear_applied()
{
    // Transform channels clear per target, not per channel: the three
    // components of a transform come back together
    // (Xformable::clear_animated_local_transform), which also runs the
    // world-transform update and the notification once, as apply() does. A
    // channel driving any other property clears that property on its own.
    m_applied_nodes.clear();
    for (Animation_channel& channel : channels) {
        if (!channel.target) {
            continue;
        }
        Xformable* node = erhe::is<Xformable>(channel.target) ? static_cast<Xformable*>(channel.target.get()) : nullptr;
        if ((node != nullptr) && (get_animation_path(channel) != Animation_path::INVALID)) {
            m_applied_nodes.push_back(node);
            continue;
        }
        if (channel.property != nullptr) {
            channel.target->clear_animated_value(*channel.property);
        }
    }
    std::sort(m_applied_nodes.begin(), m_applied_nodes.end());
    m_applied_nodes.erase(std::unique(m_applied_nodes.begin(), m_applied_nodes.end()), m_applied_nodes.end());
    for (Node* node : m_applied_nodes) {
        node->clear_animated_local_transform();
    }
}

} // namespace erhe::scene
