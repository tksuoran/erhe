// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_graphics/vulkan/vulkan_sampler.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"

#include "erhe_utility/bit_helpers.hpp"
#include "erhe_configuration/configuration.hpp"
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

#include <sstream>
#include <vector>

// https://vulkan.lunarg.com/doc/sdk/1.4.328.1/windows/khronos_validation_layer.html

namespace erhe::graphics {

Device_impl::Device_impl(Device& device, const Surface_create_info& surface_create_info)
    : m_context_window{surface_create_info.context_window}
    , m_device        {device}
    , m_shader_monitor{device}
{
    const VkResult volk_init = volkInitialize();
    if (volk_init != VK_SUCCESS) {
        abort(); // TODO handle errors
    }

    std::vector<const char*> required_extensions_c_str;
    if (m_context_window != nullptr) {
        const std::vector<std::string>& required_extensions = m_context_window->get_required_vulkan_instance_extensions();
        for (const std::string& extension_name : required_extensions) {
            required_extensions_c_str.push_back(extension_name.c_str());
        }
    }

    const VkApplicationInfo application_info{
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = nullptr,           // const void*
        .pApplicationName   = "ERHE",            // const char*
        .applicationVersion = 0,                 // uint32_t
        .pEngineName        = "ERHE",            // const char*
        .engineVersion      = 202501,            // uint32_t
        .apiVersion         = VK_API_VERSION_1_3 // VK_MAKE_API_VERSION(0, 1, 1, 0) // uint32_t
    };
    const VkInstanceCreateInfo instance_create_info = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,                  // 
        .pNext                   = nullptr,                                                 // 
        .flags                   = 0,                                                       // 
        .pApplicationInfo        = &application_info,                                       // 
        .enabledLayerCount       = 0,                                                       // 
        .ppEnabledLayerNames     = nullptr,                                                 // 
        .enabledExtensionCount   = static_cast<uint32_t>(required_extensions_c_str.size()), // 
        .ppEnabledExtensionNames = required_extensions_c_str.data()
    };
    VkResult result{VK_SUCCESS};
    result = vkCreateInstance(&instance_create_info, nullptr, &m_vulkan_instance);
    if (result != VK_SUCCESS) {
        abort(); // TODO handle error
    }

    volkLoadInstance(m_vulkan_instance);

    std::unique_ptr<Surface_impl> surface{};
    VkSurfaceKHR vulkan_surface{VK_NULL_HANDLE};
    if (m_context_window != nullptr) {
        surface = std::make_unique<Surface_impl>(m_device, surface_create_info);
        vulkan_surface = surface->get_vulkan_surface();
    }

    uint32_t physical_device_count{0};
    vkEnumeratePhysicalDevices(m_vulkan_instance, &physical_device_count, nullptr);
    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    if (physical_device_count == 0) {
        abort(); // TODO handle error
    }
    vkEnumeratePhysicalDevices(m_vulkan_instance, &physical_device_count, physical_devices.data());

    uint32_t graphics_queue_family_index = UINT32_MAX;
    uint32_t present_queue_family_index = UINT32_MAX;
    for (uint32_t physical_device_index = 0; physical_device_index < physical_device_count; ++physical_device_index) {
        VkPhysicalDevice physical_device = physical_devices[physical_device_index];
        VkPhysicalDeviceProperties device_properties{};
        vkGetPhysicalDeviceProperties(physical_device, &device_properties);

        if (surface && !surface->use_physical_device(physical_device)) {
            continue;
        }

        uint32_t queue_family_count{0};
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
        if (queue_family_count == 0) {
            continue;
        }

        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families.data());

        graphics_queue_family_index = UINT32_MAX;
        present_queue_family_index = UINT32_MAX;
        for (uint32_t queue_family_index = 0, end = static_cast<uint32_t>(queue_families.size()); queue_family_index < end; ++queue_family_index) {
            const VkQueueFamilyProperties& queue_family = queue_families[queue_family_index];
            const bool support_graphics = (queue_family.queueCount > 0) && (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT);
            const bool support_present = [&]() -> bool
            {
                VkBool32 support{VK_FALSE};
                result = vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, queue_family_index, vulkan_surface, &support);
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
            continue;
        }
        if (surface && (graphics_queue_family_index != present_queue_family_index)) {
            continue;
        }

