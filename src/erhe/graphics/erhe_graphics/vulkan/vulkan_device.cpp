#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan_external_creators.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_command_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device_sync_pool.hpp"
#include "erhe_graphics/vulkan/vulkan_gpu_timer.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_sampler.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/swapchain.hpp"

#include "erhe_utility/bit_helpers.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_compute_command_encoder.hpp"
#include "erhe_graphics/vulkan/vulkan_debug.hpp"
#include "erhe_graphics/vulkan/vulkan_render_command_encoder.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/align.hpp"
#include "erhe_window/renderdoc_capture.hpp"
#include "erhe_window/window.hpp"

#include "volk.h"
#include "vk_mem_alloc.h"

#if !defined(WIN32)
#   include <csignal>
#endif

#include "erhe_graphics/renderdoc_app.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

// https://vulkan.lunarg.com/doc/sdk/1.4.328.1/windows/khronos_validation_layer.html

namespace erhe::graphics {

auto c_str(const Device_frame_state state) -> const char*
{
    switch (state) {
        case Device_frame_state::idle:               return "idle";
        case Device_frame_state::waited:             return "waited";
        case Device_frame_state::recording:          return "recording";
        case Device_frame_state::in_swapchain_frame: return "in_swapchain_frame";
    }
    return "?";
}

void Device_impl::set_state(const Device_frame_state new_state, const char* const site)
{
    static_cast<void>(site);
    ERHE_VULKAN_SYNC_TRACE(
        "[STATE] {} -> {} (at {}), frame_index={}, slot={}",
        c_str(m_state), c_str(new_state), site,
        m_frame_index,
        m_frame_index % get_number_of_frames_in_flight()
    );
    m_state = new_state;
}

auto Device_impl::get_device_frame_in_flight(size_t index) -> Device_frame_in_flight&
{
    ERHE_VERIFY(index < m_device_submit_history.size());
    return m_device_submit_history[index];
}

void Device_impl::ensure_device_frame_slot(size_t index)
{
    // Extracted from Swapchain_impl::setup_frame's device-scope block:
    // wait on the prior-cycle fence, recycle the fence and command pool,
    // then install a fresh fence and ensure the command buffer exists.
    // Same fence flow and same guard (submit_fence != NULL) as setup_frame;
    // no swapchain primitives touched. See plan: milestone 2.
    ERHE_VERIFY(index < m_device_submit_history.size());
    Device_frame_in_flight& df = m_device_submit_history[index];
    if (df.submit_fence != VK_NULL_HANDLE) {
        const VkResult result = vkWaitForFences(m_vulkan_device, 1, &df.submit_fence, true, UINT64_MAX);
        if (result != VK_SUCCESS) {
            log_context->critical("vkWaitForFences() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }
        m_sync_pool->recycle_fence(df.submit_fence);
        reset_device_frame_command_pool(index);
        // The submit fence has signaled, so all command buffers
        // allocated last cycle from this slot's per-thread pools have
        // completed on the GPU. Drop the wrapper objects (which release
        // the VkCommandBuffer handles back to the pool) and reset every
        // pool wholesale; the TRANSIENT pool flag makes
        // vkResetCommandPool the cheap path here.
        auto& thread_pools = m_command_pools[index];
        for (Per_thread_command_pool& thread_pool : thread_pools) {
            thread_pool.allocated_command_buffers.clear();
            if (thread_pool.command_pool != VK_NULL_HANDLE) {
                const VkResult reset_result = vkResetCommandPool(m_vulkan_device, thread_pool.command_pool, 0);
                if (reset_result != VK_SUCCESS) {
                    log_context->critical(
                        "vkResetCommandPool() failed with {} {}",
                        static_cast<int32_t>(reset_result), c_str(reset_result)
                    );
                    abort();
                }
            }
        }
        df.submit_fence = VK_NULL_HANDLE;
    }
    df.submit_fence = m_sync_pool->get_fence();
    ensure_device_frame_command_buffer(index);
}

void Device_impl::ensure_device_frame_command_buffer(size_t index)
{
    ERHE_VERIFY(index < m_device_submit_history.size());
    Device_frame_in_flight& device_frame = m_device_submit_history[index];
    if (device_frame.command_pool != VK_NULL_HANDLE) {
        return;
    }

    const VkCommandPoolCreateInfo command_pool_create_info{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = m_present_queue_family_index
    };
    VkResult result = vkCreateCommandPool(m_vulkan_device, &command_pool_create_info, nullptr, &device_frame.command_pool);
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateCommandPool() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    const VkCommandBufferAllocateInfo command_buffer_allocate_info{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = device_frame.command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    result = vkAllocateCommandBuffers(m_vulkan_device, &command_buffer_allocate_info, &device_frame.command_buffer);
    if (result != VK_SUCCESS) {
        log_context->critical("vkAllocateCommandBuffer() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    set_debug_label(
        VK_OBJECT_TYPE_COMMAND_BUFFER,
        reinterpret_cast<uint64_t>(device_frame.command_buffer),
        fmt::format("Device frame in flight command buffer {}", index).c_str()
    );
}

void Device_impl::reset_device_frame_command_pool(size_t index)
{
    ERHE_VERIFY(index < m_device_submit_history.size());
    Device_frame_in_flight& device_frame = m_device_submit_history[index];
    if (device_frame.command_pool != VK_NULL_HANDLE) {
        vkResetCommandPool(m_vulkan_device, device_frame.command_pool, 0);
    }
}

Device_impl::~Device_impl() noexcept
{
    vkDeviceWaitIdle(m_vulkan_device);

    if (m_gpu_timer_query_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_vulkan_device, m_gpu_timer_query_pool, nullptr);
        m_gpu_timer_query_pool = VK_NULL_HANDLE;
    }

    for (auto& [hash, pipeline] : m_pipeline_map) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_vulkan_device, pipeline, nullptr);
        }
    }
    m_pipeline_map.clear();
    for (auto& [hash, render_pass] : m_compatible_render_pass_map) {
        if (render_pass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_vulkan_device, render_pass, nullptr);
        }
    }
    m_compatible_render_pass_map.clear();
    if (m_per_frame_descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_vulkan_device, m_per_frame_descriptor_pool, nullptr);
    }
    if (m_texture_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_vulkan_device, m_texture_set_layout, nullptr);
    }
    if (m_descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_vulkan_device, m_descriptor_set_layout, nullptr);
    }
    if (m_pipeline_cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(m_vulkan_device, m_pipeline_cache, nullptr);
    }
    // NOTE: This adds completion handlers for destroying related vulkan objects
    m_surface.reset();

    // Drop the per-thread Command_buffer wrappers before m_sync_pool is
    // released: each Command_buffer_impl destructor recycles its
    // implicit fence + semaphore back into the pool. The VkCommandBuffer
    // handles they reference remain valid because the pools they were
    // allocated from are destroyed below.
    for (auto& thread_pools : m_command_pools) {
        for (Per_thread_command_pool& thread_pool : thread_pools) {
            thread_pool.allocated_command_buffers.clear();
        }
    }

    // Sync pool outlives Swapchain_impl: Swapchain's dtor pushes its final
    // acquire/present semaphores back into the pool, so the pool must be
    // destroyed after Swapchain. Still runs before vkDestroyDevice below.
    m_sync_pool.reset();

    // Destroy ring buffers before running completion handlers -- their destructors
    // add completion handlers for VMA deallocation
    m_ring_buffers.clear();

    vkDeviceWaitIdle(m_vulkan_device);

    for (Device_frame_in_flight& device_frame : m_device_submit_history) {
        if (device_frame.command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_vulkan_device, device_frame.command_pool, nullptr);
            device_frame.command_pool   = VK_NULL_HANDLE;
            device_frame.command_buffer = VK_NULL_HANDLE;
        }
    }

    // Per-(frame_in_flight, thread_slot) pools. The cb wrappers were
    // dropped above (so their VkCommandBuffer handles are released back
    // to the pool); now destroy the pools themselves.
    for (auto& thread_pools : m_command_pools) {
        for (Per_thread_command_pool& thread_pool : thread_pools) {
            if (thread_pool.command_pool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(m_vulkan_device, thread_pool.command_pool, nullptr);
                thread_pool.command_pool = VK_NULL_HANDLE;
            }
        }
    }

    for (const Completion_handler& entry : m_completion_handlers) {
        entry.callback(*this);
    }

    if (m_vulkan_frame_end_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_vulkan_device, m_vulkan_frame_end_semaphore, nullptr);
    }

    // Dump live VMA allocations to help diagnose leaks
    {
        char* stats_string = nullptr;
        vmaBuildStatsString(m_vma_allocator, &stats_string, VK_TRUE);
        if (stats_string != nullptr) {
            log_context->info("VMA stats before destroy:\n{}", stats_string);
            vmaFreeStatsString(m_vma_allocator, stats_string);
        }
    }

    vmaDestroyAllocator(m_vma_allocator);
    if (m_vulkan_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_vulkan_device, nullptr);
    }
    if (m_debug_utils_messenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(m_vulkan_instance, m_debug_utils_messenger, nullptr);
    }
    vkDestroyInstance(m_vulkan_instance, nullptr);
    volkFinalize();
}

