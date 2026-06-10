#include "erhe_graphics/vulkan/vulkan_bind_group_layout.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_sampler.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_verify/verify.hpp"

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

auto to_vulkan_shader_stage_flags(const uint32_t stage_flags) -> VkShaderStageFlags
{
    // all maps to VK_SHADER_STAGE_ALL (covers tessellation too, matching the
    // previous default for buffer bindings); narrower masks map bit by bit.
    if (stage_flags == Shader_stage_flags::all) {
        return VK_SHADER_STAGE_ALL;
    }
    VkShaderStageFlags result = 0;
    if ((stage_flags & Shader_stage_flags::vertex)   != 0u) { result |= VK_SHADER_STAGE_VERTEX_BIT;   }
    if ((stage_flags & Shader_stage_flags::fragment) != 0u) { result |= VK_SHADER_STAGE_FRAGMENT_BIT; }
    if ((stage_flags & Shader_stage_flags::geometry) != 0u) { result |= VK_SHADER_STAGE_GEOMETRY_BIT; }
    if ((stage_flags & Shader_stage_flags::compute)  != 0u) { result |= VK_SHADER_STAGE_COMPUTE_BIT;  }
    return result;
}

} // anonymous namespace

Bind_group_layout_impl::Bind_group_layout_impl(
    Device&                             device,
    const Bind_group_layout_create_info& create_info
)
    : m_device_impl          {device.get_impl()}
    , m_vulkan_device        {m_device_impl.get_vulkan_device()}
    , m_debug_label          {create_info.debug_label}
    , m_default_uniform_block{device}
{
    // Mirror every combined_image_sampler binding into the default uniform
    // block. The shader source emitter injects these as
    // "uniform sampler* s_foo;" lines into the GLSL preamble; Vulkan's
    // SPIR-V backend then translates them into descriptor set 0 bindings
    // offset by m_sampler_binding_offset (computed below).
    //
    // Texture-heap samplers auto-allocate from m_location -- add_sampler
    // asserts against dedicated_texture_unit on them. The auto-alloc
    // lands on the caller's binding_point as long as earlier bindings
    // were in ascending order.
    for (const Bind_group_layout_binding& binding : create_info.bindings) {
        if (binding.type != Binding_type::combined_image_sampler) {
            continue;
        }
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
    }
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

    // Storage for VkSampler arrays referenced by VkDescriptorSetLayoutBinding::
    // pImmutableSamplers entries. Each binding that opts into immutable
    // samplers gets one VkSampler appended here; the address must remain
    // valid through vkCreateDescriptorSetLayout(), so we keep the vector's
    // capacity stable across the loop with reserve().
    std::vector<VkSampler> immutable_sampler_storage;
    immutable_sampler_storage.reserve(create_info.bindings.size());

    for (const Bind_group_layout_binding& binding : create_info.bindings) {
        // Stage visibility is declared per binding, not assumed from the binding
        // type. Every binding -- buffer (UBO/SSBO) and combined_image_sampler
        // alike -- must declare the stages it is accessed in. Leaving stage_flags
        // at the default Shader_stage_flags::none is a configuration error:
        // silently mapping it to a stage (to 0 = invisible, or to all = the
        // opposite) would reintroduce the stage-mismatch hazard, so it is caught
        // here.
        if (binding.stage_flags == Shader_stage_flags::none) {
            ERHE_FATAL(
                "Bind_group_layout binding %u ('%.*s') declares no shader stages (Shader_stage_flags::none); set Bind_group_layout_binding::stage_flags to the stages it is accessed in",
                binding.binding_point,
                static_cast<int>(binding.name.size()),
                binding.name.data()
            );
        }
        const VkShaderStageFlags stage_flags = to_vulkan_shader_stage_flags(binding.stage_flags);
        if (binding.type == Binding_type::combined_image_sampler) {
            m_has_sampler_bindings = true;
        }
        // Sampler bindings are offset past buffer bindings in Vulkan
        // (OpenGL has separate namespaces, Vulkan shares one)
        uint32_t vk_binding_point = binding.binding_point;
        const VkSampler* p_immutable_samplers = nullptr;
        bool             is_immutable         = false;
        if (binding.type == Binding_type::combined_image_sampler) {
            vk_binding_point += m_sampler_binding_offset;
            if (binding.immutable_sampler != nullptr) {
                immutable_sampler_storage.push_back(binding.immutable_sampler->get_impl().get_vulkan_sampler());
                p_immutable_samplers = &immutable_sampler_storage.back();
                is_immutable = true;
            }
            m_sampler_bindings.push_back(Sampler_binding_info{
                .user_binding_point = binding.binding_point,
                .vk_binding_point   = vk_binding_point,
                .aspect             = binding.sampler_aspect,
                .is_immutable       = is_immutable
            });
        }
        vk_bindings.push_back(VkDescriptorSetLayoutBinding{
            .binding            = vk_binding_point,
            .descriptorType     = to_vulkan_descriptor_type(binding.type),
            .descriptorCount    = binding.descriptor_count,
            .stageFlags         = stage_flags,
            .pImmutableSamplers = p_immutable_samplers
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

    // Pipeline layout with set 0 (this layout) and optionally set 1 (texture heap from device).
    // Set 1 is only included when the caller opts in -- otherwise compute and
    // other non-sampling pipelines declare a layout whose set is never bound,
    // which produces Vulkan validation warnings.
    std::vector<VkDescriptorSetLayout> set_layouts;
    set_layouts.push_back(m_descriptor_set_layout);
    if (create_info.uses_texture_heap) {
        VkDescriptorSetLayout texture_set_layout = m_device_impl.get_texture_set_layout();
        if (texture_set_layout != VK_NULL_HANDLE) {
            set_layouts.push_back(texture_set_layout);
        }
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
    const VkDevice              vulkan_device         = m_vulkan_device;
    const VkPipelineLayout      pipeline_layout       = m_pipeline_layout;
    const VkDescriptorSetLayout descriptor_set_layout = m_descriptor_set_layout;
    m_pipeline_layout      = VK_NULL_HANDLE;
    m_descriptor_set_layout = VK_NULL_HANDLE;

    m_device_impl.add_completion_handler(
        [vulkan_device, pipeline_layout, descriptor_set_layout](Device_impl&) {
            if (pipeline_layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(vulkan_device, pipeline_layout, nullptr);
            }
            if (descriptor_set_layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(vulkan_device, descriptor_set_layout, nullptr);
            }
        }
    );
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

auto Bind_group_layout_impl::get_default_uniform_block() const -> const Shader_resource&
{
    return m_default_uniform_block;
}

auto Bind_group_layout_impl::has_sampler_bindings() const -> bool
{
    return m_has_sampler_bindings;
}

auto Bind_group_layout_impl::get_sampler_aspect_for_binding(const uint32_t user_binding_point) const -> Sampler_aspect
{
    for (const Sampler_binding_info& info : m_sampler_bindings) {
        if (info.user_binding_point == user_binding_point) {
            return info.aspect;
        }
    }
    // Reasonable default; callers shouldn't reach this path because
    // set_sampled_image() is only valid for declared sampler bindings.
    return Sampler_aspect::color;
}

auto Bind_group_layout_impl::get_vulkan_binding_for_sampler(const uint32_t user_binding_point) const -> uint32_t
{
    for (const Sampler_binding_info& info : m_sampler_bindings) {
        if (info.user_binding_point == user_binding_point) {
            return info.vk_binding_point;
        }
    }
    return user_binding_point + m_sampler_binding_offset;
}

auto Bind_group_layout_impl::is_sampler_binding_immutable(const uint32_t user_binding_point) const -> bool
{
    for (const Sampler_binding_info& info : m_sampler_bindings) {
        if (info.user_binding_point == user_binding_point) {
            return info.is_immutable;
        }
    }
    return false;
}

} // namespace erhe::graphics