        m_vulkan_physical_device      = physical_device;
        m_graphics_queue_family_index = graphics_queue_family_index;
        m_present_queue_family_index  = present_queue_family_index;
        break;
    }


    if (
        (m_vulkan_physical_device    == VK_NULL_HANDLE) ||
        (graphics_queue_family_index == UINT32_MAX) ||
        (
            surface &&
            (present_queue_family_index == UINT32_MAX)
        )
    ) {
        abort(); // TODO handle error
    }

    m_surface = std::move(surface);

    const float queue_priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_create_info = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, // sType
        .pNext            = nullptr,                                    // pNext
        .flags            = 0,                                          // flags
        .queueFamilyIndex = graphics_queue_family_index,                // graphicsQueueIndex
        .queueCount       = 1,                                          // queueCount
        .pQueuePriorities = &queue_priority,                            // pQueuePriorities
    };
    
    VkPhysicalDeviceFeatures device_features = {};

    std::vector<const char*> device_extensions;
    device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    VkPhysicalDeviceFeatures2 device_features2{
        .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext    = nullptr,
        .features = {}
    };
    vkGetPhysicalDeviceFeatures2(m_vulkan_physical_device, &device_features2);
    // samplerAnisotropy
    // shaderCullDistance

    const VkDeviceCreateInfo device_create_info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,            // sType
        .pNext                   = nullptr,                                         // pNext
        .flags                   = 0,                                               // flags
        .queueCreateInfoCount    = 1,                                               // queueCreateInfoCount
        .pQueueCreateInfos       = &queue_create_info,                              // pQueueCreateInfos
        .enabledLayerCount       = 0,                                               // enabledLayerCount - DEPRECATED
        .ppEnabledLayerNames     = nullptr,                                         // ppEnabledLayerNames - DEPRECATED
        .enabledExtensionCount   = static_cast<uint32_t>(device_extensions.size()), // enabledExtensionCount
        .ppEnabledExtensionNames = device_extensions.data(),                        // ppEnabledExtensionNames
        .pEnabledFeatures        = nullptr
    };
    vkCreateDevice(m_vulkan_physical_device, &device_create_info, nullptr, &m_vulkan_device);

    // TODO
    //   VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT
    //   VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT
    VmaAllocatorCreateInfo vma_create_info{
        .flags                          = 0,                        // VmaAllocatorCreateFlags flags;
        .physicalDevice                 = m_vulkan_physical_device, // VkPhysicalDevice VMA_NOT_NULL physicalDevice;
        .device                         = m_vulkan_device,          // VkDevice VMA_NOT_NULL device;
        .preferredLargeHeapBlockSize    = 0,
        .pAllocationCallbacks           = nullptr,
        .pDeviceMemoryCallbacks         = nullptr,
        .pVulkanFunctions               = nullptr,
        .instance                       = m_vulkan_instance,
        .vulkanApiVersion               = application_info.apiVersion,
#if VMA_EXTERNAL_MEMORY
        .pTypeExternalMemoryHandleTypes = nullptr
#endif // #if VMA_EXTERNAL_MEMORY
    };

    VmaVulkanFunctions vma_vulkan_functions{};
    result = vmaImportVulkanFunctionsFromVolk(&vma_create_info, &vma_vulkan_functions);
    if (result != VK_SUCCESS) {
        abort(); // TODO handle error
    }
    vma_create_info.pVulkanFunctions = &vma_vulkan_functions;

    result = vmaCreateAllocator(&vma_create_info, &m_vma_allocator);
    if (result != VK_SUCCESS) {
        abort(); // TODO handle error
    }

    vkGetDeviceQueue(m_vulkan_device, graphics_queue_family_index, 0, &m_vulkan_graphics_queue);

    if (m_surface) {
        vkGetDeviceQueue(m_vulkan_device, present_queue_family_index, 0, &m_vulkan_present_queue);
    }
}

Device_impl::~Device_impl()
{
    vmaDestroyAllocator(m_vma_allocator);
    if (m_vulkan_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_vulkan_device, nullptr);
    }
    m_surface.reset();
    vkDestroyInstance(m_vulkan_instance, nullptr);
    volkFinalize();
}

auto Device_impl::get_vulkan_instance() -> VkInstance
{
    return m_vulkan_instance;
}

auto Device_impl::get_vulkan_device() -> VkDevice
{
    return m_vulkan_device;
}

