#pragma once

#include "erhe_scene/node.hpp"

#include <glm/glm.hpp>

#include <string>

namespace erhe::scene {

class Node;

enum class Animation_path : int {
    INVALID     = 0,
    TRANSLATION = 1,
    ROTATION    = 2,
    SCALE       = 3,
    WEIGHTS     = 4
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

    [[nodiscard]] auto evaluate(Animation_channel& channel, float time_current) const -> glm::vec4;

    void apply(Animation_channel& channel, float time_current) const;
    void seek (Animation_channel& channel, float time_current) const;

    Animation_interpolation_mode interpolation_mode{Animation_interpolation_mode::LINEAR};
    std::vector<float>           timestamps;
    std::vector<float>           data;
};

class Animation_channel
{
public:
    Animation_path                     path;
    std::size_t                        sampler_index;  // index in Animation
    std::shared_ptr<erhe::scene::Node> target;
    std::size_t                        start_position; // in sampler keyframes
    std::size_t                        value_offset;   // in sampler data floats
};

class Animation : public Item<Item_base, Item_base, Animation>
{
public:
    explicit Animation(const Animation& src);
    Animation& operator=(const Animation& src);
    ~Animation() noexcept override;

    explicit Animation(std::string_view name);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Animation"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    // Public API
    [[nodiscard]] auto evaluate      (float time_current, std::size_t channel_index, std::size_t component) -> float;
    [[nodiscard]] auto get_first_time() const -> float;
    [[nodiscard]] auto get_last_time () const -> float;

    void apply(float time_current);

    std::vector<Animation_sampler> samplers;
    std::vector<Animation_channel> channels;
};

} // namespace erhe::scene
