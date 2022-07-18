#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace erhe::graphics
{
    class Sampler;
    class Texture;
}

namespace erhe::primitive
{

class Material
{
public:
    Material();

    explicit Material(
        const std::string_view name,
        const glm::vec3        base_color = glm::vec3{1.0f, 1.0f, 1.0f},
        const glm::vec2        roughness  = glm::vec2{0.5f, 0.5f},
        const float            metallic   = 0.0f
    );
    ~Material() noexcept;

    std::size_t                              index{0};
    std::string                              name;
    float                                    metallic    {0.0f};
    glm::vec2                                roughness   {0.5f, 0.5f};
    float                                    transparency{0.0f};
    glm::vec4                                base_color  {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4                                emissive    {0.0f, 0.0f, 0.0f, 0.0f};
    std::shared_ptr<erhe::graphics::Texture> texture;
    std::shared_ptr<erhe::graphics::Sampler> sampler;
    bool                                     visible     {true};
};

} // namespace erhe::primitive
