// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/material_buffer.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"
#include "erhe_renderer/renderer_config.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/texture_heap.hpp"
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

        .normal_texture             = material_struct.add_uvec2("normal_texture"            )->get_offset_in_parent(),
        .occlusion_texture          = material_struct.add_uvec2("occlusion_texture"         )->get_offset_in_parent(),

        .emission_texture           = material_struct.add_uvec2("emission_texture"          )->get_offset_in_parent(),
        .opacity                    = material_struct.add_float("opacity"                   )->get_offset_in_parent(),
        .normal_texture_scale       = material_struct.add_float("normal_texture_scale"      )->get_offset_in_parent(),

        .occlusion_texture_strength = material_struct.add_float("occlusion_texture_strength")->get_offset_in_parent(),
        .reserved1                  = material_struct.add_float("reserved1"                 )->get_offset_in_parent(),
        .reserved2                  = material_struct.add_float("reserved2"                 )->get_offset_in_parent(),
        .reserved3                  = material_struct.add_float("reserved3"                 )->get_offset_in_parent(),
    }
{
    const auto& ini = erhe::configuration::get_ini_file_section(c_erhe_config_file_path, "renderer");
    ini.get("max_material_count", max_material_count);

    material_block.add_struct(
        "materials",
        &material_struct,
        erhe::graphics::Shader_resource::unsized_array
    );
    material_block.set_readonly(true);
}

Material_buffer::Material_buffer(erhe::graphics::Device& graphics_device, Material_interface& material_interface)
    : Ring_buffer_client{
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
            .min_filter  = erhe::graphics::Filter::nearest,
            .mag_filter  = erhe::graphics::Filter::nearest,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::nearest,
            .debug_label = "Material_buffer nearest"
        }
    }
    , m_linear_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::linear,
            .mag_filter  = erhe::graphics::Filter::linear,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::nearest,
            .debug_label = "Material_buffer linear"
        }
    }
{
}

auto Material_buffer::update(
    erhe::graphics::Texture_heap&                                      texture_heap,
    const std::span<const std::shared_ptr<erhe::primitive::Material>>& materials
) -> erhe::graphics::Ring_buffer_range
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

    erhe::graphics::Ring_buffer_range buffer_range = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, max_byte_count);
    std::span<std::byte>              gpu_data     = buffer_range.get_span();
    std::size_t                       write_offset = 0;
    
    uint32_t material_index = 0;
    for (const auto& material : materials) {
        ERHE_VERIFY(material);
        memset(reinterpret_cast<uint8_t*>(gpu_data.data()) + write_offset, 0, entry_size);
        using erhe::graphics::as_span;
        using erhe::graphics::write;

        auto get_texture_sampler_shader_handle = [this, &texture_heap](const erhe::primitive::Material_texture_sampler& data) -> uint64_t
        {
            if (data.texture) {
                const erhe::graphics::Sampler* sampler = data.sampler ? data.sampler.get() : &m_linear_sampler;
                const uint64_t shader_handle = texture_heap.allocate(data.texture.get(), sampler);
                ERHE_VERIFY(shader_handle != erhe::graphics::invalid_texture_handle);
                return shader_handle;
            } else {
                return erhe::graphics::invalid_texture_handle;
            }
        };

        const uint64_t base_color_shader_handle         = get_texture_sampler_shader_handle(material->texture_samplers.base_color);
        const uint64_t metallic_roughness_shader_handle = get_texture_sampler_shader_handle(material->texture_samplers.metallic_roughness);
        const uint64_t normal_shader_handle             = get_texture_sampler_shader_handle(material->texture_samplers.normal);
        const uint64_t occlusion_shader_handle          = get_texture_sampler_shader_handle(material->texture_samplers.occlusion);
        const uint64_t emission_shader_handle           = get_texture_sampler_shader_handle(material->texture_samplers.emission);

        material->material_buffer_index = material_index;

        write(gpu_data, write_offset + offsets.metallic   ,                as_span(material->metallic   ));
        write(gpu_data, write_offset + offsets.roughness  ,                as_span(material->roughness  ));
        write(gpu_data, write_offset + offsets.reflectance,                as_span(material->reflectance));
        write(gpu_data, write_offset + offsets.base_color ,                as_span(material->base_color ));
        write(gpu_data, write_offset + offsets.emissive   ,                as_span(material->emissive   ));
        write(gpu_data, write_offset + offsets.opacity    ,                as_span(material->opacity    ));
        write(gpu_data, write_offset + offsets.normal_texture_scale,       as_span(material->normal_texture_scale));
        write(gpu_data, write_offset + offsets.occlusion_texture_strength, as_span(material->occlusion_texture_strength));
        write(gpu_data, write_offset + offsets.base_color_texture,         as_span(base_color_shader_handle));
        write(gpu_data, write_offset + offsets.metallic_roughness_texture, as_span(metallic_roughness_shader_handle));
        write(gpu_data, write_offset + offsets.normal_texture,             as_span(normal_shader_handle));
        write(gpu_data, write_offset + offsets.occlusion_texture,          as_span(occlusion_shader_handle));
        write(gpu_data, write_offset + offsets.emission_texture,           as_span(emission_shader_handle));

        write_offset += entry_size;
        ++material_index;
    }
    buffer_range.bytes_written(write_offset);
    buffer_range.close();

    // SPDLOG_LOGGER_TRACE(log_material_buffer, "wrote {} entries to material buffer", material_index);

    return buffer_range;
}

} // namespace erhe::scene_renderer
