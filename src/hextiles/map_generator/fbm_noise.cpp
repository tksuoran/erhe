#ifdef _MSC_VER
#   pragma warning(push, 0)
#   pragma warning( disable : 4701 ) // glm
#endif

#include "map_generator/fbm_noise.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/noise.hpp>

#include <imgui/imgui.h>

namespace hextiles
{

void Fbm_noise::prepare()
{
    const float gain  = std::abs(m_gain);
    float amp         = gain;
    float amp_fractal = 1.0f;
    for (int i = 1; i < m_octaves; i++) {
        amp_fractal += amp;
        amp *= gain;
    }
    m_bounding = 1.0f / amp_fractal;
}

void Fbm_noise::imgui()
{
    ImGui::DragInt   ("Octaves",    &m_octaves,     0.1f,     1,         9);
    ImGui::DragFloat ("Frequency",  &m_frequency,   0.1f,     0.001f,   10.0f);
    ImGui::DragFloat ("Lacunarity", &m_lacunarity,  0.1f,     0.001f,   10.0f);
    ImGui::DragFloat ("Gain",       &m_gain,        0.1f,     0.001f,    1.0f);
    ImGui::DragFloat2("Location",   &m_location[0], 0.1f, -1000.0f,   1000.0f);
}

auto Fbm_noise::generate(const float s, const float t, const glm::vec4 seed) -> float
{
    const float x = m_location[0] + std::cos(s * glm::two_pi<float>()) * m_frequency;
    const float y = m_location[1] + std::cos(t * glm::two_pi<float>()) * m_frequency;
    const float z = m_location[0] + std::sin(s * glm::two_pi<float>()) * m_frequency;
    const float w = m_location[1] + std::sin(t * glm::two_pi<float>()) * m_frequency;

    return generate(x, y, z, w, seed);
}

auto Fbm_noise::generate(float x, float y, float z, float w, const glm::vec4 seed) -> float
{
    float sum = 0;
    float amp = m_bounding;

    for (int i = 0; i < m_octaves; i++) {
        float noise = glm::simplex(seed + glm::vec4{x, y, z, w});
        sum += noise * amp;

        x *= m_lacunarity;
        y *= m_lacunarity;
        z *= m_lacunarity;
        w *= m_lacunarity;
        amp *= m_gain;
    }

    return sum;
}

} // namespace hextiles

#ifdef _MSC_VER
#   pragma warning(pop)
#endif
