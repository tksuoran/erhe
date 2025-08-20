#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include <set>

namespace erhe::graphics {
    class Sampler;
    class Texture_heap;
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

    std::size_t normal_texture;             // uvec2
    std::size_t occlusion_texture;          // uvec2

    std::size_t emissive_texture;           // uvec2
    std::size_t opacity;                    // float
    std::size_t normal_texture_scale;       // float

    std::size_t base_color_rotation_scale;         // uvec4
    std::size_t metallic_roughness_rotation_scale; // uvec4
    std::size_t normal_rotation_scale;             // uvec4
    std::size_t occlusion_rotation_scale;          // uvec4
    std::size_t emissive_rotation_scale;           // uvec4

    std::size_t base_color_offset;                 // uvec2
    std::size_t metallic_roughness_offset;         // uvec2

    std::size_t normal_offset;                     // uvec2
    std::size_t occlusion_offset;                  // uvec2

    std::size_t emissive_offset;                   // uvec2
    std::size_t occlusion_texture_strength;        // float
    std::size_t reserved1;                         // float
};

class Material_interface
{
public:
    explicit Material_interface(erhe::graphics::Device& graphics_device);

    erhe::graphics::Shader_resource material_block;
    erhe::graphics::Shader_resource material_struct;
    Material_struct                 offsets;
    std::size_t                     max_material_count;
};

class Material_buffer : public erhe::graphics::Ring_buffer_client
{
public:
    Material_buffer(erhe::graphics::Device& graphics_device, Material_interface& material_interface);

    auto update(erhe::graphics::Texture_heap& texture_heap, const std::span<const std::shared_ptr<erhe::primitive::Material>>& materials) -> erhe::graphics::Ring_buffer_range;

private:
    erhe::graphics::Device& m_graphics_device;
    Material_interface&     m_material_interface;

    erhe::graphics::Sampler m_nearest_sampler;
    erhe::graphics::Sampler m_linear_sampler;
};

} // namespace erhe::scene_renderer
