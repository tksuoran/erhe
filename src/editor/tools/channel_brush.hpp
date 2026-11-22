#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace editor {

// Scene-free core of the channel brush (doc/editor/tools.md "Channel painting").

enum class Channel_brush_blend : unsigned int
{
    mix = 0,
    add,
    subtract
};

static constexpr const char* c_channel_brush_blend_strings[] = { "Mix", "Add", "Subtract" };

// Bit i of a channel mask selects component i (r, g, b, a) of a vec4 value.
static constexpr unsigned int c_channel_mask_r   = 0x1u;
static constexpr unsigned int c_channel_mask_g   = 0x2u;
static constexpr unsigned int c_channel_mask_b   = 0x4u;
static constexpr unsigned int c_channel_mask_a   = 0x8u;
static constexpr unsigned int c_channel_mask_all = 0xfu;

class Channel_brush_settings
{
public:
    float               radius      {0.5f};
    float               strength    {1.0f};
    glm::vec4           target      {1.0f}; // read in the masked channels only
    Channel_brush_blend blend       {Channel_brush_blend::mix};
    unsigned int        channel_mask{c_channel_mask_r};
};

// Per vertex state of one stroke; the buffers keep their capacity between strokes.
class Channel_brush_stroke
{
public:
    // Starts a stroke over values, which are the values before the stroke.
    void begin(std::span<const glm::vec4> values);
    void clear();

    std::vector<glm::vec4> before_values;
    std::vector<float>     alpha_max;
};

// Smooth falloff 3p^2 - 2p^3 with p = 1 - distance / radius; 0 at and beyond the radius.
[[nodiscard]] auto channel_brush_falloff(float distance, float radius) -> float;

// The blend of before toward target by alpha in the masked channels, clamped to [0, 1].
[[nodiscard]] auto channel_brush_blend(const Channel_brush_settings& settings, const glm::vec4& before, float alpha) -> glm::vec4;

// One dab at center over positions (parallel to values and to the stroke's
// buffers). Clears out_changed and fills it with the indices whose value changed.
void apply_dab(
    const Channel_brush_settings& settings,
    Channel_brush_stroke&         stroke,
    const glm::vec3&              center,
    std::span<const glm::vec3>    positions,
    std::span<glm::vec4>          values,
    std::vector<std::uint32_t>&   out_changed
);

}
