// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/material_buffer.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"
#include "erhe_renderer/renderer_config.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

Material_interface::Material_interface(erhe::graphics::Device& graphics_device)
    : material_block {graphics_device, "material", material_buffer_binding_point, erhe::graphics::Shader_resource::Type::shader_storage_block}
    , material_struct{graphics_device, "Material"}
    , offsets        {
        .roughness                  = material_struct.add_vec2 ("roughness"                 )->get_offset_in_parent(),
        .metallic                   = material_struct.add_float("metallic"                  )->get_offset_in_parent(),
        .reflectance                = material_struct.add_float("reflectance"               )->get_offset_in_parent(),
        .base_color                 = material_struct.add_vec4 ("base_color"                )->get_offset_in_parent(),
        .emissive                   = material_struct.add_vec4 ("emissive"                  )->get_offset_in_parent(),
        .base_color_texture         = material_struct.add_uvec2("base_color_texture"        )->get_offset_in_parent(),
        .metallic_roughness_texture = material_struct.add_uvec2("metallic_roughness_texture")->get_offset_in_parent(),
        .opacity                    = material_struct.add_float("opacity"                   )->get_offset_in_parent(),
        .reserved1                  = material_struct.add_float("reserved1"                 )->get_offset_in_parent(),
        .reserved2                  = material_struct.add_float("reserved2"                 )->get_offset_in_parent(),
        .reserved3                  = material_struct.add_float("reserved3"                 )->get_offset_in_parent()
    }
{
    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "renderer");
    ini.get("max_material_count", max_material_count);

    material_block.add_struct(
        "materials",
        &material_struct,
        erhe::graphics::Shader_resource::unsized_array
    );
    material_block.set_readonly(true);
}

Material_buffer::Material_buffer(erhe::graphics::Device& graphics_device, Material_interface& material_interface)
    : GPU_ring_buffer_client{
        graphics_device,
        erhe::graphics::Buffer_target::storage,
        "Material_buffer",
        material_interface.material_block.get_binding_point()
    }
    , m_graphics_device {graphics_device}
    , m_material_interface{material_interface}
    , m_nearest_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::nearest_mipmap_nearest,
            .mag_filter  = gl::Texture_mag_filter::nearest,
            .debug_label = "Material_buffer nearest"
        }
    }
    , m_linear_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::linear_mipmap_nearest,
            .mag_filter  = gl::Texture_mag_filter::linear,
            .debug_label = "Material_buffer linear"
        }
    }
{
}

constexpr uint32_t c_texture_unused_32 = 4294967295u;
constexpr uint32_t c_texture_unused_64 = 4294967295u;

