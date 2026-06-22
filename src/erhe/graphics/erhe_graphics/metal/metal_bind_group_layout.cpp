#include "erhe_graphics/metal/metal_bind_group_layout.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include <algorithm>

namespace erhe::graphics {

namespace {

[[nodiscard]] auto binding_has_texture_heap_sampler(const Bind_group_layout_create_info& create_info) -> bool
{
    for (const Bind_group_layout_binding& binding : create_info.bindings) {
        if ((binding.type == Binding_type::combined_image_sampler) && binding.is_texture_heap) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto binding_has_sampler_named(const Bind_group_layout_create_info& create_info, std::string_view name) -> bool
{
    for (const Bind_group_layout_binding& binding : create_info.bindings) {
        if ((binding.type == Binding_type::combined_image_sampler) && (binding.name == name)) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

Bind_group_layout_impl::Bind_group_layout_impl(Device& device, const Bind_group_layout_create_info& create_info)
    : m_debug_label         {create_info.debug_label}
    , m_default_uniform_block{device}
{
    // Compute the sampler binding offset from the buffer bindings, matching
    // the Vulkan impl. Sampler bindings live in a separate per-type namespace
    // on the host side (so a buffer at binding_point 0 and a sampler at
    // binding_point 0 don't collide). The shader emitter and the Metal
    // shader-compile path both use this offset to translate the user-facing
    // sampler binding_point to a SPIRV / Metal binding number that does not
    // collide with the buffer bindings.
    uint32_t max_buffer_binding = 0;
    bool has_buffer_binding = false;
    for (const Bind_group_layout_binding& binding : create_info.bindings) {
        if ((binding.type == Binding_type::uniform_buffer) || (binding.type == Binding_type::storage_buffer)) {
            if (!has_buffer_binding || (binding.binding_point >= max_buffer_binding)) {
                max_buffer_binding = binding.binding_point;
                has_buffer_binding = true;
            }
        }
    }
    m_sampler_binding_offset = has_buffer_binding ? (max_buffer_binding + 1) : 0;

    // Mirror every combined_image_sampler binding into the default uniform
    // block as a "uniform sampler* name;" declaration, and every storage_image
    // binding as a "layout(binding = N, <format>) uniform image2D name;"
    // declaration. The Metal shader-stages prototype reads the sampler
    // declarations to classify samplers via Shader_resource::get_is_texture_heap()
    // and the preamble emitter injects the image declarations into the compute
    // GLSL source. Texture-heap samplers must auto-allocate their binding point
    // from m_location (Shader_resource::add_sampler asserts against a
    // dedicated_texture_unit on them); the auto-allocation lands on the
    // caller's binding_point as long as earlier bindings were processed
    // in ascending order.
    for (const Bind_group_layout_binding& binding : create_info.bindings) {
        if (binding.type == Binding_type::combined_image_sampler) {
            const std::optional<uint32_t> dedicated_texture_unit = binding.is_texture_heap
                ? std::optional<uint32_t>{}
                : std::optional<uint32_t>{binding.binding_point};
            m_default_uniform_block.add_sampler(
                binding.name,
                binding.glsl_type,
                binding.sampler_aspect,
                binding.is_texture_heap,
                dedicated_texture_unit,
                (binding.array_size > 0) ? std::optional<std::size_t>{binding.array_size} : std::optional<std::size_t>{}
            );
        } else if (binding.type == Binding_type::storage_image) {
            // Storage images are emitted into the GLSL preamble as
            // "layout(binding = N, <format>) uniform image2D name;" and use the
            // raw binding_point as the [[texture(N)]] slot (no sampler-binding
            // offset). compile_spirv_to_mtl_function pins msl_texture = binding
            // and set_storage_image() binds the same slot to match.
            m_default_uniform_block.add_image(
                binding.name,
                binding.glsl_type,
                binding.image_format,
                static_cast<int>(binding.binding_point)
            );
        }
    }

    // Metal argument-buffer path: if no caller-supplied binding declared
    // a texture-heap sampler (and no dedicated binding has already
    // claimed the s_texture name), add a default s_texture so Metal's
    // argument encoder has something to populate.
    if ((device.get_info().texture_heap_path == Texture_heap_path::metal_argument_buffer) &&
        !binding_has_texture_heap_sampler(create_info) &&
        !binding_has_sampler_named(create_info, "s_texture"))
    {
        uint32_t dedicated_sampler_count = 0;
        for (const Bind_group_layout_binding& binding : create_info.bindings) {
            if (binding.type == Binding_type::combined_image_sampler) {
                dedicated_sampler_count += binding.descriptor_count;
            }
        }
        const uint32_t max_units = device.get_info().max_per_stage_descriptor_samplers;
        const uint32_t available = (dedicated_sampler_count < max_units) ? (max_units - dedicated_sampler_count) : 0;
        if (available > 0) {
            const uint32_t array_size = std::min(available, uint32_t{64});
            m_default_uniform_block.add_sampler(
                "s_texture",
                Glsl_type::sampler_2d,
                Sampler_aspect::color,
                /*is_texture_heap=*/true,
                /*dedicated_texture_unit=*/std::optional<uint32_t>{},
                /*array_size=*/array_size
            );
        }
    }
}

auto Bind_group_layout_impl::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_debug_label;
}

auto Bind_group_layout_impl::get_sampler_binding_offset() const -> uint32_t
{
    return m_sampler_binding_offset;
}

auto Bind_group_layout_impl::get_default_uniform_block() const -> const Shader_resource&
{
    return m_default_uniform_block;
}

} // namespace erhe::graphics
