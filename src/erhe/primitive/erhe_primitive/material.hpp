#pragma once

#include "erhe_item/item.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <string_view>

namespace erhe::graphics {
    class Sampler;
    class Texture;
}

namespace erhe::primitive {

struct Material_texture_sampler
{
    std::shared_ptr<erhe::graphics::Texture> texture;
    std::shared_ptr<erhe::graphics::Sampler> sampler;
    uint8_t                                  tex_coord;
};

struct Material_texture_samplers
{
    Material_texture_sampler base_color;
    Material_texture_sampler metallic_roughness;
    Material_texture_sampler normal;
    Material_texture_sampler occlusion;
    Material_texture_sampler emissive;
};

class Material_create_info
{
public:
    std::string               name;
    glm::vec3                 base_color                {1.0f, 1.0f, 1.0f};
    glm::vec2                 roughness                 {0.5f, 0.5f};
    float                     metallic                  {0.0f};
    float                     reflectance               {0.5f};
    glm::vec3                 emissive                  {0.0f, 0.0f, 0.0f};
    float                     opacity                   {1.0f};
    float                     normal_texture_scale      {1.0f};
    float                     occlusion_texture_strength{1.0f};
    Material_texture_samplers texture_samplers;
};

class Material : public erhe::Item<erhe::Item_base, erhe::Item_base, Material>
{
public:
    Material();
    explicit Material(const Material&);
    Material& operator=(const Material&);
    ~Material() noexcept override;

    explicit Material(const Material_create_info& create_info);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Material"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    uint32_t  material_buffer_index{0}; // updated by Material_buffer::update()
    glm::vec3 base_color  {1.0f, 1.0f, 1.0f};
    float     opacity     {1.0f};
    glm::vec2 roughness   {0.5f, 0.5f};
    float     metallic    {0.0f};
    float     reflectance {0.5f};
    glm::vec3 emissive    {0.0f, 0.0f, 0.0f};
    float     normal_texture_scale      {1.0f};
    float     occlusion_texture_strength{1.0f};

    Material_texture_samplers texture_samplers;

    std::optional<uint32_t> preview_slot;
};

} // namespace erhe::primitive
