#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_sampler.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"

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

auto Device_impl_debug_report_callback(
    VkDebugReportFlagsEXT      flags,
    VkDebugReportObjectTypeEXT object_type,
    uint64_t                   object,
    size_t                     location,
    int32_t                    message_code,
    const char*                layer_prefix,
    const char*                message,
    void*                      user_data
) -> VkBool32
{
    Device_impl* device_impl = static_cast<Device_impl*>(user_data);
    if (device_impl == nullptr) {
        return VK_FALSE;
    }
    return device_impl->debug_report_callback(
        flags,
        object_type,
        object,
        location,
        message_code,
        layer_prefix,
        message
    );
}

[[nodiscard]] auto Device_impl_debug_utils_messenger_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
    VkDebugUtilsMessageTypeFlagsEXT             message_types,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void*                                       user_data
) -> VkBool32
{
    Device_impl* device_impl = static_cast<Device_impl*>(user_data);
    if (device_impl == nullptr) {
        return VK_FALSE;
    }
    return device_impl->debug_utils_messenger_callback(message_severity, message_types, callback_data);
}

auto Device_impl::debug_report_callback(
    VkDebugReportFlagsEXT      flags,
    VkDebugReportObjectTypeEXT object_type,
    uint64_t                   object,
    size_t                     location,
    int32_t                    message_code,
    const char*                layer_prefix,
    const char*                message
) -> VkBool32
{
    spdlog::level::level_enum level = spdlog::level::info;
    if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT              ) level = spdlog::level::trace;
    if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT        ) level = spdlog::level::info;
    if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) level = spdlog::level::info;
    if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT            ) level = spdlog::level::warn;
    if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT              ) level = spdlog::level::err;
    std::stringstream ss;
    if (layer_prefix != nullptr) {
        ss << "[" << layer_prefix << "] ";
    }
    ss << fmt::format(
        "[{:<24}] Object {:16} (type {}) Location {:08x} Code {:04} :",
        to_string(flags),
        object,
        c_str(object_type),
        location,
        message_code
    );
    if (message != nullptr) {
        ss << " " << message;
    }  
    log_debug->log(level, ss.str());
    return VK_TRUE;
}

auto Device_impl::debug_utils_messenger_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
    VkDebugUtilsMessageTypeFlagsEXT             message_types,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data
) -> VkBool32
{
    spdlog::level::level_enum level = spdlog::level::info;
    if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) level = spdlog::level::trace;
    if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT   ) level = spdlog::level::info;
    if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) level = spdlog::level::warn;
    if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT  ) level = spdlog::level::err;
    std::stringstream ss;
    bool show_objects = true;
    if (callback_data->messageIdNumber == 0) {
        if (
            (callback_data->pMessageIdName != nullptr) &&
            strcmp(callback_data->pMessageIdName, "Loader Message") == 0
        ) {
            show_objects = false;
        }
    }
    ss << fmt::format(
        "{} {} [{} {}] {}",
        to_string(message_types),
        to_string(message_severity),
        callback_data->messageIdNumber,
        (callback_data->pMessageIdName != nullptr) ? callback_data->pMessageIdName : "",
        (callback_data->pMessage != nullptr) ? callback_data->pMessage : "<no message>"
    );
    if (callback_data->queueLabelCount > 0) {
        ss << "\n  queues: ";
        bool empty = true;
        for (uint32_t i = 0; i < callback_data->queueLabelCount; ++i) {
            if (callback_data->pQueueLabels[i].pLabelName != nullptr) {
                if (!empty) {
                    ss << ", ";
                }
                ss << callback_data->pQueueLabels[i].pLabelName;
                empty = false;
            }
        }
    }
    if (callback_data->cmdBufLabelCount > 0) {
        ss << "\n  command buffers: ";
        bool empty = true;
        for (uint32_t i = 0; i < callback_data->cmdBufLabelCount; ++i) {
            if (callback_data->pCmdBufLabels[i].pLabelName != nullptr) {
                if (!empty) {
                    ss << ", ";
                }
                ss << callback_data->pCmdBufLabels[i].pLabelName;
                empty = false;
            }
        }
    }
    if (show_objects && (callback_data->objectCount > 0)) {
        ss << "\n  objects: ";
        bool empty = true;
        for (uint32_t i = 0; i < callback_data->objectCount; ++i) {
            if (!empty) {
                ss << ", ";
            }
            ss << fmt::format(
                "{} {:08x} {}",
                c_str(callback_data->pObjects[i].objectType),
                callback_data->pObjects[i].objectHandle, 
                (callback_data->pObjects[i].pObjectName != nullptr)
                    ? callback_data->pObjects[i].pObjectName
                    : ""
            );
            empty = false;
        }
    }
    log_debug->log(level, ss.str());
    return VK_TRUE;
}

