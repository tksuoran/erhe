#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_immediate_commands.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/graphics_log.hpp"

#include "erhe_window/window.hpp"

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
) -> VkBool32;

[[nodiscard]] auto Device_impl_debug_utils_messenger_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
    VkDebugUtilsMessageTypeFlagsEXT             message_types,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void*                                       user_data
) -> VkBool32;

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

Device_impl::Device_impl(
    Device&                    device,
    const Surface_create_info& surface_create_info,
    const Graphics_config&     graphics_config
)
    : m_context_window{surface_create_info.context_window}
    , m_device        {device}
    , m_shader_monitor{device}
{
    if (graphics_config.renderdoc_capture_support) {
        initialize_frame_capture();
    }

    {
        // Disable nuisance implicit layers, but not when RenderDoc is attached
        // (RenderDoc uses an implicit layer for Vulkan capture)
        const bool renderdoc_attached = (GetModuleHandleA("renderdoc.dll") != nullptr);
        if (!renderdoc_attached) {
            set_env("VK_LOADER_LAYERS_DISABLE", "~implicit~");
        }
        set_env("DISABLE_LAYER_NV_OPTIMUS_1", "1");
        set_env("DISABLE_LAYER_NV_GR2608_1", "1");
        set_env("DISABLE_VULKAN_OBS_CAPTURE", "1");
    }

    VkResult result{VK_SUCCESS};

    result = volkInitialize();
    if (result != VK_SUCCESS) {
        log_context->critical("volkInitialize() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    uint32_t instance_layer_count{0};
    result = vkEnumerateInstanceLayerProperties(&instance_layer_count, nullptr);
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateInstanceLayerProperties() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    // Setup vulkan instance layers
    std::vector<VkLayerProperties> instance_layers(instance_layer_count);
    result = vkEnumerateInstanceLayerProperties(&instance_layer_count, instance_layers.data());
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateInstanceLayerProperties() failed with {} {}", static_cast<int32_t>(result), c_str(result));
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

    if (graphics_config.vulkan_validation_layers && !graphics_config.renderdoc_capture_support) {
        check_layer("VK_LAYER_KHRONOS_validation", m_instance_layers.m_VK_LAYER_KHRONOS_validation);
    }

    //check_layer("VK_LAYER_LUNARG_crash_diagnostic", m_instance_layers.m_VK_LAYER_LUNARG_crash_diagnostic);

    uint32_t instance_extension_count{0};
    result = vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, nullptr);
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateInstanceExtensionProperties() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    std::vector<VkExtensionProperties> instance_extensions(instance_extension_count);
    result = vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, instance_extensions.data());
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateInstanceExtensionProperties() failed with {} {}", static_cast<int32_t>(result), c_str(result));
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
        log_context->critical("vkCreateInstance() failed with {} {}", static_cast<int32_t>(result), c_str(result));
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
        log_context->critical("vkCreateInstance() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    m_memory_properties = VkPhysicalDeviceMemoryProperties2{
        .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
        .pNext            = nullptr,
        .memoryProperties = {
            .memoryTypeCount = 0,
            .memoryTypes     = {},
            .memoryHeapCount = 0,
            .memoryHeaps     = {}
        }
    };
    vkGetPhysicalDeviceMemoryProperties2(m_vulkan_physical_device, &m_memory_properties);

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

    VkPhysicalDeviceDescriptorIndexingFeatures query_descriptor_indexing_features{
        .sType                                         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
        .pNext                                         = nullptr,
        .shaderInputAttachmentArrayDynamicIndexing     = VK_FALSE,
        .shaderUniformTexelBufferArrayDynamicIndexing  = VK_FALSE,
        .shaderStorageTexelBufferArrayDynamicIndexing  = VK_FALSE,
        .shaderUniformBufferArrayNonUniformIndexing    = VK_FALSE,
        .shaderSampledImageArrayNonUniformIndexing     = VK_FALSE,
        .shaderStorageBufferArrayNonUniformIndexing    = VK_FALSE,
        .shaderStorageImageArrayNonUniformIndexing     = VK_FALSE,
        .shaderInputAttachmentArrayNonUniformIndexing  = VK_FALSE,
        .shaderUniformTexelBufferArrayNonUniformIndexing = VK_FALSE,
        .shaderStorageTexelBufferArrayNonUniformIndexing = VK_FALSE,
        .descriptorBindingUniformBufferUpdateAfterBind   = VK_FALSE,
        .descriptorBindingSampledImageUpdateAfterBind    = VK_FALSE,
        .descriptorBindingStorageImageUpdateAfterBind    = VK_FALSE,
        .descriptorBindingStorageBufferUpdateAfterBind   = VK_FALSE,
        .descriptorBindingUniformTexelBufferUpdateAfterBind = VK_FALSE,
        .descriptorBindingStorageTexelBufferUpdateAfterBind = VK_FALSE,
        .descriptorBindingUpdateUnusedWhilePending         = VK_FALSE,
        .descriptorBindingPartiallyBound                   = VK_FALSE,
        .descriptorBindingVariableDescriptorCount          = VK_FALSE,
        .runtimeDescriptorArray                            = VK_FALSE,
    };
    {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_descriptor_indexing_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    VkPhysicalDeviceVulkan11Features query_vulkan_11_features{
        .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext            = nullptr,
        .shaderDrawParameters = VK_FALSE,
    };
    {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_vulkan_11_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    VkPhysicalDeviceVulkan13Features query_vulkan_13_features{
        .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext            = nullptr,
        .synchronization2 = VK_FALSE,
        .dynamicRendering = VK_FALSE,
    };
    {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_vulkan_13_features);
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
            log_context->warn("vkCreateDebugUtilsMessengerEXT() failed with {} {}", static_cast<int32_t>(result), c_str(result));
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
            log_context->warn("vkCreateDebugReportCallbackEXT() failed with {} {}", static_cast<int32_t>(result), c_str(result));
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
        .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext    = nullptr,
        .features = {
            .multiDrawIndirect = query_device_features.features.multiDrawIndirect,
            .samplerAnisotropy = query_device_features.features.samplerAnisotropy,
            .shaderInt64       = query_device_features.features.shaderInt64,
        }
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

    VkPhysicalDeviceDescriptorIndexingFeatures set_descriptor_indexing_features{
        .sType                                         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
        .pNext                                         = nullptr,
        .shaderInputAttachmentArrayDynamicIndexing     = query_descriptor_indexing_features.shaderInputAttachmentArrayDynamicIndexing,
        .shaderUniformTexelBufferArrayDynamicIndexing  = query_descriptor_indexing_features.shaderUniformTexelBufferArrayDynamicIndexing,
        .shaderStorageTexelBufferArrayDynamicIndexing  = query_descriptor_indexing_features.shaderStorageTexelBufferArrayDynamicIndexing,
        .shaderUniformBufferArrayNonUniformIndexing    = query_descriptor_indexing_features.shaderUniformBufferArrayNonUniformIndexing,
        .shaderSampledImageArrayNonUniformIndexing     = query_descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing,
        .shaderStorageBufferArrayNonUniformIndexing    = query_descriptor_indexing_features.shaderStorageBufferArrayNonUniformIndexing,
        .shaderStorageImageArrayNonUniformIndexing     = query_descriptor_indexing_features.shaderStorageImageArrayNonUniformIndexing,
        .shaderInputAttachmentArrayNonUniformIndexing  = query_descriptor_indexing_features.shaderInputAttachmentArrayNonUniformIndexing,
        .shaderUniformTexelBufferArrayNonUniformIndexing = query_descriptor_indexing_features.shaderUniformTexelBufferArrayNonUniformIndexing,
        .shaderStorageTexelBufferArrayNonUniformIndexing = query_descriptor_indexing_features.shaderStorageTexelBufferArrayNonUniformIndexing,
        .descriptorBindingUniformBufferUpdateAfterBind   = query_descriptor_indexing_features.descriptorBindingUniformBufferUpdateAfterBind,
        .descriptorBindingSampledImageUpdateAfterBind    = query_descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind,
        .descriptorBindingStorageImageUpdateAfterBind    = query_descriptor_indexing_features.descriptorBindingStorageImageUpdateAfterBind,
        .descriptorBindingStorageBufferUpdateAfterBind   = query_descriptor_indexing_features.descriptorBindingStorageBufferUpdateAfterBind,
        .descriptorBindingUniformTexelBufferUpdateAfterBind = query_descriptor_indexing_features.descriptorBindingUniformTexelBufferUpdateAfterBind,
        .descriptorBindingStorageTexelBufferUpdateAfterBind = query_descriptor_indexing_features.descriptorBindingStorageTexelBufferUpdateAfterBind,
        .descriptorBindingUpdateUnusedWhilePending         = query_descriptor_indexing_features.descriptorBindingUpdateUnusedWhilePending,
        .descriptorBindingPartiallyBound                   = query_descriptor_indexing_features.descriptorBindingPartiallyBound,
        .descriptorBindingVariableDescriptorCount          = query_descriptor_indexing_features.descriptorBindingVariableDescriptorCount,
        .runtimeDescriptorArray                            = query_descriptor_indexing_features.runtimeDescriptorArray,
    };
    {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_descriptor_indexing_features);
        set_features_chain_last        = set_features_chain_last->pNext;
        if (set_descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE) {
            log_debug->debug("Enabled feature descriptorBindingSampledImageUpdateAfterBind");
        }
        if (set_descriptor_indexing_features.descriptorBindingPartiallyBound == VK_TRUE) {
            log_debug->debug("Enabled feature descriptorBindingPartiallyBound");
        }
        if (set_descriptor_indexing_features.descriptorBindingVariableDescriptorCount == VK_TRUE) {
            log_debug->debug("Enabled feature descriptorBindingVariableDescriptorCount");
        }
        if (set_descriptor_indexing_features.runtimeDescriptorArray == VK_TRUE) {
            log_debug->debug("Enabled feature runtimeDescriptorArray");
        }
    }

    VkPhysicalDeviceVulkan11Features set_vulkan_11_features{
        .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext            = nullptr,
        .shaderDrawParameters = query_vulkan_11_features.shaderDrawParameters,
    };
    {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_vulkan_11_features);
        set_features_chain_last        = set_features_chain_last->pNext;
        if (set_vulkan_11_features.shaderDrawParameters == VK_TRUE) {
            log_debug->debug("Enabled feature shaderDrawParameters");
        }
    }

    VkPhysicalDeviceVulkan13Features set_vulkan_13_features{
        .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext            = nullptr,
        .synchronization2 = query_vulkan_13_features.synchronization2,
        .dynamicRendering = query_vulkan_13_features.dynamicRendering,
    };
    {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_vulkan_13_features);
        set_features_chain_last        = set_features_chain_last->pNext;
        if (set_vulkan_13_features.synchronization2 == VK_TRUE) {
            log_debug->debug("Enabled feature synchronization2");
        }
        if (set_vulkan_13_features.dynamicRendering == VK_TRUE) {
            log_debug->debug("Enabled feature dynamicRendering");
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
        log_context->critical("vkCreateDevice() failed with {} {}", static_cast<int32_t>(result), c_str(result));
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
        log_context->critical("vmaImportVulkanFunctionsFromVolk() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    vma_create_info.pVulkanFunctions = &vma_vulkan_functions;

    result = vmaCreateAllocator(&vma_create_info, &m_vma_allocator);
    if (result != VK_SUCCESS) {
        log_context->critical("vmaCreateAllocator() failed with {} {}", static_cast<int32_t>(result), c_str(result));
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
        log_context->critical("vkCreateCommandPool() failed with {} {}", static_cast<int32_t>(result), c_str(result));
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
        log_context->critical("vkCreateSemaphore() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    m_immediate_commands = std::make_unique<Vulkan_immediate_commands>(
        *this, m_graphics_queue_family_index, false, "Device_impl::m_immediate_commands"
    );

    m_info.vendor                       = get_vendor(properties.vendorID);
    m_info.glsl_version                 = 460;
    m_info.vulkan_api_version           = application_info.apiVersion;
    m_info.use_binary_shaders           = true;
    m_info.use_integer_polygon_ids      = true;
    m_info.texture_heap_path            = Texture_heap_path::vulkan_descriptor_indexing;
    m_info.use_sparse_texture           = false;
    const bool has_multi_draw_indirect = (query_device_features.features.multiDrawIndirect == VK_TRUE);
    const bool has_shader_draw_parameters = (query_vulkan_11_features.shaderDrawParameters == VK_TRUE);
    if (has_multi_draw_indirect && has_shader_draw_parameters) {
        m_info.use_multi_draw_indirect_core = true;
        m_info.emulate_multi_draw_indirect  = false;
        log_startup->info("Multi Draw Indirect: native Vulkan (multiDrawIndirect + shaderDrawParameters)");
    } else {
        m_info.use_multi_draw_indirect_core = false;
        m_info.emulate_multi_draw_indirect  = true;
        log_startup->info(
            "Multi Draw Indirect: emulation via push constants (multiDrawIndirect={}, shaderDrawParameters={})",
            has_multi_draw_indirect, has_shader_draw_parameters
        );
    }
    m_info.use_multi_draw_indirect_arb  = false;
    m_info.use_compute_shader           = true;
    m_info.use_shader_storage_buffers   = true;
    m_info.use_clear_texture            = true;
    m_info.use_persistent_buffers       = true;

    // Vulkan coordinate conventions
    m_info.coordinate_conventions.native_depth_range = erhe::math::Depth_range::zero_to_one;
    m_info.coordinate_conventions.framebuffer_origin = erhe::math::Framebuffer_origin::top_left;
    m_info.coordinate_conventions.clip_space_y_flip  = erhe::math::Clip_space_y_flip::disabled;
    m_info.coordinate_conventions.texture_origin     = erhe::math::Texture_origin::top_left;

    const VkPhysicalDeviceLimits& limits = properties.limits;

    // Vulkan drivers may report UINT32_MAX for "unlimited" limits;
    // cap to INT_MAX so the int fields in Device_info stay positive.
    auto cap = [](uint32_t value) -> int {
        return static_cast<int>(std::min(value, static_cast<uint32_t>(INT_MAX)));
    };

    m_info.max_per_stage_descriptor_samplers        = limits.maxPerStageDescriptorSamplers;
    m_info.max_combined_texture_image_units          = cap(limits.maxDescriptorSetSampledImages);
    m_info.max_uniform_block_size                    = cap(limits.maxUniformBufferRange);
    m_info.max_shader_storage_buffer_bindings        = cap(limits.maxPerStageDescriptorStorageBuffers);
    m_info.max_uniform_buffer_bindings               = cap(limits.maxPerStageDescriptorUniformBuffers);
    m_info.max_compute_shader_storage_blocks         = cap(limits.maxPerStageDescriptorStorageBuffers);
    m_info.max_compute_uniform_blocks                = cap(limits.maxPerStageDescriptorUniformBuffers);
    m_info.max_compute_shared_memory_size            = cap(limits.maxComputeSharedMemorySize);
    m_info.max_vertex_shader_storage_blocks          = cap(limits.maxPerStageDescriptorStorageBuffers);
    m_info.max_vertex_uniform_blocks                 = cap(limits.maxPerStageDescriptorUniformBuffers);
    m_info.max_fragment_shader_storage_blocks        = cap(limits.maxPerStageDescriptorStorageBuffers);
    m_info.max_fragment_uniform_blocks               = cap(limits.maxPerStageDescriptorUniformBuffers);
    m_info.max_geometry_shader_storage_blocks        = cap(limits.maxPerStageDescriptorStorageBuffers);
    m_info.max_geometry_uniform_blocks               = cap(limits.maxPerStageDescriptorUniformBuffers);
    m_info.max_tess_control_shader_storage_blocks    = cap(limits.maxPerStageDescriptorStorageBuffers);
    m_info.max_tess_control_uniform_blocks           = cap(limits.maxPerStageDescriptorUniformBuffers);
    m_info.max_tess_evaluation_shader_storage_blocks = cap(limits.maxPerStageDescriptorStorageBuffers);
    m_info.max_tess_evaluation_uniform_blocks        = cap(limits.maxPerStageDescriptorUniformBuffers);
    m_info.max_vertex_attribs                        = cap(limits.maxVertexInputAttributes);
    m_info.max_samples                               = cap(limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts);
    m_info.max_color_texture_samples                 = cap(limits.framebufferColorSampleCounts);
    m_info.max_depth_texture_samples                 = cap(limits.framebufferDepthSampleCounts);
    m_info.max_framebuffer_samples                   = cap(limits.framebufferColorSampleCounts);
    m_info.max_integer_samples                       = cap(limits.framebufferColorSampleCounts);
    m_info.max_texture_size                          = cap(limits.maxImageDimension2D);
    m_info.max_3d_texture_size                       = cap(limits.maxImageDimension3D);
    m_info.max_cube_map_texture_size                 = cap(limits.maxImageDimensionCube);
    m_info.max_texture_buffer_size                   = cap(limits.maxTexelBufferElements);
    m_info.max_array_texture_layers                  = cap(limits.maxImageArrayLayers);
    m_info.max_texture_max_anisotropy                = limits.maxSamplerAnisotropy;
    m_info.shader_storage_buffer_offset_alignment    = static_cast<unsigned int>(limits.minStorageBufferOffsetAlignment);
    m_info.uniform_buffer_offset_alignment           = static_cast<unsigned int>(limits.minUniformBufferOffsetAlignment);

    // Create pipeline cache
    {
        const VkPipelineCacheCreateInfo pipeline_cache_create_info{
            .sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
            .pNext           = nullptr,
            .flags           = 0,
            .initialDataSize = 0,
            .pInitialData    = nullptr
        };
        result = vkCreatePipelineCache(m_vulkan_device, &pipeline_cache_create_info, nullptr, &m_pipeline_cache);
        if (result != VK_SUCCESS) {
            log_context->error("vkCreatePipelineCache() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        }
    }

    // Create descriptor set layout and pipeline layout.
    // Use a single set (set 0) with bindings for UBOs, SSBOs, and combined image samplers.
    // GLSL layout(binding = N) maps to set = 0, binding = N.
    {
        static constexpr uint32_t max_bindings = 16;
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        bindings.reserve(max_bindings);

        // Bindings 0..7 for uniform buffers
        for (uint32_t i = 0; i < 8; ++i) {
            bindings.push_back(VkDescriptorSetLayoutBinding{
                .binding            = i,
                .descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount    = 1,
                .stageFlags         = VK_SHADER_STAGE_ALL,
                .pImmutableSamplers = nullptr
            });
        }

        // Bindings 8..11 for storage buffers
        for (uint32_t i = 8; i < 12; ++i) {
            bindings.push_back(VkDescriptorSetLayoutBinding{
                .binding            = i,
                .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount    = 1,
                .stageFlags         = VK_SHADER_STAGE_ALL,
                .pImmutableSamplers = nullptr
            });
        }

        // Bindings 12..15 for combined image samplers
        for (uint32_t i = 12; i < 16; ++i) {
            bindings.push_back(VkDescriptorSetLayoutBinding{
                .binding            = i,
                .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount    = 1,
                .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr
            });
        }

        VkDescriptorSetLayoutCreateFlags layout_flags = 0;
        if (m_device_extensions.m_VK_KHR_push_descriptor) {
            layout_flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        }

        const VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext        = nullptr,
            .flags        = layout_flags,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings    = bindings.data()
        };

        result = vkCreateDescriptorSetLayout(m_vulkan_device, &descriptor_set_layout_create_info, nullptr, &m_descriptor_set_layout);
        if (result != VK_SUCCESS) {
            log_context->critical("vkCreateDescriptorSetLayout() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }

        // Create a second descriptor set layout for texture heap (set 1)
        // This uses descriptor indexing for a large array of combined image samplers
        {
            static constexpr uint32_t max_texture_heap_size = 256;
            const VkDescriptorBindingFlags texture_binding_flags =
                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

            const VkDescriptorSetLayoutBindingFlagsCreateInfo texture_binding_flags_info{
                .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
                .pNext         = nullptr,
                .bindingCount  = 1,
                .pBindingFlags = &texture_binding_flags
            };

            const VkDescriptorSetLayoutBinding texture_binding{
                .binding            = 0,
                .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount    = max_texture_heap_size,
                .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr
            };

            const VkDescriptorSetLayoutCreateInfo texture_layout_create_info{
                .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .pNext        = &texture_binding_flags_info,
                .flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                .bindingCount = 1,
                .pBindings    = &texture_binding
            };

            result = vkCreateDescriptorSetLayout(m_vulkan_device, &texture_layout_create_info, nullptr, &m_texture_set_layout);
            if (result != VK_SUCCESS) {
                log_context->warn("Texture heap descriptor set layout creation failed with {} - textures may not work", static_cast<int32_t>(result));
                m_texture_set_layout = VK_NULL_HANDLE;
            }
        }

        // Push constant range for draw ID
        const VkPushConstantRange push_constant_range{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(int32_t) // ERHE_DRAW_ID
        };

        // Pipeline layout with set 0 (UBO/SSBO) and optionally set 1 (texture heap)
        std::vector<VkDescriptorSetLayout> set_layouts;
        set_layouts.push_back(m_descriptor_set_layout);
        if (m_texture_set_layout != VK_NULL_HANDLE) {
            set_layouts.push_back(m_texture_set_layout);
        }

        const VkPipelineLayoutCreateInfo pipeline_layout_create_info{
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext                  = nullptr,
            .flags                  = 0,
            .setLayoutCount         = static_cast<uint32_t>(set_layouts.size()),
            .pSetLayouts            = set_layouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &push_constant_range
        };

        result = vkCreatePipelineLayout(m_vulkan_device, &pipeline_layout_create_info, nullptr, &m_pipeline_layout);
        if (result != VK_SUCCESS) {
            log_context->critical("vkCreatePipelineLayout() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }

        // Create per-frame descriptor pool for fallback binding (when push descriptors unavailable)
        if (!m_device_extensions.m_VK_KHR_push_descriptor) {
            const VkDescriptorPoolSize pool_sizes[] = {
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         256},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         256},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256}
            };

            const VkDescriptorPoolCreateInfo pool_create_info{
                .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .pNext         = nullptr,
                .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                .maxSets       = 256,
                .poolSizeCount = 3,
                .pPoolSizes    = pool_sizes
            };

            result = vkCreateDescriptorPool(m_vulkan_device, &pool_create_info, nullptr, &m_per_frame_descriptor_pool);
            if (result != VK_SUCCESS) {
                log_context->error("vkCreateDescriptorPool() for fallback pool failed with {}", static_cast<int32_t>(result));
            }
        }

        log_context->info(
            "Pipeline infrastructure created: push_descriptors={}",
            m_device_extensions.m_VK_KHR_push_descriptor ? "yes" : "no"
        );
    }
}

}
