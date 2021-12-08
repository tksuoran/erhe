#pragma once

#include <glm/glm.hpp>

#include <string>
#include <string_view>

namespace erhe::primitive
{

class Material
{
public:
    Material();
    explicit Material(
        const std::string_view name,
        const glm::vec4        base_color = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        const float            roughness  = 0.5f,
        const float            anisotropy = 0.0f,
        const float            metallic   = 0.6f
    );
    ~Material();

    size_t      index{0};
    std::string name;
    float       metallic    {0.0f};
    float       roughness   {0.0f};
    float       anisotropy  {0.0f};
    float       transparency{0.0f};
    glm::vec4   base_color  {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4   emissive    {0.0f, 0.0f, 0.0f, 0.0f};
    bool        visible     {false};
};

} // namespace erhe::primitive
