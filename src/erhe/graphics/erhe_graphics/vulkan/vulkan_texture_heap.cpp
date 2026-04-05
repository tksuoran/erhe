// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/vulkan/vulkan_texture_heap.hpp"
#include "erhe_graphics/vulkan/vulkan_bind_group_layout.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_sampler.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <etl/vector.h>

#include <algorithm>

namespace erhe::graphics {

Texture_heap_impl::Texture_heap_impl(
    Device&                    device,
    const Texture&             fallback_texture,
    const Sampler&             fallback_sampler,
    std::size_t                reserved_slot_count,
    const Bind_group_layout*   bind_group_layout,
    const Shader_resource*     default_uniform_block
)
    : m_device              {device}
    , m_bind_group_layout   {bind_group_layout}
    , m_fallback_texture    {fallback_texture}
    , m_fallback_sampler    {fallback_sampler}
    , m_reserved_slot_count {reserved_slot_count}
{
    static_cast<void>(default_uniform_block);

    Device_impl& device_impl = device.get_impl();
    VkDevice vulkan_device = device_impl.get_vulkan_device();

    m_textures.resize(m_max_textures, nullptr);
    m_samplers.resize(m_max_textures, nullptr);
    m_used_slot_count = reserved_slot_count;

    // Create a descriptor pool for combined image samplers
    const VkDescriptorPoolSize pool_size{
        .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = static_cast<uint32_t>(m_max_textures)
    };

    const VkDescriptorPoolCreateInfo pool_create_info{
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext         = nullptr,
        .flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets       = 1,
        .poolSizeCount = 1,
        .pPoolSizes    = &pool_size
    };

    VkResult result = vkCreateDescriptorPool(vulkan_device, &pool_create_info, nullptr, &m_descriptor_pool);
    if (result != VK_SUCCESS) {
        log_texture_heap->error("vkCreateDescriptorPool() failed with {}", static_cast<int32_t>(result));
        return;
    }

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

    result = vkCreateDescriptorSetLayout(vulkan_device, &layout_create_info, nullptr, &m_descriptor_set_layout);
    if (result != VK_SUCCESS) {
        log_texture_heap->error("vkCreateDescriptorSetLayout() failed with {}", static_cast<int32_t>(result));
        return;
    }

    // Allocate the descriptor set
    uint32_t variable_count = static_cast<uint32_t>(m_max_textures);
    const VkDescriptorSetVariableDescriptorCountAllocateInfo variable_info{
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .pNext              = nullptr,
        .descriptorSetCount = 1,
        .pDescriptorCounts  = &variable_count
    };

    const VkDescriptorSetAllocateInfo allocate_info{
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext              = &variable_info,
        .descriptorPool     = m_descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &m_descriptor_set_layout
    };

    result = vkAllocateDescriptorSets(vulkan_device, &allocate_info, &m_descriptor_set);
    if (result != VK_SUCCESS) {
        log_texture_heap->error("vkAllocateDescriptorSets() failed with {}", static_cast<int32_t>(result));
        m_descriptor_set = VK_NULL_HANDLE;
        return;
    }

    // Initialize all slots with fallback texture
    VkImageView  fallback_view    = m_fallback_texture.get_impl().get_vk_image_view();
    VkSampler    fallback_sampler_vk = m_fallback_sampler.get_impl().get_vulkan_sampler();
    if ((fallback_view != VK_NULL_HANDLE) && (fallback_sampler_vk != VK_NULL_HANDLE)) {
        std::vector<VkDescriptorImageInfo> image_infos(m_max_textures);
        std::vector<VkWriteDescriptorSet>  writes(m_max_textures);
        for (std::size_t i = 0; i < m_max_textures; ++i) {
            image_infos[i] = VkDescriptorImageInfo{
                .sampler     = fallback_sampler_vk,
                .imageView   = fallback_view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
            writes[i] = VkWriteDescriptorSet{
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext           = nullptr,
                .dstSet          = m_descriptor_set,
                .dstBinding      = 0,
                .dstArrayElement = static_cast<uint32_t>(i),
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &image_infos[i],
                .pBufferInfo     = nullptr,
                .pTexelBufferView = nullptr
            };
        }
        vkUpdateDescriptorSets(vulkan_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    log_texture_heap->info("Texture heap created: {} max textures, {} reserved slots", m_max_textures, reserved_slot_count);
}

Texture_heap_impl::~Texture_heap_impl() noexcept
{
    Device_impl&          device_impl          = m_device.get_impl();
    const VkDescriptorSetLayout descriptor_set_layout = m_descriptor_set_layout;
    const VkDescriptorPool      descriptor_pool       = m_descriptor_pool;
    m_descriptor_set_layout = VK_NULL_HANDLE;
    m_descriptor_pool       = VK_NULL_HANDLE;
    m_descriptor_set        = VK_NULL_HANDLE;

    device_impl.add_completion_handler(
        [&device_impl, descriptor_set_layout, descriptor_pool]() {
            VkDevice vulkan_device = device_impl.get_vulkan_device();
            if (descriptor_set_layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(vulkan_device, descriptor_set_layout, nullptr);
            }
            if (descriptor_pool != VK_NULL_HANDLE) {
                // Destroying the pool also frees all descriptor sets allocated from it
                vkDestroyDescriptorPool(vulkan_device, descriptor_pool, nullptr);
            }
        }
    );
}

void Texture_heap_impl::reset_heap()
{
    m_used_slot_count = m_reserved_slot_count;
    // Reset all non-reserved slots to fallback texture
    for (std::size_t i = m_reserved_slot_count; i < m_max_textures; ++i) {
        m_textures[i] = nullptr;
        m_samplers[i] = nullptr;
    }
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

auto Texture_heap_impl::assign(std::size_t slot, const Texture* texture, const Sampler* sampler) -> uint64_t
{
    if ((texture == nullptr) || (sampler == nullptr) || (slot >= m_max_textures)) {
        return invalid_texture_handle;
    }

    m_textures[slot] = texture;
    m_samplers[slot] = sampler;

    if (m_descriptor_set == VK_NULL_HANDLE) {
        return static_cast<uint64_t>(slot);
    }

    // For depth/stencil textures, use a depth-only image view for sampling
    Texture_impl& texture_impl = const_cast<Texture_impl&>(texture->get_impl());
    VkImageView image_view = VK_NULL_HANDLE;
    VkImageLayout image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    {
        const erhe::dataformat::Format pixelformat = texture_impl.get_pixelformat();
        const bool has_depth   = erhe::dataformat::get_depth_size_bits(pixelformat) > 0;
        const bool has_stencil = erhe::dataformat::get_stencil_size_bits(pixelformat) > 0;
        if (has_depth) {
            image_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }
        if (has_depth && has_stencil) {
            image_view = texture_impl.get_vk_image_view(
                VK_IMAGE_ASPECT_DEPTH_BIT,
                0,
                std::max(1, texture_impl.get_array_layer_count())
            );
        } else {
            image_view = texture_impl.get_vk_image_view();
        }
    }
    VkSampler vk_sampler = sampler->get_impl().get_vulkan_sampler();
    if ((image_view == VK_NULL_HANDLE) || (vk_sampler == VK_NULL_HANDLE)) {
        return static_cast<uint64_t>(slot);
    }

    const VkDescriptorImageInfo image_info{
        .sampler     = vk_sampler,
        .imageView   = image_view,
        .imageLayout = image_layout
    };

    const VkWriteDescriptorSet write{
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext           = nullptr,
        .dstSet          = m_descriptor_set,
        .dstBinding      = 0,
        .dstArrayElement = static_cast<uint32_t>(slot),
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = &image_info,
        .pBufferInfo     = nullptr,
        .pTexelBufferView = nullptr
    };

    VkDevice vulkan_device = m_device.get_impl().get_vulkan_device();
    vkUpdateDescriptorSets(vulkan_device, 1, &write, 0, nullptr);

    return static_cast<uint64_t>(slot);
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

    return assign(slot, texture, sampler);
}

void Texture_heap_impl::unbind()
{
    // No-op for descriptor sets
}

auto Texture_heap_impl::bind() -> std::size_t
{
    if (m_descriptor_set == VK_NULL_HANDLE) {
        return 0;
    }

    VkCommandBuffer command_buffer = Render_pass_impl::get_active_command_buffer();
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
        &m_descriptor_set,
        0,
        nullptr
    );

    // Push assigned sampler descriptors (e.g. shadow samplers) to set 0 at offset binding indices.
    // Only applies when the bind group layout actually declares sampler bindings.
    if ((m_bind_group_layout != nullptr) && m_bind_group_layout->get_impl().has_sampler_bindings() && m_device.get_impl().has_push_descriptor()) {
        const uint32_t sampler_offset = m_bind_group_layout->get_sampler_binding_offset();
        if (sampler_offset > 0) {
            static constexpr std::size_t max_reserved_slots = 8;
            etl::vector<VkDescriptorImageInfo, max_reserved_slots> image_infos;
            etl::vector<VkWriteDescriptorSet,  max_reserved_slots> writes;
            for (std::size_t i = 0; i < m_reserved_slot_count; ++i) {
                if ((m_textures[i] == nullptr) || (m_samplers[i] == nullptr)) {
                    continue;
                }
                Texture_impl& texture_impl = const_cast<Texture_impl&>(m_textures[i]->get_impl());
                const erhe::dataformat::Format pixelformat = texture_impl.get_pixelformat();
                const bool has_depth   = erhe::dataformat::get_depth_size_bits(pixelformat) > 0;
                const bool has_stencil = erhe::dataformat::get_stencil_size_bits(pixelformat) > 0;
                VkImageView image_view = VK_NULL_HANDLE;
                VkImageLayout push_image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                if (has_depth) {
                    push_image_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                }
                if (has_depth && has_stencil) {
                    image_view = texture_impl.get_vk_image_view(
                        VK_IMAGE_ASPECT_DEPTH_BIT,
                        0,
                        std::max(1, texture_impl.get_array_layer_count())
                    );
                } else {
                    image_view = texture_impl.get_vk_image_view();
                }
                VkSampler vk_sampler = m_samplers[i]->get_impl().get_vulkan_sampler();
                if ((image_view == VK_NULL_HANDLE) || (vk_sampler == VK_NULL_HANDLE)) {
                    continue;
                }
                image_infos.push_back(VkDescriptorImageInfo{
                    .sampler     = vk_sampler,
                    .imageView   = image_view,
                    .imageLayout = push_image_layout
                });
                writes.push_back(VkWriteDescriptorSet{
                    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .pNext           = nullptr,
                    .dstSet          = VK_NULL_HANDLE, // ignored for push descriptors
                    .dstBinding      = static_cast<uint32_t>(sampler_offset + i),
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo      = &image_infos.back(),
                    .pBufferInfo     = nullptr,
                    .pTexelBufferView = nullptr
                });
            }
            if (!writes.empty()) {
                vkCmdPushDescriptorSetKHR(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline_layout,
                    0, // set 0
                    static_cast<uint32_t>(writes.size()),
                    writes.data()
                );
            }
        }
    }

    return m_used_slot_count;
}

} // namespace erhe::graphics
