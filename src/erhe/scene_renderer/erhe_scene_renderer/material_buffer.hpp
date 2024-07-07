#pragma once

#include "erhe_graphics/shader_resource.hpp"
#include "erhe_renderer/multi_buffer.hpp"

#include <set>

namespace erhe::graphics {
    class Sampler;
}

namespace erhe::primitive {
    class Material;
}

namespace erhe::scene_renderer {

class Program_interface;
class Shader_resources;

class Material_struct
{
public:
    std::size_t roughness;                  // vec2
    std::size_t metallic;                   // float
    std::size_t reflectance;                // float
    std::size_t base_color;                 // vec4
    std::size_t emissive;                   // vec4
    std::size_t base_color_texture;         // uvec2
    std::size_t metallic_roughness_texture; // uvec2
    std::size_t opacity;                    // float
    std::size_t reserved1;                  // float
    std::size_t reserved2;                  // float
    std::size_t reserved3;                  // float
};

class Material_interface
{
public:
    explicit Material_interface(erhe::graphics::Instance& graphics_instance);

    erhe::graphics::Shader_resource material_block;
    erhe::graphics::Shader_resource material_struct;
    Material_struct                 offsets;
    std::size_t                     max_material_count;
};

class Material_buffer : public erhe::renderer::Multi_buffer
{
public:
    Material_buffer(erhe::graphics::Instance& graphics_instance, Material_interface& material_interface);

    auto update(
        const std::span<const std::shared_ptr<erhe::primitive::Material>>& materials
    ) -> erhe::renderer::Buffer_range;

    [[nodiscard]] auto used_handles() const -> const std::set<uint64_t>&;

private:
    erhe::graphics::Instance& m_graphics_instance;
    Material_interface&       m_material_interface;
    std::set<uint64_t>        m_used_handles;

    std::unique_ptr<erhe::graphics::Sampler> m_nearest_sampler;
    std::unique_ptr<erhe::graphics::Sampler> m_linear_sampler;
};

} // namespace erhe::scene_renderer
