#include "erhe_graphics/vulkan/vulkan_bind_group_layout.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"

#include <vector>

namespace erhe::graphics {

namespace {

auto to_vulkan_descriptor_type(const Binding_type type) -> VkDescriptorType
{
    switch (type) {
        case Binding_type::uniform_buffer:         return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case Binding_type::storage_buffer:         return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case Binding_type::combined_image_sampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        default:                                   return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}

} // anonymous namespace

Bind_group_layout_impl::Bind_group_layout_impl(
    Device&                             device,
    const Bind_group_layout_create_info& create_info
)
    : m_device_impl {device.get_impl()}
    , m_vulkan_device{m_device_impl.get_vulkan_device()}
    , m_debug_label  {create_info.debug_label}
{
    // Compute sampler binding offset: one past the highest buffer binding
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

    static constexpr uint32_t sampler_slot_count = 16;

    std::vector<VkDescriptorSetLayoutBinding> vk_bindings;
    vk_bindings.reserve(create_info.bindings.size());

    for (const Bind_group_layout_binding& binding : create_info.bindings) {
        VkShaderStageFlags stage_flags = VK_SHADER_STAGE_ALL;
        if (binding.type == Binding_type::combined_image_sampler) {
            stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        // Sampler bindings are offset past buffer bindings in Vulkan
        // (OpenGL has separate namespaces, Vulkan shares one)
        uint32_t vk_binding_point = binding.binding_point;
        if (binding.type == Binding_type::combined_image_sampler) {
            vk_binding_point += m_sampler_binding_offset;
        }
        vk_bindings.push_back(VkDescriptorSetLayoutBinding{
            .binding            = vk_binding_point,
            .descriptorType     = to_vulkan_descriptor_type(binding.type),
            .descriptorCount    = binding.descriptor_count,
            .stageFlags         = stage_flags,
            .pImmutableSamplers = nullptr
        });
    }

    VkDescriptorSetLayoutCreateFlags layout_flags = 0;
    if (m_device_impl.has_push_descriptor()) {
        layout_flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    }

    const VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext        = nullptr,
        .flags        = layout_flags,
        .bindingCount = static_cast<uint32_t>(vk_bindings.size()),
        .pBindings    = vk_bindings.data()
    };

    VkResult result = vkCreateDescriptorSetLayout(m_vulkan_device, &descriptor_set_layout_create_info, nullptr, &m_descriptor_set_layout);
    if (result != VK_SUCCESS) {
        log_context->critical("Bind_group_layout: vkCreateDescriptorSetLayout() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    // Push constant range for draw ID (only needed when emulating multi-draw indirect)
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset     = 0,
        .size       = sizeof(int32_t)
    };
    const bool emulate_multi_draw = m_device_impl.get_device().get_info().emulate_multi_draw_indirect;

    // Pipeline layout with set 0 (this layout) and optionally set 1 (texture heap from device)
    std::vector<VkDescriptorSetLayout> set_layouts;
    set_layouts.push_back(m_descriptor_set_layout);
    VkDescriptorSetLayout texture_set_layout = m_device_impl.get_texture_set_layout();
    if (texture_set_layout != VK_NULL_HANDLE) {
        set_layouts.push_back(texture_set_layout);
    }

    const VkPipelineLayoutCreateInfo pipeline_layout_create_info{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .setLayoutCount         = static_cast<uint32_t>(set_layouts.size()),
        .pSetLayouts            = set_layouts.data(),
        .pushConstantRangeCount = emulate_multi_draw ? 1u : 0u,
        .pPushConstantRanges    = emulate_multi_draw ? &push_constant_range : nullptr
    };

    result = vkCreatePipelineLayout(m_vulkan_device, &pipeline_layout_create_info, nullptr, &m_pipeline_layout);
    if (result != VK_SUCCESS) {
        log_context->critical("Bind_group_layout: vkCreatePipelineLayout() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
}

Bind_group_layout_impl::~Bind_group_layout_impl() noexcept
{
    if (m_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_vulkan_device, m_pipeline_layout, nullptr);
    }
    if (m_descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_vulkan_device, m_descriptor_set_layout, nullptr);
    }
}

auto Bind_group_layout_impl::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_debug_label;
}

auto Bind_group_layout_impl::get_descriptor_set_layout() const -> VkDescriptorSetLayout
{
    return m_descriptor_set_layout;
}

auto Bind_group_layout_impl::get_pipeline_layout() const -> VkPipelineLayout
{
    return m_pipeline_layout;
}

auto Bind_group_layout_impl::get_sampler_binding_offset() const -> uint32_t
{
    return m_sampler_binding_offset;
}

} // namespace erhe::graphics