auto Device_impl::get_graphics_queue_family_index() -> uint32_t const
{
    return m_graphics_queue_family_index;
}

auto Device_impl::get_present_queue_family_index () -> uint32_t const
{
    return m_present_queue_family_index;
}

auto Device_impl::get_handle(const Texture& texture, const Sampler& sampler) const -> uint64_t
{
    static_cast<void>(texture);
    static_cast<void>(sampler);
    return 0;
}

auto Device_impl::choose_depth_stencil_format(const unsigned int flags, int sample_count) const -> erhe::dataformat::Format
{
    static_cast<void>(flags);
    static_cast<void>(sample_count);
    return erhe::dataformat::Format::format_undefined;
}

auto Device_impl::create_dummy_texture() -> std::shared_ptr<Texture>
{
    const Texture::Create_info create_info{
        .device      = m_device,
        .width       = 2,
        .height      = 2,
        .debug_label = "dummy"
    };

    auto texture = std::make_shared<Texture>(m_device, create_info);
    const std::array<uint8_t, 16> dummy_pixel{
        0xee, 0x11, 0xdd, 0xff,  0xcc, 0x11, 0xbb, 0xff,
        0xcc, 0x11, 0xbb, 0xff,  0xee, 0x11, 0xdd, 0xff,
    };
    const std::span<const std::uint8_t> image_data{&dummy_pixel[0], dummy_pixel.size()};

    std::span<const std::uint8_t> src_span{dummy_pixel.data(), dummy_pixel.size()};
    std::size_t                   byte_count   = src_span.size_bytes();
    Ring_buffer_client            texture_upload_buffer{m_device, erhe::graphics::Buffer_target::pixel, "dummy texture upload"};
    Ring_buffer_range             buffer_range = texture_upload_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, byte_count);
    std::span<std::byte>          dst_span     = buffer_range.get_span();
    memcpy(dst_span.data(), src_span.data(), byte_count);
    buffer_range.bytes_written(byte_count);
    buffer_range.close();

    const int src_bytes_per_row   = 2 * 4;
    const int src_bytes_per_image = 2 * src_bytes_per_row;
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

auto Device_impl::get_buffer_alignment(Buffer_target target) -> std::size_t
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

void Device_impl::upload_to_buffer(Buffer& buffer, size_t offset, const void* data, size_t length)
{
    static_cast<void>(buffer);
    static_cast<void>(offset);
    static_cast<void>(data);
    static_cast<void>(length);
}

void Device_impl::add_completion_handler(std::function<void()> callback)
{
    m_completion_handlers.emplace_back(m_frame_number, callback);
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

void Device_impl::start_of_frame()
{
    ++m_frame_number;
}

void Device_impl::end_of_frame()
{
}

auto Device_impl::get_frame_number() const -> uint64_t
{
    return m_frame_number;
}

auto Device_impl::allocate_ring_buffer_entry(
    Buffer_target     buffer_target,
    Ring_buffer_usage ring_buffer_usage,
    std::size_t       byte_count
) -> Ring_buffer_range
{
    static_cast<void>(buffer_target);
    static_cast<void>(ring_buffer_usage);
    static_cast<void>(byte_count);
    return {};
}

void Device_impl::memory_barrier(Memory_barrier_mask barriers)
{
    static_cast<void>(barriers);
}

auto Device_impl::get_format_properties(erhe::dataformat::Format format) const -> Format_properties
{
    static_cast<void>(format);
    return {};
}

void Device_impl::clear_texture(Texture& texture, std::array<double, 4> value)
{
    static_cast<void>(texture);
    static_cast<void>(value);
    
}

auto Device_impl::make_blit_command_encoder() -> Blit_command_encoder
{
    return Blit_command_encoder(m_device);
}

auto Device_impl::make_compute_command_encoder() -> Compute_command_encoder
{
    return Compute_command_encoder(m_device);
}

auto Device_impl::make_render_command_encoder(Render_pass& render_pass) -> Render_command_encoder
{
    return Render_command_encoder(m_device, render_pass);
}

auto Device_impl::create_render_pass(
    const VkRenderPassCreateInfo* pCreateInfo,
    const VkAllocationCallbacks*  pAllocator,
    VkRenderPass*                 pRenderPass
) -> VkResult
{
    return vkCreateRenderPass(m_vulkan_device, pCreateInfo, pAllocator, pRenderPass);
}

} // namespace erhe::graphics
