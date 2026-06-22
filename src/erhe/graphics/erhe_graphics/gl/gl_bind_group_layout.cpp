#include "erhe_graphics/gl/gl_bind_group_layout.hpp"
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
    // Compute the texture heap's texture-unit range in the shader-visible
    // layout. Two cases:
    //
    // 1. An explicit texture-heap combined_image_sampler binding exists
    //    (imgui's s_textures, rendering_test's s_textures): the heap IS
    //    that binding. Its base unit is the binding_point and its count
    //    is the binding's array_size. No implicit s_texture is added.
    //
    // 2. No explicit texture-heap binding (scene renderer, hextiles): the
    //    implicit s_texture fills the texture units past the last
    //    dedicated combined_image_sampler. Base is the max dedicated
    //    end (binding_point + descriptor_count) and count is whatever
    //    is left in max_per_stage_descriptor_samplers.
    const uint32_t max_units = device.get_info().max_per_stage_descriptor_samplers;
    uint32_t max_dedicated_sampler_unit_plus_one = 0;
    bool     has_explicit_heap_sampler           = false;
    uint32_t explicit_heap_base_unit             = 0;
    uint32_t explicit_heap_unit_count            = 0;
    for (const Bind_group_layout_binding& binding : create_info.bindings) {
        if (binding.type != Binding_type::combined_image_sampler) {
            continue;
        }
        if (binding.is_texture_heap) {
            has_explicit_heap_sampler = true;
            explicit_heap_base_unit   = binding.binding_point;
            explicit_heap_unit_count  = binding.array_size;
        } else {
            const uint32_t end = binding.binding_point + binding.descriptor_count;
            if (end > max_dedicated_sampler_unit_plus_one) {
                max_dedicated_sampler_unit_plus_one = end;
            }
        }
    }
    if (has_explicit_heap_sampler) {
        m_texture_heap_base_unit  = explicit_heap_base_unit;
        m_texture_heap_unit_count = explicit_heap_unit_count;
    } else {
        m_texture_heap_base_unit  = max_dedicated_sampler_unit_plus_one;
        m_texture_heap_unit_count = (m_texture_heap_base_unit < max_units)
            ? (max_units - m_texture_heap_base_unit)
            : 0;
    }

    // Mirror every combined_image_sampler binding into the default uniform
    // block as a "uniform sampler* name;" declaration, and every storage_image
    // binding as a "layout(binding = N, <format>) uniform image2D name;"
    // declaration. The preamble emitter reads these declarations and injects
    // them into the GLSL source; the GL <4.30 fallback and the Metal
    // shader-stages prototype also use the sampler declarations (the former for
    // sampler-unit location lookup, the latter for texture-heap classification).
    //
    // Shader_resource::add_sampler forbids dedicated_texture_unit on
    // texture-heap samplers (they auto-allocate from m_location). For the
    // auto-allocation to land on the caller's binding_point, bindings
    // must be processed in ascending binding_point order -- which today
    // they always are at every call site.
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
            // raw binding_point as the image unit (GL has a separate image-unit
            // namespace; no sampler-binding offset). set_storage_image() binds
            // image unit N directly to match.
            m_default_uniform_block.add_image(
                binding.name,
                binding.glsl_type,
                binding.image_format,
                static_cast<int>(binding.binding_point)
            );
        }
    }

    // On the GL sampler-array path the texture heap reads its materials
    // from an "s_texture" sampler2D array in the default uniform block.
    // If no caller-supplied binding already declared a texture-heap
    // sampler (scene renderer, hextiles), add it here implicitly so
    // callers don't have to replicate the sizing logic. Skip the
    // implicit add if any existing binding is already named "s_texture"
    // (Text_renderer uses that name for its single font sampler). On
    // the bindless path the heap uses sampler2D(handle) inline and
    // needs no declaration, so skip the implicit add.
    if ((device.get_info().texture_heap_path == Texture_heap_path::opengl_sampler_array) &&
        !binding_has_texture_heap_sampler(create_info) &&
        !binding_has_sampler_named(create_info, "s_texture") &&
        (m_texture_heap_unit_count > 0))
    {
        const uint32_t array_size = std::min(m_texture_heap_unit_count, uint32_t{64});
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

auto Bind_group_layout_impl::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_debug_label;
}

auto Bind_group_layout_impl::get_sampler_binding_offset() const -> uint32_t
{
    return 0; // GL has separate namespaces for samplers and buffers
}

auto Bind_group_layout_impl::get_texture_heap_base_unit() const -> uint32_t
{
    return m_texture_heap_base_unit;
}

auto Bind_group_layout_impl::get_texture_heap_unit_count() const -> uint32_t
{
    return m_texture_heap_unit_count;
}

auto Bind_group_layout_impl::get_default_uniform_block() const -> const Shader_resource&
{
    return m_default_uniform_block;
}

} // namespace erhe::graphics
