#include "erhe_graphics/metal4/metal4_bind_group_layout.hpp"
#include "erhe_graphics/metal4/metal4_device.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>
#include <vector>

namespace erhe::graphics {

namespace {

// Metal's per-stage limit on directly bound sampler states: [[sampler(N)]]
// requires 0 <= N <= 15.
constexpr uint32_t c_metal_max_dedicated_samplers = 16;

// Number of material (texture, sampler) pairs the argument-buffer texture heap
// holds. Unlike the GL sampler-array path, this does NOT come out of the
// per-stage sampler budget: dedicated samplers are direct
// [[texture(N)]]/[[sampler(M)]] bindings while the heap lives in its own
// argument-buffer index space, so the two never compete. Tier 2 argument
// buffers (every Metal device erhe targets) allow far more than this; the value
// matches the Vulkan descriptor-indexing path's
// Texture_heap_impl::m_max_textures so a scene that fits on one backend fits on
// the other. Texture_heap_impl refuses allocations past it.
constexpr uint32_t c_metal_texture_heap_slot_count = 4096;

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

// The argument descriptor texture type of a sampled image GLSL type (the
// fragment argument buffer, Bind_group_layout_impl::Fragment_argument).
[[nodiscard]] auto to_argument_texture_type(const Glsl_type glsl_type) -> MTL::TextureType
{
    switch (glsl_type) {
        case Glsl_type::sampler_1d:
        case Glsl_type::sampler_1d_shadow:
        case Glsl_type::int_sampler_1d:
        case Glsl_type::unsigned_int_sampler_1d:                   return MTL::TextureType1D;
        case Glsl_type::sampler_1d_array:
        case Glsl_type::sampler_1d_array_shadow:
        case Glsl_type::int_sampler_1d_array:
        case Glsl_type::unsigned_int_sampler_1d_array:             return MTL::TextureType1DArray;
        case Glsl_type::sampler_3d:
        case Glsl_type::int_sampler_3d:
        case Glsl_type::unsigned_int_sampler_3d:                   return MTL::TextureType3D;
        case Glsl_type::sampler_cube:
        case Glsl_type::sampler_cube_shadow:
        case Glsl_type::int_sampler_cube:
        case Glsl_type::unsigned_int_sampler_cube:                 return MTL::TextureTypeCube;
        case Glsl_type::sampler_cube_map_array:
        case Glsl_type::sampler_cube_map_array_shadow:
        case Glsl_type::int_sampler_cube_map_array:
        case Glsl_type::unsigned_int_sampler_cube_map_array:       return MTL::TextureTypeCubeArray;
        case Glsl_type::sampler_2d_array:
        case Glsl_type::sampler_2d_array_shadow:
        case Glsl_type::int_sampler_2d_array:
        case Glsl_type::unsigned_int_sampler_2d_array:             return MTL::TextureType2DArray;
        case Glsl_type::sampler_buffer:
        case Glsl_type::int_sampler_buffer:
        case Glsl_type::unsigned_int_sampler_buffer:               return MTL::TextureTypeTextureBuffer;
        case Glsl_type::sampler_2d_multisample:
        case Glsl_type::int_sampler_2d_multisample:
        case Glsl_type::unsigned_int_sampler_2d_multisample:       return MTL::TextureType2DMultisample;
        case Glsl_type::sampler_2d_multisample_array:
        case Glsl_type::int_sampler_2d_multisample_array:
        case Glsl_type::unsigned_int_sampler_2d_multisample_array: return MTL::TextureType2DMultisampleArray;
        default:                                                   return MTL::TextureType2D;
    }
}

} // anonymous namespace

Bind_group_layout_impl::~Bind_group_layout_impl() noexcept
{
    if (m_fragment_argument_encoder != nullptr) {
        m_fragment_argument_encoder->release();
        m_fragment_argument_encoder = nullptr;
    }
}

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

    // Allocate the compact Metal sampler index for every dedicated sampler.
    // See Bind_group_layout_impl::get_metal_sampler_slot for why the GLSL
    // binding number cannot be reused as the [[sampler(N)]] index.
    {
        std::vector<std::pair<uint32_t, uint32_t>> dedicated_sampler_bindings; // GLSL binding, stage flags
        for (const Bind_group_layout_binding& binding : create_info.bindings) {
            if ((binding.type == Binding_type::combined_image_sampler) && !binding.is_texture_heap) {
                dedicated_sampler_bindings.push_back({binding.binding_point + m_sampler_binding_offset, binding.stage_flags});
            }
        }
        std::sort(dedicated_sampler_bindings.begin(), dedicated_sampler_bindings.end());
        uint32_t next_slot = 0;
        for (const std::pair<uint32_t, uint32_t>& entry : dedicated_sampler_bindings) {
            m_dedicated_samplers[entry.first] = Dedicated_sampler{.sampler_slot = next_slot, .stage_flags = entry.second};
            ++next_slot;
        }
        // Metal caps [[sampler(N)]] at N < 16 per stage. A layout that declares
        // more dedicated samplers than that cannot be expressed, and would
        // otherwise surface as an opaque MSL compile error ("'sampler'
        // attribute parameter is out of bounds") much later, in every shader
        // built from this layout.
        ERHE_VERIFY(next_slot <= c_metal_max_dedicated_samplers);
    }

