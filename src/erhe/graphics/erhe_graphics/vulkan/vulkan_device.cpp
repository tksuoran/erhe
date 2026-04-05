#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_immediate_commands.hpp"
#include "erhe_graphics/vulkan/vulkan_sampler.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
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

auto Device_impl::allocate_command_buffer() -> VkCommandBuffer
{
    const VkCommandBufferAllocateInfo allocate_info{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = m_vulkan_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    VkCommandBuffer vulkan_command_buffer{VK_NULL_HANDLE};
    VkResult result = vkAllocateCommandBuffers(m_vulkan_device, &allocate_info, &vulkan_command_buffer);
    if (result != VK_SUCCESS) {
        log_context->critical("vkAllocateCommandBuffers() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    return vulkan_command_buffer;
}

Device_impl::~Device_impl() noexcept
{
    vkDeviceWaitIdle(m_vulkan_device);

    for (auto& [hash, pipeline] : m_pipeline_map) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_vulkan_device, pipeline, nullptr);
        }
    }
    m_pipeline_map.clear();
    if (m_per_frame_descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_vulkan_device, m_per_frame_descriptor_pool, nullptr);
    }
    if (m_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_vulkan_device, m_pipeline_layout, nullptr);
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
    if (m_vulkan_command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_vulkan_device, m_vulkan_command_pool, nullptr);
    }
    // Destroy immediate commands before the device - it owns a command pool,
    // command buffers, fences, and semaphores
    m_immediate_commands.reset();

    // NOTE: This adds completion handlers for destroying related vulkan objects
    m_surface.reset();

    // Destroy ring buffers before running completion handlers -- their destructors
    // add completion handlers for VMA deallocation
    m_ring_buffers.clear();

    vkDeviceWaitIdle(m_vulkan_device);

    for (const Completion_handler& entry : m_completion_handlers) {
        entry.callback();
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

auto Device_impl::get_pipeline_layout() const -> VkPipelineLayout
{
    return m_pipeline_layout;
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

    return descriptor_set;
}

void Device_impl::reset_descriptor_pool()
{
    if (m_per_frame_descriptor_pool != VK_NULL_HANDLE) {
        vkResetDescriptorPool(m_vulkan_device, m_per_frame_descriptor_pool, 0);
    }
}

auto Device_impl::get_or_create_graphics_pipeline(const VkGraphicsPipelineCreateInfo& create_info, std::size_t hash) -> VkPipeline
{
    std::lock_guard<std::mutex> lock{m_pipeline_map_mutex};

    auto it = m_pipeline_map.find(hash);
    if (it != m_pipeline_map.end()) {
        return it->second;
    }

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(m_vulkan_device, m_pipeline_cache, 1, &create_info, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        log_program->error("vkCreateGraphicsPipelines() failed with {}", static_cast<int32_t>(result));
        return VK_NULL_HANDLE;
    }

    set_debug_label(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(pipeline),
        fmt::format("Pipeline hash={:016x}", hash).c_str());

    m_pipeline_map[hash] = pipeline;
    return pipeline;
}

auto Device_impl::choose_physical_device(
    Surface_impl*             surface_impl,
    std::vector<const char*>& device_extensions_c_str
) -> bool
{
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
    VkPhysicalDevice selected_device{VK_NULL_HANDLE};
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

auto Device_impl::get_memory_type(uint32_t memory_type_index) const -> const VkMemoryType&
{
    return m_memory_properties.memoryProperties.memoryTypes[memory_type_index];
}

auto Device_impl::get_memory_heap(uint32_t memory_heap_index) const -> const VkMemoryHeap&
{
    return m_memory_properties.memoryProperties.memoryHeaps[memory_heap_index];
}

auto Device_impl::get_immediate_commands() -> Vulkan_immediate_commands&
{
    return *m_immediate_commands.get();
}

auto Device_impl::get_handle(const Texture& texture, const Sampler& sampler) const -> uint64_t
{
    // TODO Implement bindless texture handles via descriptor indexing
    static_cast<void>(texture);
    static_cast<void>(sampler);
    return 0;
}

auto Device_impl::create_dummy_texture(const erhe::dataformat::Format format) -> std::shared_ptr<Texture>
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

    Ring_buffer_client   texture_upload_buffer{m_device, erhe::graphics::Buffer_target::transfer_src, "dummy texture upload"};
    Ring_buffer_range    buffer_range = texture_upload_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, byte_count);
    std::span<std::byte> dst_span     = buffer_range.get_span();
    memcpy(dst_span.data(), dummy_pixels.data(), byte_count);
    buffer_range.bytes_written(byte_count);
    buffer_range.close();

    const std::size_t src_bytes_per_image = height * src_bytes_per_row;
    Blit_command_encoder encoder{m_device};
    encoder.copy_from_buffer(
        buffer_range.get_buffer()->get_buffer(),         // source_buffer
        buffer_range.get_byte_start_offset_in_buffer(),  // source_offset
        src_bytes_per_row,                               // source_bytes_per_row
        src_bytes_per_image,                             // source_bytes_per_image
        glm::ivec3{2, 2, 1},                             // source_size
        texture.get(),                                   // destination_texture
        0,                                               // destination_slice
        0,                                               // destination_level
        glm::ivec3{0, 0, 0}                              // destination_origin
    );

    buffer_range.release();

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

void Device_impl::upload_to_buffer(const Buffer& buffer, const size_t offset, const void* data, const size_t length)
{
    // For host-visible buffers, map and copy directly
    // const_cast is needed because map_bytes is non-const (it modifies internal map state)
    Buffer_impl& impl = const_cast<Buffer_impl&>(buffer.get_impl());
    if (impl.is_host_visible()) {
        std::span<std::byte> map = impl.map_bytes(offset, length);
        if (!map.empty()) {
            std::memcpy(map.data(), data, length);
            impl.flush_bytes(offset, length);
            return;
        }
    }

    // Staging buffer upload for non-host-visible (device-local) buffers
    VkBufferCreateInfo staging_buffer_create_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size  = length,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    VmaAllocationCreateInfo staging_alloc_info{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VmaAllocation staging_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo staging_allocation_info{};
    VkResult result = vmaCreateBuffer(m_vma_allocator, &staging_buffer_create_info, &staging_alloc_info, &staging_buffer, &staging_allocation, &staging_allocation_info);
    if (result != VK_SUCCESS) {
        log_buffer->error("upload_to_buffer: staging buffer creation failed with {}", static_cast<int32_t>(result));
        return;
    }
    vmaSetAllocationName(m_vma_allocator, staging_allocation, "upload_to_buffer staging");
    std::memcpy(staging_allocation_info.pMappedData, data, length);
    vmaFlushAllocation(m_vma_allocator, staging_allocation, 0, length);

    const Vulkan_immediate_commands::Command_buffer_wrapper& cmd = m_immediate_commands->acquire();
    const VkBufferCopy copy_region{
        .srcOffset = 0,
        .dstOffset = offset,
        .size      = length
    };
    vkCmdCopyBuffer(cmd.m_cmd_buf, staging_buffer, impl.get_vk_buffer(), 1, &copy_region);
    Submit_handle submit = m_immediate_commands->submit(cmd);
    m_immediate_commands->wait(submit);

    vmaDestroyBuffer(m_vma_allocator, staging_buffer, staging_allocation);
}

void Device_impl::upload_to_texture(
    const Texture&               texture,
    int                          level,
    int                          x,
    int                          y,
    int                          width,
    int                          height,
    erhe::dataformat::Format     pixelformat,
    const void*                  data,
    int                          row_stride
)
{
    if ((data == nullptr) || (width <= 0) || (height <= 0)) {
        return;
    }

    const std::size_t pixel_size  = erhe::dataformat::get_format_size_bytes(pixelformat);
    const std::size_t src_stride  = (row_stride > 0) ? static_cast<std::size_t>(row_stride) : (static_cast<std::size_t>(width) * pixel_size);
    const std::size_t data_size   = src_stride * static_cast<std::size_t>(height);

    // Create staging buffer
    VmaAllocationCreateInfo staging_alloc_info{};
    staging_alloc_info.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    staging_alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    const VkBufferCreateInfo staging_buffer_info{
        .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .size                  = data_size,
        .usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr
    };

    VkBuffer       staging_buffer     = VK_NULL_HANDLE;
    VmaAllocation  staging_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo staging_alloc_result{};

    VkResult result = vmaCreateBuffer(m_vma_allocator, &staging_buffer_info, &staging_alloc_info, &staging_buffer, &staging_allocation, &staging_alloc_result);
    if (result != VK_SUCCESS) {
        log_texture->error("upload_to_texture: staging buffer creation failed with {}", static_cast<int32_t>(result));
        return;
    }
    vmaSetAllocationName(m_vma_allocator, staging_allocation, "upload_to_texture staging");

    // Copy data to staging buffer
    std::memcpy(staging_alloc_result.pMappedData, data, data_size);
    vmaFlushAllocation(m_vma_allocator, staging_allocation, 0, data_size);

    // Record copy command
    const Vulkan_immediate_commands::Command_buffer_wrapper& cmd = m_immediate_commands->acquire();

    const Texture_impl& tex_impl = texture.get_impl();
    VkImage destination_image = tex_impl.get_vk_image();

    // Transition to transfer dst using tracked layout
    tex_impl.transition_layout(cmd.m_cmd_buf, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    const VkBufferImageCopy region{
        .bufferOffset      = 0,
        .bufferRowLength   = (row_stride > 0) ? static_cast<uint32_t>(row_stride / pixel_size) : 0,
        .bufferImageHeight = 0,
        .imageSubresource  = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = static_cast<uint32_t>(level),
            .baseArrayLayer = 0,
            .layerCount     = 1
        },
        .imageOffset = {x, y, 0},
        .imageExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
            1
        }
    };
    vkCmdCopyBufferToImage(cmd.m_cmd_buf, staging_buffer, destination_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition to shader read using tracked layout
    tex_impl.transition_layout(cmd.m_cmd_buf, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Submit and wait (staging buffer must survive until copy completes)
    Submit_handle submit = m_immediate_commands->submit(cmd);
    m_immediate_commands->wait(submit);

    // Destroy staging buffer
    vmaDestroyBuffer(m_vma_allocator, staging_buffer, staging_allocation);
}

void Device_impl::add_completion_handler(std::function<void()> callback)
{
    m_completion_handlers.emplace_back(m_frame_index, callback);
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
            entry.callback();
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

auto Device_impl::wait_frame(Frame_state& out_frame_state) -> bool
{
    if (m_surface) {
        Swapchain* swapchain = m_surface->get_swapchain();
        if (swapchain != nullptr) {
            return swapchain->wait_frame(out_frame_state);
        }
    }
    out_frame_state.predicted_display_time   = 0;
    out_frame_state.predicted_display_period = 0;
    out_frame_state.should_render            = false;
    return false;
}

auto Device_impl::begin_frame(const Frame_begin_info& frame_begin_info) -> bool
{
    // Reset per-frame descriptor pool at the start of each frame
    reset_descriptor_pool();

    if (m_surface) {
        Swapchain* swapchain = m_surface->get_swapchain();
        if (swapchain != nullptr) {
            return swapchain->begin_frame(frame_begin_info);
        }
    }
    return false;
}

auto Device_impl::end_frame(const Frame_end_info& frame_end_info) -> bool
{
    bool result = true;
    if (m_surface) {
        Swapchain* swapchain = m_surface->get_swapchain();
        if (swapchain != nullptr) {
            result = swapchain->end_frame(frame_end_info);
        }
    }

    update_frame_completion();

    return result;
}

void Device_impl::update_frame_completion()
{
    VkResult result = VK_SUCCESS;

    const VkTimelineSemaphoreSubmitInfo semaphore_submit_info = {
        .sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .pNext                     = nullptr,
        .waitSemaphoreValueCount   = 0, 
        .pWaitSemaphoreValues      = nullptr,
        .signalSemaphoreValueCount = 1,
        .pSignalSemaphoreValues    = &m_frame_index,
    };

    const VkSubmitInfo submit_info{
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,  // VkStructureType
        .pNext                = &semaphore_submit_info,         // const void*
        .waitSemaphoreCount   = 0,                              // uint32_t
        .pWaitSemaphores      = nullptr,                        // const VkSemaphore*
        .pWaitDstStageMask    = nullptr,                        // const VkPipelineStageFlags*
        .commandBufferCount   = 0,                              // uint32_t
        .pCommandBuffers      = nullptr,                        // const VkCommandBuffer*
        .signalSemaphoreCount = 1,                              // uint32_t
        .pSignalSemaphores    = &m_vulkan_frame_end_semaphore,  // const VkSemaphore*
    };
    log_context->trace("vkQueueSubmit() end of frame timeline semaphore @ frame index = {}", m_frame_index);
    result = vkQueueSubmit(m_vulkan_graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        log_context->critical("vkQueueSubmit() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    ++m_frame_index;

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

void Device_impl::memory_barrier(Memory_barrier_mask barriers)
{
    // TODO Implement using vkCmdPipelineBarrier on the active command buffer
    // For now, this is a no-op since Vulkan barriers are per-command-buffer
    static_cast<void>(barriers);
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
    return Format_properties{
        .supported = erhe::utility::test_bit_set(
            static_cast<uint32_t>(vulkan_properties.formatProperties.optimalTilingFeatures),
            static_cast<uint32_t>(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
        ),
        .color_renderable = erhe::utility::test_bit_set(
            static_cast<uint32_t>(vulkan_properties.formatProperties.optimalTilingFeatures),
            static_cast<uint32_t>(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
        ),
        .depth_renderable = erhe::utility::test_bit_set(
            static_cast<uint32_t>(vulkan_properties.formatProperties.optimalTilingFeatures),
            static_cast<uint32_t>(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        ) && (erhe::dataformat::get_depth_size_bits(format) > 0),
        .stencil_renderable = erhe::utility::test_bit_set(
            static_cast<uint32_t>(vulkan_properties.formatProperties.optimalTilingFeatures),
            static_cast<uint32_t>(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        ) && (erhe::dataformat::get_stencil_size_bits(format) > 0),
        .filter = erhe::utility::test_bit_set(
            static_cast<uint32_t>(vulkan_properties.formatProperties.optimalTilingFeatures),
            static_cast<uint32_t>(VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
        ),
        .framebuffer_blend = erhe::utility::test_bit_set(
            static_cast<uint32_t>(vulkan_properties.formatProperties.optimalTilingFeatures),
            static_cast<uint32_t>(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
        ),
        .red_size     = static_cast<int>(erhe::dataformat::get_red_size_bits    (format)),
        .green_size   = static_cast<int>(erhe::dataformat::get_green_size_bits  (format)),
        .blue_size    = static_cast<int>(erhe::dataformat::get_blue_size_bits   (format)),
        .alpha_size   = static_cast<int>(erhe::dataformat::get_alpha_size_bits  (format)),
        .depth_size   = static_cast<int>(erhe::dataformat::get_depth_size_bits  (format)),
        .stencil_size = static_cast<int>(erhe::dataformat::get_stencil_size_bits(format))
    };
    // TODO Sample counts and max size
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

void Device_impl::clear_texture(const Texture& texture, std::array<double, 4> value)
{
    const Texture_impl& tex_impl = texture.get_impl();
    VkImage image = tex_impl.get_vk_image();
    if (image == VK_NULL_HANDLE) {
        return;
    }

    const erhe::dataformat::Format pixelformat = tex_impl.get_pixelformat();
    const bool is_depth   = erhe::dataformat::get_depth_size_bits(pixelformat) > 0;
    const bool is_stencil = erhe::dataformat::get_stencil_size_bits(pixelformat) > 0;

    const Vulkan_immediate_commands::Command_buffer_wrapper& cmd = m_immediate_commands->acquire();

    tex_impl.transition_layout(cmd.m_cmd_buf, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    if (is_depth || is_stencil) {
        const VkClearDepthStencilValue clear_depth_stencil{
            .depth   = static_cast<float>(value[0]),
            .stencil = static_cast<uint32_t>(value[1])
        };
        VkImageAspectFlags aspect_mask = 0;
        if (is_depth)   aspect_mask |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (is_stencil) aspect_mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        const VkImageSubresourceRange range{aspect_mask, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
        vkCmdClearDepthStencilImage(cmd.m_cmd_buf, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_depth_stencil, 1, &range);
    } else {
        const VkClearColorValue clear_color{
            .float32 = {
                static_cast<float>(value[0]),
                static_cast<float>(value[1]),
                static_cast<float>(value[2]),
                static_cast<float>(value[3])
            }
        };
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
        vkCmdClearColorImage(cmd.m_cmd_buf, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &range);
    }

    tex_impl.transition_layout(cmd.m_cmd_buf, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    Submit_handle submit = m_immediate_commands->submit(cmd);
    m_immediate_commands->wait(submit);
}

namespace {
auto to_vk_image_layout(Image_layout layout) -> VkImageLayout
{
    switch (layout) {
        case Image_layout::undefined:                        return VK_IMAGE_LAYOUT_UNDEFINED;
        case Image_layout::shader_read_only_optimal:         return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case Image_layout::transfer_dst_optimal:             return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case Image_layout::transfer_src_optimal:             return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case Image_layout::color_attachment_optimal:          return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case Image_layout::depth_stencil_attachment_optimal: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        default:                                             return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}
} // anonymous namespace

void Device_impl::transition_texture_layout(const Texture& texture, Image_layout new_layout)
{
    const Texture_impl& tex_impl = texture.get_impl();
    VkImage image = tex_impl.get_vk_image();
    if (image == VK_NULL_HANDLE) {
        return;
    }

    const Vulkan_immediate_commands::Command_buffer_wrapper& cmd = m_immediate_commands->acquire();
    tex_impl.transition_layout(cmd.m_cmd_buf, to_vk_image_layout(new_layout));
    Submit_handle submit = m_immediate_commands->submit(cmd);
    m_immediate_commands->wait(submit);
}

void Device_impl::initialize_frame_capture()
{
#if defined(_WIN32)
    // Try to get renderdoc API from already-loaded DLL (launched from RenderDoc)
    HMODULE renderdoc_module = GetModuleHandleA("renderdoc.dll");
    if (renderdoc_module == nullptr) {
        // Try to load it explicitly
        renderdoc_module = LoadLibraryExA("C:\\Program Files\\RenderDoc\\renderdoc.dll", NULL, 0);
    }
    if (renderdoc_module != nullptr) {
        auto RENDERDOC_GetAPI = reinterpret_cast<pRENDERDOC_GetAPI>(
            GetProcAddress(renderdoc_module, "RENDERDOC_GetAPI")
        );
        if (RENDERDOC_GetAPI != nullptr) {
            RENDERDOC_API_1_6_0* api = nullptr;
            int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&api));
            if (ret == 1) {
                m_renderdoc_api = api;
                api->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
                api->SetCaptureOptionU32(eRENDERDOC_Option_CaptureCallstacks, 1);
                log_context->info("RenderDoc frame capture initialized");
            }
        }
    }
#elif __unix__
    void* renderdoc_so = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
    if (renderdoc_so == nullptr) {
        renderdoc_so = dlopen("librenderdoc.so", RTLD_NOW);
    }
    if (renderdoc_so != nullptr) {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(renderdoc_so, "RENDERDOC_GetAPI"));
        if (RENDERDOC_GetAPI != nullptr) {
            RENDERDOC_API_1_6_0* api = nullptr;
            int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&api));
            if (ret == 1) {
                m_renderdoc_api = api;
                log_context->info("RenderDoc frame capture initialized");
            }
        }
    }
#endif
}

void Device_impl::start_frame_capture()
{
    if (m_renderdoc_api == nullptr) {
        return;
    }
    RENDERDOC_API_1_6_0* api = static_cast<RENDERDOC_API_1_6_0*>(m_renderdoc_api);
    RENDERDOC_DevicePointer device = RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(m_vulkan_instance);
    api->StartFrameCapture(device, nullptr);
    log_context->info("RenderDoc: StartFrameCapture()");
}

void Device_impl::end_frame_capture()
{
    if (m_renderdoc_api == nullptr) {
        return;
    }
    RENDERDOC_API_1_6_0* api = static_cast<RENDERDOC_API_1_6_0*>(m_renderdoc_api);
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

auto Device_impl::make_blit_command_encoder() -> Blit_command_encoder
{
    return Blit_command_encoder(m_device);
}

auto Device_impl::make_compute_command_encoder() -> Compute_command_encoder
{
    return Compute_command_encoder(m_device);
}

auto Device_impl::make_render_command_encoder() -> Render_command_encoder
{
    return Render_command_encoder(m_device);
}

void Device_impl::set_debug_label(VkObjectType object_type, uint64_t object_handle, const char* label)
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

void Device_impl::set_debug_label(VkObjectType object_type, uint64_t object_handle, const std::string& label)
{
    set_debug_label(object_type, object_handle, label.c_str());
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

} // namespace erhe::graphics
