#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_sampler.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_graphics/swapchain.hpp"

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

#include <cstdlib>
#include <sstream>
#include <vector>

// https://vulkan.lunarg.com/doc/sdk/1.4.328.1/windows/khronos_validation_layer.html

namespace erhe::graphics {

[[nodiscard]] auto get_vendor(const uint32_t vendor_id) -> Vendor
{
    switch (vendor_id) {
        case 0x00001002u: return Vendor::Amd;
        default: return Vendor::Unknown;
    }
}

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
        to_string_VkDebugReportFlagsEXT(flags),
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
    if (callback_data->messageIdNumber == 0x675dc32e) {
        // Validation Warning: [ BestPractices-specialuse-extension ] | MessageID = 0x675dc32e
        // vkCreateInstance(): Attempting to enable extension VK_EXT_debug_utils, but this extension is intended to support use by applications when debugging and it is strongly recommended that it be otherwise avoided.
    }

    spdlog::level::level_enum level = spdlog::level::info;
    if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) != 0) level = spdlog::level::trace;
    if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT   ) != 0) level = spdlog::level::info;
    if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) level = spdlog::level::warn;
    if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT  ) != 0) level = spdlog::level::err;
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
        to_string_VkDebugUtilsMessageTypeFlagsEXT(message_types),
        to_string_VkDebugUtilsMessageSeverityFlagsEXT(message_severity),
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

    const bool is_validation = (message_types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0;
    if (is_validation) {
        const bool is_warning = (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0;
        const bool is_error   = (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT  ) != 0;
        if (is_error) {
            static int counter = 0;
            ++counter;
        }
        return (is_warning || is_error) ? VK_TRUE : VK_FALSE;
    }

    return VK_TRUE;
}

void set_env(const char* key, const char* value)
{
    int ret = -1;
#if defined(ERHE_OS_WINDOWS)
    {
        std::string assignment = fmt::format("{}={}", key, value);
        ret = _putenv(assignment.c_str());
    }
#elif defined(ERHE_OS_LINUX)
    {
        int ret = setenv(key, value, 1);
    }
#endif
    if (ret != 0) {
        log_context->warn("Setting {}={} environment variable failed with error code {}.", key, value, ret);
    }
}

Device_impl::Device_impl(Device& device, const Surface_create_info& surface_create_info)
    : m_context_window{surface_create_info.context_window}
    , m_device        {device}
    , m_shader_monitor{device}
{
    if (true) {
        // For now, avoid extra layers as they might cause validation or other issues
        set_env("VK_LOADER_LAYERS_DISABLE", "~implicit~");
        set_env("DISABLE_LAYER_NV_OPTIMUS_1", "1");
        set_env("DISABLE_LAYER_NV_GR2608_1", "1");
        set_env("DISABLE_VULKAN_OBS_CAPTURE", "1");
    }

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
    }

    std::vector<const char*> enabled_instance_layers_c_str;
    auto check_layer = [&](const char* name, bool& enable)
    {
        for (const VkLayerProperties& layer : instance_layers) {
            if (strcmp(layer.layerName, name) == 0) {
                enabled_instance_layers_c_str.push_back(name);
                enable = true;
                log_debug->info("  Enabling {}", layer.layerName);
                return;
            }
        }
    };

    // TODO Add config to erhe.toml
    check_layer("VK_LAYER_KHRONOS_validation",      m_instance_layers.m_VK_LAYER_KHRONOS_validation);
    //check_layer("VK_LAYER_LUNARG_crash_diagnostic", m_instance_layers.m_VK_LAYER_LUNARG_crash_diagnostic);

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

    // Create vulkan instance
    const VkApplicationInfo application_info{
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = nullptr,
        .pApplicationName   = "ERHE",
        .applicationVersion = 0,
        .pEngineName        = "ERHE",
        .engineVersion      = 2025012,
        .apiVersion         = VK_API_VERSION_1_3
    };

    // Setup vulkan instance extensions

    // SDL_Vulkan_GetInstanceExtensions() will request KHR_SURFACE and platform specific required instance extensions
    std::vector<const char*> enabled_instance_extensions_c_str{};
    if (m_context_window != nullptr) {
        const std::vector<std::string>& required_extensions = m_context_window->get_required_vulkan_instance_extensions();
        for (const std::string& extension_name : required_extensions) {
            enabled_instance_extensions_c_str.push_back(extension_name.c_str());
            log_debug->info("  SDL_Vulkan_GetInstanceExtensions(): {}", extension_name);
        }
    }

    auto check_instance_extension = [&](const char* name, bool& enable)
    {
        for (const VkExtensionProperties& extension : instance_extensions) {
            if (strcmp(extension.extensionName, name) == 0) {
                enabled_instance_extensions_c_str.push_back(name);
                enable = true;
                log_debug->info("  Enabling {}", extension.extensionName);
                return;
            }
        }
    };

    for (const VkExtensionProperties& extension : instance_extensions) {
        log_debug->info(
            "Vulkan Instance Extension: {} spec_version {:08x}",
            extension.extensionName,
            extension.specVersion
        );
    }

    // First check extensions that have not been deprecated / promoted to cor
    check_instance_extension(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, m_instance_extensions.m_VK_KHR_get_surface_capabilities2);
    check_instance_extension(VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME     , m_instance_extensions.m_VK_KHR_surface_maintenance1     );
    check_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME               , m_instance_extensions.m_VK_EXT_debug_utils              );
    check_instance_extension(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME     , m_instance_extensions.m_VK_EXT_swapchain_colorspace     );

    // Check extensions which are promoted to core or deprecated
    if (application_info.apiVersion < VK_API_VERSION_1_1) {
        check_instance_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, m_instance_extensions.m_VK_KHR_get_physical_device_properties2);
    } else {
        m_instance_extensions.m_VK_KHR_get_surface_capabilities2       = true;
        m_instance_extensions.m_VK_KHR_get_physical_device_properties2 = true;
    }

    m_capabilities.m_surface_capabilities2 = m_instance_extensions.m_VK_KHR_get_surface_capabilities2;

    if (!m_instance_extensions.m_VK_KHR_surface_maintenance1) {
        check_instance_extension(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME, m_instance_extensions.m_VK_EXT_surface_maintenance1);
    }

    if (!m_instance_extensions.m_VK_EXT_debug_utils) {
        check_instance_extension(VK_EXT_DEBUG_REPORT_EXTENSION_NAME, m_instance_extensions.m_VK_EXT_debug_report);
    }

    // https://vulkan.lunarg.com/doc/sdk/1.4.328.1/windows/khronos_validation_layer.html
    // https://vulkan.lunarg.com/doc/view/1.4.328.1/windows/layer_configuration.html
    // https://github.com/KhronosGroup/Vulkan-LoaderAndValidationLayers/blob/master/layers/vk_layer_settings.txt
    std::vector<std::unique_ptr<VkBool32>> bool_values;
    std::vector<VkLayerSettingEXT> layer_settings;
    auto set_validation_setting_bool = [&](const char* key, const bool value)
    {
        bool_values.push_back(std::make_unique<VkBool32>(value ? VK_TRUE : VK_FALSE));
        layer_settings.emplace_back(
            std::move(
                VkLayerSettingEXT{
                    .pLayerName   = "VK_LAYER_KHRONOS_validation",
                    .pSettingName = key,
                    .type         = VK_LAYER_SETTING_TYPE_BOOL32_EXT,
                    .valueCount   = 1,
                    .pValues      = bool_values.back().get()
                }
            )
        );
    };
    auto set_validation_setting_c_str = [&](const char* key, const char* value)
    {
        layer_settings.emplace_back(
            std::move(
                VkLayerSettingEXT{
                    .pLayerName   = "VK_LAYER_KHRONOS_validation",
                    .pSettingName = key,
                    .type         = VK_LAYER_SETTING_TYPE_BOOL32_EXT,
                    .valueCount   = 1,
                    .pValues      = value
                }
            )
        );
    };
    set_validation_setting_c_str("debug_action", "VK_DBG_LAYER_ACTION_CALLBACK");
    set_validation_setting_bool("validate_sync",                     true);
    set_validation_setting_bool("syncval_shader_accesses_heuristic", true);

    // https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/11207
    // set_validation_setting_bool("validate_best_practices",     true);
    set_validation_setting_bool("validate_best_practices_amd",    true);
    set_validation_setting_bool("validate_best_practices_arm",    true);
    set_validation_setting_bool("validate_best_practices_img",    true);
    set_validation_setting_bool("validate_best_practices_nvidia", true);

    const VkLayerSettingsCreateInfoEXT layer_settings_create_info{
        .sType        = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
        .pNext        = nullptr,
        .settingCount = static_cast<uint32_t>(layer_settings.size()),
        .pSettings    = layer_settings.data()
    };

    const VkInstanceCreateInfo instance_create_info = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = &layer_settings_create_info,
        .flags                   = 0,
        .pApplicationInfo        = &application_info,
        .enabledLayerCount       = static_cast<uint32_t>(enabled_instance_layers_c_str.size()),
        .ppEnabledLayerNames     = enabled_instance_layers_c_str.data(),
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
        surface_impl   = std::make_unique<Surface_impl>(*this, surface_create_info);
        vulkan_surface = surface_impl->get_vulkan_surface();
    }

    std::vector<const char*> device_extensions_c_str{};
    const bool physical_device_ok = choose_physical_device(surface_impl.get(), device_extensions_c_str);
    if (!physical_device_ok) {
        log_context->critical("vkCreateInstance() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    m_driver_properties = VkPhysicalDeviceDriverProperties{
        .sType              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
        .pNext              = nullptr,
        .driverID           = VK_DRIVER_ID_MAX_ENUM,
        .driverName         = {},
        .driverInfo         = {},
        .conformanceVersion = {},
    };
    VkPhysicalDeviceProperties2 physical_device_properties2 {
        .sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext      = &m_driver_properties,
        .properties = {}
    };
    vkGetPhysicalDeviceProperties2(m_vulkan_physical_device, &physical_device_properties2);
    VkPhysicalDeviceProperties& properties = physical_device_properties2.properties;
    const uint32_t api_version_variant = VK_API_VERSION_VARIANT(properties.apiVersion);
    const uint32_t api_version_major   = VK_API_VERSION_MAJOR  (properties.apiVersion);
    const uint32_t api_version_minor   = VK_API_VERSION_MINOR  (properties.apiVersion);
    const uint32_t api_version_patch   = VK_API_VERSION_PATCH  (properties.apiVersion);
    const VkConformanceVersion& conformance = m_driver_properties.conformanceVersion;
    log_context->info("Vulkan physical device properties:");
    log_context->info("  API version         = {}.{}.{}.{}", api_version_major, api_version_minor, api_version_patch, api_version_variant);
    log_context->info("  Driver ID           = {}",          c_str(m_driver_properties.driverID));
    log_context->info("  Driver name         = {}",          m_driver_properties.driverName);
    log_context->info("  Driver info         = {}",          m_driver_properties.driverInfo);
    log_context->info("  Driver version      = {:08x}",      properties.driverVersion);
    log_context->info("  Driver conformance  = {}",          conformance.major, conformance.minor, conformance.subminor, conformance.patch);
    log_context->info("  Vendor ID           = {:08x}",      properties.vendorID);
    log_context->info("  Device ID           = {:08x}",      properties.deviceID);
    log_context->info("  Device type         = {}",          c_str(properties.deviceType));
    log_context->info("  Device name         = {}",          properties.deviceName);

    VkPhysicalDeviceFeatures2 query_device_features{
        .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext    = nullptr,
        .features = VkPhysicalDeviceFeatures{}
    };
    VkBaseOutStructure* query_features_chain_last = reinterpret_cast<VkBaseOutStructure*>(&query_device_features);

    VkPhysicalDeviceTimelineSemaphoreFeatures query_timeline_semaphore_features = {
        .sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        .pNext             = nullptr,
        .timelineSemaphore = VK_FALSE
    };
    {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_timeline_semaphore_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR query_present_mode_fifo_latest_ready_features{
        .sType                      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_KHR,
        .pNext                      = nullptr,
        .presentModeFifoLatestReady = VK_FALSE
    };
    m_capabilities.m_present_mode_fifo_latest_ready =
        m_device_extensions.m_VK_KHR_present_mode_fifo_latest_ready ||
        m_device_extensions.m_VK_EXT_present_mode_fifo_latest_ready;
    if (m_capabilities.m_present_mode_fifo_latest_ready) {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_present_mode_fifo_latest_ready_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    m_capabilities.m_surface_maintenance1 =
        m_instance_extensions.m_VK_KHR_surface_maintenance1 ||
        m_instance_extensions.m_VK_EXT_surface_maintenance1;

    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT query_swapchain_maintenance_features{
        .sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
        .pNext                 = nullptr,
        .swapchainMaintenance1 = VK_FALSE,
    };
    m_capabilities.m_swapchain_maintenance1 =
        m_device_extensions.m_VK_KHR_swapchain_maintenance1 ||
        m_device_extensions.m_VK_EXT_swapchain_maintenance1;
    if (m_capabilities.m_swapchain_maintenance1) {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_swapchain_maintenance_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    vkGetPhysicalDeviceFeatures2(m_vulkan_physical_device, &query_device_features);

    bool debug_callback_registered = false;
    if (m_instance_extensions.m_VK_EXT_debug_utils) {
        VkDebugUtilsMessageTypeFlagsEXT message_types =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT    |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
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

    VkPhysicalDeviceFeatures2 set_device_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = nullptr
    };
    VkBaseOutStructure* set_features_chain_last = reinterpret_cast<VkBaseOutStructure*>(&set_device_features);

    VkPhysicalDeviceTimelineSemaphoreFeatures set_timeline_semaphore_features = {
        .sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        .pNext             = nullptr,
        .timelineSemaphore = query_timeline_semaphore_features.timelineSemaphore
    };
    {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_timeline_semaphore_features);
        set_features_chain_last        = set_features_chain_last->pNext;
    }

    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT set_swapchain_maintenance_features{
        .sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
        .pNext                 = nullptr,
        .swapchainMaintenance1 = query_swapchain_maintenance_features.swapchainMaintenance1
    };
    if (m_capabilities.m_swapchain_maintenance1) {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_swapchain_maintenance_features);
        set_features_chain_last        = set_features_chain_last->pNext;
        if (set_swapchain_maintenance_features.swapchainMaintenance1 == VK_TRUE) {
            log_debug->debug("Enabled feature swapchain maintenance1");
        }
    }
    VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR set_present_mode_fifo_latest_ready_features{
        .sType                      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_KHR,
        .pNext                      = nullptr,
        .presentModeFifoLatestReady = query_present_mode_fifo_latest_ready_features.presentModeFifoLatestReady
    };
    if (m_capabilities.m_present_mode_fifo_latest_ready) {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_present_mode_fifo_latest_ready_features);
        set_features_chain_last = set_features_chain_last->pNext;
        if (query_present_mode_fifo_latest_ready_features.presentModeFifoLatestReady == VK_TRUE) {
            log_debug->debug("Enabled feature present mode fifo latest ready");
        }
    }

    const VkDeviceCreateInfo device_create_info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &set_device_features,
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

    const VkCommandPoolCreateInfo command_pool_create_info{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_graphics_queue_family_index
    };
    result = vkCreateCommandPool(m_vulkan_device, &command_pool_create_info, nullptr, &m_vulkan_command_pool);
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateCommandPool() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    const VkSemaphoreTypeCreateInfo semaphore_type_create_info{
        .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .pNext         = nullptr,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue  = 0
    };

    const VkSemaphoreCreateInfo semaphore_create_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &semaphore_type_create_info,
        .flags = 0
    };

    result = vkCreateSemaphore(m_vulkan_device, &semaphore_create_info, nullptr, &m_vulkan_frame_end_semaphore);
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateSemaphore() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    m_info.glsl_version       = 460;
    m_info.vulkan_api_version = application_info.apiVersion;
    m_info.vendor             = get_vendor(properties.vendorID);

    m_info.max_per_stage_descriptor_samplers = properties.limits.maxPerStageDescriptorSamplers;
}

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
        log_context->critical("vkAllocateCommandBuffers() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }
    return vulkan_command_buffer;
}