    // The fragment argument buffer: every dedicated sampler a fragment
    // function reads, in ascending GLSL binding order, texture at id 2k and
    // sampler at id 2k + 1 (see Fragment_argument).
    {
        std::vector<std::pair<uint32_t, Glsl_type>> fragment_bindings; // GLSL binding, type
        for (const Bind_group_layout_binding& binding : create_info.bindings) {
            if (
                (binding.type == Binding_type::combined_image_sampler) &&
                !binding.is_texture_heap &&
                ((binding.stage_flags & Shader_stage_flags::fragment) != 0)
            ) {
                fragment_bindings.push_back({binding.binding_point + m_sampler_binding_offset, binding.glsl_type});
            }
        }
        std::sort(
            fragment_bindings.begin(), fragment_bindings.end(),
            [](const std::pair<uint32_t, Glsl_type>& lhs, const std::pair<uint32_t, Glsl_type>& rhs) -> bool { return lhs.first < rhs.first; }
        );
        // [[buffer(15)]] holds the fragment argument buffer, so no buffer
        // binding of the layout may claim it.
        if (!fragment_bindings.empty()) {
            for (const Bind_group_layout_binding& binding : create_info.bindings) {
                if ((binding.type == Binding_type::uniform_buffer) || (binding.type == Binding_type::storage_buffer)) {
                    ERHE_VERIFY(binding.binding_point != fragment_argument_buffer_index);
                }
            }
        }
        MTL::Device* const mtl_device = device.get_impl().get_mtl_device();
        if (!fragment_bindings.empty() && (mtl_device != nullptr)) {
            std::vector<MTL::ArgumentDescriptor*> descriptors;
            for (std::size_t k = 0, end = fragment_bindings.size(); k < end; ++k) {
                const uint32_t texture_id = static_cast<uint32_t>(2 * k);
                const uint32_t sampler_id = texture_id + 1;
                m_fragment_arguments.push_back(
                    Fragment_argument{
                        .glsl_binding = fragment_bindings[k].first,
                        .texture_id   = texture_id,
                        .sampler_id   = sampler_id
                    }
                );
                MTL::ArgumentDescriptor* const texture_descriptor = MTL::ArgumentDescriptor::argumentDescriptor();
                texture_descriptor->setDataType   (MTL::DataTypeTexture);
                texture_descriptor->setIndex      (texture_id);
                texture_descriptor->setAccess     (MTL::ArgumentAccessReadOnly);
                texture_descriptor->setTextureType(to_argument_texture_type(fragment_bindings[k].second));
                descriptors.push_back(texture_descriptor);
                MTL::ArgumentDescriptor* const sampler_descriptor = MTL::ArgumentDescriptor::argumentDescriptor();
                sampler_descriptor->setDataType(MTL::DataTypeSampler);
                sampler_descriptor->setIndex   (sampler_id);
                sampler_descriptor->setAccess  (MTL::ArgumentAccessReadOnly);
                descriptors.push_back(sampler_descriptor);
            }
            NS::Array* const descriptor_array = NS::Array::array(reinterpret_cast<NS::Object* const*>(descriptors.data()), descriptors.size());
            m_fragment_argument_encoder = mtl_device->newArgumentEncoder(descriptor_array);
            ERHE_VERIFY(m_fragment_argument_encoder != nullptr);
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
        m_default_uniform_block.add_sampler(
            "s_texture",
            Glsl_type::sampler_2d,
            Sampler_aspect::color,
            /*is_texture_heap=*/true,
            /*dedicated_texture_unit=*/std::optional<uint32_t>{},
            /*array_size=*/c_metal_texture_heap_slot_count
        );
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

auto Bind_group_layout_impl::get_dedicated_sampler(const uint32_t glsl_binding) const -> const Dedicated_sampler&
{
    const std::unordered_map<uint32_t, Dedicated_sampler>::const_iterator i = m_dedicated_samplers.find(glsl_binding);
    ERHE_VERIFY(i != m_dedicated_samplers.end());
    return i->second;
}

auto Bind_group_layout_impl::get_metal_sampler_slot(const uint32_t glsl_binding) const -> uint32_t
{
    return get_dedicated_sampler(glsl_binding).sampler_slot;
}

auto Bind_group_layout_impl::get_sampler_stage_flags(const uint32_t glsl_binding) const -> uint32_t
{
    return get_dedicated_sampler(glsl_binding).stage_flags;
}

auto Bind_group_layout_impl::get_fragment_argument_index(const uint32_t glsl_binding) const -> std::optional<std::size_t>
{
    for (std::size_t k = 0, end = m_fragment_arguments.size(); k < end; ++k) {
        if (m_fragment_arguments[k].glsl_binding == glsl_binding) {
            return k;
        }
    }
    return {};
}

auto Bind_group_layout_impl::get_fragment_arguments() const -> std::span<const Fragment_argument>
{
    return m_fragment_arguments;
}

auto Bind_group_layout_impl::get_fragment_argument_encoder() const -> MTL::ArgumentEncoder*
{
    return m_fragment_argument_encoder;
}

} // namespace erhe::graphics
