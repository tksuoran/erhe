// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/material_buffer.hpp"
#include "erhe_scene_renderer/material_set.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"
#include "erhe_renderer/renderer_config.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/generated/graphics_config.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_profile/profile.hpp"

#include <glm/gtc/packing.hpp>

#include <cstring>
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
    , max_material_count{static_cast<std::size_t>(max_material_count)}
    , half_records{use_half_material_records(graphics_device)}
{
    // The float fields take the half layout's types when the device stores
    // them as 16-bit floats (half_records); the member names and order are
    // the same in both layouts.
    const auto add_f1 = [this](const char* name) { return half_records ? material_struct.add_float16(name) : material_struct.add_float(name); };
    const auto add_f2 = [this](const char* name) { return half_records ? material_struct.add_f16vec2(name) : material_struct.add_vec2 (name); };
    const auto add_f4 = [this](const char* name) { return half_records ? material_struct.add_f16vec4(name) : material_struct.add_vec4 (name); };
    offsets = Material_struct{
        .roughness                  = add_f2("roughness"                 )->get_offset_in_parent(),
        .metallic                   = add_f1("metallic"                  )->get_offset_in_parent(),
        .reflectance                = add_f1("reflectance"               )->get_offset_in_parent(),

        .base_color                 = add_f4("base_color"                )->get_offset_in_parent(),
        .emissive                   = add_f4("emissive"                  )->get_offset_in_parent(),

        .base_color_texture         = material_struct.add_uvec2("base_color_texture"        )->get_offset_in_parent(),
        .metallic_roughness_texture = material_struct.add_uvec2("metallic_roughness_texture")->get_offset_in_parent(),

        .normal_texture             = material_struct.add_uvec2("normal_texture"            )->get_offset_in_parent(),
        .occlusion_texture          = material_struct.add_uvec2("occlusion_texture"         )->get_offset_in_parent(),

        .emissive_texture           = material_struct.add_uvec2("emissive_texture"          )->get_offset_in_parent(),
        .opacity                    = add_f1("opacity"                   )->get_offset_in_parent(),
        .normal_texture_scale       = add_f1("normal_texture_scale"      )->get_offset_in_parent(),
        .alpha_cutoff               = add_f1("alpha_cutoff"              )->get_offset_in_parent(),

        .base_color_rotation_scale         = add_f4("base_color_rotation_scale"        )->get_offset_in_parent(),
        .metallic_roughness_rotation_scale = add_f4("metallic_roughness_rotation_scale")->get_offset_in_parent(),
        .normal_rotation_scale             = add_f4("normal_rotation_scale"            )->get_offset_in_parent(),
        .occlusion_rotation_scale          = add_f4("occlusion_rotation_scale"         )->get_offset_in_parent(),
        .emissive_rotation_scale           = add_f4("emissive_rotation_scale"          )->get_offset_in_parent(),
        .base_color_offset                 = add_f2("base_color_offset"                )->get_offset_in_parent(),
        .metallic_roughness_offset         = add_f2("metallic_roughness_offset"        )->get_offset_in_parent(),
        .normal_offset                     = add_f2("normal_offset"                    )->get_offset_in_parent(),
        .occlusion_offset                  = add_f2("occlusion_offset"                 )->get_offset_in_parent(),
        .emissive_offset                   = add_f2("emissive_offset"                  )->get_offset_in_parent(),

        .occlusion_texture_strength = add_f1("occlusion_texture_strength")->get_offset_in_parent(),

        // occlusion_texture_strength leaves the struct at a vec2-aligned
        // tail; ior + transmission + bxdf_model fill it to the next
        // 16-byte boundary.
        .ior                        = add_f1("ior"                       )->get_offset_in_parent(),
        .transmission               = add_f1("transmission"              )->get_offset_in_parent(),
        .bxdf_model                 = material_struct.add_uint ("bxdf_model"                )->get_offset_in_parent(),

        // The normal texture decode: `texel * scale + bias`, both vec4 and
        // vec4-aligned, so the struct size stays a multiple of 16 bytes.
        .normal_texture_decode_scale = add_f4("normal_texture_decode_scale")->get_offset_in_parent(),
        .normal_texture_decode_bias  = add_f4("normal_texture_decode_bias" )->get_offset_in_parent(),

        // The channel each scalar input reads out of its slot's texture:
        // (metallic, roughness, occlusion, opacity). uvec4, vec4-aligned.
        .texture_channels            = material_struct.add_uvec4("texture_channels"           )->get_offset_in_parent(),

        // Where the base color and the fragment alpha come from
        // (erhe::primitive::Material_input_source): (base_color, opacity).
        // The padding after it keeps the struct size a multiple of 16 bytes.
        .input_sources               = material_struct.add_uvec2("input_sources"              )->get_offset_in_parent(),
        .input_sources_padding       = material_struct.add_uvec2("input_sources_padding"      )->get_offset_in_parent(),
    };
    material_block.add_struct("materials", &material_struct, erhe::graphics::Shader_resource::unsized_array);
    log_startup->info(
        "Material records: {} float fields, {} bytes per record (material_record_half '{}', storage_buffer_16bit_access {}, shader_float16 {})",
        half_records ? "16-bit" : "32-bit",
        material_struct.get_size_bytes(),
        graphics_device.get_graphics_config().material_record_half,
        graphics_device.get_info().storage_buffer_16bit_access,
        graphics_device.get_info().shader_float16
    );
}

