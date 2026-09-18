#pragma once

#include "erhe_scene/node.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_property/dependency_property.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace erhe::scene {

class Xformable; using Node = Xformable;

// Classification of a channel by the property it drives (get_animation_path):
// the three local transform components a glTF animation and a USD xformOp
// have a carrier for. A channel driving any other property classifies as
// INVALID; the channel itself is named by its property, not by this enum.
enum class Animation_path : int {
    INVALID     = 0,
    TRANSLATION = 1,
    ROTATION    = 2,
    SCALE       = 3
};

[[nodiscard]] auto c_str(Animation_path path) -> const char*;

enum class Animation_interpolation_mode : int {
    INVALID     = 0,
    STEP        = 1,
    LINEAR      = 2,
    CUBICSPLINE = 3
};

[[nodiscard]] auto c_str(Animation_interpolation_mode interpolation_mode) -> const char*;

[[nodiscard]] auto get_component_count(Animation_path path) -> std::size_t;

class Animation_channel;

class Animation_sampler
{
public:
    Animation_sampler();
    explicit Animation_sampler(Animation_interpolation_mode interpolation_mode);
    ~Animation_sampler() noexcept;

    void set(std::vector<float>&& timestamps_in, std::vector<float>&& values_in);

    // The sampled value of the channel's property, packed into the leading
    // components of a vec4: a quaternion as (x, y, z, w), anything else in
    // its own component order with the unused components left at zero.
    [[nodiscard]] auto evaluate(Animation_channel& channel, float time_current) const -> glm::vec4;

    void apply(Animation_channel& channel, float time_current) const;
    void seek (Animation_channel& channel, float time_current) const;

    Animation_interpolation_mode interpolation_mode{Animation_interpolation_mode::LINEAR};
    std::vector<float>           timestamps;
    std::vector<float>           data;
};

// One driven property of one target object. The property is a registered
// property of the target (doc/property_system.md): the transform components
// of an Xformable are the common case, and any other property whose type the
// sampler packing covers (is_animatable) is driven the same way. Playback
// writes the animated layer (D5), so the authored value under it is what a
// save writes.
class Animation_channel
{
public:
    const erhe::property::Dependency_property* property      {nullptr};
    std::size_t                                sampler_index {0};  // index in Animation
    std::shared_ptr<erhe::Item_base>           target        {};
    std::size_t                                start_position{0};  // in sampler keyframes
    std::size_t                                value_offset  {0};  // in sampler data floats
    // Set once the sampler has refused to write this channel, so a channel
    // naming a property that carries no animatable value warns once rather
    // than every frame.
    bool                                       refusal_logged{false};
};

// Components a value of this property occupies in a sampler's data: 1 for a
// scalar, 2 / 3 / 4 for the vectors and 4 for a quaternion. Zero for a type
// no sampler can carry (string, object reference, asset path, matrix, array),
// which is also what is_animatable() answers.
[[nodiscard]] auto get_component_count(const erhe::property::Dependency_property& property) -> std::size_t;
[[nodiscard]] auto get_component_count(const Animation_channel& channel) -> std::size_t;
[[nodiscard]] auto is_animatable      (const erhe::property::Dependency_property& property) -> bool;

// The local transform property a path names, and the classification back: a
// channel driving translation_property / rotation_property / scale_property
// is TRANSLATION / ROTATION / SCALE, and every other channel is INVALID.
[[nodiscard]] auto get_transform_property(Animation_path path) -> const erhe::property::Dependency_property*;
[[nodiscard]] auto get_animation_path    (const Animation_channel& channel) -> Animation_path;

// The channel's target when it is an Xformable, else an empty pointer.
[[nodiscard]] auto get_target_node(const Animation_channel& channel) -> std::shared_ptr<Xformable>;

// A channel driving one local transform component of a prim: what a glTF
// animation channel and a sampled USD xformOp import as.
[[nodiscard]] auto make_transform_channel(
    std::shared_ptr<Xformable> target,
    Animation_path             path,
    std::size_t                sampler_index,
    std::size_t                value_offset = 0
) -> Animation_channel;

class Animation : public Item<Item_base, erhe::Typed, Animation>
{
public:
    explicit Animation(const Animation& src);
    Animation& operator=(const Animation& src);
    ~Animation() noexcept override;

    explicit Animation(std::string_view name);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Animation"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Typed::get_static_type() | Item_type::animation; }

    // Overrides erhe::Typed: the class fixes the token. USD has no prim type
    // for this kind, so the token is the erhe class name, written as a custom
    // typeName (doc/usd_compatibility.md).
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Animation"; }

    // Read-only computed properties (doc/property_system.md D26, section
    // 4.16) over the samplers and channels: the keyed time range and the
    // counts. A writer of `samplers` / `channels` / a sampler's timestamps
    // calls notify_keyframes_changed() so an expression reading them
    // re-evaluates; the values themselves are always current on read.
    static const erhe::property::Property<float> first_time_property;
    static const erhe::property::Property<float> last_time_property;
    static const erhe::property::Property<int>   sampler_count_property;
    static const erhe::property::Property<int>   channel_count_property;
    void notify_keyframes_changed();

    // Public API
    [[nodiscard]] auto evaluate      (float time_current, std::size_t channel_index, std::size_t component) -> float;
    [[nodiscard]] auto get_first_time() const -> float;
    [[nodiscard]] auto get_last_time () const -> float;

    // Puts every channel target into the sampled pose at `time_current` by
    // writing the animated layer (doc/property_system.md D5) of the driven
    // property. The pose is not authored state: the value the target authored
    // stays readable as the base under it, a save writes that base, and
    // clear_applied() puts the target back on it.
    void apply(float time_current);

    // Drops the animated layer of every channel, so each target holds the
    // value it authored again. The player calls it when playback stops and
    // when it lets go of the animation.
    void clear_applied();

    std::vector<Animation_sampler> samplers;
    std::vector<Animation_channel> channels;

private:
    // Distinct Xformable channel targets of the last apply(). A member only to
    // keep its capacity across frames; carries no state between calls.
    std::vector<Node*> m_applied_nodes;
};

} // namespace erhe::scene
