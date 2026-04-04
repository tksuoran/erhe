#pragma once

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_utility/debug_label.hpp"

#include <volk.h>

namespace erhe::graphics {

class Device;
class Device_impl;

class Bind_group_layout_impl
{
public:
    Bind_group_layout_impl(Device& device, const Bind_group_layout_create_info& create_info);
    ~Bind_group_layout_impl() noexcept;
    Bind_group_layout_impl(const Bind_group_layout_impl&) = delete;
    void operator=(const Bind_group_layout_impl&) = delete;
    Bind_group_layout_impl(Bind_group_layout_impl&&) = delete;
    void operator=(Bind_group_layout_impl&&) = delete;

    [[nodiscard]] auto get_debug_label           () const -> erhe::utility::Debug_label;
    [[nodiscard]] auto get_descriptor_set_layout () const -> VkDescriptorSetLayout;
    [[nodiscard]] auto get_pipeline_layout       () const -> VkPipelineLayout;
    [[nodiscard]] auto get_sampler_binding_offset() const -> uint32_t;

private:
    Device_impl&               m_device_impl;
    VkDevice                   m_vulkan_device            {VK_NULL_HANDLE};
    VkDescriptorSetLayout      m_descriptor_set_layout    {VK_NULL_HANDLE};
    VkPipelineLayout           m_pipeline_layout          {VK_NULL_HANDLE};
    uint32_t                   m_sampler_binding_offset   {0};
    erhe::utility::Debug_label m_debug_label;
};

} // namespace erhe::graphics