auto use_half_material_records(const erhe::graphics::Device& graphics_device) -> bool
{
    const erhe::graphics::Device_info& info = graphics_device.get_info();
    if (!info.storage_buffer_16bit_access || !info.shader_float16) {
        return false;
    }
    const std::string& setting = graphics_device.get_graphics_config().material_record_half;
    if (setting == "on") {
        return true;
    }
    if (setting == "off") {
        return false;
    }
#if defined(ERHE_OS_ANDROID)
    return true;
#else
    return false;
#endif
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
    inputs.input_sources               = glm::uvec2{
        erhe::primitive::to_uint32(data.base_color_source),
        erhe::primitive::to_uint32(data.opacity_source)
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

    const erhe::primitive::Material_texture_samplers& texture_samplers = material.get_data().texture_samplers;
    inputs.base_color_texture         = gather_texture(texture_samplers.base_color);
    inputs.metallic_roughness_texture = gather_texture(texture_samplers.metallic_roughness);
    inputs.normal_texture             = gather_texture(texture_samplers.normal);
    inputs.occlusion_texture          = gather_texture(texture_samplers.occlusion);
    inputs.emissive_texture           = gather_texture(texture_samplers.emissive);
    return inputs;
}

auto get_default_material_record_inputs() -> Material_record_inputs
{
    // Built from the Material_values defaults so the unbound look tracks
    // erhe's own material defaults; only the base color differs from them.
    const erhe::primitive::Material_values defaults{};

    Material_record_inputs inputs{};
    inputs.roughness                   = defaults.roughness;
    inputs.metallic                    = defaults.metallic;
    inputs.reflectance                 = defaults.reflectance;
    inputs.base_color                  = glm::vec3{0.18f, 0.18f, 0.18f};
    inputs.opacity                     = defaults.opacity;
    inputs.emissive                    = defaults.emissive;
    inputs.normal_texture_scale        = defaults.normal_texture_scale;
    inputs.alpha_cutoff                = defaults.alpha_cutoff;
    inputs.occlusion_texture_strength  = defaults.occlusion_texture_strength;
    inputs.ior                         = defaults.ior;
    inputs.transmission                = defaults.transmission;
    inputs.bxdf_model                  = static_cast<uint32_t>(defaults.bxdf_model);
    inputs.normal_texture_decode_scale = defaults.normal_texture_decode_scale;
    inputs.normal_texture_decode_bias  = defaults.normal_texture_decode_bias;
    inputs.texture_channels            = glm::uvec4{
        erhe::primitive::to_uint32(defaults.metallic_channel),
        erhe::primitive::to_uint32(defaults.roughness_channel),
        erhe::primitive::to_uint32(defaults.occlusion_channel),
        erhe::primitive::to_uint32(defaults.opacity_channel)
    };
    inputs.input_sources               = glm::uvec2{
        erhe::primitive::to_uint32(defaults.base_color_source),
        erhe::primitive::to_uint32(defaults.opacity_source)
    };
    // Every texture slot stays empty, so the handles resolve to
    // invalid_texture_handle and no heap allocation is made.
    return inputs;
}

auto get_vertex_colored_default_material_record_inputs() -> Material_record_inputs
{
    Material_record_inputs inputs = get_default_material_record_inputs();
    inputs.base_color = glm::vec3{1.0f, 1.0f, 1.0f};
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

    // The float fields, as 32-bit floats or as 16-bit floats in the half
    // layout (Material_interface::half_records).
    const bool half = m_material_interface.half_records;
    const auto write_floats = [&](const std::size_t field_offset, const float* values, const std::size_t count) {
        std::byte* const destination = gpu_data.data() + write_offset + field_offset;
        if (half) {
            for (std::size_t i = 0; i < count; ++i) {
                const uint16_t packed = glm::packHalf1x16(values[i]);
                std::memcpy(destination + (2 * i), &packed, sizeof(packed));
            }
        } else {
            std::memcpy(destination, values, count * sizeof(float));
        }
    };

    write_floats(offsets.roughness,   &inputs.roughness.x,   2);
    write_floats(offsets.metallic,    &inputs.metallic,      1);
    write_floats(offsets.reflectance, &inputs.reflectance,   1);

    write_floats(offsets.base_color,  &inputs.base_color.x,  3);
    write_floats(offsets.emissive,    &inputs.emissive.x,    3);

    write(gpu_data, write_offset + offsets.base_color_texture,         as_span(base_color_handle));
    write(gpu_data, write_offset + offsets.metallic_roughness_texture, as_span(metallic_roughness_handle));

    write(gpu_data, write_offset + offsets.normal_texture,             as_span(normal_handle));
    write(gpu_data, write_offset + offsets.occlusion_texture,          as_span(occlusion_handle));

    write(gpu_data, write_offset + offsets.emissive_texture,           as_span(emissive_handle));
    write_floats(offsets.opacity,              &inputs.opacity,              1);
    write_floats(offsets.normal_texture_scale, &inputs.normal_texture_scale, 1);
    write_floats(offsets.alpha_cutoff,         &inputs.alpha_cutoff,         1);

    write_floats(offsets.base_color_rotation_scale,         inputs.base_color_texture        .rotation_scale, 4);
    write_floats(offsets.metallic_roughness_rotation_scale, inputs.metallic_roughness_texture.rotation_scale, 4);
    write_floats(offsets.normal_rotation_scale,             inputs.normal_texture            .rotation_scale, 4);
    write_floats(offsets.occlusion_rotation_scale,          inputs.occlusion_texture         .rotation_scale, 4);
    write_floats(offsets.emissive_rotation_scale,           inputs.emissive_texture          .rotation_scale, 4);

    write_floats(offsets.base_color_offset,                 inputs.base_color_texture        .offset, 2);
    write_floats(offsets.metallic_roughness_offset,         inputs.metallic_roughness_texture.offset, 2);
    write_floats(offsets.normal_offset,                     inputs.normal_texture            .offset, 2);
    write_floats(offsets.occlusion_offset,                  inputs.occlusion_texture         .offset, 2);
    write_floats(offsets.emissive_offset,                   inputs.emissive_texture          .offset, 2);

    write_floats(offsets.occlusion_texture_strength, &inputs.occlusion_texture_strength, 1);
    write_floats(offsets.ior,                        &inputs.ior,                        1);
    write_floats(offsets.transmission,               &inputs.transmission,               1);
    write(gpu_data, write_offset + offsets.bxdf_model,                        as_span(inputs.bxdf_model));
    write_floats(offsets.normal_texture_decode_scale, &inputs.normal_texture_decode_scale.x, 4);
    write_floats(offsets.normal_texture_decode_bias,  &inputs.normal_texture_decode_bias.x,  4);
    write(gpu_data, write_offset + offsets.texture_channels,                  as_span(inputs.texture_channels));
    write(gpu_data, write_offset + offsets.input_sources,                     as_span(inputs.input_sources));
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

    // The reserved default slots, written once per update from the shared
    // default inputs; every other null entry is a hole and stays zeroed.
    if (!slot_materials.empty()) {
        ERHE_VERIFY(slot_materials.size() >= Material_set::first_material_slot_index);
        ERHE_VERIFY(slot_materials[Material_set::default_material_slot_index] == nullptr);
        ERHE_VERIFY(slot_materials[Material_set::vertex_colored_default_material_slot_index] == nullptr);
        write_record(gpu_data, Material_set::default_material_slot_index * entry_size, get_default_material_record_inputs(), texture_heap);
        write_record(gpu_data, Material_set::vertex_colored_default_material_slot_index * entry_size, get_vertex_colored_default_material_record_inputs(), texture_heap);
    }

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
