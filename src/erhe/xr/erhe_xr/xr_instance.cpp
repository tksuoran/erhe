#include "erhe_xr/xr_instance.hpp"
#include "erhe_xr/xr_session.hpp"
#include "erhe_xr/xr.hpp"
#include "erhe_xr/xr_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#ifdef _WIN32
#   include <unknwn.h>
#   define XR_USE_PLATFORM_WIN32      1
#endif

#ifdef linux
#   define XR_USE_PLATFORM_LINUX      1
#endif

//#define XR_USE_GRAPHICS_API_VULKAN 1
#define XR_USE_GRAPHICS_API_OPENGL 1
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <thread>

namespace erhe::xr {

Xr_instance::Xr_instance(const Xr_configuration& configuration)
    : m_configuration(configuration)
{
    ERHE_PROFILE_FUNCTION();

    if (!enumerate_layers()) {
        return;
    }

    if (!enumerate_extensions()) {
        return;
    }

    if (!create_instance()) {
        return;
    }

    if (!get_system_info()) {
        return;
    }

    if (!enumerate_view_configurations()) {
        return;
    }

    if (!enumerate_blend_modes()) {
        return;
    }

    if (!initialize_actions()) {
        return;
    }
}

auto Xr_instance::is_available() const -> bool
{
    return m_xr_instance != XR_NULL_HANDLE;
}

auto xr_debug_utils_messenger_callback(
    XrDebugUtilsMessageSeverityFlagsEXT         messageSeverity,
    XrDebugUtilsMessageTypeFlagsEXT             messageTypes,
    const XrDebugUtilsMessengerCallbackDataEXT* callbackData,
    void*                                       userData
) -> XrBool32
{
    auto* instance = reinterpret_cast<Xr_instance*>(userData);
    return instance->debug_utils_messenger_callback(messageSeverity, messageTypes, callbackData);
}


auto Xr_instance::debug_utils_messenger_callback(
    XrDebugUtilsMessageSeverityFlagsEXT         messageSeverity,
    XrDebugUtilsMessageTypeFlagsEXT             messageTypes,
    const XrDebugUtilsMessengerCallbackDataEXT* callbackData
) const -> XrBool32
{
    log_xr->info(
        "XR: S:{} T:{} I:{} M:{}",
        to_string_message_severity(messageSeverity),
        to_string_message_type(messageTypes),
        callbackData->messageId,
        callbackData->message
    );

    if (callbackData->objectCount > 0) {
        log_xr->info("Objects:");
        //const erhe::log::Indenter scope_indent;
        for (uint32_t i = 0; i < callbackData->objectCount; ++i) {
            log_xr->info(
                "{} {} {}",
                c_str(callbackData->objects[i].objectType),
                callbackData->objects[i].objectHandle,
                callbackData->objects[i].objectName
            );
        }
    }

    if (callbackData->sessionLabelCount > 0) {
        log_xr->info("Session labels:");
        for (uint32_t i = 0; i < callbackData->sessionLabelCount; ++i) {
            log_xr->info("    {}", callbackData->sessionLabels[i].labelName);
        }
    }

    return XR_FALSE;
}

XrBool32 erhe_xrDebugUtilsMessengerCallbackEXT(
    XrDebugUtilsMessageSeverityFlagsEXT         message_severity,
    XrDebugUtilsMessageTypeFlagsEXT             message_types,
    const XrDebugUtilsMessengerCallbackDataEXT* callback_data,
    void*                                       user_data
)
{
    Xr_instance* instance = static_cast<Xr_instance*>(user_data);
    return instance->debug_utils_messenger_callback(message_severity, message_types, callback_data);
}

[[nodiscard]] auto Xr_instance::debug_utils_messenger_callback(
    XrDebugUtilsMessageSeverityFlagsEXT         message_severity,
    XrDebugUtilsMessageTypeFlagsEXT             message_types,
    const XrDebugUtilsMessengerCallbackDataEXT* callback_data
) -> XrBool32
{
    spdlog::level::level_enum level = spdlog::level::level_enum::info;
    if (message_severity & XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) { level = spdlog::level::level_enum::trace; }
    if (message_severity & XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT   ) { level = spdlog::level::level_enum::info; }
    if (message_severity & XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) { level = spdlog::level::level_enum::warn; }
    if (message_severity & XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT  ) { level = spdlog::level::level_enum::err; }

    std::stringstream type_ss;
    if (message_types & XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT    ) { type_ss << " general"; }
    if (message_types & XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT ) { type_ss << " validation"; }
    if (message_types & XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) { type_ss << " performance"; }
    if (message_types & XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT) { type_ss << " validation"; }

    std::stringstream objects_ss;
    if (callback_data->objectCount > 0) {
        objects_ss << "\nObjects:";
        for (uint32_t i = 0; i < callback_data->objectCount; ++i) {
            XrDebugUtilsObjectNameInfoEXT& object = callback_data->objects[i];
            objects_ss << fmt::format("\n    [{}] {} handle = {} name = {}", i, c_str(object.objectType), object.objectHandle, object.objectName);
        }
    }
    std::stringstream labels_ss;
    if (callback_data->sessionLabelCount > 0) {
        labels_ss << "\nSession labels:";
        for (uint32_t i = 0; i < callback_data->sessionLabelCount; ++i) {
            XrDebugUtilsLabelEXT& label = callback_data->sessionLabels[i];
            labels_ss << fmt::format("\n    [{}] {}", i, label.labelName);
        }
    }

    log_xr->log(
        level,
        fmt::format(
            "OpenXR debug message: type ={}, id = {}, function = {}, message = {}{}{}",
            type_ss.str(),
            callback_data->messageId,
            (callback_data->functionName != nullptr) ? callback_data->functionName : "",
            callback_data->message,
            objects_ss.str(),
            labels_ss.str()
        )
    );
    return XR_TRUE;
}

auto Xr_instance::create_instance() -> bool
{
    ERHE_PROFILE_FUNCTION();

    log_xr->trace("{}", __func__);

    std::vector<const char*> required_extensions;
    required_extensions.push_back(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);

    if (m_configuration.debug && has_extension(XR_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        extensions.EXT_debug_utils = true;
        required_extensions.push_back(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    if (m_configuration.quad_view && has_extension(XR_VARJO_QUAD_VIEWS_EXTENSION_NAME)) {
        extensions.VARJO_quad_views = true;
        required_extensions.push_back(XR_VARJO_QUAD_VIEWS_EXTENSION_NAME);
    }
    if (m_configuration.depth) {
        if (has_extension(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME)) {
            extensions.KHR_composition_layer_depth = true;
            required_extensions.push_back(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
        }
        if (has_extension(XR_VARJO_ENVIRONMENT_DEPTH_ESTIMATION_EXTENSION_NAME)) {
            extensions.VARJO_environment_depth_estimation = true;
            required_extensions.push_back(XR_VARJO_ENVIRONMENT_DEPTH_ESTIMATION_EXTENSION_NAME);
        }
        //XR_VARJO_COMPOSITION_LAYER_DEPTH_TEST_EXTENSION_NAME,
    }
    if (m_configuration.visibility_mask && has_extension(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME)) {
        extensions.KHR_visibility_mask = true;
        required_extensions.push_back(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME);
    }
    if (m_configuration.hand_tracking && has_extension(XR_EXT_HAND_TRACKING_EXTENSION_NAME)) {
        extensions.EXT_hand_tracking = true;
        required_extensions.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
    }
    if (m_configuration.passthrough_fb && has_extension(XR_FB_PASSTHROUGH_EXTENSION_NAME)) {
        extensions.FB_passthrough = true;
        required_extensions.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
    }
    // XR_OCULUS_recenter_event
    if (has_extension(XR_FB_COLOR_SPACE_EXTENSION_NAME)) {
        extensions.FB_color_space = true;
        required_extensions.push_back(XR_FB_COLOR_SPACE_EXTENSION_NAME);
    }
    if (has_extension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME)) {
        extensions.FB_display_refresh_rate = true;
        required_extensions.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    }

    // XR_META_passthrough_layer_resumed_event
    // XR_META_passthrough_color_lut
    // XR_META_passthrough_preferences

    const XrInstanceCreateInfo create_info {
        .type                   = XR_TYPE_INSTANCE_CREATE_INFO,
        .next                   = nullptr,
        .createFlags            = 0,
        .applicationInfo = {
            .applicationName    = { 'e', 'r', 'h', 'e', '\0' },
            .applicationVersion = 1,
            .engineName         = { 'e', 'r', 'h', 'e', '\0' },
            .engineVersion      = 1,
            .apiVersion         = XR_API_VERSION_1_1
        },
        .enabledApiLayerCount   = 0,
        .enabledApiLayerNames   = nullptr,
        .enabledExtensionCount  = static_cast<uint32_t>(required_extensions.size()),
        .enabledExtensionNames  = required_extensions.data(),
    };

    ERHE_XR_CHECK(xrCreateInstance(&create_info, &m_xr_instance));

    // m_xrSetEnvironmentDepthEstimationVARJO = nullptr;
    // if (!check("xrGetInstanceProcAddr",
    //            xrGetInstanceProcAddr(m_xr_instance,
    //                                  "xrSetEnvironmentDepthEstimationVARJO",
    //                                  reinterpret_cast<PFN_xrVoidFunction*>(&m_xrSetEnvironmentDepthEstimationVARJO))))
    // {
    //     return false;
    // }

    const XrDebugUtilsMessengerCreateInfoEXT debug_utils_messenger_create_info{
        .type              = XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .next              = nullptr,
        .messageSeverities =
            XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT    |
            XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageTypes      =
            XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT     |
            XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT  |
            XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
            XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT,
        .userCallback      = xr_debug_utils_messenger_callback,
        .userData          = this
    };

    if (extensions.EXT_debug_utils) {
        xrCreateDebugUtilsMessengerEXT = get_proc_addr<PFN_xrCreateDebugUtilsMessengerEXT>("xrCreateDebugUtilsMessengerEXT");
        XrDebugUtilsMessageSeverityFlagsEXT severities =
            XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT    |
            XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        XrDebugUtilsMessageTypeFlagsEXT types =
            XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT     |
            XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT  |
            XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
            XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
        XrDebugUtilsMessengerCreateInfoEXT debug_utils_create_info{
            .type              = XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .next              = nullptr,
            .messageSeverities = severities,
            .messageTypes      = types,  
            .userCallback      = erhe_xrDebugUtilsMessengerCallbackEXT,
            .userData          = this
        };
        XrResult result = xrCreateDebugUtilsMessengerEXT(m_xr_instance, &debug_utils_create_info, &m_debug_utils_messenger);
        if (result != XR_SUCCESS) {
            log_xr->warn("OpenXR: xrCreateDebugUtilsMessengerEXT() failed");
        }
    }

    xrGetOpenGLGraphicsRequirementsKHR = get_proc_addr<PFN_xrGetOpenGLGraphicsRequirementsKHR>("xrGetOpenGLGraphicsRequirementsKHR");

    XrInstanceProperties instance_properties {
        .type = XR_TYPE_INSTANCE_PROPERTIES,
        .next = nullptr,
    };
    xrGetInstanceProperties(m_xr_instance, &instance_properties);
    const uint16_t major = (instance_properties.runtimeVersion >> 48) & uint64_t{0x0000ffffu};
    const uint16_t minor = (instance_properties.runtimeVersion >> 32) & uint64_t{0x0000ffffu};
    const uint16_t patch = (instance_properties.runtimeVersion >>  0) & uint64_t{0xffffffffu};
    log_xr->info("OpenXR runtime version: {}.{}.{}", major, minor, patch);
    log_xr->info("OpenXR runtime: {}", instance_properties.runtimeName);

    if (extensions.KHR_visibility_mask) {
        xrGetVisibilityMaskKHR = get_proc_addr<PFN_xrGetVisibilityMaskKHR>("xrGetVisibilityMaskKHR");
    }

    if (extensions.FB_passthrough) {
        xrCreatePassthroughFB            = get_proc_addr<PFN_xrCreatePassthroughFB           >("xrCreatePassthroughFB");
        xrDestroyPassthroughFB           = get_proc_addr<PFN_xrDestroyPassthroughFB          >("xrDestroyPassthroughFB");
        xrPassthroughStartFB             = get_proc_addr<PFN_xrPassthroughStartFB            >("xrPassthroughStartFB");
        xrPassthroughPauseFB             = get_proc_addr<PFN_xrPassthroughPauseFB            >("xrPassthroughPauseFB");
        xrCreatePassthroughLayerFB       = get_proc_addr<PFN_xrCreatePassthroughLayerFB      >("xrCreatePassthroughLayerFB");
        xrDestroyPassthroughLayerFB      = get_proc_addr<PFN_xrDestroyPassthroughLayerFB     >("xrDestroyPassthroughLayerFB");
        xrPassthroughLayerPauseFB        = get_proc_addr<PFN_xrPassthroughLayerPauseFB       >("xrPassthroughLayerPauseFB");
        xrPassthroughLayerResumeFB       = get_proc_addr<PFN_xrPassthroughLayerResumeFB      >("xrPassthroughLayerResumeFB");
        xrPassthroughLayerSetStyleFB     = get_proc_addr<PFN_xrPassthroughLayerSetStyleFB    >("xrPassthroughLayerSetStyleFB");
        xrCreateGeometryInstanceFB       = get_proc_addr<PFN_xrCreateGeometryInstanceFB      >("xrCreateGeometryInstanceFB");
        xrDestroyGeometryInstanceFB      = get_proc_addr<PFN_xrDestroyGeometryInstanceFB     >("xrDestroyGeometryInstanceFB");
        xrGeometryInstanceSetTransformFB = get_proc_addr<PFN_xrGeometryInstanceSetTransformFB>("xrGeometryInstanceSetTransformFB");
    }

    if (extensions.FB_color_space) {
        xrEnumerateColorSpacesFB         = get_proc_addr<PFN_xrEnumerateColorSpacesFB>("xrEnumerateColorSpacesFB");
        xrSetColorSpaceFB                = get_proc_addr<PFN_xrSetColorSpaceFB       >("xrSetColorSpaceFB");
    }

    return true;
}

//void Xr_instance::set_environment_depth_estimation(XrSession xr_session, bool enabled)
//{
//    if (m_xrSetEnvironmentDepthEstimationVARJO == nullptr) {
//        return;
//    }
//    m_xrSetEnvironmentDepthEstimationVARJO(xr_session, enabled ? XR_TRUE : XR_FALSE);
//}

auto Xr_instance::get_proc_addr(const char* function) const -> PFN_xrVoidFunction
{
    PFN_xrVoidFunction function_pointer;

    XrResult result = xrGetInstanceProcAddr(m_xr_instance, function, &function_pointer);
    if ((result == XR_SUCCESS) && (function_pointer != nullptr)) {
        return function_pointer;
    }
    log_xr->log(spdlog::level::level_enum::err, "OpenXR returned error {}", c_str(result));
    return nullptr;
}

Xr_instance::~Xr_instance() noexcept
{
    ERHE_PROFILE_FUNCTION();

    if (m_xr_instance != nullptr) {
        xrDestroyInstance(m_xr_instance);
    }
}

auto Xr_instance::get_xr_instance() const -> XrInstance
{
    return m_xr_instance;
}

auto Xr_instance::get_xr_system_id() const -> XrSystemId
{
    return m_xr_system_id;
}

auto Xr_instance::get_xr_view_configuration_type() const -> XrViewConfigurationType
{
    return m_xr_view_configuration_type;
}

auto Xr_instance::get_xr_view_configuration_views() const -> const std::vector<XrViewConfigurationView>&
{
    return m_xr_view_configuration_views;
}

auto Xr_instance::get_xr_environment_blend_mode() const -> XrEnvironmentBlendMode
{
    return m_xr_environment_blend_mode;
}

auto Xr_instance::enumerate_layers() -> bool
{
    ERHE_PROFILE_FUNCTION();

    log_xr->trace("{}", __func__);

    uint32_t count{0};
    ERHE_XR_CHECK(xrEnumerateApiLayerProperties(0, &count, nullptr));

    if (count == 0) {
        return true; // Consider no layers to be okay.
    }

    m_xr_api_layer_properties.resize(count);
    for (auto& api_layer : m_xr_api_layer_properties) {
        api_layer.type = XR_TYPE_API_LAYER_PROPERTIES;
        api_layer.next = nullptr;
    }
    ERHE_XR_CHECK(xrEnumerateApiLayerProperties(count, &count, m_xr_api_layer_properties.data()));

    log_xr->info("OpenXR API Layer Properties:");
    for (const auto& api_layer : m_xr_api_layer_properties) {
        log_xr->info(
            "    {} layer version {} spec version",
            api_layer.layerName,
            api_layer.layerVersion,
            api_layer.specVersion
        );
    }
    return true;
}

auto Xr_instance::enumerate_extensions() -> bool
{
    ERHE_PROFILE_FUNCTION();

    log_xr->trace("{}", __func__);

    uint32_t instance_extension_count{0};
    ERHE_XR_CHECK(xrEnumerateInstanceExtensionProperties(nullptr, 0, &instance_extension_count, nullptr));
    if (instance_extension_count == 0) {
        return true; // Consider no extensions to be okay. TODO consider this be an error.
    }

    m_xr_extensions.resize(instance_extension_count);
    for (auto& extension : m_xr_extensions) {
        extension.type = XR_TYPE_EXTENSION_PROPERTIES;
    }

    ERHE_XR_CHECK(
        xrEnumerateInstanceExtensionProperties(nullptr, instance_extension_count, &instance_extension_count, m_xr_extensions.data())
    );

    log_xr->info("Supported extensions:");
    for (const XrExtensionProperties& extension : m_xr_extensions) {
        log_xr->info("    {}, {}", extension.extensionName, extension.extensionVersion);
    }

    return true;
}

auto Xr_instance::has_extension(const char* extension_name) const -> bool
{
    for (const XrExtensionProperties& extension : m_xr_extensions) {
        if (strcmp(extension.extensionName, extension_name) == 0) {
            return true;
        }
    }
    return false;
}

auto Xr_instance::get_system_info() -> bool
{
    ERHE_PROFILE_FUNCTION();

    log_xr->trace("{}", __func__);

    m_xr_system_info.type       = XR_TYPE_SYSTEM_GET_INFO;
    m_xr_system_info.next       = nullptr;
    m_xr_system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    bool waiting_for_headset_notified = false;
    for (;;) {
        const XrResult get_system_result = xrGetSystem(m_xr_instance, &m_xr_system_info, &m_xr_system_id);
        if (get_system_result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
            // Sleep and retry
            if (!waiting_for_headset_notified) {
                log_xr->info("OpenXR: Waiting for HMD to become available");
                waiting_for_headset_notified = true;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        if (get_system_result == XR_SUCCESS) {
            log_xr->info("OpenXR: HMD is now ready");
            break;
        }
    }

    XrSystemProperties system_properties{
        .type = XR_TYPE_SYSTEM_PROPERTIES,
        .next = nullptr
    };

    XrSystemHandTrackingPropertiesEXT system_hand_tracking_properties {
        .type                 = XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT,
        .next                 = nullptr,
        .supportsHandTracking = false
    };
    if (extensions.EXT_hand_tracking) {
        system_properties.next = &system_hand_tracking_properties;
    }

    ERHE_XR_CHECK(xrGetSystemProperties(m_xr_instance, m_xr_system_id, &system_properties));
    log_xr->info("OpenXR System: {}",                  system_properties.systemName);
    log_xr->info("OpenXR SystemId: {:x}",              system_properties.systemId);
    log_xr->info("OpenXR VendorId: {:x}",              system_properties.vendorId);
    log_xr->info("OpenXR Max Swapchain Image {} x {}", system_properties.graphicsProperties.maxSwapchainImageWidth, system_properties.graphicsProperties.maxSwapchainImageHeight);
    log_xr->info("OpenXR Max Layer Count {}",          system_properties.graphicsProperties.maxLayerCount);
    log_xr->info("OpenXR Orientation Tracking: {}",    (system_properties.trackingProperties.orientationTracking == XR_TRUE) ? "yes" : "no");
    log_xr->info("OpenXR Position Tracking: {}",       (system_properties.trackingProperties.positionTracking == XR_TRUE) ? "yes" : "no");
    log_xr->info("OpenXR Hand Tracking {}",            m_configuration.hand_tracking && (system_hand_tracking_properties.supportsHandTracking == XR_TRUE) ? "yes" : "no");

    if (extensions.EXT_hand_tracking) {
        if (system_hand_tracking_properties.supportsHandTracking) {
            xrCreateHandTrackerEXT  = get_proc_addr<PFN_xrCreateHandTrackerEXT >("xrCreateHandTrackerEXT" );
            xrDestroyHandTrackerEXT = get_proc_addr<PFN_xrDestroyHandTrackerEXT>("xrDestroyHandTrackerEXT");
            xrLocateHandJointsEXT   = get_proc_addr<PFN_xrLocateHandJointsEXT  >("xrLocateHandJointsEXT"  );
        }
    }

    if (extensions.FB_color_space) {
        XrSystemColorSpacePropertiesFB color_space_properties{
            .type = XR_TYPE_SYSTEM_COLOR_SPACE_PROPERTIES_FB,
            .next = nullptr
        };
        XrSystemProperties system_properties_{
            .type = XR_TYPE_SYSTEM_PROPERTIES,
            .next = &color_space_properties
        };
        ERHE_XR_CHECK(xrGetSystemProperties(m_xr_instance, m_xr_system_id, &system_properties_));
        log_xr->info("OpenXR Native Color Space: {}", c_str(color_space_properties.colorSpace));
    }

    if (extensions.FB_passthrough) {
        XrSystemPassthroughProperties2FB passthrough_properties2{
            .type = XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES2_FB,
            .next = nullptr,
        };
        XrSystemPassthroughPropertiesFB passthrough_properties{
            .type = XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB,
            .next = &passthrough_properties2,
        };
        XrSystemProperties system_properties_{
            .type = XR_TYPE_SYSTEM_PROPERTIES,
            .next = &passthrough_properties
        };
        ERHE_XR_CHECK(xrGetSystemProperties(m_xr_instance, m_xr_system_id, &system_properties_));

        log_xr->info("OpenXR FB_passthrough supported: {}", passthrough_properties.supportsPassthrough == XR_TRUE);
        if (passthrough_properties.supportsPassthrough != XR_TRUE) {
            extensions.FB_passthrough = false;
        }
        if (extensions.FB_passthrough) {
            if (passthrough_properties2.capabilities & XR_PASSTHROUGH_CAPABILITY_BIT_FB) {
                log_xr->info("OpenXR FB_passthrough supports capability");
            }
            if (passthrough_properties2.capabilities & XR_PASSTHROUGH_CAPABILITY_COLOR_BIT_FB) {
                log_xr->info("OpenXR FB_passthrough supports capability color");
            }
            if (passthrough_properties2.capabilities & XR_PASSTHROUGH_CAPABILITY_LAYER_DEPTH_BIT_FB) {
                log_xr->info("OpenXR FB_passthrough supports capability layer depth");
            }
        }
    }

    return true;
}

auto view_configuration_score(const XrViewConfigurationType view_configuration_type) -> int
{
    switch (view_configuration_type) {
        case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO:                              return 2;
        case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO:                            return 3;
        case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO:                        return 4;
        case XR_VIEW_CONFIGURATION_TYPE_SECONDARY_MONO_FIRST_PERSON_OBSERVER_MSFT: return 1;
        default:                                                                   return 0;
    }
}

auto blend_mode_score(const XrEnvironmentBlendMode environment_blend_mode) -> int
{
    switch (environment_blend_mode) {
        case XR_ENVIRONMENT_BLEND_MODE_OPAQUE:      return 1;
        case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE:    return 2;
        case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND: return 3;
        default: return 0;
    }
}

auto Xr_instance::enumerate_blend_modes() -> bool
{
    ERHE_PROFILE_FUNCTION();

    uint32_t environment_blend_mode_count{0};

    ERHE_XR_CHECK(xrEnumerateEnvironmentBlendModes(m_xr_instance, m_xr_system_id, m_xr_view_configuration_type, 0, &environment_blend_mode_count, nullptr));
    if (environment_blend_mode_count == 0) {
        log_xr->error("xrEnumerateEnvironmentBlendModes() returned 0 environment blend modes");
        return false;
    }

    m_xr_environment_blend_modes.resize(environment_blend_mode_count);
    ERHE_XR_CHECK(xrEnumerateEnvironmentBlendModes(m_xr_instance, m_xr_system_id, m_xr_view_configuration_type, environment_blend_mode_count, &environment_blend_mode_count, m_xr_environment_blend_modes.data()));

    log_xr->info("Environment blend modes:");

    int best_score = 0;
    m_xr_environment_blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    for (XrEnvironmentBlendMode blend_mode : m_xr_environment_blend_modes) {
        log_xr->info("    {}", c_str(blend_mode));
        auto mode_score = blend_mode_score(blend_mode);
        if (mode_score > best_score) {
            m_xr_environment_blend_mode = blend_mode;
            best_score = mode_score;
        }
    }

    log_xr->info("Selected environment blend mode: {}", c_str(m_xr_environment_blend_mode));
    return true;
}

auto Xr_instance::enumerate_view_configurations() -> bool
{
    ERHE_PROFILE_FUNCTION();

    log_xr->trace("{}", __func__);

    uint32_t view_configuration_type_count = 0;
    ERHE_XR_CHECK(xrEnumerateViewConfigurations(m_xr_instance, m_xr_system_id, 0, &view_configuration_type_count, nullptr));
    if (view_configuration_type_count == 0) {
        return false; // Consider no views to be an error.
    }

    std::vector<XrViewConfigurationType> view_configuration_types{view_configuration_type_count};

    ERHE_XR_CHECK(xrEnumerateViewConfigurations(m_xr_instance, m_xr_system_id, view_configuration_type_count, &view_configuration_type_count, view_configuration_types.data()));

    log_xr->info("View configuration types:");
    int best_score{0};
    m_xr_view_configuration_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO;
    bool primary_stereo_supported{false};
    bool primary_quad_supported{false};

    for (const auto view_configuration_type : view_configuration_types) {
        uint32_t dummy{0};
        XrViewConfigurationView views[4] = {};
        for (XrViewConfigurationView& view : views) {
            view.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
            view.next = 0;
        }
        const XrResult result = xrEnumerateViewConfigurationViews(m_xr_instance, m_xr_system_id, view_configuration_type, 4, &dummy, &views[0]);
        if (result != XR_SUCCESS) {
            log_xr->info("    {} is not ok", c_str(view_configuration_type));
            continue;
        }

        log_xr->info("    {}", c_str(view_configuration_type));
        const int type_score = view_configuration_score(view_configuration_type);
        if (type_score > best_score) {
            best_score = type_score;
            m_xr_view_configuration_type = view_configuration_type;
        }
        if (view_configuration_type == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
            primary_stereo_supported = true;
        }
        if (view_configuration_type == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO) {
            primary_quad_supported = true;
        }
    }
    if (best_score == 0) {
        log_xr->error("No working view configuration types found");
        return false;
    }
    if (primary_quad_supported && extensions.VARJO_quad_views) {
        m_xr_view_configuration_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO_WITH_FOVEATED_INSET;
    }
    if (primary_stereo_supported && !(m_configuration.quad_view && extensions.VARJO_quad_views)) {
        m_xr_view_configuration_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    }
    log_xr->info("Selected view configuration type: {}", c_str(m_xr_view_configuration_type));

    uint32_t view_count{0};
    ERHE_XR_CHECK(xrEnumerateViewConfigurationViews(m_xr_instance, m_xr_system_id, m_xr_view_configuration_type, 0, &view_count, nullptr));
    if (view_count == 0) {
        log_xr->error("xrEnumerateViewConfigurationViews() returned 0 views");
        return false;
    }

    m_xr_view_configuration_views = std::vector<XrViewConfigurationView>(
        view_count,
        {
            .type                            = XR_TYPE_VIEW_CONFIGURATION_VIEW,
            .next                            = nullptr,
            .recommendedImageRectWidth       = 0,
            .maxImageRectWidth               = 0,
            .recommendedImageRectHeight      = 0,
            .maxImageRectHeight              = 0,
            .recommendedSwapchainSampleCount = 0,
            .maxSwapchainSampleCount         = 0
        }
    );

    ERHE_XR_CHECK(
        xrEnumerateViewConfigurationViews(m_xr_instance, m_xr_system_id, m_xr_view_configuration_type, view_count, &view_count, m_xr_view_configuration_views.data())
    );

    log_xr->info("View configuration views:");
    std::size_t index = 0;
    for (const auto& view_configuration_view : m_xr_view_configuration_views) {
        log_xr->info(
            "    View {}: Size recommended = {} x {}, Max = {} x {}, recommended sample count = {}, max sample count = {}",
            index++,
            view_configuration_view.recommendedImageRectWidth,
            view_configuration_view.recommendedImageRectHeight,
            view_configuration_view.maxImageRectWidth,
            view_configuration_view.maxImageRectHeight,
            view_configuration_view.recommendedSwapchainSampleCount,
            view_configuration_view.maxSwapchainSampleCount
        );
    }

    return true;
}

void Xr_instance::get_paths()
{
    get_path("/user/head"                                   , paths.user_head                                  );
    get_path("/user/hand/left"                              , paths.user_hand_left                             );
    get_path("/user/hand/right"                             , paths.user_hand_right                            );
    get_path("/interaction_profiles/khr/simple_controller"  , paths.interaction_profile_khr_simple_controller  );
    get_path("/interaction_profiles/htc/vive_controller"    , paths.interaction_profile_htc_vive_controller    );
    get_path("/interaction_profiles/oculus/touch_controller", paths.interaction_profile_oculus_touch_controller);
    get_path("/interaction_profiles/valve/index_controller" , paths.interaction_profile_valve_index_controller );
}

void Xr_instance::get_path(const char* path, XrPath& xr_path)
{
    check(
        xrStringToPath(m_xr_instance, path, &xr_path)
    );
}

void Xr_instance::collect_bindings(const Xr_action& action)
{
    if ((action.profile_mask & Profile_mask::khr_simple) != 0) {
        ERHE_VERIFY(action.action != XR_NULL_HANDLE);
        m_khr_simple_bindings.push_back(
            XrActionSuggestedBinding{
                .action  = action.action,
                .binding = action.path
            }
        );
    }
    if ((action.profile_mask & Profile_mask::oculus_touch) != 0) {
        ERHE_VERIFY(action.action != XR_NULL_HANDLE);
        m_oculus_touch_bindings.push_back(
            XrActionSuggestedBinding{
                .action  = action.action,
                .binding = action.path
            }
        );
    }
    if ((action.profile_mask & Profile_mask::valve_index) != 0) {
        ERHE_VERIFY(action.action != XR_NULL_HANDLE);
        m_valve_index_bindings.push_back(
            XrActionSuggestedBinding{
                .action  = action.action,
                .binding = action.path
            }
        );
    }
    if ((action.profile_mask & Profile_mask::htc_vive) != 0) {
        ERHE_VERIFY(action.action != XR_NULL_HANDLE);
        m_htc_vive_bindings.push_back(
            XrActionSuggestedBinding{
                .action  = action.action,
                .binding = action.path
            }
        );
    }
}

auto Xr_instance::create_boolean_action(const unsigned int profile_mask, const std::string_view path_name) -> Xr_action_boolean*
{
    m_boolean_actions.emplace_back(m_xr_instance, m_action_set, path_name, profile_mask);
    auto& action = m_boolean_actions.back();
    if (action.action == XR_NULL_HANDLE) {
        m_boolean_actions.pop_back();
        return nullptr;
    }
    collect_bindings(action);
    return &action;
}

auto Xr_instance::create_float_action(const unsigned int profile_mask, const std::string_view path_name) -> Xr_action_float*
{
    m_float_actions.emplace_back(m_xr_instance, m_action_set, path_name, profile_mask);
    auto& action = m_float_actions.back();
    if (action.action == XR_NULL_HANDLE) {
        m_float_actions.pop_back();
        return nullptr;
    }
    collect_bindings(action);
    return &action;
}

auto Xr_instance::create_vector2f_action(const unsigned int profile_mask, const std::string_view path_name) -> Xr_action_vector2f*
{
    m_vector2f_actions.emplace_back(m_xr_instance, m_action_set, path_name, profile_mask);
    auto& action = m_vector2f_actions.back();
    if (action.action == XR_NULL_HANDLE) {
        m_vector2f_actions.pop_back();
        return nullptr;
    }
    collect_bindings(action);
    return &action;
}

auto Xr_instance::create_pose_action(const unsigned int profile_mask, const std::string_view path_name) -> Xr_action_pose*
{
    m_pose_actions.emplace_back(m_xr_instance, m_action_set, path_name, profile_mask);
    auto& action = m_pose_actions.back();
    if (action.action == XR_NULL_HANDLE) {
        m_pose_actions.pop_back();
        return nullptr;
    }
    collect_bindings(action);
    return &action;
}

auto Xr_instance::initialize_actions() -> bool
{
    get_paths();

    {
        const XrActionSetCreateInfo create_info
        {
            .type                   = XR_TYPE_ACTION_SET_CREATE_INFO,
            .next                   = nullptr,
            .actionSetName          = { 'e', 'r', 'h', 'e', '\0' },
            .localizedActionSetName = { 'e', 'r', 'h', 'e', '\0' },
            .priority               = 0,
        };
        const XrResult result = xrCreateActionSet(m_xr_instance, &create_info, &m_action_set);
        log_xr->info(
            "xrCreateActionSet(.actionSetName = '{}', .localizedActionSetName = '{}', .priority = {}) result = {}, actionSet = {}",
            create_info.actionSetName,
            create_info.localizedActionSetName,
            create_info.priority,
            c_str(result),
            fmt::ptr(m_action_set)
        );
        if (result != XR_SUCCESS) {
            return false;
        }
    }

    //const unsigned int ____ = 0;
    const unsigned int K___ = Profile_mask::khr_simple;
    const unsigned int _V__ = Profile_mask::htc_vive;
    const unsigned int __O_ = Profile_mask::oculus_touch;
    const unsigned int ___I = Profile_mask::valve_index;
    const unsigned int _V_I = Profile_mask::htc_vive    | Profile_mask::valve_index;
    const unsigned int __OI = Profile_mask::valve_index | Profile_mask::oculus_touch;
    const unsigned int _VOI = Profile_mask::htc_vive    | Profile_mask::valve_index | Profile_mask::oculus_touch;
    const unsigned int KV__ = Profile_mask::khr_simple  | Profile_mask::htc_vive;
    const unsigned int KVO_ = Profile_mask::khr_simple  | Profile_mask::htc_vive | Profile_mask::oculus_touch;
    const unsigned int KVOI = Profile_mask::khr_simple  | Profile_mask::htc_vive | Profile_mask::oculus_touch | Profile_mask::valve_index;

    static_cast<void>(KVO_);

    Xr_actions& l = actions_left;
    Xr_actions& r = actions_right;
    //l.select_click     = create_boolean_action (K___, "/user/hand/left/input/select/click"    );
    //l.system_click     = create_boolean_action (_V_I, "/user/hand/left/input/system/click"    );
    l.menu_click       = create_boolean_action (KVO_, "/user/hand/left/input/menu/click"      );
    l.squeeze_click    = create_boolean_action (_V__, "/user/hand/left/input/squeeze/click"   );
    l.x_click          = create_boolean_action (__O_, "/user/hand/left/input/x/click"         );
    l.x_touch          = create_boolean_action (__O_, "/user/hand/left/input/x/touch"         );
    l.y_click          = create_boolean_action (__O_, "/user/hand/left/input/y/click"         );
    l.y_touch          = create_boolean_action (__O_, "/user/hand/left/input/y/touch"         );
    //l.a_click          = create_boolean_action (___I, "/user/hand/left/input/a/click"         );
    //l.a_touch          = create_boolean_action (___I, "/user/hand/left/input/a/touch"         );
    //l.b_click          = create_boolean_action (__OI, "/user/hand/left/input/b/click"         );
    //l.b_touch          = create_boolean_action (__OI, "/user/hand/left/input/b/touch"         );
    l.trigger_click    = create_boolean_action (_V_I, "/user/hand/left/input/trigger/click"   );
    l.trigger_touch    = create_boolean_action (__OI, "/user/hand/left/input/trigger/touch"   );
   //l.trackpad_click   = create_boolean_action (_V__, "/user/hand/left/input/trackpad/click"  );
   //l.trackpad_touch   = create_boolean_action (_V_I, "/user/hand/left/input/trackpad/touch"  );
    l.thumbstick_click = create_boolean_action (__OI, "/user/hand/left/input/thumbstick/click");
    l.thumbstick_touch = create_boolean_action (__OI, "/user/hand/left/input/thumbstick/touch");
    l.thumbrest_touch  = create_boolean_action (__O_, "/user/hand/left/input/thumbrest/touch" );
    l.squeeze_value    = create_float_action   (__OI, "/user/hand/left/input/squeeze/value"   );
    l.squeeze_force    = create_float_action   (___I, "/user/hand/left/input/squeeze/force"   );
    l.trigger_value    = create_float_action   (_VOI, "/user/hand/left/input/trigger/value"   );
    l.trackpad_force   = create_float_action   (___I, "/user/hand/left/input/trackpad/force"  );
    l.trackpad         = create_vector2f_action(_V_I, "/user/hand/left/input/trackpad"        );
    l.thumbstick       = create_vector2f_action(__OI, "/user/hand/left/input/thumbstick"      );
    l.grip_pose        = create_pose_action    (KVOI, "/user/hand/left/input/grip/pose"       );
    l.aim_pose         = create_pose_action    (KVOI, "/user/hand/left/input/aim/pose"        );

    r.select_click     = create_boolean_action (K___, "/user/hand/right/input/select/click"    );
    r.system_click     = create_boolean_action (_VOI, "/user/hand/right/input/system/click"    );
    r.menu_click       = create_boolean_action (KV__, "/user/hand/right/input/menu/click"      );
    r.squeeze_click    = create_boolean_action (_V__, "/user/hand/right/input/squeeze/click"   );
    //r.x_click          = create_boolean_action (____, "/user/hand/right/input/x/click"         );
    //r.x_touch          = create_boolean_action (____, "/user/hand/right/input/x/touch"         );
    //r.y_click          = create_boolean_action (____, "/user/hand/right/input/y/click"         );
    //r.y_touch          = create_boolean_action (____, "/user/hand/right/input/y/touch"         );
    r.a_click          = create_boolean_action (__OI, "/user/hand/right/input/a/click"         );
    //r.a_touch          = create_boolean_action (__OI, "/user/hand/right/input/a/touch"         );
    r.b_click          = create_boolean_action (__OI, "/user/hand/right/input/b/click"         );
    //r.b_touch          = create_boolean_action (__OI, "/user/hand/right/input/b/touch"         );
    r.trigger_click    = create_boolean_action (_V_I, "/user/hand/right/input/trigger/click"   );
    //r.trigger_touch    = create_boolean_action (__OI, "/user/hand/right/input/trigger/touch"   );
    r.trackpad_click   = create_boolean_action (_V__, "/user/hand/right/input/trackpad/click"  );
    //r.trackpad_touch   = create_boolean_action (_V_I, "/user/hand/right/input/trackpad/touch"  );
    r.thumbstick_click = create_boolean_action (__OI, "/user/hand/right/input/thumbstick/click");
    //r.thumbstick_touch = create_boolean_action (__OI, "/user/hand/right/input/thumbstick/touch");
    r.thumbrest_touch  = create_boolean_action (__O_, "/user/hand/right/input/thumbrest/touch" );
    r.squeeze_value    = create_float_action   (__OI, "/user/hand/right/input/squeeze/value"   );
    r.squeeze_force    = create_float_action   (___I, "/user/hand/right/input/squeeze/force"   );
    r.trigger_value    = create_float_action   (_VOI, "/user/hand/right/input/trigger/value"   );
    r.trackpad_force   = create_float_action   (___I, "/user/hand/right/input/trackpad/force"  );
    r.trackpad         = create_vector2f_action(_V_I, "/user/hand/right/input/trackpad"        );
    r.thumbstick       = create_vector2f_action(__OI, "/user/hand/right/input/thumbstick"      );
    r.grip_pose        = create_pose_action    (KVOI, "/user/hand/right/input/grip/pose"       );
    r.aim_pose         = create_pose_action    (KVOI, "/user/hand/right/input/aim/pose"        );

    update_action_bindings();
    return true;
}

auto Xr_instance::get_path_string(XrPath path) -> std::string
{
    if (path == XR_NULL_PATH) {
        return std::string{"XR_NULL_PATH"};
    }
    uint32_t length = 0;
    const XrResult length_query_result = xrPathToString(m_xr_instance, path, 0, &length, nullptr);
    if (length_query_result != XR_SUCCESS) {
        return std::string{"INVALID-PATH"};
    }
    if (length == 0) {
        return {};
    }
    std::vector<char> buffer;
    buffer.resize(length);
    uint32_t required_capacity = 0;
    const XrResult result = xrPathToString(m_xr_instance, path, length, &required_capacity, buffer.data());
    if (result == XR_SUCCESS) {
        return std::string{buffer.data(), length - 1};
    } else {
        return std::string{"INVALID-PATH"};
    }
}

void Xr_instance::update_action_bindings()
{
    if (!m_khr_simple_bindings.empty()) {
        const XrInteractionProfileSuggestedBinding bindings{
            .type                   = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
            .next                   = nullptr,
            .interactionProfile     = paths.interaction_profile_khr_simple_controller,
            .countSuggestedBindings = static_cast<uint32_t>(m_khr_simple_bindings.size()),
            .suggestedBindings      = m_khr_simple_bindings.data()
        };
        const auto result = xrSuggestInteractionProfileBindings(m_xr_instance, &bindings);
        if (result == XR_SUCCESS) {
            log_xr->warn("Installed suggested bindings ({}) for KHR simple controller", m_khr_simple_bindings.size());
        } else if (result != XR_ERROR_PATH_UNSUPPORTED) {
            log_xr->warn("xrSuggestInteractionProfileBindings() for KHR simple interaction profile returned error {}", c_str(result));
        }
    }

    if (!m_oculus_touch_bindings.empty()) {
        const XrInteractionProfileSuggestedBinding bindings{
            .type                   = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
            .next                   = nullptr,
            .interactionProfile     = paths.interaction_profile_oculus_touch_controller,
            .countSuggestedBindings = static_cast<uint32_t>(m_oculus_touch_bindings.size()),
            .suggestedBindings      = m_oculus_touch_bindings.data()
        };

        for (const auto& binding : m_oculus_touch_bindings) {
            log_xr->info("Binding action = {}, path = '{}'", fmt::ptr(binding.action), get_path_string(binding.binding));
        }
        const auto result = xrSuggestInteractionProfileBindings(m_xr_instance, &bindings);
        log_xr->info(
            "xrSuggestInteractionProfileBindings(.interactionProfile = '{}', .countSuggestedBindings = {}) result = {}",
            get_path_string(bindings.interactionProfile),
            bindings.countSuggestedBindings,
            c_str(result)
        );
        if (result == XR_SUCCESS) {
            log_xr->warn("Installed suggested bindings ({}) for Oculus Touch controller", m_oculus_touch_bindings.size());
        } else if (result != XR_ERROR_PATH_UNSUPPORTED) {
            log_xr->warn("xrSuggestInteractionProfileBindings() for Oculus touch interaction profile returned error {}", c_str(result));
        }
    }

    if (!m_htc_vive_bindings.empty()) {
        const XrInteractionProfileSuggestedBinding bindings{
            .type                   = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
            .next                   = nullptr,
            .interactionProfile     = paths.interaction_profile_htc_vive_controller,
            .countSuggestedBindings = static_cast<uint32_t>(m_htc_vive_bindings.size()),
            .suggestedBindings      = m_htc_vive_bindings.data()
        };
        const auto result = xrSuggestInteractionProfileBindings(m_xr_instance, &bindings);
        if (result == XR_SUCCESS) {
            log_xr->warn("Installed suggested bindings ({}) for HTV Vive controller", m_htc_vive_bindings.size());
        } else if (result != XR_ERROR_PATH_UNSUPPORTED) {
            log_xr->warn("xrSuggestInteractionProfileBindings() for HTC Vive interaction profile returned error {}", c_str(result));
        }
    }

    // TODO Valve index
}

auto Xr_instance::update_actions(Xr_session& session) -> bool
{
    ERHE_PROFILE_FUNCTION();

    const XrActiveActionSet active_action_set{
        .actionSet     = m_action_set,
        .subactionPath = XR_NULL_PATH
    };

    const XrActionsSyncInfo actions_sync_info{
        .type                  = XR_TYPE_ACTIONS_SYNC_INFO,
        .next                  = nullptr,
        .countActiveActionSets = 1,
        .activeActionSets      = &active_action_set
    };

    {
        log_xr->trace("xrSyncActions()");
        const auto result = xrSyncActions(session.get_xr_session(), &actions_sync_info);
        switch (result) {
            case XR_SUCCESS: {
                break;
            }

            case XR_SESSION_LOSS_PENDING:
            case XR_SESSION_NOT_FOCUSED: {
                // TODO
                //actions.trigger_value.state .isActive = XR_FALSE;
                //actions.trigger_click.state .isActive = XR_FALSE;
                //actions.menu_click.state    .isActive = XR_FALSE;
                //actions.squeeze_click.state .isActive = XR_FALSE;
                //actions.trackpad_touch.state.isActive = XR_FALSE;
                //actions.trackpad_click.state.isActive = XR_FALSE;
                //actions.trackpad.state      .isActive = XR_FALSE;
                return true;
            }

            case XR_ERROR_RUNTIME_FAILURE: {
                log_xr->error("xrSyncActions() triggered OpenXR runtime failure");
                return false;
            }

            default: {
                log_xr->error("xrSyncActions() returned error {}", c_str(result));
                return false;
            }
        }
    }

    const auto   xr_session = session.get_xr_session();
    const XrTime time       = session.get_xr_frame_state().predictedDisplayTime;
    const auto   base_space = session.get_xr_reference_space_stage();

    for (auto& action : m_boolean_actions) {
        action.get(xr_session);
    }
    for (auto& action : m_float_actions) {
        action.get(xr_session);
    }
    for (auto& action : m_vector2f_actions) {
        action.get(xr_session);
    }
    for (auto& action : m_pose_actions) {
        action.get(xr_session, time, base_space);
    }
    return true;
}

auto Xr_instance::get_boolean_actions() -> etl::vector<Xr_action_boolean, max_action_count>&
{
    return m_boolean_actions;
}

auto Xr_instance::get_float_actions() -> etl::vector<Xr_action_float, max_action_count>&
{
    return m_float_actions;
}

auto Xr_instance::get_vector2f_actions() -> etl::vector<Xr_action_vector2f, max_action_count>&
{
    return m_vector2f_actions;
}

auto Xr_instance::get_pose_actions() -> etl::vector<Xr_action_pose, max_action_count>&
{
    return m_pose_actions;
}

auto Xr_instance::attach_actions(const XrSession session) -> bool
{
    std::vector<XrActionSet> sets;
    sets.push_back(m_action_set);
    const XrSessionActionSetsAttachInfo attach_info{
        .type            = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
        .next            = nullptr,
        .countActionSets = static_cast<uint32_t>(sets.size()),
        .actionSets      = sets.data()
    };
    const XrResult result = xrAttachSessionActionSets(session, &attach_info);
    log_xr->info(
        "xrAttachSessionActionSets(.countActionSets = {}, .actionSets = {}) result = {}",
        attach_info.countActionSets,
        fmt::ptr(*attach_info.actionSets),
        c_str(result)
    );
    if (result != XR_SUCCESS) {
        return false;
    }

    for (auto& action : m_pose_actions) {
        action.attach(session);
    }
    return true;
}

auto Xr_instance::get_configuration() const -> const Xr_configuration&
{
    return m_configuration;
}

auto Xr_instance::get_current_interaction_profile(Xr_session& session) -> bool
{
    ERHE_PROFILE_FUNCTION();

    XrInteractionProfileState left{
        .type               = XR_TYPE_INTERACTION_PROFILE_STATE,
        .next               = nullptr,
        .interactionProfile = XR_NULL_PATH
    };
    XrInteractionProfileState right{
        .type               = XR_TYPE_INTERACTION_PROFILE_STATE,
        .next               = nullptr,
        .interactionProfile = XR_NULL_PATH
    };

    const XrResult left_result = xrGetCurrentInteractionProfile(session.get_xr_session(), paths.user_hand_left, &left);
    const XrResult right_result = xrGetCurrentInteractionProfile(session.get_xr_session(), paths.user_hand_right, &right);
    const std::string left_profile_name  = (left_result  == XR_SUCCESS) ? get_path_string(left .interactionProfile) : "failure";
    const std::string right_profile_name = (right_result == XR_SUCCESS) ? get_path_string(right.interactionProfile) : "failure";
    log_xr->info("Current interaction profile for left hand:  {}", left_profile_name);
    log_xr->info("Current interaction profile for right hand: {}", right_profile_name);
    if ((left_result != XR_SUCCESS) && (right_result != XR_SUCCESS)) {
        log_xr->warn("Expected interaction profile query to work");
    }

    return true;
}

auto Xr_instance::poll_xr_events(Xr_session& session) -> bool
{
    ERHE_PROFILE_FUNCTION();

    for (;;) {
        XrEventDataBuffer buffer{};
        buffer.type = XR_TYPE_EVENT_DATA_BUFFER;
        buffer.next = nullptr;
        log_xr->trace("xrPollEvent()");
        const auto result = xrPollEvent(m_xr_instance, &buffer);
        if (result == XR_SUCCESS) {
            XrEventDataBaseHeader* base_header = reinterpret_cast<XrEventDataBaseHeader*>(&buffer);
            log_xr->info("XR event {}", c_str(base_header->type));

            if (base_header->type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED) {
                get_current_interaction_profile(session);
            }

            if (base_header->type == XR_TYPE_EVENT_DATA_EVENTS_LOST) {
                auto data_events_lost = *reinterpret_cast<const XrEventDataEventsLost*>(base_header);
                log_xr->warn("XrEventDataEventsLost::lostEventCount = {}", data_events_lost.lostEventCount);
                continue;
            }

            if (buffer.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                auto session_state_changed_event = *reinterpret_cast<const XrEventDataSessionStateChanged*>(base_header);
                log_xr->info("XrEventDataSessionStateChanged::state = {}", c_str(session_state_changed_event.state));
                session.set_state(session_state_changed_event.state);
                switch (session_state_changed_event.state) {
                    case XR_SESSION_STATE_UNKNOWN:
                    case XR_SESSION_STATE_IDLE: {
                        break;
                    }

                    case XR_SESSION_STATE_READY: {
                        if (!session.begin_session()) {
                            log_xr->warn("XR_SESSION_STATE_READY: begin_session() failed");
                        }
                        break;
                    }

                    case XR_SESSION_STATE_SYNCHRONIZED:
                    case XR_SESSION_STATE_VISIBLE: {
                        break;
                    }

                    case XR_SESSION_STATE_FOCUSED: {
                        get_current_interaction_profile(session);
                        break;
                    }

                    case XR_SESSION_STATE_STOPPING: {
                        if (!session.end_session()) {
                            log_xr->warn("XR_SESSION_STATE_STOPPING: end_session() failed");
                        }
                        break;
                    }

                    case XR_SESSION_STATE_LOSS_PENDING:
                    case XR_SESSION_STATE_EXITING:
                    default: {
                        break;
                    }
                }
            }

            // if (buffer.type == XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR)
            // {
            // }

            continue;
        } else if (result == XR_EVENT_UNAVAILABLE) {
            break;
        }

        log_xr->error("xrPollEvent() returned error {}", c_str(result));
        return false;
    }

    return true;
}

} // namespace erhe::xr
