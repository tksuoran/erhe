#pragma once

#include "erhe_item/item.hpp"
#include "erhe_item/unique_id.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
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
    : public erhe::Item<erhe::Item_base, erhe::Item_base, Material>
{
public:
    Material();
    explicit Material(const Material&);
    Material& operator=(const Material&);
    ~Material() noexcept override;

    explicit Material(
        const std::string_view name,
        const glm::vec3        base_color = glm::vec3{1.0f, 1.0f, 1.0f},
        const glm::vec2        roughness  = glm::vec2{0.5f, 0.5f},
        const float            metallic   = 0.0f
    );

    // Interface similar to erhe::scene::Scene_item
    static constexpr std::string_view static_type_name{"Material"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    uint32_t                                 material_buffer_index{0}; // updated by Material_buffer::update()
    glm::vec4                                base_color  {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2                                roughness   {0.5f, 0.5f};
    float                                    metallic    {0.0f};
    float                                    reflectance {0.5f};
    glm::vec4                                emissive    {0.0f, 0.0f, 0.0f, 0.0f};
    float                                    opacity     {1.0f};
    std::shared_ptr<erhe::graphics::Texture> base_color_texture;
    std::shared_ptr<erhe::graphics::Texture> metallic_roughness_texture;
    std::shared_ptr<erhe::graphics::Sampler> base_color_sampler;
    std::shared_ptr<erhe::graphics::Sampler> metallic_roughness_sampler;
    std::optional<uint32_t>                  preview_slot;
};

} // namespace erhe::primitive
