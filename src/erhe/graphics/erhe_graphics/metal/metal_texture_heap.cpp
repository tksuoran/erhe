#include "erhe_graphics/metal/metal_texture_heap.hpp"
#include "erhe_graphics/metal/metal_texture.hpp"
#include "erhe_graphics/metal/metal_sampler.hpp"
#include "erhe_graphics/metal/metal_buffer.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/metal/metal_render_pass.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <Metal/Metal.hpp>

#include <algorithm>

namespace erhe::graphics {

Texture_heap_impl::Texture_heap_impl(
    Device&                    device,
    const Texture&             fallback_texture,
    const Sampler&             fallback_sampler,
    std::size_t                reserved_slot_count,
    const Bind_group_layout*   /*bind_group_layout*/,
    const Shader_resource*     default_uniform_block
)
    : m_device             {device}
    , m_fallback_texture   {fallback_texture}
    , m_fallback_sampler   {fallback_sampler}
    , m_reserved_slot_count{reserved_slot_count}
    , m_used_slot_count    {0}
{
    m_textures.resize(m_reserved_slot_count, &m_fallback_texture);
    m_samplers.resize(m_reserved_slot_count, &m_fallback_sampler);
    m_assigned.resize(m_reserved_slot_count, false);

    if (default_uniform_block == nullptr) {
        return;
    }

    // Build slot layout from sampler declarations in the default uniform block.
    // The layout must match what SPIRV-Cross compilation produces (see
    // metal_shader_stages_prototype.cpp which assigns the same ids).
    //
    // Formula: iterate samplers sorted by GLSL binding. For each (array_size S):
    //   texture_id = running_offset
    //   sampler_id = running_offset + S
    //   running_offset += 2 * S

    struct Sampler_info
    {
        uint32_t binding;
        uint32_t array_size;
    };
    std::vector<Sampler_info> sampler_infos;
    for (const auto& member : default_uniform_block->get_members()) {
        if (member->get_type() != Shader_resource::Type::sampler) {
            continue;
        }
        uint32_t binding    = static_cast<uint32_t>(member->get_texture_unit());
        uint32_t array_size = member->get_array_size().has_value()
            ? static_cast<uint32_t>(member->get_array_size().value())
            : 1;
        sampler_infos.push_back({binding, array_size});
    }
    std::sort(sampler_infos.begin(), sampler_infos.end(),
        [](const Sampler_info& a, const Sampler_info& b) { return a.binding < b.binding; });

    // Compute [[id(N)]] for each slot.
    // Reserved slots map to individual samplers or to elements of an array.
    // Dynamic slots (material textures) are array elements beyond reserved_slot_count.
    uint32_t arg_offset = 0;
    for (const Sampler_info& info : sampler_infos) {
        // Populate per-slot layout for all slots this sampler covers
        for (uint32_t i = 0; i < info.array_size; ++i) {
            uint32_t slot = info.binding + i;
            if (slot < m_reserved_slot_count) {
                m_slot_layouts.resize(std::max(m_slot_layouts.size(), static_cast<std::size_t>(slot + 1)));
                m_slot_layouts[slot] = Slot_layout{
                    .texture_arg_index = arg_offset + i,
                    .sampler_arg_index = arg_offset + info.array_size + i
                };
            }
        }
        // Track the array sampler for dynamic (beyond reserved) slots
        if (info.array_size > 1) {
            m_array_base_texture_id  = arg_offset;
            m_array_base_sampler_id  = arg_offset + info.array_size;
        }
        arg_offset += 2 * info.array_size;
    }

    // Create argument encoder from explicit descriptors
    MTL::Device* mtl_device = device.get_impl().get_mtl_device();
    if (mtl_device == nullptr) {
        return;
    }

    // Build argument descriptors for all entries
    std::vector<MTL::ArgumentDescriptor*> descriptors;
    arg_offset = 0;
    for (const Sampler_info& info : sampler_infos) {
        // Texture descriptor(s)
        {
            MTL::ArgumentDescriptor* desc = MTL::ArgumentDescriptor::argumentDescriptor();
            desc->setDataType(MTL::DataTypeTexture);
            desc->setIndex(arg_offset);
            desc->setArrayLength(info.array_size);
            desc->setAccess(MTL::ArgumentAccessReadOnly);
            desc->setTextureType(MTL::TextureType2D);
            descriptors.push_back(desc);
        }
        // Sampler descriptor(s)
        {
            MTL::ArgumentDescriptor* desc = MTL::ArgumentDescriptor::argumentDescriptor();
            desc->setDataType(MTL::DataTypeSampler);
            desc->setIndex(arg_offset + info.array_size);
            desc->setArrayLength(info.array_size);
            desc->setAccess(MTL::ArgumentAccessReadOnly);
            descriptors.push_back(desc);
        }
        arg_offset += 2 * info.array_size;
    }

    NS::Array* desc_array = NS::Array::array(
        reinterpret_cast<NS::Object* const*>(descriptors.data()),
        descriptors.size()
    );
    m_argument_encoder = mtl_device->newArgumentEncoder(desc_array);

    if (m_argument_encoder != nullptr) {
        log_texture_heap->info(
            "Texture heap encoder created from default_uniform_block: encodedLength={}, arg_buffer=[[buffer({})]]",
            m_argument_encoder->encodedLength(), m_argument_buffer_index
        );
    } else {
        log_texture_heap->error("Failed to create argument encoder from default_uniform_block descriptors");
    }
}

Texture_heap_impl::~Texture_heap_impl() noexcept
{
    if (m_argument_encoder != nullptr) {
        m_argument_encoder->release();
        m_argument_encoder = nullptr;
    }
    m_argument_buffer_range.cancel();
}

auto Texture_heap_impl::assign(std::size_t slot, const Texture* texture, const Sampler* sampler) -> uint64_t
{
    if (texture == nullptr) {
        texture = &m_fallback_texture;
    }
    if (sampler == nullptr) {
        sampler = &m_fallback_sampler;
    }

    if (slot >= m_textures.size()) {
        m_textures.resize(slot + 1, &m_fallback_texture);
        m_samplers.resize(slot + 1, &m_fallback_sampler);
        m_assigned.resize(slot + 1, false);
    }

    m_textures[slot] = texture;
    m_samplers[slot] = sampler;
    m_assigned[slot] = true;
    m_dirty = true;

    return static_cast<uint64_t>(slot);
}

void Texture_heap_impl::reset_heap()
{
    m_textures.resize(m_reserved_slot_count);
    m_samplers.resize(m_reserved_slot_count);
    m_assigned.resize(m_reserved_slot_count);
    std::fill(m_textures.begin(), m_textures.end(), &m_fallback_texture);
    std::fill(m_samplers.begin(), m_samplers.end(), &m_fallback_sampler);
    std::fill(m_assigned.begin(), m_assigned.end(), false);
    m_used_slot_count = 0;
    m_dirty = true;
}

auto Texture_heap_impl::allocate(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    if ((texture == nullptr) || (sampler == nullptr)) {
        return invalid_texture_handle;
    }

    // Check if already allocated
    for (std::size_t i = 0; i < m_used_slot_count; ++i) {
        std::size_t slot = m_reserved_slot_count + i;
        if ((m_textures[slot] == texture) && (m_samplers[slot] == sampler)) {
            return static_cast<uint64_t>(i);
        }
    }

    m_textures.push_back(texture);
    m_samplers.push_back(sampler);
    m_assigned.push_back(true);

    uint64_t material_index = static_cast<uint64_t>(m_used_slot_count);
    ++m_used_slot_count;
    m_dirty = true;

    return material_index;
}

auto Texture_heap_impl::get_shader_handle(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    ERHE_VERIFY(texture != nullptr);
    ERHE_VERIFY(sampler != nullptr);

    for (std::size_t i = 0; i < m_used_slot_count; ++i) {
        std::size_t slot = m_reserved_slot_count + i;
        if ((m_textures[slot] == texture) && (m_samplers[slot] == sampler)) {
            return static_cast<uint64_t>(i);
        }
    }
    ERHE_FATAL("texture not found in texture heap");
}

void Texture_heap_impl::encode_argument_buffer()
{
    if (m_argument_encoder == nullptr) {
        return;
    }

    // Release previous range before acquiring a new one
    m_argument_buffer_range.release();

    NS::UInteger encoded_length = m_argument_encoder->encodedLength();
    m_argument_buffer_range = m_device.allocate_ring_buffer_entry(
        Buffer_target::storage,
        Ring_buffer_usage::CPU_write,
        static_cast<std::size_t>(encoded_length)
    );

    Ring_buffer* ring_buffer = m_argument_buffer_range.get_buffer();
    if (ring_buffer == nullptr) {
        return;
    }
    Buffer* buffer = ring_buffer->get_buffer();
    if (buffer == nullptr) {
        return;
    }
    MTL::Buffer* mtl_buffer = buffer->get_impl().get_mtl_buffer();
    if (mtl_buffer == nullptr) {
        return;
    }

    NS::UInteger offset = static_cast<NS::UInteger>(m_argument_buffer_range.get_byte_start_offset_in_buffer());
    m_argument_encoder->setArgumentBuffer(mtl_buffer, offset);

    MTL::Texture*      fallback_mtl_texture = m_fallback_texture.get_impl().get_mtl_texture();
    MTL::SamplerState* fallback_mtl_sampler = m_fallback_sampler.get_impl().get_mtl_sampler();

    // Encode all slots
    const std::size_t total_slot_count = m_reserved_slot_count + m_used_slot_count;
    for (std::size_t slot = 0; slot < total_slot_count; ++slot) {
        if ((slot < m_reserved_slot_count) && !m_assigned[slot]) {
            continue;
        }
        const Texture* texture = m_textures[slot];
        const Sampler* sampler = m_samplers[slot];
        MTL::Texture*      mtl_texture = (texture != nullptr) ? texture->get_impl().get_mtl_texture() : fallback_mtl_texture;
        MTL::SamplerState* mtl_sampler = (sampler != nullptr) ? sampler->get_impl().get_mtl_sampler() : fallback_mtl_sampler;
        if (mtl_texture == nullptr) { mtl_texture = fallback_mtl_texture; }
        if (mtl_sampler == nullptr) { mtl_sampler = fallback_mtl_sampler; }

        uint32_t tex_id = 0;
        uint32_t smp_id = 0;

        if (slot < m_slot_layouts.size()) {
            // Reserved slot (individual sampler like shadow textures)
            tex_id = m_slot_layouts[slot].texture_arg_index;
            smp_id = m_slot_layouts[slot].sampler_arg_index;
        } else {
            // Dynamic slot (material texture array element)
            uint32_t array_index = static_cast<uint32_t>(slot) - static_cast<uint32_t>(m_reserved_slot_count);
            tex_id = m_array_base_texture_id + array_index;
            smp_id = m_array_base_sampler_id + array_index;
        }

        if (mtl_texture != nullptr) {
            m_argument_encoder->setTexture(mtl_texture, tex_id);
        }
        if (mtl_sampler != nullptr) {
            m_argument_encoder->setSamplerState(mtl_sampler, smp_id);
        }
    }

    m_argument_buffer_range.bytes_written(static_cast<std::size_t>(encoded_length));
    m_argument_buffer_range.close();
}

auto Texture_heap_impl::bind() -> std::size_t
{
    if (m_argument_encoder == nullptr) {
        log_texture_heap->trace("Texture_heap::bind() skipped: no argument encoder (default_uniform_block not provided?)");
        return m_used_slot_count;
    }

    MTL::RenderCommandEncoder* render_encoder = Render_pass_impl::get_active_mtl_encoder();
    if (render_encoder == nullptr) {
        return m_used_slot_count;
    }

    if (m_dirty) {
        encode_argument_buffer();
        m_dirty = false;
    }

    Ring_buffer* ring_buffer = m_argument_buffer_range.get_buffer();
    if (ring_buffer == nullptr) {
        return m_used_slot_count;
    }
    Buffer* buffer = ring_buffer->get_buffer();
    if (buffer == nullptr) {
        return m_used_slot_count;
    }
    MTL::Buffer* mtl_buffer = buffer->get_impl().get_mtl_buffer();
    if (mtl_buffer == nullptr) {
        return m_used_slot_count;
    }

    NS::UInteger offset = static_cast<NS::UInteger>(m_argument_buffer_range.get_byte_start_offset_in_buffer());
    render_encoder->setFragmentBuffer(mtl_buffer, offset, m_argument_buffer_index);

    for (std::size_t slot = 0; slot < m_reserved_slot_count + m_used_slot_count; ++slot) {
        if ((slot < m_reserved_slot_count) && !m_assigned[slot]) {
            continue;
        }
        const Texture* texture = m_textures[slot];
        if (texture == nullptr) {
            continue;
        }
        MTL::Texture* mtl_texture = texture->get_impl().get_mtl_texture();
        if (mtl_texture != nullptr) {
            render_encoder->useResource(mtl_texture, MTL::ResourceUsageRead | MTL::ResourceUsageSample);
        }
    }
    MTL::Texture* fallback_mtl = m_fallback_texture.get_impl().get_mtl_texture();
    if (fallback_mtl != nullptr) {
        render_encoder->useResource(fallback_mtl, MTL::ResourceUsageRead | MTL::ResourceUsageSample);
    }

    return m_used_slot_count;
}

void Texture_heap_impl::unbind()
{
}

} // namespace erhe::graphics
