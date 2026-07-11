// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/vulkan/vulkan_texture_heap.hpp"
#include "erhe_graphics/vulkan/vulkan_bind_group_layout.hpp"
#include "erhe_graphics/vulkan/vulkan_command_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_render_command_encoder.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_sampler.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_verify/verify.hpp"

#include <etl/vector.h>

#include <algorithm>

namespace erhe::graphics {

namespace {

// Sets per VkDescriptorPool. Pools are created on demand and kept for the
// heap's lifetime; steady state needs roughly
// (reset_heap() calls per frame) x (frames in flight) sets, after which the
// recycling in acquire_descriptor_set() stops all pool growth.
constexpr uint32_t s_sets_per_pool = 8;

} // anonymous namespace

Texture_heap_impl::Texture_heap_impl(
    Device&                    device,
    const Texture&             fallback_texture,
    const Sampler&             fallback_sampler,
    const Bind_group_layout*   bind_group_layout
)
    : m_device_impl           {device.get_impl()}
    , m_bind_group_layout     {bind_group_layout}
    , m_fallback_texture      {fallback_texture}
    , m_fallback_sampler      {fallback_sampler}
{
    // The texture heap is the bindless color sampled-image array (set 1,
    // binding 0). The shader accesses it via sampler2D / sampler2D_array,
    // which can only commit to a single image aspect, so all textures stored
    // in the heap must be color (VUID-VkDescriptorImageInfo-imageView-01976).
    // Depth/stencil samplers belong on dedicated combined_image_sampler
    // bindings declared in the Bind_group_layout and are bound via
    // Render_command_encoder::set_sampled_image() instead.
    {
        const erhe::dataformat::Format fallback_format = m_fallback_texture.get_pixelformat();
        ERHE_VERIFY(erhe::dataformat::get_depth_size_bits  (fallback_format) == 0);
        ERHE_VERIFY(erhe::dataformat::get_stencil_size_bits(fallback_format) == 0);
    }

    VkDevice vulkan_device = m_device_impl.get_vulkan_device();

    m_textures.resize(m_max_textures, nullptr);
    m_samplers.resize(m_max_textures, nullptr);
    m_used_slot_count = 0;

    // Create our own descriptor set layout matching the one in Device_impl's pipeline layout (set 1)
    const VkDescriptorBindingFlags binding_flags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

    const VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info{
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .pNext         = nullptr,
        .bindingCount  = 1,
        .pBindingFlags = &binding_flags
    };

    const VkDescriptorSetLayoutBinding binding{
        .binding            = 0,
        .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount    = static_cast<uint32_t>(m_max_textures),
        .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr
    };

    const VkDescriptorSetLayoutCreateInfo layout_create_info{
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext        = &binding_flags_info,
        .flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 1,
        .pBindings    = &binding
    };

    const VkResult result = vkCreateDescriptorSetLayout(vulkan_device, &layout_create_info, nullptr, &m_descriptor_set_layout);
    if (result != VK_SUCCESS) {
        log_texture_heap->error("vkCreateDescriptorSetLayout() failed with {}", static_cast<int32_t>(result));
        return;
    }

    // Resolve the fallback descriptor contents once. The heap binds COLOR
    // textures only (asserted above), so we always request the COLOR aspect.
    Texture_impl& fallback_impl = const_cast<Texture_impl&>(m_fallback_texture.get_impl());
    m_fallback_view = fallback_impl.get_vk_image_view(
        VK_IMAGE_ASPECT_COLOR_BIT,
        0,
        std::max(1, fallback_impl.get_array_layer_count())
    );
    m_fallback_vk_sampler = m_fallback_sampler.get_impl().get_vulkan_sampler();

    log_texture_heap->debug("Texture heap created: {} max textures, {} sets per pool", m_max_textures, s_sets_per_pool);
}

Texture_heap_impl::~Texture_heap_impl() noexcept
{
    const VkDescriptorSetLayout descriptor_set_layout = m_descriptor_set_layout;
    std::vector<VkDescriptorPool> descriptor_pools = std::move(m_descriptor_pools);
    m_descriptor_set_layout = VK_NULL_HANDLE;
    m_descriptor_pools.clear();
    m_set_entries.clear();
    m_current_set_index = s_invalid_set_index;

    m_device_impl.add_completion_handler(
        [descriptor_set_layout, descriptor_pools](Device_impl& device_impl) {
            VkDevice vulkan_device = device_impl.get_vulkan_device();
            if (descriptor_set_layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(vulkan_device, descriptor_set_layout, nullptr);
            }
            for (const VkDescriptorPool descriptor_pool : descriptor_pools) {
                // Destroying a pool also frees all descriptor sets allocated from it
                vkDestroyDescriptorPool(vulkan_device, descriptor_pool, nullptr);
            }
        }
    );
}

void Texture_heap_impl::write_fallback_slots(VkDescriptorSet set, const std::size_t slot_count)
{
    if ((set == VK_NULL_HANDLE) || (slot_count == 0) || (m_fallback_view == VK_NULL_HANDLE) || (m_fallback_vk_sampler == VK_NULL_HANDLE)) {
        return;
    }

    const VkDescriptorImageInfo fallback_image_info{
        .sampler     = m_fallback_vk_sampler,
        .imageView   = m_fallback_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    m_scratch_image_infos.assign(slot_count, fallback_image_info);

    const VkWriteDescriptorSet write{
        .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext            = nullptr,
        .dstSet           = set,
        .dstBinding       = 0,
        .dstArrayElement  = 0,
        .descriptorCount  = static_cast<uint32_t>(slot_count),
        .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo       = m_scratch_image_infos.data(),
        .pBufferInfo      = nullptr,
        .pTexelBufferView = nullptr
    };
    vkUpdateDescriptorSets(m_device_impl.get_vulkan_device(), 1, &write, 0, nullptr);
}

auto Texture_heap_impl::allocate_new_set() -> VkDescriptorSet
{
    if (m_descriptor_set_layout == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    VkDevice vulkan_device = m_device_impl.get_vulkan_device();

    uint32_t variable_count = static_cast<uint32_t>(m_max_textures);
    const VkDescriptorSetVariableDescriptorCountAllocateInfo variable_info{
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .pNext              = nullptr,
        .descriptorSetCount = 1,
        .pDescriptorCounts  = &variable_count
    };

    VkDescriptorSetAllocateInfo allocate_info{
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext              = &variable_info,
        .descriptorPool     = VK_NULL_HANDLE,
        .descriptorSetCount = 1,
        .pSetLayouts        = &m_descriptor_set_layout
    };

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!m_descriptor_pools.empty()) {
        allocate_info.descriptorPool = m_descriptor_pools.back();
        const VkResult result = vkAllocateDescriptorSets(vulkan_device, &allocate_info, &set);
        if (result != VK_SUCCESS) {
            set = VK_NULL_HANDLE; // pool exhausted (or fragmented) - fall through to create a new pool
        }
    }

    if (set == VK_NULL_HANDLE) {
        const VkDescriptorPoolSize pool_size{
            .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = s_sets_per_pool * static_cast<uint32_t>(m_max_textures)
        };
        const VkDescriptorPoolCreateInfo pool_create_info{
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext         = nullptr,
            .flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
            .maxSets       = s_sets_per_pool,
            .poolSizeCount = 1,
            .pPoolSizes    = &pool_size
        };
        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        VkResult result = vkCreateDescriptorPool(vulkan_device, &pool_create_info, nullptr, &descriptor_pool);
        if (result != VK_SUCCESS) {
            log_texture_heap->error("vkCreateDescriptorPool() failed with {}", static_cast<int32_t>(result));
            return VK_NULL_HANDLE;
        }
        m_descriptor_pools.push_back(descriptor_pool);

        allocate_info.descriptorPool = descriptor_pool;
        result = vkAllocateDescriptorSets(vulkan_device, &allocate_info, &set);
        if (result != VK_SUCCESS) {
            log_texture_heap->error("vkAllocateDescriptorSets() failed with {}", static_cast<int32_t>(result));
            return VK_NULL_HANDLE;
        }
    }

    // Initialize all slots with the fallback texture so unallocated slots
    // are never invalid descriptors.
    write_fallback_slots(set, m_max_textures);

    return set;
}

void Texture_heap_impl::acquire_descriptor_set()
{
    const uint64_t current_frame = m_device_impl.get_frame_index();

    // Prefer recycling a set whose last use the GPU has fully retired.
    // Sets acquired earlier in the current frame are never eligible: the
    // current frame cannot be completed while it is still being recorded,
    // which is exactly the invariant that keeps every recorded pass bound
    // to an immutable descriptor snapshot.
    for (Set_entry& entry : m_set_entries) {
        if (m_device_impl.is_frame_completed(entry.frame)) {
            // Restore previously written slots to the fallback so the
            // recycled set never carries stale (potentially destroyed)
            // texture descriptors.
            write_fallback_slots(entry.set, entry.written_count);
            entry.written_count = 0;
            entry.frame         = current_frame;
            m_current_set_index = static_cast<std::size_t>(&entry - m_set_entries.data());
            return;
        }
    }

    VkDescriptorSet set = allocate_new_set();
    if (set == VK_NULL_HANDLE) {
        // Never fall back to an in-flight set here - overwriting one is the
        // cross-pass aliasing bug this class exists to prevent. allocate()
        // and bind() degrade gracefully when no set is current.
        m_current_set_index = s_invalid_set_index;
        return;
    }
    m_set_entries.push_back(
        Set_entry{
            .set           = set,
            .frame         = current_frame,
            .written_count = 0
        }
    );
    m_current_set_index = m_set_entries.size() - 1;
    ERHE_VULKAN_DESC_TRACE("[HEAP_SET_ALLOC] sets={}", m_set_entries.size());
}

void Texture_heap_impl::reset_heap(Command_buffer& command_buffer)
{
    Scoped_debug_group debug_group{command_buffer, "Texture_heap_impl::reset_heap()"};
    m_used_slot_count = 0;
    for (std::size_t i = 0; i < m_max_textures; ++i) {
        m_textures[i] = nullptr;
        m_samplers[i] = nullptr;
    }
    acquire_descriptor_set();
}

auto Texture_heap_impl::get_shader_handle(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    if ((texture == nullptr) || (sampler == nullptr)) {
        return invalid_texture_handle;
    }

    // Search for existing allocation
    for (std::size_t i = 0; i < m_used_slot_count; ++i) {
        if ((m_textures[i] == texture) && (m_samplers[i] == sampler)) {
            return static_cast<uint64_t>(i);
        }
    }

    return invalid_texture_handle;
}

auto Texture_heap_impl::allocate(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    if ((texture == nullptr) || (sampler == nullptr)) {
        return invalid_texture_handle;
    }

    // Check if already allocated
    for (std::size_t i = 0; i < m_used_slot_count; ++i) {
        if ((m_textures[i] == texture) && (m_samplers[i] == sampler)) {
            return static_cast<uint64_t>(i);
        }
    }

    if (m_used_slot_count >= m_max_textures) {
        log_texture_heap->error("Texture heap full ({} textures)", m_max_textures);
        return invalid_texture_handle;
    }

    std::size_t slot = m_used_slot_count;
    ++m_used_slot_count;

    m_textures[slot] = texture;
    m_samplers[slot] = sampler;

    // Callers reset_heap() before allocating; the lazy acquire only covers
    // an allocate() before the first reset_heap().
    if (m_current_set_index == s_invalid_set_index) {
        acquire_descriptor_set();
    }
    if (m_current_set_index == s_invalid_set_index) {
        return static_cast<uint64_t>(slot);
    }
    Set_entry& current_entry = m_set_entries[m_current_set_index];

    // Texture heap only holds color textures; depth/stencil samplers are
    // bound via Render_command_encoder::set_sampled_image().
    {
        const erhe::dataformat::Format pixelformat = texture->get_pixelformat();
        ERHE_VERIFY(erhe::dataformat::get_depth_size_bits  (pixelformat) == 0);
        ERHE_VERIFY(erhe::dataformat::get_stencil_size_bits(pixelformat) == 0);
    }
    Texture_impl& texture_impl = const_cast<Texture_impl&>(texture->get_impl());
    VkImageView image_view = texture_impl.get_vk_image_view(
        VK_IMAGE_ASPECT_COLOR_BIT,
        0,
        std::max(1, texture_impl.get_array_layer_count())
    );
    VkSampler vk_sampler = sampler->get_impl().get_vulkan_sampler();
    if ((image_view == VK_NULL_HANDLE) || (vk_sampler == VK_NULL_HANDLE)) {
        return static_cast<uint64_t>(slot);
    }

    const VkDescriptorImageInfo image_info{
        .sampler     = vk_sampler,
        .imageView   = image_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    const VkWriteDescriptorSet write{
        .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext            = nullptr,
        .dstSet           = current_entry.set,
        .dstBinding       = 0,
        .dstArrayElement  = static_cast<uint32_t>(slot),
        .descriptorCount  = 1,
        .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo       = &image_info,
        .pBufferInfo      = nullptr,
        .pTexelBufferView = nullptr
    };
    vkUpdateDescriptorSets(m_device_impl.get_vulkan_device(), 1, &write, 0, nullptr);
    current_entry.written_count = std::max(current_entry.written_count, slot + 1);
    ERHE_VULKAN_DESC_TRACE("[HEAP_ALLOC] slot={}", slot);

    return static_cast<uint64_t>(slot);
}

void Texture_heap_impl::unbind(Command_buffer& command_buffer)
{
    Scoped_debug_group debug_group{command_buffer, "Texture_heap_impl::unbind()"};
    // No-op for descriptor sets
}

auto Texture_heap_impl::bind(Render_command_encoder& encoder) -> std::size_t
{
    // Covers a bind() before the first reset_heap(); normal passes have a
    // current set acquired by reset_heap().
    if (m_current_set_index == s_invalid_set_index) {
        acquire_descriptor_set();
    }
    if (m_current_set_index == s_invalid_set_index) {
        return 0;
    }
    const Set_entry& current_entry = m_set_entries[m_current_set_index];

    const VkCommandBuffer command_buffer = encoder.get_command_buffer().get_impl().get_vulkan_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return 0;
    }

    ERHE_VERIFY(m_bind_group_layout != nullptr);
    VkPipelineLayout pipeline_layout = m_bind_group_layout->get_impl().get_pipeline_layout();

    // Bind the texture descriptor set at set index 1
    vkCmdBindDescriptorSets(
        command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_layout,
        1, // set index 1 for textures
        1,
        &current_entry.set,
        0,
        nullptr
    );

    ++m_device_impl.m_desc_heap_bind_count;
    ERHE_VULKAN_DESC_TRACE("[HEAP_BIND] set=1 slots={}", m_used_slot_count);

    // Phase B: the dual-path push-descriptor branch is gone. Named sampler
    // bindings (shadow maps, etc.) are now bound by the renderer via
    // Render_command_encoder::set_sampled_image() before the draw, not by
    // the texture heap. Texture_heap is purely the bindless color array.
    return m_used_slot_count;
}

} // namespace erhe::graphics
