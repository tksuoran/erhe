#pragma once

#include "renderers/multi_buffer.hpp"

#include "erhe/graphics/shader_resource.hpp"

#include <set>

namespace erhe::primitive
{
    class Material;
}

namespace editor
{

class Programs;
class Program_interface;
class Shader_resources;

class Material_struct
{
public:
    std::size_t roughness;    // vec2
    std::size_t metallic;     // float
    std::size_t transparency; // float
    std::size_t base_color;   // vec4
    std::size_t emissive;     // vec4
    std::size_t base_texture; // uvec2
    std::size_t reserved;     // uvec2
};

class Material_interface
{
public:
    explicit Material_interface(std::size_t max_material_count);

    erhe::graphics::Shader_resource material_block {"material", 0, erhe::graphics::Shader_resource::Type::uniform_block};
    erhe::graphics::Shader_resource material_struct{"Material"};
    Material_struct                 offsets        {};
    std::size_t                     max_material_count;
};

class Material_buffer
    : public Multi_buffer
{
public:
    explicit Material_buffer(const Material_interface& material_interface);

    auto update(
        const gsl::span<const std::shared_ptr<erhe::primitive::Material>>& materials,
        const std::shared_ptr<Programs>&                                   programs
    ) -> erhe::application::Buffer_range;

    [[nodiscard]] auto used_handles() const -> const std::set<uint64_t>&;

private:
    const Material_interface& m_material_interface;
    std::set<uint64_t>        m_used_handles;
};

} // namespace editor
