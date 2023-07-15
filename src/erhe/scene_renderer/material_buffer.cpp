// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/scene_renderer/material_buffer.hpp"

#include "erhe/configuration/configuration.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/scene_renderer/scene_renderer_log.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::scene_renderer
{

Material_interface::Material_interface(
    erhe::graphics::Instance& graphics_instance
)
    : material_block {graphics_instance, "material", 0, erhe::graphics::Shader_resource::Type::shader_storage_block}
    , material_struct{graphics_instance, "Material"}
    , offsets        {
        .roughness                  = material_struct.add_vec2 ("roughness"                 )->offset_in_parent(),
        .metallic                   = material_struct.add_float("metallic"                  )->offset_in_parent(),
        .reflectance                = material_struct.add_float("reflectance"               )->offset_in_parent(),
        .base_color                 = material_struct.add_vec4 ("base_color"                )->offset_in_parent(),
        .emissive                   = material_struct.add_vec4 ("emissive"                  )->offset_in_parent(),
        .base_color_texture         = material_struct.add_uvec2("base_color_texture"        )->offset_in_parent(),
        .metallic_roughness_texture = material_struct.add_uvec2("metallic_roughness_texture")->offset_in_parent(),
        .opacity                    = material_struct.add_float("opacity"                   )->offset_in_parent(),
        .reserved1                  = material_struct.add_float("reserved1"                 )->offset_in_parent(),
        .reserved2                  = material_struct.add_float("reserved2"                 )->offset_in_parent(),
        .reserved3                  = material_struct.add_float("reserved3"                 )->offset_in_parent()
    }
{
    auto ini = erhe::configuration::get_ini("erhe.ini", "renderer");
    ini->get("max_material_count", max_material_count);

    material_block.add_struct(
        "materials",
        &material_struct,
        erhe::graphics::Shader_resource::unsized_array
    );
}

Material_buffer::Material_buffer(
    erhe::graphics::Instance& graphics_instance,
    Material_interface&       material_interface
)
    : Multi_buffer        {graphics_instance, "material"}
    , m_graphics_instance {graphics_instance}
    , m_material_interface{material_interface}
{
    m_nearest_sampler = std::make_unique<erhe::graphics::Sampler>(
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::nearest,
            .mag_filter  = gl::Texture_mag_filter::nearest,
            .debug_label = "Material_buffer nearest"
        }
    );

    m_linear_sampler = std::make_unique<erhe::graphics::Sampler>(
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::linear,
            .mag_filter  = gl::Texture_mag_filter::linear,
            .debug_label = "Material_buffer linear"
        }
    );

    Multi_buffer::allocate(
        gl::Buffer_target::shader_storage_buffer,
        m_material_interface.material_block.binding_point(),
        // TODO
        8 * m_material_interface.material_struct.size_bytes() * m_material_interface.max_material_count
    );
}

auto Material_buffer::update(
    const gsl::span<const std::shared_ptr<erhe::primitive::Material>>& materials
) -> erhe::renderer::Buffer_range
{
    ERHE_PROFILE_FUNCTION();

    SPDLOG_LOGGER_TRACE(
        log_render,
        "materials.size() = {}, m_material_writer.write_offset = {}",
        materials.size(),
        m_writer.write_offset
    );

    auto&             buffer         = current_buffer();
    const auto        entry_size     = m_material_interface.material_struct.size_bytes();
    const auto&       offsets        = m_material_interface.offsets;
    const std::size_t max_byte_count = materials.size() * entry_size;
    const auto        gpu_data       = m_writer.begin(&buffer, max_byte_count);
    
    m_used_handles.clear();
    uint32_t material_index = 0;
    for (const auto& material : materials) {
        ERHE_VERIFY(material);
        if ((m_writer.write_offset + entry_size) > m_writer.write_end) {
            log_render->critical("material buffer capacity {} exceeded", buffer.capacity_byte_count());
            ERHE_FATAL("material buffer capacity exceeded");
            break;
        }
        memset(reinterpret_cast<uint8_t*>(gpu_data.data()) + m_writer.write_offset, 0, entry_size);
        using erhe::graphics::as_span;
        using erhe::graphics::write;

        const uint64_t base_color_handle = material->base_color_texture
            ?
                m_graphics_instance.get_handle(
                    *material->base_color_texture.get(),
                    material->base_color_sampler
                        ? *material->base_color_sampler.get()
                        : *m_linear_sampler.get()
                )
            : 0;
        const uint64_t metallic_roughness_handle = material->metallic_roughness_texture
            ?
                m_graphics_instance.get_handle(
                    *material->metallic_roughness_texture.get(),
                    material->metallic_roughness_sampler
                        ? *material->metallic_roughness_sampler.get()
                        : *m_linear_sampler.get()
                )
            : 0;


        if (base_color_handle != 0) {
            m_used_handles.insert(base_color_handle);
        }
        if (metallic_roughness_handle != 0) {
            m_used_handles.insert(metallic_roughness_handle);
        }

        material->material_buffer_index = material_index;

        write(gpu_data, m_writer.write_offset + offsets.metallic   , as_span(material->metallic   ));
        write(gpu_data, m_writer.write_offset + offsets.roughness  , as_span(material->roughness  ));
        write(gpu_data, m_writer.write_offset + offsets.reflectance, as_span(material->reflectance));
        write(gpu_data, m_writer.write_offset + offsets.base_color , as_span(material->base_color ));
        write(gpu_data, m_writer.write_offset + offsets.emissive   , as_span(material->emissive   ));
        write(gpu_data, m_writer.write_offset + offsets.opacity    , as_span(material->opacity    ));

        if (m_graphics_instance.info.use_bindless_texture) {
            write(gpu_data, m_writer.write_offset + offsets.base_color_texture,         as_span(base_color_handle));
            write(gpu_data, m_writer.write_offset + offsets.metallic_roughness_texture, as_span(metallic_roughness_handle));
        } else {
            {
                std::optional<std::size_t> opt_texture_unit = m_graphics_instance.texture_unit_cache_allocate(base_color_handle);
                const uint64_t texture_unit = static_cast<uint64_t>(opt_texture_unit.has_value() ? opt_texture_unit.value() : 0);
                const uint64_t magic        = static_cast<uint64_t>(0x7fff'ffff);
                const uint64_t value        = texture_unit | (magic << 32);
                write(gpu_data, m_writer.write_offset + offsets.base_color_texture, as_span(value));
            }
            {
                std::optional<std::size_t> opt_texture_unit = m_graphics_instance.texture_unit_cache_allocate(metallic_roughness_handle);
                const uint64_t texture_unit = static_cast<uint64_t>(opt_texture_unit.has_value() ? opt_texture_unit.value() : 0);
                const uint64_t magic        = static_cast<uint64_t>(0x7fff'ffff);
                const uint64_t value        = texture_unit | (magic << 32);
                write(gpu_data, m_writer.write_offset + offsets.metallic_roughness_texture, as_span(value));
            }
        }

        m_writer.write_offset += entry_size;
        ERHE_VERIFY(m_writer.write_offset <= m_writer.write_end);
        ++material_index;
    }
    m_writer.end();

    SPDLOG_LOGGER_TRACE(log_draw, "wrote {} entries to material buffer", material_index);

    return m_writer.range;
}

[[nodiscard]] auto Material_buffer::used_handles() const -> const std::set<uint64_t>&
{
    return m_used_handles;
}

} // namespace erhe::scene_renderer
