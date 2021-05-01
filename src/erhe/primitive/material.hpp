#ifndef material_hpp_erhe_primitive
#define material_hpp_erhe_primitive

#include <glm/glm.hpp>

#include <map>
#include <memory>
#include <string>

namespace erhe::primitive
{

struct Material
{
    Material() = default;

    Material(const std::string& name,
             glm::vec4          base_color,
             float              roughness  = 1.0f,
             float              anisotropy = 0.0f,
             float              metallic   = 0.6f)
    : name      {name}
    , metallic  {metallic}
    , roughness {roughness}
    , anisotropy{anisotropy}
    , base_color{base_color}
    , emissive  {0.0f, 0.0f, 0.0f, 0.0f}
    {
    }

    size_t      index{0};
    std::string name;
    float       metallic    {0.0f};
    float       roughness   {0.0f};
    float       anisotropy  {0.0f};
    float       transparency{0.0f};
    glm::vec4   base_color  {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4   emissive    {0.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace erhe::primitive

#endif