Device_impl::Device_impl(Device& device, const Surface_create_info& surface_create_info)
    : m_context_window{surface_create_info.context_window}
    , m_device        {device}
    , m_shader_monitor{device}
{
    VkResult result{VK_SUCCESS};

    result = volkInitialize();
    if (result != VK_SUCCESS) {
        log_context->critical("volkInitialize() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    uint32_t instance_layer_count{0};
    result = vkEnumerateInstanceLayerProperties(&instance_layer_count, nullptr);
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateInstanceLayerProperties() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    // Setup vulkan instance layers
    std::vector<const char*> required_layers_c_str;
    auto check_layer = [&required_layers_c_str](const VkLayerProperties& layer, const char* name, bool& enable)
    {
        if (strcmp(layer.layerName, name) == 0) {
            required_layers_c_str.push_back(name);
            enable = true;
            log_debug->info("  Enabling {}", layer.layerName);
        }
    };
    std::vector<VkLayerProperties> instance_layers(instance_layer_count);
    result = vkEnumerateInstanceLayerProperties(&instance_layer_count, instance_layers.data());
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateInstanceLayerProperties() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }
    for (const VkLayerProperties& layer : instance_layers) {
        log_debug->info(
            "Vulkan Instance Layer: {} spec_version {:08x} implementation_version {:08x} {}",
            layer.layerName,
            layer.specVersion,
            layer.implementationVersion,
            layer.description
        );
        // TODO Add config to erhe.toml
        check_layer(layer, "VK_LAYER_KHRONOS_validation",      m_instance_layers.m_VK_LAYER_KHRONOS_validation);
        check_layer(layer, "VK_LAYER_LUNARG_crash_diagnostic", m_instance_layers.m_VK_LAYER_LUNARG_crash_diagnostic);
    }

    uint32_t instance_extension_count{0};
    result = vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, nullptr);
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateInstanceExtensionProperties() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }
    std::vector<VkExtensionProperties> instance_extensions(instance_extension_count);
    result = vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, instance_extensions.data());
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateInstanceExtensionProperties() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    // Setup vulkan instance extensions
    std::vector<const char*> enabled_instance_extensions_c_str;
    if (m_context_window != nullptr) {
        const std::vector<std::string>& required_extensions = m_context_window->get_required_vulkan_instance_extensions();
        for (const std::string& extension_name : required_extensions) {
            enabled_instance_extensions_c_str.push_back(extension_name.c_str());
        }
    }

    auto check_instance_extension = [&enabled_instance_extensions_c_str](const VkExtensionProperties& extension, const char* name, bool& enable)
    {
        if (strcmp(extension.extensionName, name) == 0) {
            enabled_instance_extensions_c_str.push_back(name);
            enable = true;
            log_debug->info("  Enabling {}", extension.extensionName);
        }
    };

    for (const VkExtensionProperties& extension: instance_extensions) {
        log_debug->info(
            "Vulkan Instance Extension: {} spec_version {:08x}",
            extension.extensionName,
            extension.specVersion
        );
        check_instance_extension(extension, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, m_instance_extensions.m_VK_KHR_get_physical_device_properties2);
        check_instance_extension(extension, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME      , m_instance_extensions.m_VK_KHR_get_surface_capabilities2      );
        check_instance_extension(extension, VK_KHR_SURFACE_EXTENSION_NAME                         , m_instance_extensions.m_VK_KHR_surface                        );
        check_instance_extension(extension, VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME           , m_instance_extensions.m_VK_KHR_surface_maintenance1           );
        check_instance_extension(extension, VK_KHR_WIN32_SURFACE_EXTENSION_NAME                   , m_instance_extensions.m_VK_KHR_win32_surface                  );
        check_instance_extension(extension, VK_EXT_DEBUG_REPORT_EXTENSION_NAME                    , m_instance_extensions.m_VK_EXT_debug_report                   );
        check_instance_extension(extension, VK_EXT_DEBUG_UTILS_EXTENSION_NAME                     , m_instance_extensions.m_VK_EXT_debug_utils                    );
        check_instance_extension(extension, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME           , m_instance_extensions.m_VK_EXT_swapchain_colorspace           );
        check_instance_extension(extension, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME         , m_instance_extensions.m_VK_KHR_portability_enumeration        );
    }

    // Create vulkan instance
    const VkApplicationInfo application_info{
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = nullptr,
        .pApplicationName   = "ERHE",
        .applicationVersion = 0,
        .pEngineName        = "ERHE",
        .engineVersion      = 202501,
        .apiVersion         = VK_API_VERSION_1_3
    };

    const VkInstanceCreateInfo instance_create_info = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .pApplicationInfo        = &application_info,
        .enabledLayerCount       = static_cast<uint32_t>(required_layers_c_str.size()),
        .ppEnabledLayerNames     = required_layers_c_str.data(),
        .enabledExtensionCount   = static_cast<uint32_t>(enabled_instance_extensions_c_str.size()),
        .ppEnabledExtensionNames = enabled_instance_extensions_c_str.data()
    };
    result = vkCreateInstance(&instance_create_info, nullptr, &m_vulkan_instance);
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateInstance() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    volkLoadInstance(m_vulkan_instance);

    // Create vulkan surface
    std::unique_ptr<Surface_impl> surface_impl{};
    VkSurfaceKHR vulkan_surface{VK_NULL_HANDLE};
    if (m_context_window != nullptr) {
        surface_impl = std::make_unique<Surface_impl>(*this, surface_create_info);
        vulkan_surface = surface_impl->get_vulkan_surface();
    }

    std::vector<const char*> device_extensions_c_str{};
    const bool physical_device_ok = choose_physical_device(surface_impl.get(), device_extensions_c_str);
    if (!physical_device_ok) {
        log_context->critical("vkCreateInstance() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    VkPhysicalDeviceFeatures2 available_device_features{
        .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext    = nullptr,
        .features = {}
    };
    vkGetPhysicalDeviceFeatures2(m_vulkan_physical_device, &available_device_features);

    bool debug_callback_registered = false;
    if (m_instance_extensions.m_VK_EXT_debug_utils) {
        VkDebugUtilsMessageTypeFlagsEXT message_types =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT    |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        // TODO
        // if (m_device_extensions.m_VK_EXT_device_address_binding_report) {
        //     message_types = message_types | VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;
        //}
        const VkDebugUtilsMessengerCreateInfoEXT debug_utils_messenger_create_info{
            .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext           = nullptr,
            .flags           = 0,
            .messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT    |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType     = message_types,
            .pfnUserCallback = Device_impl_debug_utils_messenger_callback,
            .pUserData       = static_cast<void*>(this)
        };
        result = vkCreateDebugUtilsMessengerEXT(
            m_vulkan_instance,
            &debug_utils_messenger_create_info,
            nullptr,
            &m_debug_utils_messenger
        );
        if (result == VK_SUCCESS) {
            debug_callback_registered = true;
        } else {
            log_context->warn("vkCreateDebugUtilsMessengerEXT() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        }
    }
    if (m_instance_extensions.m_VK_EXT_debug_report && !debug_callback_registered) {
        const VkDebugReportCallbackCreateInfoEXT debug_report_callback_create_info{
            .sType       = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
            .pNext       = nullptr,
            .flags       =
                VK_DEBUG_REPORT_INFORMATION_BIT_EXT         |
                VK_DEBUG_REPORT_WARNING_BIT_EXT             |
                VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
                VK_DEBUG_REPORT_ERROR_BIT_EXT               |
                VK_DEBUG_REPORT_DEBUG_BIT_EXT,
            .pfnCallback = Device_impl_debug_report_callback,
            .pUserData   = static_cast<void*>(this),
        };
        result = vkCreateDebugReportCallbackEXT(
            m_vulkan_instance,
            &debug_report_callback_create_info,
            nullptr,
            &m_debug_report_callback
        );
        if (result != VK_SUCCESS) {
            log_context->warn("vkCreateDebugReportCallbackEXT() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        }
    }

    const float queue_priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_create_info = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .queueFamilyIndex = m_graphics_queue_family_index,
        .queueCount       = 1,
        .pQueuePriorities = &queue_priority,
    };

    const VkDeviceCreateInfo device_create_info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &queue_create_info,
        .enabledLayerCount       = 0,
        .ppEnabledLayerNames     = nullptr,
        .enabledExtensionCount   = static_cast<uint32_t>(device_extensions_c_str.size()),
        .ppEnabledExtensionNames = device_extensions_c_str.data(),
        .pEnabledFeatures        = nullptr
    };
    result = vkCreateDevice(m_vulkan_physical_device, &device_create_info, nullptr, &m_vulkan_device);
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateDevice() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    if (surface_impl && !surface_impl->use_physical_device(m_vulkan_physical_device)) {
        log_context->critical("Vulkan surface is not suitable");
        abort();
    }
    m_surface = std::make_unique<Surface>(std::move(surface_impl));

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
#endif
    };

    VmaVulkanFunctions vma_vulkan_functions{};
    result = vmaImportVulkanFunctionsFromVolk(&vma_create_info, &vma_vulkan_functions);
    if (result != VK_SUCCESS) {
        log_context->critical("vmaImportVulkanFunctionsFromVolk() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }
    vma_create_info.pVulkanFunctions = &vma_vulkan_functions;

    result = vmaCreateAllocator(&vma_create_info, &m_vma_allocator);
    if (result != VK_SUCCESS) {
        log_context->critical("vmaCreateAllocator() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    vkGetDeviceQueue(m_vulkan_device, m_graphics_queue_family_index, 0, &m_vulkan_graphics_queue);
    if (m_vulkan_graphics_queue == VK_NULL_HANDLE) {
        log_context->critical("vkGetDeviceQueue() returned VK_NULL_HANDLE for graphics queue");
        abort();
    }

    if (m_surface) {
        vkGetDeviceQueue(m_vulkan_device, m_present_queue_family_index, 0, &m_vulkan_present_queue);
        if (m_vulkan_present_queue == VK_NULL_HANDLE) {
            log_context->critical("vkGetDeviceQueue() returned VK_NULL_HANDLE for present queue");
            abort();
        }
    }

    m_info.glsl_version       = 460;
    m_info.vulkan_api_version = application_info.apiVersion;

    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(m_vulkan_physical_device, &physical_device_properties);

    m_info.max_per_stage_descriptor_samplers = physical_device_properties.limits.maxPerStageDescriptorSamplers;
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

auto Device_impl::choose_physical_device(
    Surface_impl*             surface_impl,
    std::vector<const char*>& device_extensions_c_str
) -> bool
{
    uint32_t physical_device_count{0};
    VkResult result{VK_SUCCESS};
    result = vkEnumeratePhysicalDevices(m_vulkan_instance, &physical_device_count, nullptr);
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumeratePhysicalDevices() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        return false;
    }
    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    if (physical_device_count == 0) {
        log_context->critical("vkEnumeratePhysicalDevices() returned 0 physical devices");
        return false;
    }
    result = vkEnumeratePhysicalDevices(m_vulkan_instance, &physical_device_count, physical_devices.data());
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumeratePhysicalDevices() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
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

    const bool queues_ok = query_device_queue_family_indices(vulkan_physical_device, surface_impl, nullptr, nullptr);
    if (!queues_ok) {
        return 0.0f;
    }

    Device_extensions device_extensions{};
    const float extension_score = query_device_extensions(vulkan_physical_device, device_extensions, nullptr);
    return extension_score;
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
                log_context->warn("vkGetPhysicalDeviceSurfaceSupportKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
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

    // Check device extensions
    auto check_device_extension =
        [&device_extensions_c_str, &total_score]
        (const VkExtensionProperties& extension, const char* name, bool& enable, const float extension_score)
    {
        if (strcmp(extension.extensionName, name) == 0) {
            if (device_extensions_c_str != nullptr) {
                device_extensions_c_str->push_back(name);
                log_debug->info("  Enabling {}", extension.extensionName);
            }
            enable = true;
            total_score += extension_score;
        }
    };
    uint32_t device_extension_count{0};
    VkResult result{VK_SUCCESS};
    result = vkEnumerateDeviceExtensionProperties(vulkan_physical_device, nullptr, &device_extension_count, nullptr);
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateDeviceExtensionProperties() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        return 0.0f;
    }
    std::vector<VkExtensionProperties> device_extensions(device_extension_count);
    result = vkEnumerateDeviceExtensionProperties(vulkan_physical_device, nullptr, &device_extension_count, device_extensions.data());
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateDeviceExtensionProperties() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        return 0.0f;
    }

    for (const VkExtensionProperties& extension : device_extensions) {
        log_debug->info(
            "Vulkan Device Extension: {} spec_version {:08x}",
            extension.extensionName,
            extension.specVersion
        );
        check_device_extension(extension, VK_KHR_SWAPCHAIN_EXTENSION_NAME,                      device_extensions_out.m_VK_KHR_swapchain                     , 1.0f);
        check_device_extension(extension, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,        device_extensions_out.m_VK_EXT_swapchain_maintenance1        , 2.0f);
        check_device_extension(extension, VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,        device_extensions_out.m_VK_KHR_swapchain_maintenance1        , 2.0f);
        check_device_extension(extension, VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME,             device_extensions_out.m_VK_EXT_load_store_op_none            , 2.0f);
        check_device_extension(extension, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,                device_extensions_out.m_VK_KHR_push_descriptor               , 1.0f);
        check_device_extension(extension, VK_KHR_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME, device_extensions_out.m_VK_KHR_present_mode_fifo_latest_ready, 3.0f);
        check_device_extension(extension, VK_EXT_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME, device_extensions_out.m_VK_EXT_present_mode_fifo_latest_ready, 3.0f);
        check_device_extension(extension, VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME,  device_extensions_out.m_VK_EXT_device_address_binding_report , 1.0f);
    }
    return total_score;
}

auto Device_impl::get_surface() -> Surface*
{
    return m_surface.get();
}

auto Device_impl::get_vulkan_instance() -> VkInstance
{
    return m_vulkan_instance;
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

auto Device_impl::get_instance_layers() const -> const Instance_layers&
{
    return m_instance_layers;
}

auto Device_impl::get_instance_extensions() const -> const Instance_extensions&
{
    return m_instance_extensions;
}

auto Device_impl::get_device_extensions() const -> const Device_extensions&
{
    return m_device_extensions;
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
            static_cast<uint32_t>(result),
            c_str(result)
        );
    }
}

void Device_impl::set_debug_label(VkObjectType object_type, uint64_t object_handle, const std::string& label)
{
    set_debug_label(object_type, object_handle, label.c_str());
}

} // namespace erhe::graphics


// VK_KHR_16bit_storage
// VK_KHR_8bit_storage
// VK_KHR_bind_memory2
// VK_KHR_buffer_device_address
// VK_KHR_copy_commands2
// VK_KHR_create_renderpass2
// VK_KHR_dedicated_allocation
// VK_KHR_deferred_host_operations
// VK_KHR_depth_stencil_resolve
// VK_KHR_descriptor_update_template
// VK_KHR_device_group
// VK_KHR_draw_indirect_count
// VK_KHR_driver_properties
// VK_KHR_dynamic_rendering
// VK_KHR_fragment_shading_rate
// VK_KHR_get_memory_requirements2
// VK_KHR_global_priority
// VK_KHR_imageless_framebuffer
// VK_KHR_image_format_list
// VK_KHR_maintenance1
// VK_KHR_maintenance2
// VK_KHR_maintenance3
// VK_KHR_maintenance4
// VK_KHR_multiview
// VK_KHR_pipeline_executable_properties
// VK_KHR_push_descriptor
// VK_KHR_ray_query
// VK_KHR_relaxed_block_layout
// VK_KHR_sampler_mirror_clamp_to_edge
// VK_KHR_sampler_ycbcr_conversion
// VK_KHR_separate_depth_stencil_layouts
// VK_KHR_shader_atomic_int64
// VK_KHR_shader_clock
// VK_KHR_shader_draw_parameters
// VK_KHR_shader_float16_int8
// VK_KHR_shader_float_controls
// VK_KHR_shader_integer_dot_product
// VK_KHR_shader_non_semantic_info
// VK_KHR_shader_subgroup_extended_types
// VK_KHR_shader_subgroup_uniform_control_flow
// VK_KHR_shader_terminate_invocation
// VK_KHR_spirv_1_4
// VK_KHR_storage_buffer_storage_class
// VK_KHR_swapchain
// VK_KHR_synchronization2
// VK_KHR_timeline_semaphore
// VK_KHR_uniform_buffer_standard_layout
// VK_KHR_variable_pointers
// VK_KHR_vulkan_memory_model

// VK_EXT_4444_formats
// VK_EXT_border_color_swizzle
// VK_EXT_color_write_enable
// VK_EXT_conservative_rasterization
// VK_EXT_custom_border_color
// VK_EXT_depth_clamp_zero_one
// VK_EXT_depth_clip_control
// VK_EXT_depth_clip_enable
// VK_EXT_descriptor_indexing
// VK_EXT_device_address_binding_report
// VK_EXT_device_fault
// VK_EXT_extended_dynamic_state
// VK_EXT_extended_dynamic_state2
// VK_EXT_global_priority
// VK_EXT_global_priority_query
// VK_EXT_host_query_reset
// VK_EXT_image_2d_view_of_3d
// VK_EXT_image_robustness
// VK_EXT_image_view_min_lod
// VK_EXT_index_type_uint8
// VK_EXT_inline_uniform_block
// VK_EXT_line_rasterization
// VK_EXT_load_store_op_none
// VK_EXT_pipeline_creation_cache_control
// VK_EXT_pipeline_creation_feedback
// VK_EXT_primitive_topology_list_restart
// VK_EXT_private_data
// VK_EXT_provoking_vertex
// VK_EXT_queue_family_foreign
// VK_EXT_robustness2
// VK_EXT_sampler_filter_minmax
// VK_EXT_sample_locations
// VK_EXT_scalar_block_layout
// VK_EXT_separate_stencil_usage
// VK_EXT_shader_atomic_float
// VK_EXT_shader_demote_to_helper_invocation
// VK_EXT_shader_stencil_export
// VK_EXT_shader_subgroup_ballot
// VK_EXT_shader_subgroup_vote
// VK_EXT_shader_viewport_index_layer
// VK_EXT_subgroup_size_control

// VK_EXT_texel_buffer_alignment
// VK_EXT_tooling_info
// VK_EXT_transform_feedback
// VK_EXT_vertex_attribute_divisor
// VK_EXT_vertex_input_dynamic_state

// VK_EXT_swapchain_maintenance1 / VK_KHR_swapchain_maintenance1