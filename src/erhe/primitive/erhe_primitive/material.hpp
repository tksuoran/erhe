#pragma once

#include "erhe_item/item.hpp"
#include "erhe_primitive/enums.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <string_view>

namespace erhe::graphics {
    class Sampler;
    class Texture;
}

namespace erhe::primitive {

class Material_texture_sampler
{
public:
    std::shared_ptr<erhe::graphics::Texture> texture;
    std::shared_ptr<erhe::graphics::Sampler> sampler;
    uint32_t                                 tex_coord{0};
    float                                    rotation {0.0f};
    glm::vec2                                offset   {0.0f, 0.0f};
    glm::vec2                                scale    {1.0f, 1.0f};
};

[[nodiscard]] auto operator==(const Material_texture_sampler& lhs, const Material_texture_sampler& rhs);
[[nodiscard]] auto operator!=(const Material_texture_sampler& lhs, const Material_texture_sampler& rhs);

class Material_texture_samplers
{
public:
    Material_texture_sampler base_color;
    Material_texture_sampler metallic_roughness;
    Material_texture_sampler normal;
    Material_texture_sampler occlusion;
    Material_texture_sampler emissive;
};

[[nodiscard]] auto operator==(const Material_texture_samplers& lhs, const Material_texture_samplers& rhs);
[[nodiscard]] auto operator!=(const Material_texture_samplers& lhs, const Material_texture_samplers& rhs);

// True when the mode needs framebuffer blending (so the host mesh must
// route through the translucent composition-pass family). alpha_test and
// screen_door render with depth write enabled and discard for masked
// pixels, so they stay in the opaque pass.
[[nodiscard]] inline auto needs_translucent_pass(const Material_blending_mode mode) -> bool
{
    switch (mode) {
        case Material_blending_mode::alpha_blend:
        case Material_blending_mode::multiply:
        case Material_blending_mode::add:
        case Material_blending_mode::subtract:
            return true;
        case Material_blending_mode::opaque:
        case Material_blending_mode::screen_door:
        case Material_blending_mode::alpha_test:
            return false;
    }
    return false;
}

class Material_data
{
public:
    glm::vec3                 base_color                {1.0f, 1.0f, 1.0f};
    float                     opacity                   {1.0f};
    glm::vec2                 roughness                 {0.5f, 0.5f};
    float                     metallic                  {0.0f};
    float                     reflectance               {0.5f};
    glm::vec3                 emissive                  {0.0f, 0.0f, 0.0f};
    float                     normal_texture_scale      {1.0f};
    float                     occlusion_texture_strength{1.0f};
    Bxdf_model                bxdf_model                {Bxdf_model::isotropic_brdf};
    Material_blending_mode    blending_mode             {Material_blending_mode::opaque};
    float                     alpha_cutoff              {0.5f};
    bool                      use_circular_brushed_metal{false};
    bool                      use_aniso_control         {false};
    Material_texture_samplers texture_samplers          {};
};

class Material_create_info
{
public:
    std::string   name{};
    Material_data data{};
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
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::material; }

    uint32_t                material_buffer_index{0}; // updated by Material_buffer::update()
    std::optional<uint32_t> preview_slot;
    Material_data           data;
};

[[nodiscard]] auto operator==(const Material_data& lhs, const Material_data& rhs) -> bool;
[[nodiscard]] auto operator!=(const Material_data& lhs, const Material_data& rhs) -> bool;

[[nodiscard]] auto operator==(const Material& lhs, const Material& rhs) -> bool;
[[nodiscard]] auto operator!=(const Material& lhs, const Material& rhs) -> bool;

} // namespace erhe::primitive
