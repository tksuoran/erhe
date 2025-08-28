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

        .emissive_texture           = material_struct.add_uvec2("emissive_texture"          )->get_offset_in_parent(),
        .opacity                    = material_struct.add_float("opacity"                   )->get_offset_in_parent(),
        .normal_texture_scale       = material_struct.add_float("normal_texture_scale"      )->get_offset_in_parent(),

        .base_color_rotation_scale         = material_struct.add_vec4("base_color_rotation_scale"        )->get_offset_in_parent(),
        .metallic_roughness_rotation_scale = material_struct.add_vec4("metallic_roughness_rotation_scale")->get_offset_in_parent(),
        .normal_rotation_scale             = material_struct.add_vec4("normal_rotation_scale"            )->get_offset_in_parent(),
        .occlusion_rotation_scale          = material_struct.add_vec4("occlusion_rotation_scale"         )->get_offset_in_parent(),
        .emissive_rotation_scale           = material_struct.add_vec4("emissive_rotation_scale"          )->get_offset_in_parent(),
        .base_color_offset                 = material_struct.add_vec2("base_color_offset"                )->get_offset_in_parent(),
        .metallic_roughness_offset         = material_struct.add_vec2("metallic_roughness_offset"        )->get_offset_in_parent(),
        .normal_offset                     = material_struct.add_vec2("normal_offset"                    )->get_offset_in_parent(),
        .occlusion_offset                  = material_struct.add_vec2("occlusion_offset"                 )->get_offset_in_parent(),
        .emissive_offset                   = material_struct.add_vec2("emissive_offset"                  )->get_offset_in_parent(),

        .occlusion_texture_strength = material_struct.add_float("occlusion_texture_strength")->get_offset_in_parent(),
        .unlit                      = material_struct.add_uint ("unlit")->get_offset_in_parent(),
    }
{
    const auto& ini = erhe::configuration::get_ini_file_section(c_erhe_config_file_path, "renderer");
    ini.get("max_material_count", max_material_count);

    material_block.add_struct("materials", &material_struct, erhe::graphics::Shader_resource::unsized_array);
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

    class Texture_sampler_data
    {
    public:
        uint64_t shader_handle;
        float    rotation_scale[4];
        float    offset[2];
    };

    uint32_t material_index = 0;
    for (const auto& material : materials) {
        ERHE_VERIFY(material);
        memset(reinterpret_cast<uint8_t*>(gpu_data.data()) + write_offset, 0, entry_size);
        using erhe::graphics::as_span;
        using erhe::graphics::write;

        auto get_texture_sampler_shader_handle = [this, &texture_heap](const erhe::primitive::Material_texture_sampler& data) -> Texture_sampler_data
        {
            const float     c = std::cos(data.rotation);
            const float     s = std::sin(data.rotation);
            const glm::mat2 rotation{c, s, -s, c};
            const glm::mat2 scale{data.scale.x, 0.0f, 0.0f, data.scale.y};
            const glm::mat2 m = rotation * scale;
            Texture_sampler_data result{
                .rotation_scale = { m[0][0], m[0][1], m[1][0], m[1][1] }, // Packing order: c0r0, c0r1, c1r0, c1r1
                .offset         = { data.offset.x, data.offset.y }
            };
            if (data.texture) {
                const erhe::graphics::Sampler* sampler = data.sampler ? data.sampler.get() : &m_linear_sampler;
                result.shader_handle = texture_heap.allocate(data.texture.get(), sampler);
                ERHE_VERIFY(result.shader_handle != erhe::graphics::invalid_texture_handle);
            } else {
                result.shader_handle = erhe::graphics::invalid_texture_handle;
            }
            return result;
        };

        const erhe::primitive::Material_data&             material_data             = material->data;
        const erhe::primitive::Material_texture_samplers& material_texture_samplers = material_data.texture_samplers;
        const Texture_sampler_data base_color         = get_texture_sampler_shader_handle(material_texture_samplers.base_color);
        const Texture_sampler_data metallic_roughness = get_texture_sampler_shader_handle(material_texture_samplers.metallic_roughness);
        const Texture_sampler_data normal             = get_texture_sampler_shader_handle(material_texture_samplers.normal);
        const Texture_sampler_data occlusion          = get_texture_sampler_shader_handle(material_texture_samplers.occlusion);
        const Texture_sampler_data emissive           = get_texture_sampler_shader_handle(material_texture_samplers.emissive);

        material->material_buffer_index = material_index;

        uint32_t unlit_value = material_data.unlit ? 1 : 0;

        write(gpu_data, write_offset + offsets.roughness  ,                as_span(material_data.roughness  ));
        write(gpu_data, write_offset + offsets.metallic   ,                as_span(material_data.metallic   ));
        write(gpu_data, write_offset + offsets.reflectance,                as_span(material_data.reflectance));

        write(gpu_data, write_offset + offsets.base_color ,                as_span(material_data.base_color ));
        write(gpu_data, write_offset + offsets.emissive   ,                as_span(material_data.emissive   ));

        write(gpu_data, write_offset + offsets.base_color_texture,         as_span(base_color.shader_handle));
        write(gpu_data, write_offset + offsets.metallic_roughness_texture, as_span(metallic_roughness.shader_handle));

        write(gpu_data, write_offset + offsets.normal_texture,             as_span(normal.shader_handle));
        write(gpu_data, write_offset + offsets.occlusion_texture,          as_span(occlusion.shader_handle));

        write(gpu_data, write_offset + offsets.emissive_texture,           as_span(emissive.shader_handle));
        write(gpu_data, write_offset + offsets.opacity,                    as_span(material_data.opacity    ));
        write(gpu_data, write_offset + offsets.normal_texture_scale,       as_span(material_data.normal_texture_scale));

        write(gpu_data, write_offset + offsets.base_color_rotation_scale,         as_span(base_color        .rotation_scale)); // uvec4
        write(gpu_data, write_offset + offsets.metallic_roughness_rotation_scale, as_span(metallic_roughness.rotation_scale)); // uvec4
        write(gpu_data, write_offset + offsets.normal_rotation_scale,             as_span(normal            .rotation_scale)); // uvec4
        write(gpu_data, write_offset + offsets.occlusion_rotation_scale,          as_span(occlusion         .rotation_scale)); // uvec4
        write(gpu_data, write_offset + offsets.emissive_rotation_scale,           as_span(emissive          .rotation_scale)); // uvec4

        write(gpu_data, write_offset + offsets.base_color_offset,                 as_span(base_color        .offset));         // uvec2
        write(gpu_data, write_offset + offsets.metallic_roughness_offset,         as_span(metallic_roughness.offset));         // uvec2

        write(gpu_data, write_offset + offsets.normal_offset,                     as_span(normal            .offset));         // uvec2
        write(gpu_data, write_offset + offsets.occlusion_offset,                  as_span(occlusion         .offset));         // uvec2

        write(gpu_data, write_offset + offsets.emissive_offset,                   as_span(emissive          .offset));         // uvec2
        write(gpu_data, write_offset + offsets.occlusion_texture_strength,        as_span(material_data.occlusion_texture_strength));
        write(gpu_data, write_offset + offsets.unlit,                             as_span(unlit_value));

        write_offset += entry_size;
        ++material_index;
    }
    buffer_range.bytes_written(write_offset);
    buffer_range.close();

    // SPDLOG_LOGGER_TRACE(log_material_buffer, "wrote {} entries to material buffer", material_index);

    return buffer_range;
}

} // namespace erhe::scene_renderer