auto Material_buffer::update(const std::span<const std::shared_ptr<erhe::primitive::Material>>& materials) -> erhe::graphics::Buffer_range
{
    ERHE_PROFILE_FUNCTION();

    if (materials.empty()) {
        return {};
    }

    // SPDLOG_LOGGER_TRACE(
    //     log_material_buffer,
    //     "materials.size() = {}, write_offset = {}",
    //     materials.size(),
    //     m_writer.write_offset
    // );

    const auto        entry_size     = m_material_interface.material_struct.get_size_bytes();
    const auto&       offsets        = m_material_interface.offsets;
    const std::size_t max_byte_count = materials.size() * entry_size;

    erhe::graphics::Buffer_range buffer_range = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, max_byte_count);
    std::span<std::byte>         gpu_data     = buffer_range.get_span();
    std::size_t                  write_offset = 0;
    
    m_used_handles.clear();
    uint32_t material_index = 0;
    for (const auto& material : materials) {
        ERHE_VERIFY(material);
        memset(reinterpret_cast<uint8_t*>(gpu_data.data()) + write_offset, 0, entry_size);
        using erhe::graphics::as_span;
        using erhe::graphics::write;

        const uint64_t base_color_handle = material->textures.base_color
            ? m_graphics_device.get_handle(
                *material->textures.base_color.get(),
                material->samplers.base_color ? *material->samplers.base_color.get() : m_linear_sampler
            )
            : c_texture_unused_64;
        const uint64_t metallic_roughness_handle = material->textures.metallic_roughness
            ? m_graphics_device.get_handle(
                *material->textures.metallic_roughness.get(),
                material->samplers.metallic_roughness ? *material->samplers.metallic_roughness.get() : m_linear_sampler
            )
            : c_texture_unused_64;

        if (base_color_handle != c_texture_unused_64) {
            m_used_handles.insert(base_color_handle);
            SPDLOG_LOGGER_TRACE(
                log_material_buffer,
                "[{}] {} base_color_handle = {} - {} - {}",
                material_index,
                material->get_name(),
                material->textures.base_color->get_name(),
                material->textures.base_color->get_source_path().string(),
                erhe::graphics::format_texture_handle(base_color_handle)
            );
        //} else {
        //    SPDLOG_LOGGER_TRACE(log_material_buffer, "{}: no base_color_handle", material->get_name());
        }
        if (metallic_roughness_handle != c_texture_unused_64) {
            m_used_handles.insert(metallic_roughness_handle);
            SPDLOG_LOGGER_TRACE(
                log_material_buffer,
                "[{}] {} metallic_roughness_handle = {} - {} - {}",
                material->get_name(),
                material->get_name(),
                material->textures.metallic_roughness->get_name(),
                material->textures.metallic_roughness->get_source_path().string(),
                erhe::graphics::format_texture_handle(metallic_roughness_handle)
            );
        //} else {
        //    SPDLOG_LOGGER_TRACE(log_material_buffer, "{}: no metallic_roughness_handle", material->get_name());
        }

        material->material_buffer_index = material_index;

        write(gpu_data, write_offset + offsets.metallic   , as_span(material->metallic   ));
        write(gpu_data, write_offset + offsets.roughness  , as_span(material->roughness  ));
        write(gpu_data, write_offset + offsets.reflectance, as_span(material->reflectance));
        write(gpu_data, write_offset + offsets.base_color , as_span(material->base_color ));
        write(gpu_data, write_offset + offsets.emissive   , as_span(material->emissive   ));
        write(gpu_data, write_offset + offsets.opacity    , as_span(material->opacity    ));

        if (m_graphics_device.info.use_bindless_texture) {
            write(gpu_data, write_offset + offsets.base_color_texture,         as_span(base_color_handle));
            write(gpu_data, write_offset + offsets.metallic_roughness_texture, as_span(metallic_roughness_handle));
        } else {
            {
                std::optional<std::size_t> opt_texture_unit = material->textures.base_color
                    ? m_graphics_device.texture_unit_cache_allocate(base_color_handle)
                    : std::nullopt;
                if (opt_texture_unit.has_value()) {
                    SPDLOG_LOGGER_TRACE(log_material_buffer, "[{}] {} base_color allocated texture unit = {}", material_index, material->get_name(), opt_texture_unit.value());
                } else {
                    SPDLOG_LOGGER_TRACE(log_material_buffer, "[{}] {} base_color texture unit not allocated", material_index, material->get_name());
                }

                const uint64_t texture_unit = static_cast<uint64_t>(opt_texture_unit.has_value() ? opt_texture_unit.value() : c_texture_unused_32);
                //const uint64_t reserved     = static_cast<uint64_t>(0);
                const uint64_t value        = texture_unit; // | (reserved << 32);
                write(gpu_data, write_offset + offsets.base_color_texture, as_span(value));
            }
            {
                std::optional<std::size_t> opt_texture_unit = material->textures.metallic_roughness
                    ? m_graphics_device.texture_unit_cache_allocate(metallic_roughness_handle)
                    : std::nullopt;
                const uint64_t texture_unit = static_cast<uint64_t>(opt_texture_unit.has_value() ? opt_texture_unit.value() : c_texture_unused_32);
                //const uint64_t reserved     = static_cast<uint64_t>(0);
                const uint64_t value        = texture_unit; // | (reserved << 32);
                write(gpu_data, write_offset + offsets.metallic_roughness_texture, as_span(value));
            }
        }

        write_offset += entry_size;
        ++material_index;
    }
    buffer_range.bytes_written(write_offset);
    buffer_range.close();

    // SPDLOG_LOGGER_TRACE(log_material_buffer, "wrote {} entries to material buffer", material_index);

    return buffer_range;
}

auto Material_buffer::used_handles() const -> const std::set<uint64_t>&
{
    return m_used_handles;
}

} // namespace erhe::scene_renderer
