#pragma once

#include "erhe_graphics/texture_heap.hpp"

#include "volk.h"

#include <vector>

namespace erhe::graphics {

class Device_impl;

class Texture_heap_impl final
{
public:
    Texture_heap_impl(
        Device&                  device,
        const Texture&           fallback_texture,
        const Sampler&           fallback_sampler,
        const Bind_group_layout* bind_group_layout = nullptr
    );
    ~Texture_heap_impl() noexcept;

    void reset_heap       (Command_buffer& command_buffer);
    auto allocate         (const Texture* texture, const Sampler* sample) -> uint64_t;
    auto get_shader_handle(const Texture* texture, const Sampler* sample) -> uint64_t;
    auto bind             (Render_command_encoder& encoder) -> std::size_t;
    void unbind           (Command_buffer& command_buffer);

protected:
    // Per-pass descriptor set snapshot. Draws are only recorded into command
    // buffers and all command buffers of a frame execute after the frame's
    // single end-of-frame submit, so slot descriptors written by a later pass
    // must not overwrite descriptors a recorded-but-unretired pass references.
    // Each reset_heap() therefore acquires a descriptor set of its own:
    // recycled from a pass whose frame the GPU has retired
    // (Device_impl::is_frame_completed()), or newly allocated from the pools.
    class Set_entry
    {
    public:
        VkDescriptorSet set          {VK_NULL_HANDLE};
        uint64_t        frame        {0}; // frame index of last acquisition
        std::size_t     written_count{0}; // slots holding non-fallback descriptors
    };

    static constexpr std::size_t s_invalid_set_index = ~static_cast<std::size_t>(0);

    void acquire_descriptor_set();
    auto allocate_new_set      () -> VkDescriptorSet;
    void write_fallback_slots  (VkDescriptorSet set, std::size_t slot_count);

    Device_impl&                       m_device_impl;
    const Bind_group_layout*           m_bind_group_layout{nullptr};
    const Texture&                     m_fallback_texture;
    const Sampler&                     m_fallback_sampler;
    std::vector<const Texture*>        m_textures;
    std::vector<const Sampler*>        m_samplers;
    std::size_t                        m_used_slot_count{0};
    std::vector<VkDescriptorPool>      m_descriptor_pools;
    VkDescriptorSetLayout              m_descriptor_set_layout{VK_NULL_HANDLE};
    std::vector<Set_entry>             m_set_entries;
    std::size_t                        m_current_set_index{s_invalid_set_index};
    VkImageView                        m_fallback_view{VK_NULL_HANDLE};
    VkSampler                          m_fallback_vk_sampler{VK_NULL_HANDLE};
    std::vector<VkDescriptorImageInfo> m_scratch_image_infos; // batched fallback writes; capacity kept
    std::size_t                        m_max_textures{256};
};

} // namespace erhe::graphics