auto Device_impl::get_pipeline_cache() const -> VkPipelineCache
{
    return m_pipeline_cache;
}

auto Device_impl::get_descriptor_set_layout() const -> VkDescriptorSetLayout
{
    return m_descriptor_set_layout;
}

auto Device_impl::has_push_descriptor() const -> bool
{
    return m_device_extensions.m_VK_KHR_push_descriptor;
}

auto Device_impl::get_texture_set_layout() const -> VkDescriptorSetLayout
{
    return m_texture_set_layout;
}

auto Device_impl::allocate_descriptor_set() -> VkDescriptorSet
{
    if (m_per_frame_descriptor_pool == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    const VkDescriptorSetAllocateInfo allocate_info{
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext              = nullptr,
        .descriptorPool     = m_per_frame_descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &m_descriptor_set_layout
    };

    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(m_vulkan_device, &allocate_info, &descriptor_set);
    if (result != VK_SUCCESS) {
        log_context->warn("vkAllocateDescriptorSets() failed with {}", static_cast<int32_t>(result));
        return VK_NULL_HANDLE;
    }

    ++m_desc_alloc_set_count;
    ERHE_VULKAN_DESC_TRACE("[ALLOC_SET]");
    return descriptor_set;
}

void Device_impl::reset_descriptor_pool()
{
    ERHE_VULKAN_DESC_TRACE(
        "[FRAME_SUMMARY] frame={} push_buf={} push_img={} alloc_set={} heap_binds={} draws={}",
        m_frame_index,
        m_desc_push_buf_count,
        m_desc_push_img_count,
        m_desc_alloc_set_count,
        m_desc_heap_bind_count,
        m_desc_draw_count
    );
    m_desc_push_buf_count  = 0;
    m_desc_push_img_count  = 0;
    m_desc_alloc_set_count = 0;
    m_desc_heap_bind_count = 0;
    m_desc_draw_count      = 0;

    if (m_per_frame_descriptor_pool != VK_NULL_HANDLE) {
        vkResetDescriptorPool(m_vulkan_device, m_per_frame_descriptor_pool, 0);
    }
    ERHE_VULKAN_DESC_TRACE("[POOL_RESET] frame={}", m_frame_index);
}

auto Device_impl::get_cached_pipeline(const std::size_t hash) -> VkPipeline
{
    std::lock_guard<std::mutex> lock{m_pipeline_map_mutex};
    auto it = m_pipeline_map.find(hash);
    if (it != m_pipeline_map.end()) {
        return it->second;
    }
    return VK_NULL_HANDLE;
}

auto Device_impl::create_graphics_pipeline(const VkGraphicsPipelineCreateInfo& create_info, const std::size_t hash) -> VkPipeline
{
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(m_vulkan_device, m_pipeline_cache, 1, &create_info, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        log_program->error("vkCreateGraphicsPipelines() failed with {}", static_cast<int32_t>(result));
        return VK_NULL_HANDLE;
    }

    set_debug_label(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(pipeline),
        fmt::format("Pipeline hash={:016x}", hash).c_str());

    std::lock_guard<std::mutex> lock{m_pipeline_map_mutex};
    m_pipeline_map[hash] = pipeline;
    return pipeline;
}

auto Device_impl::get_or_create_graphics_pipeline(const VkGraphicsPipelineCreateInfo& create_info, const std::size_t hash) -> VkPipeline
{
    VkPipeline cached = get_cached_pipeline(hash);
    if (cached != VK_NULL_HANDLE) {
        return cached;
    }
    return create_graphics_pipeline(create_info, hash);
}

auto Device_impl::get_or_create_compatible_render_pass(
    const unsigned int                             color_attachment_count,
    const std::array<erhe::dataformat::Format, 4>& color_attachment_formats,
    const erhe::dataformat::Format                 depth_attachment_format,
    const erhe::dataformat::Format                 stencil_attachment_format,
    const unsigned int                             sample_count,
    VkPipelineStageFlags                           incoming_src_stage,
    VkAccessFlags                                  incoming_src_access,
    VkPipelineStageFlags                           incoming_dst_stage,
    VkAccessFlags                                  incoming_dst_access,
    VkPipelineStageFlags                           outgoing_src_stage,
    VkAccessFlags                                  outgoing_src_access,
    VkPipelineStageFlags                           outgoing_dst_stage,
    VkAccessFlags                                  outgoing_dst_access
) -> VkRenderPass
{
    // Compute hash from format tuple and dependency masks
    auto hash_combine = [](std::size_t& seed, std::size_t value) {
        seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    };

    std::size_t hash = 0;
    hash_combine(hash, static_cast<std::size_t>(color_attachment_count));
    for (unsigned int i = 0; i < color_attachment_count; ++i) {
        hash_combine(hash, static_cast<std::size_t>(color_attachment_formats[i]));
    }
    hash_combine(hash, static_cast<std::size_t>(depth_attachment_format));
    hash_combine(hash, static_cast<std::size_t>(stencil_attachment_format));
    hash_combine(hash, static_cast<std::size_t>(sample_count));
    hash_combine(hash, static_cast<std::size_t>(incoming_src_stage));
    hash_combine(hash, static_cast<std::size_t>(incoming_src_access));
    hash_combine(hash, static_cast<std::size_t>(incoming_dst_stage));
    hash_combine(hash, static_cast<std::size_t>(incoming_dst_access));
    hash_combine(hash, static_cast<std::size_t>(outgoing_src_stage));
    hash_combine(hash, static_cast<std::size_t>(outgoing_src_access));
    hash_combine(hash, static_cast<std::size_t>(outgoing_dst_stage));
    hash_combine(hash, static_cast<std::size_t>(outgoing_dst_access));

    {
        std::lock_guard<std::mutex> lock{m_compatible_render_pass_mutex};
        auto it = m_compatible_render_pass_map.find(hash);
        if (it != m_compatible_render_pass_map.end()) {
            return it->second;
        }
    }

    // Create a new compatible render pass
    const VkSampleCountFlagBits vk_sample_count = get_vulkan_sample_count(sample_count);

    std::vector<VkAttachmentDescription2> attachments;
    std::vector<VkAttachmentReference2>   color_refs;

    // Color attachments
    for (unsigned int i = 0; i < color_attachment_count; ++i) {
        VkFormat vk_format = to_vulkan(color_attachment_formats[i]);
        attachments.push_back(VkAttachmentDescription2{
            .sType          = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
            .pNext          = nullptr,
            .flags          = 0,
            .format         = vk_format,
            .samples        = vk_sample_count,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        });
        color_refs.push_back(VkAttachmentReference2{
            .sType      = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
            .pNext      = nullptr,
            .attachment = static_cast<uint32_t>(attachments.size() - 1),
            .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT
        });
    }

    // Depth/stencil attachment
    VkAttachmentReference2 depth_ref{
        .sType      = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
        .pNext      = nullptr,
        .attachment = VK_ATTACHMENT_UNUSED,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT
    };
    const bool has_depth   = (depth_attachment_format   != erhe::dataformat::Format::format_undefined);
    const bool has_stencil = (stencil_attachment_format != erhe::dataformat::Format::format_undefined);
    if (has_depth || has_stencil) {
        VkFormat depth_vk_format = has_depth
            ? to_vulkan(depth_attachment_format)
            : to_vulkan(stencil_attachment_format);
        attachments.push_back(VkAttachmentDescription2{
            .sType          = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
            .pNext          = nullptr,
            .flags          = 0,
            .format         = depth_vk_format,
            .samples        = vk_sample_count,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        });
        depth_ref.attachment = static_cast<uint32_t>(attachments.size() - 1);
        depth_ref.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
            (has_stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0u);
    }

    const VkSubpassDescription2 subpass{
        .sType                   = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
        .pNext                   = nullptr,
        .flags                   = 0,
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .viewMask                = 0,
        .inputAttachmentCount    = 0,
        .pInputAttachments       = nullptr,
        .colorAttachmentCount    = static_cast<uint32_t>(color_refs.size()),
        .pColorAttachments       = color_refs.empty() ? nullptr : color_refs.data(),
        .pResolveAttachments     = nullptr,
        .pDepthStencilAttachment = (depth_ref.attachment != VK_ATTACHMENT_UNUSED) ? &depth_ref : nullptr,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments    = nullptr
    };

    // Dependencies must match the actual render passes for validation compatibility.
    // Use caller-provided masks, or derive defaults from attachments.
    if (incoming_src_stage == 0 && incoming_dst_stage == 0 && outgoing_src_stage == 0 && outgoing_dst_stage == 0) {
        incoming_src_stage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        incoming_src_access = VK_ACCESS_SHADER_READ_BIT;
        outgoing_dst_stage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        outgoing_dst_access = VK_ACCESS_SHADER_READ_BIT;
        if (color_attachment_count > 0) {
            incoming_dst_stage  |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            incoming_dst_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            outgoing_src_stage  |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            outgoing_src_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }
        if (has_depth || has_stencil) {
            incoming_dst_stage  |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            incoming_dst_access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            outgoing_src_stage  |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            outgoing_src_access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
    }
    if (incoming_src_stage == 0) {
        incoming_src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    if (incoming_dst_stage == 0) {
        incoming_dst_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    if (outgoing_src_stage == 0) {
        outgoing_src_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    if (outgoing_dst_stage == 0) {
        outgoing_dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    // The validation layer compares subpass dependency arrays as part of
    // render-pass compatibility, so the compatibility render pass must use
    // the exact same canonical dependencies that the in-use render passes
    // produce. The caller-supplied stage/access masks are intentionally
    // ignored here and only the structural (has_color / has_depth_stencil)
    // bits drive the dependencies.
    static_cast<void>(incoming_src_stage);
    static_cast<void>(incoming_src_access);
    static_cast<void>(incoming_dst_stage);
    static_cast<void>(incoming_dst_access);
    static_cast<void>(outgoing_src_stage);
    static_cast<void>(outgoing_src_access);
    static_cast<void>(outgoing_dst_stage);
    static_cast<void>(outgoing_dst_access);

    VkSubpassDependency2 canonical_dependencies[2];
    make_canonical_subpass_dependencies2(color_attachment_count > 0, has_depth || has_stencil, canonical_dependencies);

    const VkRenderPassCreateInfo2 render_pass_create_info{
        .sType                   = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
        .pNext                   = nullptr,
        .flags                   = 0,
        .attachmentCount         = static_cast<uint32_t>(attachments.size()),
        .pAttachments            = attachments.empty() ? nullptr : attachments.data(),
        .subpassCount            = 1,
        .pSubpasses              = &subpass,
        .dependencyCount         = 2,
        .pDependencies           = canonical_dependencies,
        .correlatedViewMaskCount = 0,
        .pCorrelatedViewMasks    = nullptr
    };

    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkResult result = vkCreateRenderPass2(m_vulkan_device, &render_pass_create_info, nullptr, &render_pass);
    if (result != VK_SUCCESS) {
        log_render_pass->error(
            "vkCreateRenderPass2() for pipeline compatibility failed with {} {}",
            static_cast<int32_t>(result), c_str(result)
        );
        return VK_NULL_HANDLE;
    }

    {
        std::lock_guard<std::mutex> lock{m_compatible_render_pass_mutex};
        m_compatible_render_pass_map[hash] = render_pass;
    }

    return render_pass;
}

auto Device_impl::choose_physical_device(
    Surface_impl*             surface_impl,
    std::vector<const char*>& device_extensions_c_str
) -> bool
{
    VkPhysicalDevice selected_device{VK_NULL_HANDLE};

    if ((m_external_creators != nullptr) && static_cast<bool>(m_external_creators->pick_physical_device)) {
        log_context->info("Picking Vulkan physical device via external (XR) hook");
        const VkResult xr_pick_result = m_external_creators->pick_physical_device(m_vulkan_instance, &selected_device);
        if ((xr_pick_result != VK_SUCCESS) || (selected_device == VK_NULL_HANDLE)) {
            log_context->critical("External physical device pick failed with {} {}", static_cast<int32_t>(xr_pick_result), c_str(xr_pick_result));
            return false;
        }
    } else {
        uint32_t physical_device_count{0};
        VkResult result{VK_SUCCESS};
        result = vkEnumeratePhysicalDevices(m_vulkan_instance, &physical_device_count, nullptr);
        if (result != VK_SUCCESS) {
            log_context->critical("vkEnumeratePhysicalDevices() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            return false;
        }
        std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
        if (physical_device_count == 0) {
            log_context->critical("vkEnumeratePhysicalDevices() returned 0 physical devices");
            return false;
        }
        result = vkEnumeratePhysicalDevices(m_vulkan_instance, &physical_device_count, physical_devices.data());
        if (result != VK_SUCCESS) {
            log_context->critical("vkEnumeratePhysicalDevices() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            return false;
        }

        float best_score = std::numeric_limits<float>::lowest();
        for (uint32_t physical_device_index = 0; physical_device_index < physical_device_count; ++physical_device_index) {
            const VkPhysicalDevice physical_device = physical_devices[physical_device_index];

            const float score = get_physical_device_score(physical_device, surface_impl);
            if (score > best_score) {
                best_score      = score;
                selected_device = physical_device;
            }
        }

        if (selected_device == VK_NULL_HANDLE) {
            return false;
        }
    }

    m_vulkan_physical_device = selected_device;
    const bool queues_ok = query_device_queue_family_indices(
        m_vulkan_physical_device, surface_impl, &m_graphics_queue_family_index, &m_present_queue_family_index
    );
    if (!queues_ok) {
        return false;
    }

    query_device_extensions(m_vulkan_physical_device, m_device_extensions, &device_extensions_c_str);
    return true;
}

auto Device_impl::get_physical_device_score(VkPhysicalDevice vulkan_physical_device, Surface_impl* surface_impl) -> float
{
    VkPhysicalDeviceProperties device_properties{};
    vkGetPhysicalDeviceProperties(vulkan_physical_device, &device_properties);
    const float device_type_score = 0.0f;
    switch (device_properties.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:          return 101.0f;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 102.0f;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 104.0f;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return 103.0f;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return   0.0f;
        default: {
            log_context->warn("Vulkan device type {:4x} not recognized", static_cast<uint32_t>(device_properties.deviceType));
            return 0.0f; // reject device
        }
    }

    const bool queues_ok = query_device_queue_family_indices(vulkan_physical_device, surface_impl, nullptr, nullptr);
    if (!queues_ok) {
        return 0.0f; // reject device
    }

    Device_extensions device_extensions{};
    const float extension_score = query_device_extensions(vulkan_physical_device, device_extensions, nullptr);

    return device_type_score + extension_score;
}

// Check if device meets queue requirements, optionally returns queue family indices
auto Device_impl::query_device_queue_family_indices(
    VkPhysicalDevice vulkan_physical_device,
    Surface_impl*    surface_impl,
    uint32_t*        graphics_queue_family_index_out,
    uint32_t*        present_queue_family_index_out
) -> bool
{
    VkSurfaceKHR vulkan_surface{VK_NULL_HANDLE};
    if (surface_impl != nullptr) {
        if (!surface_impl->can_use_physical_device(vulkan_physical_device)) {
            return false;
        }
        vulkan_surface = surface_impl->get_vulkan_surface();
    }

    uint32_t queue_family_count{0};
    vkGetPhysicalDeviceQueueFamilyProperties(vulkan_physical_device, &queue_family_count, nullptr);
    if (queue_family_count == 0) {
        return false;
    }

    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(vulkan_physical_device, &queue_family_count, queue_families.data());

    // Require graphics
    // Require present if surface is used
    uint32_t graphics_queue_family_index = UINT32_MAX;
    uint32_t present_queue_family_index  = UINT32_MAX;
    VkResult result{VK_SUCCESS};
    for (uint32_t queue_family_index = 0, end = static_cast<uint32_t>(queue_families.size()); queue_family_index < end; ++queue_family_index) {
        const VkQueueFamilyProperties& queue_family = queue_families[queue_family_index];
        const bool support_graphics = (queue_family.queueCount > 0) && (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT);
        const bool support_present = [&]() -> bool
        {
            if (vulkan_surface == VK_NULL_HANDLE) {
                return false;
            }
            VkBool32 support{VK_FALSE};
            result = vkGetPhysicalDeviceSurfaceSupportKHR(vulkan_physical_device, queue_family_index, vulkan_surface, &support);
            if (result != VK_SUCCESS) {
                log_context->warn("vkGetPhysicalDeviceSurfaceSupportKHR() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            }
            return (result == VK_SUCCESS) && (support == VK_TRUE);
        }();
        if (support_graphics && support_present) {
            graphics_queue_family_index = queue_family_index;
            present_queue_family_index = queue_family_index;
            break;
        }
        if ((graphics_queue_family_index == UINT32_MAX) && support_graphics) {
            graphics_queue_family_index = queue_family_index;
        }
        if ((present_queue_family_index == UINT32_MAX) && support_present) {
            present_queue_family_index = queue_family_index;
        }
    }

    if (graphics_queue_family_index == UINT32_MAX) {
        return false;
    }

    if (
        (surface_impl != nullptr) &&
        (graphics_queue_family_index != present_queue_family_index)
    ) {
        return false;
    }

    if (graphics_queue_family_index_out != nullptr) {
        *graphics_queue_family_index_out = graphics_queue_family_index;
    }
    if (present_queue_family_index_out != nullptr) {
        *present_queue_family_index_out = present_queue_family_index;
    }
    return true;
}

// Gathers available recognized extensions and computes score
auto Device_impl::query_device_extensions(
    VkPhysicalDevice          vulkan_physical_device,
    Device_extensions&        device_extensions_out,
    std::vector<const char*>* device_extensions_c_str
) -> float
{
    float total_score = 0.0f;

    uint32_t device_extension_count{0};
    VkResult result{VK_SUCCESS};
    result = vkEnumerateDeviceExtensionProperties(vulkan_physical_device, nullptr, &device_extension_count, nullptr);
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateDeviceExtensionProperties() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        return 0.0f;
    }
    std::vector<VkExtensionProperties> device_extensions(device_extension_count);
    result = vkEnumerateDeviceExtensionProperties(vulkan_physical_device, nullptr, &device_extension_count, device_extensions.data());
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateDeviceExtensionProperties() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        return 0.0f;
    }

    for (const VkExtensionProperties& extension : device_extensions) {
        log_debug->info("Vulkan Device Extension: {} spec_version {:08x}", extension.extensionName, extension.specVersion);
    }

    // Check device extensions
    auto check_device_extension = [&](const char* name, bool& enable, const float extension_score)
    {
        for (const VkExtensionProperties& extension : device_extensions) {
            if (strcmp(extension.extensionName, name) == 0) {
                if (device_extensions_c_str != nullptr) {
                    device_extensions_c_str->push_back(name);
                    log_debug->info("  Enabling {}", extension.extensionName);
                    enable = true;
                    total_score += extension_score;
                }
                return;
            }
        }
    };

    check_device_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME,                      device_extensions_out.m_VK_KHR_swapchain                     , 1.0f);
    check_device_extension(VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,        device_extensions_out.m_VK_KHR_swapchain_maintenance1        , 2.0f);
    check_device_extension(VK_KHR_LOAD_STORE_OP_NONE_EXTENSION_NAME,             device_extensions_out.m_VK_KHR_load_store_op_none            , 2.0f);
    check_device_extension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,                device_extensions_out.m_VK_KHR_push_descriptor               , 1.0f);
    check_device_extension(VK_KHR_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME, device_extensions_out.m_VK_KHR_present_mode_fifo_latest_ready, 3.0f);

    // VK_KHR_portability_subset must be enabled whenever the physical device
    // advertises it (Vulkan spec VUID-VkDeviceCreateInfo-pProperties-04451).
    // MoltenVK always does, because it is a portability implementation.
    // Using the literal name keeps us independent of vulkan_beta.h /
    // VK_ENABLE_BETA_EXTENSIONS gating of VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME.
    check_device_extension("VK_KHR_portability_subset",                          device_extensions_out.m_VK_KHR_portability_subset            , 0.0f);

    if (!device_extensions_out.m_VK_KHR_load_store_op_none) {
        check_device_extension(VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME,             device_extensions_out.m_VK_EXT_load_store_op_none            , 2.0f);
    }
    if (!device_extensions_out.m_VK_KHR_swapchain_maintenance1) {
        check_device_extension(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,        device_extensions_out.m_VK_EXT_swapchain_maintenance1        , 2.0f);
    }
    if (!device_extensions_out.m_VK_KHR_present_mode_fifo_latest_ready) {
        check_device_extension(VK_EXT_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME, device_extensions_out.m_VK_EXT_present_mode_fifo_latest_ready, 3.0f);
    }
    return total_score;
}

auto Device_impl::get_device() -> Device&
{
    return m_device;
}

auto Device_impl::get_surface() -> Surface*
{
    return m_surface.get();
}

auto Device_impl::get_native_handles() const -> Native_device_handles
{
    Native_device_handles handles{};
    handles.vk_instance           = reinterpret_cast<uint64_t>(m_vulkan_instance);
    handles.vk_physical_device    = reinterpret_cast<uint64_t>(m_vulkan_physical_device);
    handles.vk_device             = reinterpret_cast<uint64_t>(m_vulkan_device);
    handles.vk_queue_family_index = m_graphics_queue_family_index;
    handles.vk_queue_index        = 0;
    return handles;
}

auto Device_impl::get_vulkan_instance() -> VkInstance
{
    return m_vulkan_instance;
}

auto Device_impl::get_vulkan_physical_device() -> VkPhysicalDevice
{
    return m_vulkan_physical_device;
}

auto Device_impl::get_vulkan_device() -> VkDevice
{
    return m_vulkan_device;
}

auto Device_impl::get_graphics_queue_family_index() const -> uint32_t
{
    return m_graphics_queue_family_index;
}

auto Device_impl::get_present_queue_family_index () const -> uint32_t
{
    return m_present_queue_family_index;
}

auto Device_impl::get_graphics_queue() const -> VkQueue
{
    return m_vulkan_graphics_queue;
}

auto Device_impl::get_present_queue() const -> VkQueue
{
    return m_vulkan_present_queue;
}

auto Device_impl::get_capabilities() const -> const Capabilities&
{
    return m_capabilities;
}

auto Device_impl::get_driver_properties() const -> const VkPhysicalDeviceDriverProperties&
{
    return m_driver_properties;
}

auto Device_impl::get_portability_subset_features() const -> const VkPhysicalDevicePortabilitySubsetFeaturesKHR&
{
    return m_portability_subset_features;
}

auto Device_impl::get_portability_subset_properties() const -> const VkPhysicalDevicePortabilitySubsetPropertiesKHR&
{
    return m_portability_subset_properties;
}

auto Device_impl::get_memory_type(uint32_t memory_type_index) const -> const VkMemoryType&
{
    return m_memory_properties.memoryProperties.memoryTypes[memory_type_index];
}

auto Device_impl::get_memory_heap(uint32_t memory_heap_index) const -> const VkMemoryHeap&
{
    return m_memory_properties.memoryProperties.memoryHeaps[memory_heap_index];
}

auto Device_impl::get_handle(const Texture& texture, const Sampler& sampler) const -> uint64_t
{
    // TODO Implement bindless texture handles via descriptor indexing
    static_cast<void>(texture);
    static_cast<void>(sampler);
    return 0;
}

auto Device_impl::create_dummy_texture(Command_buffer& init_command_buffer, const erhe::dataformat::Format format) -> std::shared_ptr<Texture>
{
    // TODO Move function to Device instead of Device_impl?
    const Texture_create_info create_info{
        .device      = m_device,
        .usage_mask  = Image_usage_flag_bit_mask::sampled | Image_usage_flag_bit_mask::transfer_dst,
        .type        = Texture_type::texture_2d,
        .pixelformat = format,
        .width       = 2,
        .height      = 2,
        .debug_label = "dummy"
    };

    auto texture = std::make_shared<Texture>(m_device, create_info);

    const std::size_t bytes_per_pixel   = erhe::dataformat::get_format_size_bytes(format);
    const std::size_t width             = 2;
    const std::size_t height            = 2;
    const std::size_t src_bytes_per_row = width * bytes_per_pixel;
    const std::size_t byte_count        = height * src_bytes_per_row;

    // Fill with a simple pattern -- content doesn't matter much for a dummy texture
    std::vector<uint8_t> dummy_pixels(byte_count, 0);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            std::size_t offset = (y * width + x) * bytes_per_pixel;
            // Fill first 4 bytes with a visible pattern, rest with 0
            std::size_t fill = std::min(bytes_per_pixel, std::size_t{4});
            const uint8_t pattern[4] = {
                static_cast<uint8_t>(((x + y) & 1) ? 0xee : 0xcc),
                0x11,
                static_cast<uint8_t>(((x + y) & 1) ? 0xdd : 0xbb),
                0xff
            };
            memcpy(&dummy_pixels[offset], pattern, fill);
        }
    }

    // Record the pixel upload into the caller-supplied init cb.
    // Caller is responsible for ending + submitting + waiting before
    // the texture is sampled.
    init_command_buffer.upload_to_texture(
        *texture.get(),
        0,                              // level
        0, 0,                           // x, y
        static_cast<int>(width),
        static_cast<int>(height),
        format,
        dummy_pixels.data(),
        static_cast<int>(src_bytes_per_row)
    );

    return texture;
}

auto Device_impl::get_shader_monitor() -> Shader_monitor&
{
    return m_shader_monitor;
}

auto Device_impl::get_info() const -> const Device_info&
{
    return m_info;
}

auto Device_impl::get_graphics_config() const -> const Graphics_config&
{
    return m_graphics_config;
}

auto Device_impl::get_allocator() -> VmaAllocator&
{
    return m_vma_allocator;
}

auto Device_impl::get_buffer_alignment(const Buffer_target target) -> std::size_t
{
    switch (target) {
        case Buffer_target::storage: {
            return m_info.shader_storage_buffer_offset_alignment;
        }

        case Buffer_target::uniform: {
            return m_info.uniform_buffer_offset_alignment;
        }

        case Buffer_target::draw_indirect: {
            // TODO Consider Draw_primitives_indirect_command
            return sizeof(Draw_indexed_primitives_indirect_command);
        }
        default: {
            return 64; // TODO
        }
    }
}

void Device_impl::submit_command_buffers(std::span<Command_buffer* const> command_buffers)
{
    if (command_buffers.empty()) {
        return;
    }

    // CPU-side wait phase: any wait_for_cpu(other) registered on the
    // cbs in this batch is converted into a vkWaitForFences before the
    // submit is enqueued. Done up front so a batched submit never
    // partially blocks.
    for (Command_buffer* command_buffer : command_buffers) {
        ERHE_VERIFY(command_buffer != nullptr);
        command_buffer->get_impl().pre_submit_wait();
    }

    Vulkan_submit_info submit_info_aggregate;
    submit_info_aggregate.command_buffers.reserve(command_buffers.size());
    for (Command_buffer* command_buffer : command_buffers) {
        command_buffer->get_impl().collect_synchronization(submit_info_aggregate);
    }

    const VkSubmitInfo2 submit_info{
        .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .pNext                    = nullptr,
        .flags                    = 0,
        .waitSemaphoreInfoCount   = static_cast<uint32_t>(submit_info_aggregate.wait_semaphores.size()),
        .pWaitSemaphoreInfos      = submit_info_aggregate.wait_semaphores.data(),
        .commandBufferInfoCount   = static_cast<uint32_t>(submit_info_aggregate.command_buffers.size()),
        .pCommandBufferInfos      = submit_info_aggregate.command_buffers.data(),
        .signalSemaphoreInfoCount = static_cast<uint32_t>(submit_info_aggregate.signal_semaphores.size()),
        .pSignalSemaphoreInfos    = submit_info_aggregate.signal_semaphores.data(),
    };

    // Bridge: Swapchain_impl::setup_frame still allocates and waits on
    // Device_frame_in_flight::submit_fence to gate its own per-slot
    // bookkeeping (acquire-semaphore recycle, swapchain_garbage cleanup).
    // The fence used to be signaled by the legacy end_frame submit; that
    // branch is gone, so when a cb engaged the swapchain we attach the
    // slot fence to this submit. Goes away with the legacy
    // setup_frame/submit_fence path.
    VkFence submit_fence = submit_info_aggregate.fence;
    if (!submit_info_aggregate.swapchains_to_present.empty() && (submit_fence == VK_NULL_HANDLE)) {
        const size_t slot = static_cast<size_t>(get_frame_in_flight_index());
        submit_fence = m_device_submit_history[slot].submit_fence;
    }

    log_swapchain->trace(
        "submit_command_buffers: vkQueueSubmit2 cb_count={} wait_sem_count={} signal_sem_count={} fence=0x{:x} swapchains_to_present={}",
        submit_info_aggregate.command_buffers.size(),
        submit_info_aggregate.wait_semaphores.size(),
        submit_info_aggregate.signal_semaphores.size(),
        reinterpret_cast<std::uintptr_t>(submit_fence),
        submit_info_aggregate.swapchains_to_present.size()
    );
    const VkResult result = vkQueueSubmit2(m_vulkan_graphics_queue, 1, &submit_info, submit_fence);
    if (result != VK_SUCCESS) {
        log_context->critical(
            "vkQueueSubmit2() in submit_command_buffers failed with {} {}",
            static_cast<int32_t>(result), c_str(result)
        );
        abort();
    }

    // Implicit present: any cb that engaged a swapchain via
    // begin_swapchain has already had the swapchain's present_semaphore
    // routed into the signal list above. Now drive vkQueuePresentKHR
    // on each so callers don't have to manage a separate present step.
    for (Swapchain_impl* swapchain : submit_info_aggregate.swapchains_to_present) {
        ERHE_VERIFY(swapchain != nullptr);
        const bool present_ok = swapchain->present();
        log_swapchain->trace("submit_command_buffers: implicit present ok={}", present_ok);
        static_cast<void>(present_ok);
    }
}

void Device_impl::add_completion_handler(std::function<void(Device_impl&)> callback)
{
    m_completion_handlers.emplace_back(m_frame_index, std::move(callback));
}

void Device_impl::on_thread_enter()
{
}

void Device_impl::frame_completed(const uint64_t completed_frame)
{
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        ring_buffer->frame_completed(completed_frame);
    }
    for (const Completion_handler& entry : m_completion_handlers) {
        if (entry.frame_number == completed_frame) {
            entry.callback(*this);
        }
    }
    auto i = std::remove_if(
        m_completion_handlers.begin(),
        m_completion_handlers.end(),
        [completed_frame](Completion_handler& entry) { return entry.frame_number == completed_frame; }
    );
    if (i != m_completion_handlers.end()) {
        m_completion_handlers.erase(i, m_completion_handlers.end());
    }
}

auto Device_impl::wait_frame() -> bool
{
    ERHE_VERIFY(m_state == Device_frame_state::idle);

    // Per-(frame_in_flight, thread) command pool reset, gated on the
    // device's timeline semaphore. update_frame_completion (called from
    // end_frame) signals m_vulkan_frame_end_semaphore at value
    // m_frame_index right before incrementing, so once the GPU reaches
    // value F-N the previous use of the slot we're about to enter
    // (frame F-N, slot S = F % N) has fully retired and its cbs +
    // implicit fences/semaphores can be recycled.
    //
    // For the first N frames the slot has never been used so nothing
    // needs waiting on; for F > N we wait until the timeline reports
    // F-N before clearing. The clear destroys each
    // Command_buffer wrapper, which in turn recycles its implicit
    // VkSemaphore / VkFence back to Device_sync_pool.
    const size_t   slot = static_cast<size_t>(get_frame_in_flight_index());
    const uint64_t N    = get_number_of_frames_in_flight();
    if (m_frame_index > N) {
        const uint64_t wait_value = m_frame_index - N;
        const VkSemaphoreWaitInfo wait_info{
            .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .pNext          = nullptr,
            .flags          = 0,
            .semaphoreCount = 1,
            .pSemaphores    = &m_vulkan_frame_end_semaphore,
            .pValues        = &wait_value,
        };
        const VkResult result = vkWaitSemaphores(m_vulkan_device, &wait_info, UINT64_MAX);
        if (result != VK_SUCCESS) {
            log_context->critical(
                "vkWaitSemaphores() in wait_frame failed with {} {}",
                static_cast<int32_t>(result), c_str(result)
            );
            abort();
        }
    }

    auto& thread_pools = m_command_pools[slot];
    for (Per_thread_command_pool& thread_pool : thread_pools) {
        thread_pool.allocated_command_buffers.clear();
        if (thread_pool.command_pool != VK_NULL_HANDLE) {
            const VkResult reset_result = vkResetCommandPool(m_vulkan_device, thread_pool.command_pool, 0);
            if (reset_result != VK_SUCCESS) {
                log_context->critical(
                    "vkResetCommandPool() in wait_frame failed with {} {}",
                    static_cast<int32_t>(reset_result), c_str(reset_result)
                );
                abort();
            }
        }
    }

    // Read GPU timer results from the slice we are about to reuse. The
    // timeline-semaphore wait above has already guaranteed that this
    // slice's previous frame has finished on the GPU, so query results
    // are available. After reading, mark this slice for reset by the
    // first cb of the new frame; the slice is then refilled with this
    // frame's begin/end timestamps.
    if (m_gpu_timers_supported && (m_gpu_timer_query_pool != VK_NULL_HANDLE)) {
        // Snapshot the fired bitmap and check whether any timer wrote in
        // this slice the previous time it was used. If none, skip the
        // vkGetQueryPoolResults call entirely -- not just for performance
        // but because the slice may still be in the "never reset" state on
        // the first frame, where reading is undefined.
        std::array<bool, s_max_gpu_timers> fired_snapshot{};
        bool any_fired = false;
        {
            const std::lock_guard<std::mutex> lock{m_gpu_timer_mutex};
            std::array<bool, s_max_gpu_timers>& fired = m_gpu_timer_fired[slot];
            for (std::size_t i = 0; i < s_max_gpu_timers; ++i) {
                fired_snapshot[i] = fired[i];
                if (fired[i]) {
                    any_fired  = true;
                    fired[i]   = false;
                }
            }
        }

        if (any_fired) {
            const std::size_t slice_size = s_max_gpu_timers * 2;
            const std::size_t slice_base = slot * slice_size;

            // Two uint64_t per query: timestamp + availability. Reuses a
            // single buffer, sized for the whole slice.
            std::array<uint64_t, s_max_gpu_timers * 2 * 2> results{};
            const VkResult get_result = vkGetQueryPoolResults(
                m_vulkan_device,
                m_gpu_timer_query_pool,
                static_cast<uint32_t>(slice_base),
                static_cast<uint32_t>(slice_size),
                sizeof(uint64_t) * 2 * slice_size,
                results.data(),
                sizeof(uint64_t) * 2,
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT
            );
            if ((get_result == VK_SUCCESS) || (get_result == VK_NOT_READY)) {
                for (std::size_t i = 0; i < s_max_gpu_timers; ++i) {
                    if (!fired_snapshot[i]) {
                        continue;
                    }
                    const uint64_t begin_ts    = results[(2 * i + 0) * 2 + 0];
                    const uint64_t begin_avail = results[(2 * i + 0) * 2 + 1];
                    const uint64_t end_ts      = results[(2 * i + 1) * 2 + 0];
                    const uint64_t end_avail   = results[(2 * i + 1) * 2 + 1];
                    if ((begin_avail == 0) || (end_avail == 0)) {
                        continue;
                    }
                    Gpu_timer_impl* timer = nullptr;
                    {
                        const std::lock_guard<std::mutex> lock{m_gpu_timer_mutex};
                        timer = m_gpu_timer_by_index[i];
                    }
                    if (timer == nullptr) {
                        continue;
                    }
                    const uint64_t delta_ticks =
                        ((end_ts & m_gpu_timer_valid_mask) - (begin_ts & m_gpu_timer_valid_mask))
                        & m_gpu_timer_valid_mask;
                    const double   delta_ns    = static_cast<double>(delta_ticks) * m_gpu_timer_timestamp_period;
                    timer->store_last_result_ns(static_cast<uint64_t>(delta_ns));
                }
            }
        }

        const std::lock_guard<std::mutex> lock{m_gpu_timer_mutex};
        m_gpu_timer_reset_pending[slot] = true;
    }

    set_state(Device_frame_state::waited, "wait_frame");
    return true;
}


auto Device_impl::begin_frame() -> bool
{
    ERHE_VERIFY(m_state == Device_frame_state::waited);
    ERHE_VULKAN_SYNC_TRACE("[FRAME_BEGIN] frame_index={}", m_frame_index);

    // Open a device frame for recording with no swapchain involvement.
    // Reset the per-frame descriptor pool, open the slot's primary
    // command buffer for recording, and flip state. The caller may then
    // nest a swapchain frame via begin_swapchain_frame.
    reset_descriptor_pool();
    m_had_swapchain_frame = false;

    // Open the slot's device command buffer. The slot must have been
    // primed exactly once before begin_frame: either by
    // Swapchain_impl::setup_frame (called from wait_swapchain_frame), or
    // by Device_impl::prime_device_frame_slot (the no-swapchain path used
    // for init and OpenXR ticks). Both routes call
    // ensure_device_frame_command_buffer for the same slot the device
    // will index here, so df.command_buffer is always non-null at this
    // point. The assert catches future regressions of the slot-priming
    // contract immediately, instead of letting cb=NULL propagate into
    // get_device_frame_command_buffer warnings tens of upload sites later.
    const size_t slot = static_cast<size_t>(get_frame_in_flight_index());
    Device_frame_in_flight& df = m_device_submit_history[slot];
    ERHE_VERIFY(df.command_buffer != VK_NULL_HANDLE);
    const VkCommandBufferBeginInfo begin_info{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext            = nullptr,
        .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr
    };
    const VkResult result = vkBeginCommandBuffer(df.command_buffer, &begin_info);
    if (result != VK_SUCCESS) {
        log_context->critical(
            "vkBeginCommandBuffer() failed with {} {}",
            static_cast<int32_t>(result), c_str(result)
        );
        abort();
    }

    set_state(Device_frame_state::recording, "begin_frame");
    return true;
}

auto Device_impl::begin_frame(const Frame_begin_info& frame_begin_info) -> bool
{
    // Legacy compat: used to combine device-frame open with the
    // swapchain-frame open. begin_swapchain_frame moved to
    // Command_buffer::begin_swapchain, so the swapchain side is no
    // longer engaged here -- callers wanting the swapchain part must
    // call command_buffer.begin_swapchain(...) explicitly. This body
    // keeps compiling so hello_swap can iterate to the new pattern.
    static_cast<void>(frame_begin_info);
    return begin_frame();
}

auto Device_impl::end_frame() -> bool
{
    // CONTRACT: end_frame advances the frame index. That is its ONLY job.
    //
    // It does not submit any command buffer, it does not present any
    // swapchain image, and it does not touch any GPU resource other
    // than the device-owned timeline semaphore that update_frame_completion
    // signals. Callers must have already submitted every cb they
    // recorded via Device::submit_command_buffers (which presents
    // implicitly when a cb engaged a swapchain via begin_swapchain).
    //
    // Mechanics: update_frame_completion() submits an empty
    // vkQueueSubmit2 that signals m_vulkan_frame_end_semaphore at the
    // current m_frame_index, then increments m_frame_index. The next
    // wait_frame() consults the timeline semaphore at value
    // (m_frame_index - N) to gate per-(frame_in_flight, thread_slot)
    // command-pool reset.
    ERHE_VERIFY(
        (m_state == Device_frame_state::in_swapchain_frame) ||
        (m_state == Device_frame_state::recording) ||
        (m_state == Device_frame_state::waited)
    );

    m_had_swapchain_frame = false;
    update_frame_completion();

    ERHE_VULKAN_SYNC_TRACE("[FRAME_END] frame_index={}", m_frame_index);
    set_state(Device_frame_state::idle, "end_frame");
    return true;
}

auto Device_impl::end_frame(const Frame_end_info& frame_end_info) -> bool
{
    // Legacy compat overload. The Frame_end_info argument used to carry
    // the predicted-display-time / present-mode hints into
    // Swapchain_impl::end_frame, but presentation has moved into
    // Device::submit_command_buffers (driven by Command_buffer::begin_swapchain).
    // The argument is now ignored; both overloads have identical
    // behaviour: advance frame index. This overload is kept only so
    // existing call sites compile while they migrate to the no-args form.
    static_cast<void>(frame_end_info);
    return end_frame();
}


auto Device_impl::recreate_surface_for_new_window() -> bool
{
    if (!m_surface) {
        log_context->warn("Device_impl::recreate_surface_for_new_window(): no surface");
        return false;
    }
    return m_surface->get_impl().recreate_for_new_window();
}

void Device_impl::wait_idle()
{
    const VkResult result = vkDeviceWaitIdle(m_vulkan_device);
    if (result != VK_SUCCESS) {
        log_context->error(
            "vkDeviceWaitIdle() failed with {} {}",
            static_cast<int32_t>(result), c_str(result)
        );
    }

    // Every frame submitted so far is guaranteed complete on the GPU.
    // Drive frame_completed() up to the last submitted frame so ring
    // buffers recycle and completion handlers fire.
    //
    // m_frame_index is the next frame to be submitted; frames strictly
    // less than m_frame_index have already called update_frame_completion()
    // (which does ++m_frame_index). update_frame_completion() also advances
    // m_latest_completed_frame as the GPU timeline semaphore reports
    // completion; here we just force-complete everything up to the latest
    // submitted.
    if (m_frame_index > 0) {
        const uint64_t last_submitted = m_frame_index - 1;
        for (; m_latest_completed_frame <= last_submitted; ++m_latest_completed_frame) {
            frame_completed(m_latest_completed_frame);
        }
    }

    // Anything left in m_completion_handlers is for a frame that never
    // submitted - drain it too so staging buffers etc. are released.
    for (const Completion_handler& entry : m_completion_handlers) {
        entry.callback(*this);
    }
    m_completion_handlers.clear();
}

auto Device_impl::is_in_swapchain_frame() const -> bool
{
    return m_state == Device_frame_state::in_swapchain_frame;
}

void Device_impl::update_frame_completion()
{
    VkResult result = VK_SUCCESS;

    const VkSemaphoreSubmitInfo signal_semaphore_info{
        .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .pNext       = nullptr,
        .semaphore   = m_vulkan_frame_end_semaphore,
        .value       = m_frame_index,
        .stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .deviceIndex = 0,
    };
    const VkSubmitInfo2 submit_info{
        .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .pNext                    = nullptr,
        .flags                    = 0,
        .waitSemaphoreInfoCount   = 0,
        .pWaitSemaphoreInfos      = nullptr,
        .commandBufferInfoCount   = 0,
        .pCommandBufferInfos      = nullptr,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos    = &signal_semaphore_info,
    };
    log_context->trace("vkQueueSubmit2() end of frame timeline semaphore @ frame index = {}", m_frame_index);
    result = vkQueueSubmit2(m_vulkan_graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        log_context->critical("vkQueueSubmit2() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    ++m_frame_index;
    ERHE_VULKAN_SYNC_TRACE(
        "[FRAME_INDEX] advanced to {} (slot now {})",
        m_frame_index,
        m_frame_index % get_number_of_frames_in_flight()
    );

    uint64_t latest_completed_frame{0};
    result = vkGetSemaphoreCounterValue(m_vulkan_device, m_vulkan_frame_end_semaphore, &latest_completed_frame);
    if (result != VK_SUCCESS) {
        log_context->error("vkGetSemaphoreCounterValue() failed with {} {}", static_cast<int32_t>(result), c_str(result));
    } else {
        for (; m_latest_completed_frame <= latest_completed_frame; ++m_latest_completed_frame) {
            log_context->trace("GPU has completed frame index = {}", m_latest_completed_frame);
            frame_completed(m_latest_completed_frame);
        }
    }
}

auto Device_impl::allocate_ring_buffer_entry(
    Buffer_target     buffer_target,
    Ring_buffer_usage ring_buffer_usage,
    std::size_t       byte_count
) -> Ring_buffer_range
{
    m_need_sync = true;
    const std::size_t required_alignment = erhe::utility::next_power_of_two_16bit(get_buffer_alignment(buffer_target));
    std::size_t alignment_byte_count_without_wrap{0};
    std::size_t available_byte_count_without_wrap{0};
    std::size_t available_byte_count_with_wrap{0};

    // Pass 1: Do we have buffer that can be used without a wrap?
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        if (!ring_buffer->match(ring_buffer_usage)) {
            continue;
        }
        ring_buffer->get_size_available_for_write(
            required_alignment,
            alignment_byte_count_without_wrap,
            available_byte_count_without_wrap,
            available_byte_count_with_wrap
        );
        if (available_byte_count_without_wrap >= byte_count) {
            return ring_buffer->acquire(required_alignment, ring_buffer_usage, byte_count);
        }
    }

    // Pass 2: Do we have buffer that can be used with a wrap?
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        if (!ring_buffer->match(ring_buffer_usage)) {
            continue;
        }
        ring_buffer->get_size_available_for_write(
            required_alignment,
            alignment_byte_count_without_wrap,
            available_byte_count_without_wrap,
            available_byte_count_with_wrap
        );
        if (available_byte_count_with_wrap >= byte_count) {
            return ring_buffer->acquire(required_alignment, ring_buffer_usage, byte_count);
        }
    }

    // No existing usable buffer found, create new buffer
    Ring_buffer_create_info create_info{
        .size              = std::max(m_min_buffer_size, 4 * byte_count),
        .ring_buffer_usage = ring_buffer_usage,
        .debug_label       = "Ring_buffer"
    };
    m_ring_buffers.push_back(std::make_unique<Ring_buffer>(m_device, create_info));
    return m_ring_buffers.back()->acquire(required_alignment, ring_buffer_usage, byte_count);
}

auto Device_impl::get_format_properties(erhe::dataformat::Format format) const -> Format_properties
{
    //ERHE_FATAL("Not implemented");
    const VkFormat vulkan_format = to_vulkan(format);
    VkFormatProperties2 vulkan_properties{
        .sType            = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
        .pNext            = nullptr,
        .formatProperties = {}
    };
    vkGetPhysicalDeviceFormatProperties2(m_vulkan_physical_device, vulkan_format, &vulkan_properties);
    //const VkFormatFeatureFlags supported =
    //    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
    using namespace erhe::utility;
    const uint32_t features = vulkan_properties.formatProperties.optimalTilingFeatures;

    Format_properties result{
        .supported          = erhe::utility::test_bit_set(features, static_cast<uint32_t>(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)),
        .color_renderable   = erhe::utility::test_bit_set(features, static_cast<uint32_t>(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)),
        .depth_renderable   = erhe::utility::test_bit_set(features, static_cast<uint32_t>(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) && (erhe::dataformat::get_depth_size_bits(format) > 0),
        .stencil_renderable = erhe::utility::test_bit_set(features, static_cast<uint32_t>(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) && (erhe::dataformat::get_stencil_size_bits(format) > 0),
        .filter             = erhe::utility::test_bit_set(features, static_cast<uint32_t>(VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)),
        .framebuffer_blend  = erhe::utility::test_bit_set(features, static_cast<uint32_t>(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)),
        .red_size           = static_cast<int>(erhe::dataformat::get_red_size_bits    (format)),
        .green_size         = static_cast<int>(erhe::dataformat::get_green_size_bits  (format)),
        .blue_size          = static_cast<int>(erhe::dataformat::get_blue_size_bits   (format)),
        .alpha_size         = static_cast<int>(erhe::dataformat::get_alpha_size_bits  (format)),
        .depth_size         = static_cast<int>(erhe::dataformat::get_depth_size_bits  (format)),
        .stencil_size       = static_cast<int>(erhe::dataformat::get_stencil_size_bits(format))
    };

    // Query image format properties for 2D array limits and sample counts.
    // Only probe with vkGetPhysicalDeviceImageFormatProperties2 if the format
    // actually supports the requested usage according to the format features
    // we just queried -- otherwise the call returns VK_ERROR_FORMAT_NOT_SUPPORTED
    // and the validation layer flags it as a best-practices error.
    const bool is_depth = erhe::dataformat::get_depth_size_bits(format) > 0;
    const VkImageUsageFlags usage = is_depth
        ? (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
        : (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    const bool attachment_supported = is_depth
        ? (result.depth_renderable || result.stencil_renderable)
        : result.color_renderable;
    if (!result.supported || !attachment_supported) {
        return result;
    }

    const VkPhysicalDeviceImageFormatInfo2 image_format_info{
        .sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext  = nullptr,
        .format = vulkan_format,
        .type   = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage  = usage,
        .flags  = 0
    };

    VkImageFormatProperties2 image_format_properties{
        .sType             = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
        .pNext             = nullptr,
        .imageFormatProperties = {}
    };

    VkResult image_result = vkGetPhysicalDeviceImageFormatProperties2(
        m_vulkan_physical_device, &image_format_info, &image_format_properties
    );
    if (image_result == VK_SUCCESS) {
        const VkImageFormatProperties& p = image_format_properties.imageFormatProperties;
        result.texture_2d_array_max_width  = static_cast<int>(p.maxExtent.width);
        result.texture_2d_array_max_height = static_cast<int>(p.maxExtent.height);
        result.texture_2d_array_max_layers = static_cast<int>(p.maxArrayLayers);

        for (int bit = 0; bit < 7; ++bit) {
            if (p.sampleCounts & (1u << bit)) {
                result.texture_2d_sample_counts.push_back(1 << bit);
            }
        }
    }

    return result;
}

auto Device_impl::probe_image_format_support(
    const erhe::dataformat::Format format,
    const uint64_t                 usage_mask
) const -> bool
{
    const VkFormat          vk_format = to_vulkan(format);
    const VkImageUsageFlags vk_usage  = get_vulkan_image_usage_flags(usage_mask);
    if ((vk_format == VK_FORMAT_UNDEFINED) || (vk_usage == 0)) {
        return false;
    }

    const VkPhysicalDeviceImageFormatInfo2 image_format_info{
        .sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext  = nullptr,
        .format = vk_format,
        .type   = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage  = vk_usage,
        .flags  = 0
    };

    VkImageFormatProperties2 image_format_properties{
        .sType                 = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
        .pNext                 = nullptr,
        .imageFormatProperties = {}
    };

    const VkResult image_result = vkGetPhysicalDeviceImageFormatProperties2(
        m_vulkan_physical_device, &image_format_info, &image_format_properties
    );
    return image_result == VK_SUCCESS;
}

auto Device_impl::get_supported_depth_stencil_formats() const -> std::vector<erhe::dataformat::Format>
{
    std::vector<erhe::dataformat::Format> result;
    // Query common depth/stencil formats for support
    const std::vector<erhe::dataformat::Format> candidates = {
        erhe::dataformat::Format::format_d32_sfloat,
        erhe::dataformat::Format::format_d32_sfloat_s8_uint,
        erhe::dataformat::Format::format_d24_unorm_s8_uint,
        erhe::dataformat::Format::format_d16_unorm,
    };
    for (erhe::dataformat::Format candidate : candidates) {
        Format_properties properties = get_format_properties(candidate);
        if (properties.depth_renderable || properties.stencil_renderable) {
            result.push_back(candidate);
        }
    }
    return result;
}

void Device_impl::sort_depth_stencil_formats(std::vector<erhe::dataformat::Format>& formats, unsigned int sort_flags, int requested_sample_count) const
{
    // Simple sort: prefer depth+stencil over depth-only, prefer higher precision
    static_cast<void>(sort_flags);
    static_cast<void>(requested_sample_count);
    std::sort(formats.begin(), formats.end(), [](erhe::dataformat::Format a, erhe::dataformat::Format b) {
        int a_depth   = static_cast<int>(erhe::dataformat::get_depth_size_bits(a));
        int b_depth   = static_cast<int>(erhe::dataformat::get_depth_size_bits(b));
        int a_stencil = static_cast<int>(erhe::dataformat::get_stencil_size_bits(a));
        int b_stencil = static_cast<int>(erhe::dataformat::get_stencil_size_bits(b));
        int a_score   = a_depth + a_stencil;
        int b_score   = b_depth + b_stencil;
        return a_score > b_score;
    });
}

auto Device_impl::choose_depth_stencil_format(const std::vector<erhe::dataformat::Format>& formats) const -> erhe::dataformat::Format
{
    if (formats.empty()) {
        return erhe::dataformat::Format::format_d32_sfloat;
    }
    return formats.front();
}

auto Device_impl::choose_depth_stencil_format(unsigned int sort_flags, int requested_sample_count) const -> erhe::dataformat::Format
{
    std::vector<erhe::dataformat::Format> formats = get_supported_depth_stencil_formats();
    sort_depth_stencil_formats(formats, sort_flags, requested_sample_count);
    return choose_depth_stencil_format(formats);
}

void Device_impl::start_frame_capture()
{
    RENDERDOC_API_1_7_0* api = static_cast<RENDERDOC_API_1_7_0*>(erhe::window::get_renderdoc_api());
    if (api == nullptr) {
        return;
    }
    RENDERDOC_DevicePointer device = RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(m_vulkan_instance);
    api->StartFrameCapture(device, nullptr);
    log_context->info("RenderDoc: StartFrameCapture()");
}

void Device_impl::end_frame_capture()
{
    RENDERDOC_API_1_7_0* api = static_cast<RENDERDOC_API_1_7_0*>(erhe::window::get_renderdoc_api());
    if (api == nullptr) {
        return;
    }
    RENDERDOC_DevicePointer device = RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(m_vulkan_instance);
    uint32_t result = api->EndFrameCapture(device, nullptr);
    if (result == 0) {
        log_context->warn("RenderDoc: EndFrameCapture() failed");
        return;
    }
    if (api->IsTargetControlConnected()) {
        api->ShowReplayUI();
    } else {
        api->LaunchReplayUI(1, nullptr);
    }
}

auto Device_impl::make_blit_command_encoder(Command_buffer& command_buffer) -> Blit_command_encoder
{
    return Blit_command_encoder{m_device, command_buffer};
}

auto Device_impl::make_compute_command_encoder(Command_buffer& command_buffer) -> Compute_command_encoder
{
    return Compute_command_encoder{m_device, command_buffer};
}

auto Device_impl::make_render_command_encoder(Command_buffer& command_buffer) -> Render_command_encoder
{
    return Render_command_encoder{m_device, command_buffer};
}

void Device_impl::set_debug_label(const VkObjectType object_type, const uint64_t object_handle, const char* label)
{
    if (!m_instance_extensions.m_VK_EXT_debug_utils) {
        return;
    }
    const VkDebugUtilsObjectNameInfoEXT name_info{
        .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .pNext        = nullptr,
        .objectType   = object_type,
        .objectHandle = object_handle,
        .pObjectName  = label
    };
    VkResult result = vkSetDebugUtilsObjectNameEXT(m_vulkan_device, &name_info);
    if (result != VK_SUCCESS) {
        log_debug->warn(
            "vkSetDebugUtilsObjectNameEXT() failed with {} {}",
            static_cast<int32_t>(result),
            c_str(result)
        );
    }
}

void Device_impl::set_debug_label(const VkObjectType object_type, const uint64_t object_handle, const std::string& label)
{
    set_debug_label(object_type, object_handle, label.c_str());
}

// Active render pass tracking
Device_impl* Device_impl::s_device_impl{nullptr};

auto Device_impl::get_device_impl() -> Device_impl*
{
    return s_device_impl;
}

auto Device_impl::get_sync_pool() -> Device_sync_pool&
{
    return *m_sync_pool;
}

auto Device_impl::get_command_buffer(unsigned int thread_slot) -> Command_buffer&
{
    ERHE_VERIFY(thread_slot < s_number_of_thread_slots);
    const size_t frame_slot = static_cast<size_t>(get_frame_in_flight_index());
    Per_thread_command_pool& pool = m_command_pools[frame_slot][thread_slot];
    ERHE_VERIFY(pool.command_pool != VK_NULL_HANDLE);

    const VkCommandBufferAllocateInfo allocate_info{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = pool.command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VkCommandBuffer vk_command_buffer{VK_NULL_HANDLE};
    const VkResult result = vkAllocateCommandBuffers(m_vulkan_device, &allocate_info, &vk_command_buffer);
    if (result != VK_SUCCESS) {
        log_context->critical(
            "vkAllocateCommandBuffers() failed with {} {}",
            static_cast<int32_t>(result), c_str(result)
        );
        abort();
    }

    auto label = erhe::utility::Debug_label{
        fmt::format("Device cb (frame_slot={}, thread_slot={}, allocation_index={})",
            frame_slot, thread_slot, pool.allocated_command_buffers.size())
    };
    set_debug_label(VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<uint64_t>(vk_command_buffer), label.data());

    auto command_buffer = std::make_unique<Command_buffer>(m_device, label);
    command_buffer->get_impl().set_vulkan_command_buffer(vk_command_buffer);

    // Implicit per-cb sync primitives, owned for the lifetime of the
    // wrapper (until the per-thread pool is reset on the next frame
    // cycle). Recycled in Command_buffer_impl::~Command_buffer_impl.
    const VkSemaphore implicit_semaphore = m_sync_pool->get_semaphore();
    const VkFence     implicit_fence     = m_sync_pool->get_fence();
    set_debug_label(
        VK_OBJECT_TYPE_SEMAPHORE,
        reinterpret_cast<uint64_t>(implicit_semaphore),
        fmt::format("Cb implicit semaphore (frame_slot={}, thread_slot={})", frame_slot, thread_slot).c_str()
    );
    set_debug_label(
        VK_OBJECT_TYPE_FENCE,
        reinterpret_cast<uint64_t>(implicit_fence),
        fmt::format("Cb implicit fence (frame_slot={}, thread_slot={})", frame_slot, thread_slot).c_str()
    );
    command_buffer->get_impl().set_implicit_sync(implicit_semaphore, implicit_fence);

    pool.allocated_command_buffers.push_back(std::move(command_buffer));
    return *pool.allocated_command_buffers.back();
}

auto Device_impl::get_active_render_pass() const -> VkRenderPass
{
    if (m_active_render_pass != nullptr) {
        return m_active_render_pass->m_render_pass;
    }
    return VK_NULL_HANDLE;
}

auto Device_impl::get_active_render_pass_impl() const -> Render_pass_impl*
{
    return m_active_render_pass;
}

void Device_impl::set_active_render_pass_impl(Render_pass_impl* render_pass_impl)
{
    m_active_render_pass = render_pass_impl;
}

auto Device_impl::get_number_of_frames_in_flight() const -> size_t
{
    return s_number_of_frames_in_flight;
}

auto Device_impl::get_frame_index() const -> uint64_t
{
    return m_frame_index;
}

auto Device_impl::get_frame_in_flight_index() const -> uint64_t
{
    return m_frame_index % get_number_of_frames_in_flight();
}

auto Device_impl::are_gpu_timers_supported() const -> bool
{
    return m_gpu_timers_supported;
}

auto Device_impl::get_gpu_timer_timestamp_period() const -> double
{
    return m_gpu_timer_timestamp_period;
}

auto Device_impl::allocate_gpu_timer_slot(Gpu_timer_impl* timer) -> int
{
    const std::lock_guard<std::mutex> lock{m_gpu_timer_mutex};
    for (std::size_t i = 0; i < s_max_gpu_timers; ++i) {
        if (!m_gpu_timer_slot_used[i]) {
            m_gpu_timer_slot_used[i] = true;
            m_gpu_timer_by_index[i]  = timer;
            return static_cast<int>(i);
        }
    }
    log_context->warn(
        "GPU timer slot pool exhausted (limit {}); subsequent timer is inert",
        s_max_gpu_timers
    );
    return -1;
}

void Device_impl::release_gpu_timer_slot(int slot)
{
    if (slot < 0) {
        return;
    }
    const std::lock_guard<std::mutex> lock{m_gpu_timer_mutex};
    const std::size_t index = static_cast<std::size_t>(slot);
    if (index >= s_max_gpu_timers) {
        return;
    }
    m_gpu_timer_slot_used[index] = false;
    m_gpu_timer_by_index[index]  = nullptr;
    for (std::array<bool, s_max_gpu_timers>& fired : m_gpu_timer_fired) {
        fired[index] = false;
    }
}

void Device_impl::record_gpu_timer_begin_query(VkCommandBuffer cb, int slot)
{
    if ((slot < 0) || !m_gpu_timers_supported || (m_gpu_timer_query_pool == VK_NULL_HANDLE)) {
        return;
    }
    const std::size_t in_flight = static_cast<std::size_t>(get_frame_in_flight_index());
    const std::size_t index     = static_cast<std::size_t>(slot);
    const std::size_t slice_size = s_max_gpu_timers * 2;
    const std::size_t query_idx  = in_flight * slice_size + index * 2;
    {
        const std::lock_guard<std::mutex> lock{m_gpu_timer_mutex};
        m_gpu_timer_fired[in_flight][index] = true;
    }
    vkCmdWriteTimestamp(
        cb,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        m_gpu_timer_query_pool,
        static_cast<uint32_t>(query_idx)
    );
}

void Device_impl::record_gpu_timer_end_query(VkCommandBuffer cb, int slot)
{
    if ((slot < 0) || !m_gpu_timers_supported || (m_gpu_timer_query_pool == VK_NULL_HANDLE)) {
        return;
    }
    const std::size_t in_flight = static_cast<std::size_t>(get_frame_in_flight_index());
    const std::size_t index     = static_cast<std::size_t>(slot);
    const std::size_t slice_size = s_max_gpu_timers * 2;
    const std::size_t query_idx  = in_flight * slice_size + index * 2 + 1;
    vkCmdWriteTimestamp(
        cb,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        m_gpu_timer_query_pool,
        static_cast<uint32_t>(query_idx)
    );
}

void Device_impl::maybe_reset_gpu_timer_slice(VkCommandBuffer cb)
{
    if (!m_gpu_timers_supported || (m_gpu_timer_query_pool == VK_NULL_HANDLE)) {
        return;
    }
    const std::size_t in_flight = static_cast<std::size_t>(get_frame_in_flight_index());
    bool need_reset = false;
    {
        const std::lock_guard<std::mutex> lock{m_gpu_timer_mutex};
        if (m_gpu_timer_reset_pending[in_flight]) {
            m_gpu_timer_reset_pending[in_flight] = false;
            need_reset = true;
        }
    }
    if (!need_reset) {
        return;
    }
    const std::size_t slice_size = s_max_gpu_timers * 2;
    const std::size_t slice_base = in_flight * slice_size;
    vkCmdResetQueryPool(
        cb,
        m_gpu_timer_query_pool,
        static_cast<uint32_t>(slice_base),
        static_cast<uint32_t>(slice_size)
    );
}

} // namespace erhe::graphics
