#pragma once

#include <glm/glm.hpp>

namespace hextiles
{

class Fbm_noise
{
public:
    void prepare ();
    auto generate(float s, float t, glm::vec4 seed) -> float;
    void imgui   ();

private:
    auto generate(float x, float y, float z, float w, glm::vec4 seed) -> float;

    float m_bounding   {0.0f};
    float m_frequency  {0.4f};
    float m_lacunarity {1.50f};
    float m_gain       {0.75f};
    int   m_octaves    {5};
    float m_location[2]{0.0f, 0.0f};
};

} // namespace hextiles
