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

class Material_create_info
{
public:
    struct Textures
    {
        std::shared_ptr<erhe::graphics::Texture> base_color;
        std::shared_ptr<erhe::graphics::Texture> metallic_roughness;
    };
    struct Samplers
    {
        std::shared_ptr<erhe::graphics::Sampler> base_color;
        std::shared_ptr<erhe::graphics::Sampler> metallic_roughness;
    };
    struct TexCoords
    {
        uint8_t base_color        {0};
        uint8_t metallic_roughness{0};
    };

    std::string name;
    glm::vec3   base_color {1.0f, 1.0f, 1.0f};
    glm::vec2   roughness  {0.5f, 0.5f};
    float       metallic   {0.0f};
    float       reflectance{0.5f};
    glm::vec3   emissive   {0.0f, 0.0f, 0.0f};
    float       opacity    {1.0f};

    Textures    textures{};
    Samplers    samplers{};
    TexCoords   tex_coords{};

};

class Material : public erhe::Item<erhe::Item_base, erhe::Item_base, Material>
{
public:
    struct Textures
    {
        std::shared_ptr<erhe::graphics::Texture> base_color;
        std::shared_ptr<erhe::graphics::Texture> metallic_roughness;
    };
    struct Samplers
    {
        std::shared_ptr<erhe::graphics::Sampler> base_color;
        std::shared_ptr<erhe::graphics::Sampler> metallic_roughness;
    };
    struct TexCoords
    {
        uint8_t base_color        {0};
        uint8_t metallic_roughness{0};
    };

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

    uint32_t                material_buffer_index{0}; // updated by Material_buffer::update()
    glm::vec3               base_color  {1.0f, 1.0f, 1.0f};
    float                   opacity     {1.0f};
    glm::vec2               roughness   {0.5f, 0.5f};
    float                   metallic    {0.0f};
    float                   reflectance {0.5f};
    glm::vec3               emissive    {0.0f, 0.0f, 0.0f};

    Textures                textures;
    Samplers                samplers;
    TexCoords               tex_coords{};

    std::optional<uint32_t> preview_slot;
};

} // namespace erhe::primitive
