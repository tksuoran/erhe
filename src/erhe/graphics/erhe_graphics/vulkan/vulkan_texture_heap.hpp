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

    void reset_heap       ();
    auto allocate         (const Texture* texture, const Sampler* sample) -> uint64_t;
    auto get_shader_handle(const Texture* texture, const Sampler* sample) -> uint64_t;
    auto bind             (Render_command_encoder& encoder) -> std::size_t;
    void unbind           ();

protected:
    Device_impl&                m_device_impl;
    const Bind_group_layout*    m_bind_group_layout{nullptr};
    const Texture&              m_fallback_texture;
    const Sampler&              m_fallback_sampler;
    std::vector<const Texture*> m_textures;
    std::vector<const Sampler*> m_samplers;
    std::size_t                 m_used_slot_count{0};
    VkDescriptorPool            m_descriptor_pool{VK_NULL_HANDLE};
    VkDescriptorSetLayout       m_descriptor_set_layout{VK_NULL_HANDLE};
    VkDescriptorSet             m_descriptor_set{VK_NULL_HANDLE};
    std::size_t                 m_max_textures{256};
};

} // namespace erhe::graphics
