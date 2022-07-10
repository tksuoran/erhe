// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "renderers/material_buffer.hpp"
#include "renderers/programs.hpp"
#include "renderers/program_interface.hpp"
#include "log.hpp"

#include "erhe/graphics/texture.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/toolkit/profile.hpp"

#include <imgui/imgui.h>

namespace editor
{

Material_interface::Material_interface(std::size_t max_material_count)
    : material_block {"material", 0, erhe::graphics::Shader_resource::Type::uniform_block}
    , material_struct{"Material"}
    , offsets        {
        .roughness    = material_struct.add_vec2 ("roughness"   )->offset_in_parent(),
        .metallic     = material_struct.add_float("metallic"    )->offset_in_parent(),
        .transparency = material_struct.add_float("transparency")->offset_in_parent(),
        .base_color   = material_struct.add_vec4 ("base_color"  )->offset_in_parent(),
        .emissive     = material_struct.add_vec4 ("emissive"    )->offset_in_parent(),
        .base_texture = material_struct.add_uvec2("base_texture")->offset_in_parent(),
        .reserved     = material_struct.add_uvec2("reserved"    )->offset_in_parent()
    },
    max_material_count{max_material_count}
{
    material_block.add_struct("materials", &material_struct, max_material_count);
}

Material_buffer::Material_buffer(const Material_interface& material_interface)
    : Multi_buffer        {"material"}
    , m_material_interface{material_interface}
{
    Multi_buffer::allocate(
        gl::Buffer_target::uniform_buffer,
        m_material_interface.material_block.binding_point(),
        m_material_interface.material_block.size_bytes(),
        m_name
    );
}

auto Material_buffer::update(
    const gsl::span<const std::shared_ptr<erhe::primitive::Material>>& materials,
    const std::shared_ptr<Programs>&                                   programs
) -> erhe::application::Buffer_range
{
    ERHE_PROFILE_FUNCTION

    SPDLOG_LOGGER_TRACE(
        log_render,
        "materials.size() = {}, m_material_writer.write_offset = {}",
        materials.size(),
        m_writer.write_offset
    );

    auto&       buffer         = current_buffer();
    const auto  entry_size     = m_material_interface.material_struct.size_bytes();
    const auto& offsets        = m_material_interface.offsets;
    const auto  gpu_data       = buffer.map();
    std::size_t material_index = 0;
    m_writer.begin(buffer.target());
    for (const auto& material : materials)
    {
        if ((m_writer.write_offset + entry_size) > buffer.capacity_byte_count())
        {
            log_render->critical("material buffer capacity {} exceeded", buffer.capacity_byte_count());
            ERHE_FATAL("material buffer capacity exceeded");
            break;
        }
        memset(reinterpret_cast<uint8_t*>(gpu_data.data()) + m_writer.write_offset, 0, entry_size);
        using erhe::graphics::as_span;
        using erhe::graphics::write;

        const uint64_t handle = material->texture
            ?
                erhe::graphics::get_handle(
                    *material->texture.get(),
                    *programs->linear_sampler.get()
                )
            : 0;
        write(gpu_data, m_writer.write_offset + offsets.metallic    , as_span(material->metallic    ));
        write(gpu_data, m_writer.write_offset + offsets.roughness   , as_span(material->roughness   ));
        write(gpu_data, m_writer.write_offset + offsets.transparency, as_span(material->transparency));
        write(gpu_data, m_writer.write_offset + offsets.base_color  , as_span(material->base_color  ));
        write(gpu_data, m_writer.write_offset + offsets.emissive    , as_span(material->emissive    ));
        write(gpu_data, m_writer.write_offset + offsets.base_texture, as_span(handle                ));

        m_writer.write_offset += entry_size;
        ERHE_VERIFY(m_writer.write_offset <= buffer.capacity_byte_count());
        ++material_index;
    }
    m_writer.end();

    SPDLOG_LOGGER_TRACE(log_draw, "wrote {} entries to material buffer", material_index);

    return m_writer.range;
}

} // namespace editor
