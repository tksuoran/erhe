// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/material_buffer.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"
#include "erhe_renderer/renderer_config.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_hash/hash.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <cmath>
#include <cstring>

namespace erhe::scene_renderer {

Material_interface::Material_interface(erhe::graphics::Device& graphics_device, const int max_material_count)
    : material_block{
        graphics_device,
        {
            .name          = "material",
            .binding_point = material_buffer_binding_point,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
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
        .alpha_cutoff               = material_struct.add_float("alpha_cutoff"              )->get_offset_in_parent(),

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

        // occlusion_texture_strength leaves the struct at a vec2-aligned
        // tail; ior + transmission + bxdf_model fill it to the next
        // 16-byte boundary.
        .ior                        = material_struct.add_float("ior"                       )->get_offset_in_parent(),
        .transmission               = material_struct.add_float("transmission"              )->get_offset_in_parent(),
        .bxdf_model                 = material_struct.add_uint ("bxdf_model"                )->get_offset_in_parent(),

        // The normal texture decode: `texel * scale + bias`, both vec4 and
        // vec4-aligned, so the struct size stays a multiple of 16 bytes.
        .normal_texture_decode_scale = material_struct.add_vec4 ("normal_texture_decode_scale")->get_offset_in_parent(),
        .normal_texture_decode_bias  = material_struct.add_vec4 ("normal_texture_decode_bias" )->get_offset_in_parent(),

        // The channel each scalar input reads out of its slot's texture:
        // (metallic, roughness, occlusion, opacity). uvec4, vec4-aligned.
        .texture_channels            = material_struct.add_uvec4("texture_channels"           )->get_offset_in_parent(),
    }
    , max_material_count{static_cast<std::size_t>(max_material_count)}
{
    material_block.add_struct("materials", &material_struct, erhe::graphics::Shader_resource::unsized_array);
}

Material_sampler_cache::Material_sampler_cache(erhe::graphics::Device& graphics_device)
    : m_graphics_device{graphics_device}
{
}

auto Material_sampler_cache::get(const erhe::primitive::Material_sampler_state& state) -> const erhe::graphics::Sampler&
{
    for (const auto& entry : m_samplers) {
        if (entry.first == state) {
            return *entry.second;
        }
    }
    m_samplers.emplace_back(state, std::make_unique<erhe::graphics::Sampler>(m_graphics_device, erhe::primitive::to_sampler_create_info(state)));
    return *m_samplers.back().second;
}

auto gather_material_record_inputs(
    const erhe::primitive::Material& material,
    Material_sampler_cache&          sampler_cache
) -> Material_record_inputs
{
    const erhe::primitive::Material_values data = material.get_values();

    Material_record_inputs inputs{};
    inputs.roughness                  = data.roughness;
    inputs.metallic                   = data.metallic;
    inputs.reflectance                = data.reflectance;
    inputs.base_color                 = data.base_color;
    inputs.opacity                    = data.opacity;
    inputs.emissive                   = data.emissive;
    inputs.normal_texture_scale       = data.normal_texture_scale;
    inputs.alpha_cutoff               = data.alpha_cutoff;
    inputs.occlusion_texture_strength = data.occlusion_texture_strength;
    inputs.ior                        = data.ior;
    inputs.transmission               = data.transmission;
    inputs.bxdf_model                 = static_cast<uint32_t>(data.bxdf_model);
    inputs.normal_texture_decode_scale = data.normal_texture_decode_scale;
    inputs.normal_texture_decode_bias  = data.normal_texture_decode_bias;
    inputs.texture_channels            = glm::uvec4{
        erhe::primitive::to_uint32(data.metallic_channel),
        erhe::primitive::to_uint32(data.roughness_channel),
        erhe::primitive::to_uint32(data.occlusion_channel),
        erhe::primitive::to_uint32(data.opacity_channel)
    };

    const auto gather_texture = [&sampler_cache](
        const erhe::primitive::Material_texture_sampler& texture_sampler
    ) -> Material_texture_record_inputs
    {
        const float     c = std::cos(texture_sampler.rotation);
        const float     s = std::sin(texture_sampler.rotation);
        const glm::mat2 rotation{c, s, -s, c};
        const glm::mat2 scale{texture_sampler.scale.x, 0.0f, 0.0f, texture_sampler.scale.y};
        const glm::mat2 m = rotation * scale;
        // The slot's texture reference resolves to a live texture every time
        // it is read: a plain Texture returns itself, an editor Graph_texture
        // returns its most recently baked output.
        const erhe::graphics::Texture* texture = texture_sampler.texture_reference
            ? texture_sampler.texture_reference->get_referenced_texture()
            : nullptr;
        return Material_texture_record_inputs{
            .texture        = texture,
            // Only meaningful together with a texture, and left null without
            // one so two textureless slots hash alike.
            .sampler        = (texture != nullptr) ? &sampler_cache.get(texture_sampler.sampler) : nullptr,
            .rotation_scale = { m[0][0], m[0][1], m[1][0], m[1][1] }, // Packing order: c0r0, c0r1, c1r0, c1r1
            .offset         = { texture_sampler.offset.x, texture_sampler.offset.y }
        };
    };

    const erhe::primitive::Material_texture_samplers& texture_samplers = material.data.texture_samplers;
    inputs.base_color_texture         = gather_texture(texture_samplers.base_color);
    inputs.metallic_roughness_texture = gather_texture(texture_samplers.metallic_roughness);
    inputs.normal_texture             = gather_texture(texture_samplers.normal);
    inputs.occlusion_texture          = gather_texture(texture_samplers.occlusion);
    inputs.emissive_texture           = gather_texture(texture_samplers.emissive);
    return inputs;
}

Material_buffer::Material_buffer(erhe::graphics::Device& graphics_device, Material_interface& material_interface)
    : m_graphics_device {graphics_device}
    , m_material_interface{material_interface}
    , m_sampler_cache{graphics_device}
{
}

auto Material_buffer::get_record_byte_count() const -> std::size_t
{
    return m_material_interface.material_struct.get_size_bytes();
}

auto Material_buffer::get_content_hash(const erhe::primitive::Material* material) const -> uint64_t
{
    if (material == nullptr) {
        return 0;
    }
    const Material_record_inputs inputs = gather_material_record_inputs(*material, m_sampler_cache);
    return erhe::hash::hash(&inputs, sizeof(inputs));
}

void Material_buffer::write_record(
    const std::span<std::byte>    gpu_data,
    const std::size_t             write_offset,
    const Material_record_inputs& inputs,
    erhe::graphics::Texture_heap& texture_heap
)
{
    using erhe::graphics::as_span;
    using erhe::graphics::write;

    const Material_struct& offsets = m_material_interface.offsets;

    const auto shader_handle = [&texture_heap](const Material_texture_record_inputs& texture_inputs) -> uint64_t
    {
        if (texture_inputs.texture == nullptr) {
            return erhe::graphics::invalid_texture_handle;
        }
        const uint64_t handle = texture_heap.allocate(texture_inputs.texture, texture_inputs.sampler);
        ERHE_VERIFY(handle != erhe::graphics::invalid_texture_handle);
        return handle;
    };

    const uint64_t base_color_handle         = shader_handle(inputs.base_color_texture);
    const uint64_t metallic_roughness_handle = shader_handle(inputs.metallic_roughness_texture);
    const uint64_t normal_handle             = shader_handle(inputs.normal_texture);
    const uint64_t occlusion_handle          = shader_handle(inputs.occlusion_texture);
    const uint64_t emissive_handle           = shader_handle(inputs.emissive_texture);

    write(gpu_data, write_offset + offsets.roughness  ,                as_span(inputs.roughness  ));
    write(gpu_data, write_offset + offsets.metallic   ,                as_span(inputs.metallic   ));
    write(gpu_data, write_offset + offsets.reflectance,                as_span(inputs.reflectance));

    write(gpu_data, write_offset + offsets.base_color ,                as_span(inputs.base_color ));
    write(gpu_data, write_offset + offsets.emissive   ,                as_span(inputs.emissive   ));

    write(gpu_data, write_offset + offsets.base_color_texture,         as_span(base_color_handle));
    write(gpu_data, write_offset + offsets.metallic_roughness_texture, as_span(metallic_roughness_handle));

    write(gpu_data, write_offset + offsets.normal_texture,             as_span(normal_handle));
    write(gpu_data, write_offset + offsets.occlusion_texture,          as_span(occlusion_handle));

    write(gpu_data, write_offset + offsets.emissive_texture,           as_span(emissive_handle));
    write(gpu_data, write_offset + offsets.opacity,                    as_span(inputs.opacity));
    write(gpu_data, write_offset + offsets.normal_texture_scale,       as_span(inputs.normal_texture_scale));
    write(gpu_data, write_offset + offsets.alpha_cutoff,               as_span(inputs.alpha_cutoff));

    write(gpu_data, write_offset + offsets.base_color_rotation_scale,         as_span(inputs.base_color_texture        .rotation_scale)); // uvec4
    write(gpu_data, write_offset + offsets.metallic_roughness_rotation_scale, as_span(inputs.metallic_roughness_texture.rotation_scale)); // uvec4
    write(gpu_data, write_offset + offsets.normal_rotation_scale,             as_span(inputs.normal_texture            .rotation_scale)); // uvec4
    write(gpu_data, write_offset + offsets.occlusion_rotation_scale,          as_span(inputs.occlusion_texture         .rotation_scale)); // uvec4
    write(gpu_data, write_offset + offsets.emissive_rotation_scale,           as_span(inputs.emissive_texture          .rotation_scale)); // uvec4

    write(gpu_data, write_offset + offsets.base_color_offset,                 as_span(inputs.base_color_texture        .offset));         // uvec2
    write(gpu_data, write_offset + offsets.metallic_roughness_offset,         as_span(inputs.metallic_roughness_texture.offset));         // uvec2

    write(gpu_data, write_offset + offsets.normal_offset,                     as_span(inputs.normal_texture            .offset));         // uvec2
    write(gpu_data, write_offset + offsets.occlusion_offset,                  as_span(inputs.occlusion_texture         .offset));         // uvec2

    write(gpu_data, write_offset + offsets.emissive_offset,                   as_span(inputs.emissive_texture          .offset));         // uvec2
    write(gpu_data, write_offset + offsets.occlusion_texture_strength,        as_span(inputs.occlusion_texture_strength));
    write(gpu_data, write_offset + offsets.ior,                               as_span(inputs.ior));
    write(gpu_data, write_offset + offsets.transmission,                      as_span(inputs.transmission));
    write(gpu_data, write_offset + offsets.bxdf_model,                        as_span(inputs.bxdf_model));
    write(gpu_data, write_offset + offsets.normal_texture_decode_scale,       as_span(inputs.normal_texture_decode_scale));
    write(gpu_data, write_offset + offsets.normal_texture_decode_bias,        as_span(inputs.normal_texture_decode_bias));
    write(gpu_data, write_offset + offsets.texture_channels,                  as_span(inputs.texture_channels));
}

void Material_buffer::write_records(
    const std::span<std::byte>                              gpu_data,
    erhe::graphics::Texture_heap&                           texture_heap,
    const std::span<const erhe::primitive::Material* const> slot_materials
)
{
    ERHE_PROFILE_FUNCTION();

    const std::size_t entry_size = get_record_byte_count();
    ERHE_VERIFY(gpu_data.size() >= slot_materials.size() * entry_size);

    // Zero the whole span - holes and the alignment tail included - so a slot
    // that is not live reads as an all-zero record rather than as whatever the
    // previous payload written into this copy left behind.
    std::memset(gpu_data.data(), 0, gpu_data.size());

    std::size_t write_offset = 0;
    for (const erhe::primitive::Material* material : slot_materials) {
        if (material != nullptr) {
            const Material_record_inputs inputs = gather_material_record_inputs(*material, m_sampler_cache);
            write_record(gpu_data, write_offset, inputs, texture_heap);
        }
        write_offset += entry_size;
    }
}

} // namespace erhe::scene_renderer