Device_impl::~Device_impl() noexcept
{
    vkDeviceWaitIdle(m_vulkan_device);

    if (m_vulkan_command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_vulkan_device, m_vulkan_command_pool, nullptr);
    }
    // NOTE: This adds completion handlers for destroying related vulkan objects
    m_surface.reset();

    vkDeviceWaitIdle(m_vulkan_device);

    for (const Completion_handler& entry : m_completion_handlers) {
        entry.callback();
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

auto Device_impl::get_handle(const Texture& texture, const Sampler& sampler) const -> uint64_t
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(texture);
    static_cast<void>(sampler);
    return 0;
}

auto Device_impl::choose_depth_stencil_format(const unsigned int flags, int sample_count) const -> erhe::dataformat::Format
{
    ERHE_FATAL("Not implemented");
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
    ERHE_FATAL("Not implemented");
    static_cast<void>(buffer);
    static_cast<void>(offset);
    static_cast<void>(data);
    static_cast<void>(length);
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
        log_context->critical("vkQueueSubmit() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    ++m_frame_index;

    uint64_t latest_completed_frame{0};
    result = vkGetSemaphoreCounterValue(m_vulkan_device, m_vulkan_frame_end_semaphore, &latest_completed_frame);
    if (result != VK_SUCCESS) {
        log_context->error("vkGetSemaphoreCounterValue() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
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
    ERHE_FATAL("Not implemented");
    static_cast<void>(buffer_target);
    static_cast<void>(ring_buffer_usage);
    static_cast<void>(byte_count);
    return {};
}

void Device_impl::memory_barrier(Memory_barrier_mask barriers)
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(barriers);
}

auto Device_impl::get_format_properties(erhe::dataformat::Format format) const -> Format_properties
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(format);
    return {};
}

void Device_impl::clear_texture(Texture& texture, std::array<double, 4> value)
{
    ERHE_FATAL("Not implemented");
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
