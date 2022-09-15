#include "erhe/xr/xr_instance.hpp"
#include "erhe/xr/xr_session.hpp"
#include "erhe/xr/xr.hpp"
#include "erhe/xr/xr_log.hpp"
#include "erhe/toolkit/profile.hpp"

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

Xr_instance::Xr_instance()
{
    ERHE_PROFILE_FUNCTION

    if (!enumerate_layers())
    {
        return;
    }

    if (!enumerate_extensions())
    {
        return;
    }

    if (!create_instance())
    {
        return;
    }

    if (!get_system_info())
    {
        return;
    }

    if (!enumerate_view_configurations())
    {
        return;
    }

    if (!enumerate_blend_modes())
    {
        return;
    }

    if (!initialize_actions())
    {
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
    return instance->debug_utils_messenger_callback(
        messageSeverity,
        messageTypes,
        callbackData
    );
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

    if (callbackData->objectCount > 0)
    {
        log_xr->info("Objects:");
        //const erhe::log::Indenter scope_indent;
        for (uint32_t i = 0; i < callbackData->objectCount; ++i)
        {
            log_xr->info(
                "{} {} {}",
                c_str(callbackData->objects[i].objectType),
                callbackData->objects[i].objectHandle,
                callbackData->objects[i].objectName
            );
        }
    }

    if (callbackData->sessionLabelCount > 0)
    {
        log_xr->info("Session labels:");
        for (uint32_t i = 0; i < callbackData->sessionLabelCount; ++i)
        {
            log_xr->info("    {}", callbackData->sessionLabels[i].labelName);
        }
    }

    return XR_FALSE;
}

auto Xr_instance::create_instance() -> bool
{
    ERHE_PROFILE_FUNCTION

    log_xr->trace("{}", __func__);

    const char* const required_extensions[] = {
        XR_EXT_DEBUG_UTILS_EXTENSION_NAME,
        XR_KHR_OPENGL_ENABLE_EXTENSION_NAME,
        XR_VARJO_QUAD_VIEWS_EXTENSION_NAME,
        XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME,
        XR_VARJO_ENVIRONMENT_DEPTH_ESTIMATION_EXTENSION_NAME,
        XR_KHR_VISIBILITY_MASK_EXTENSION_NAME,
        XR_EXT_HAND_TRACKING_EXTENSION_NAME,
        //XR_VARJO_COMPOSITION_LAYER_DEPTH_TEST_EXTENSION_NAME,
    };

    const XrInstanceCreateInfo create_info
    {
        .type                   = XR_TYPE_INSTANCE_CREATE_INFO,
        .next                   = nullptr,
        .createFlags            = 0,
        .applicationInfo = {
            .applicationName    = { 'e', 'r', 'h', 'e', '\0' },
            .applicationVersion = 1,
            .engineName         = { 'e', 'r', 'h', 'e', '\0' },
            .engineVersion      = 1,
            .apiVersion         = XR_CURRENT_API_VERSION
        },
        .enabledApiLayerCount   = 0,
        .enabledApiLayerNames   = nullptr,
        .enabledExtensionCount  = 7,
        .enabledExtensionNames  = required_extensions,
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

    xrCreateDebugUtilsMessengerEXT     = get_proc_addr<PFN_xrCreateDebugUtilsMessengerEXT    >("xrCreateDebugUtilsMessengerEXT");
    xrGetVisibilityMaskKHR             = get_proc_addr<PFN_xrGetVisibilityMaskKHR            >("xrGetVisibilityMaskKHR");
    xrGetOpenGLGraphicsRequirementsKHR = get_proc_addr<PFN_xrGetOpenGLGraphicsRequirementsKHR>("xrGetOpenGLGraphicsRequirementsKHR");

    return true;
}

//void Xr_instance::set_environment_depth_estimation(XrSession xr_session, bool enabled)
//{
//    if (m_xrSetEnvironmentDepthEstimationVARJO == nullptr)
//    {
//        return;
//    }
//    m_xrSetEnvironmentDepthEstimationVARJO(xr_session, enabled ? XR_TRUE : XR_FALSE);
//}

auto Xr_instance::get_proc_addr(const char* function) const -> PFN_xrVoidFunction
{
    PFN_xrVoidFunction function_pointer;
    if (
        !check(
            xrGetInstanceProcAddr(
                m_xr_instance,
                function,
                &function_pointer
            )
        )
    )
    {
        return nullptr;
    }
    return function_pointer;
}

Xr_instance::~Xr_instance()
{
    ERHE_PROFILE_FUNCTION

    if (m_xr_instance != nullptr)
    {
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

auto Xr_instance::get_xr_view_configuration_views() const
-> const std::vector<XrViewConfigurationView>&
{
    return m_xr_view_configuration_views;
}

auto Xr_instance::get_xr_environment_blend_mode() const -> XrEnvironmentBlendMode
{
    return m_xr_environment_blend_mode;
}

auto Xr_instance::enumerate_layers() -> bool
{
    ERHE_PROFILE_FUNCTION

    log_xr->trace("{}", __func__);

    uint32_t count{0};
    ERHE_XR_CHECK(xrEnumerateApiLayerProperties(0, &count, nullptr));

    if (count == 0)
    {
        return true; // Consider no layers to be okay.
    }

    m_xr_api_layer_properties.resize(count);
    for (auto& api_layer : m_xr_api_layer_properties)
    {
        api_layer.type = XR_TYPE_API_LAYER_PROPERTIES;
        api_layer.next = nullptr;
    }
    ERHE_XR_CHECK(xrEnumerateApiLayerProperties(count, &count, m_xr_api_layer_properties.data()));

    log_xr->info("OpenXR API Layer Properties:");
    for (const auto& api_layer : m_xr_api_layer_properties)
    {
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
    ERHE_PROFILE_FUNCTION

    log_xr->trace("{}", __func__);

    uint32_t instance_extension_count{0};
    //const auto res = xrEnumerateInstanceExtensionProperties(nullptr,
    //                                                        0,
    //                                                        &instance_extension_count,
    //                                                        nullptr);
    //if (res == XR_ERROR_RUNTIME_UNAVAILABLE)
    //{
    //    return false;
    //}
    ERHE_XR_CHECK(
        xrEnumerateInstanceExtensionProperties(
            nullptr,
            0,
            &instance_extension_count,
            nullptr
        )
    );
    if (instance_extension_count == 0)
    {
        return true; // Consider no extensions to be okay. TODO consider this be an error.
    }

    m_xr_extensions.resize(instance_extension_count);
    for (auto& extension : m_xr_extensions)
    {
        extension.type = XR_TYPE_EXTENSION_PROPERTIES;
    }

    ERHE_XR_CHECK(
        xrEnumerateInstanceExtensionProperties(
            nullptr,
            instance_extension_count,
            &instance_extension_count,
            m_xr_extensions.data()
        )
    );

    log_xr->info("Supported extensions:");
    for (const auto& extension : m_xr_extensions)
    {
        log_xr->info("    {}, {}", extension.extensionName, extension.extensionVersion);
    }

    return true;
}

auto Xr_instance::get_system_info() -> bool
{
    ERHE_PROFILE_FUNCTION

    log_xr->trace("{}", __func__);

    m_xr_system_info.type       = XR_TYPE_SYSTEM_GET_INFO;
    m_xr_system_info.next       = nullptr;
    m_xr_system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    ERHE_XR_CHECK(xrGetSystem(m_xr_instance, &m_xr_system_info, &m_xr_system_id));

    XrSystemHandTrackingPropertiesEXT system_hand_tracking_properties
    {
        .type                 = XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT,
        .next                 = nullptr,
        .supportsHandTracking = false
    };
    XrSystemProperties system_properties
    {
        .type = XR_TYPE_SYSTEM_PROPERTIES,
        .next = &system_hand_tracking_properties
    };
    ERHE_XR_CHECK(xrGetSystemProperties(m_xr_instance, m_xr_system_id, &system_properties));

    if (system_hand_tracking_properties.supportsHandTracking)
    {
        log_xr->info("Hand tracking is supported");
        xrCreateHandTrackerEXT  = reinterpret_cast<PFN_xrCreateHandTrackerEXT >(get_proc_addr("xrCreateHandTrackerEXT" ));
        xrDestroyHandTrackerEXT = reinterpret_cast<PFN_xrDestroyHandTrackerEXT>(get_proc_addr("xrDestroyHandTrackerEXT"));
        xrLocateHandJointsEXT   = reinterpret_cast<PFN_xrLocateHandJointsEXT  >(get_proc_addr("xrLocateHandJointsEXT"  ));
    }
    else
    {
        log_xr->info("Hand tracking is not supported");
    }

    return true;
}

auto score(const XrViewConfigurationType view_configuration_type) -> int
{
    switch (view_configuration_type)
    {
        case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO:                              return 2;
        case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO:                            return 3;
        case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO:                        return 4;
        case XR_VIEW_CONFIGURATION_TYPE_SECONDARY_MONO_FIRST_PERSON_OBSERVER_MSFT: return 1;
        default:                                                                   return 0;
    }
}

auto score(const XrEnvironmentBlendMode environment_blend_mode) -> int
{
    switch (environment_blend_mode)
    {
        case XR_ENVIRONMENT_BLEND_MODE_OPAQUE:      return 1;
        case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE:    return 2;
        case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND: return 3;
        default: return 0;
    }
}

auto Xr_instance::enumerate_blend_modes() -> bool
{
    ERHE_PROFILE_FUNCTION

    uint32_t environment_blend_mode_count{0};

    ERHE_XR_CHECK(
        xrEnumerateEnvironmentBlendModes(
            m_xr_instance,
            m_xr_system_id,
            m_xr_view_configuration_type,
            0,
            &environment_blend_mode_count,
            nullptr
        )
    );
    if (environment_blend_mode_count == 0)
    {
        log_xr->error("xrEnumerateEnvironmentBlendModes() returned 0 environment blend modes");
        return false;
    }

    m_xr_environment_blend_modes.resize(environment_blend_mode_count);
    ERHE_XR_CHECK(
        xrEnumerateEnvironmentBlendModes(
            m_xr_instance,
            m_xr_system_id,
            m_xr_view_configuration_type,
            environment_blend_mode_count,
            &environment_blend_mode_count,
            m_xr_environment_blend_modes.data()
        )
    );

    int best_score = 0;
    m_xr_environment_blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    for (auto mode : m_xr_environment_blend_modes)
    {
        auto mode_score = score(mode);
        if (mode_score > best_score)
        {
            m_xr_environment_blend_mode = mode;
            best_score = mode_score;
        }
    }

    log_xr->info("Selected environment blend mode: {}", c_str(m_xr_environment_blend_mode));
    return true;
}

auto Xr_instance::enumerate_view_configurations() -> bool
{
    ERHE_PROFILE_FUNCTION

    log_xr->trace("{}", __func__);

    uint32_t view_configuration_type_count;
    ERHE_XR_CHECK(
        xrEnumerateViewConfigurations(
            m_xr_instance,
            m_xr_system_id,
            0,
            &view_configuration_type_count,
            nullptr
        )
    );
    if (view_configuration_type_count == 0)
    {
        return false; // Consider no views to be an error.
    }

    std::vector<XrViewConfigurationType> view_configuration_types{view_configuration_type_count};

    ERHE_XR_CHECK(
        xrEnumerateViewConfigurations(
            m_xr_instance,
            m_xr_system_id,
            view_configuration_type_count,
            &view_configuration_type_count,
            view_configuration_types.data()
        )
    );

    log_xr->info("View configuration types:");
    int best_score{0};
    m_xr_view_configuration_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO;
    //bool primary_stereo_supported{false};
    //bool primary_quad_supported{false};

    for (const auto view_configuration_type : view_configuration_types)
    {
        uint32_t dummy{0};
        XrViewConfigurationView views[4];
        auto result = xrEnumerateViewConfigurationViews(
            m_xr_instance,
            m_xr_system_id,
            view_configuration_type,
            4,
            &dummy,
            &views[0]
        );
        if (result != XR_SUCCESS)
        {
            log_xr->info("    {} is not ok", c_str(view_configuration_type));
            continue;
        }

        log_xr->info("    {}", c_str(view_configuration_type));
        const int type_score = score(view_configuration_type);
        if (type_score > best_score)
        {
            best_score = type_score;
            m_xr_view_configuration_type = view_configuration_type;
        }
        //if (view_configuration_type == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
        //{
        //    primary_stereo_supported = true;
        //}
        //if (view_configuration_type == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO)
        //{
        //    primary_quad_supported = true;
        //}
    }
    if (best_score == 0)
    {
        log_xr->error("No working view configuration types found");
        return false;
    }
    //if (primary_stereo_supported)
    //{
    //    m_xr_view_configuration_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    //}
    log_xr->info("Selected view configuration type: {}", c_str(m_xr_view_configuration_type));

    uint32_t view_count{0};

    ERHE_XR_CHECK(
        xrEnumerateViewConfigurationViews(
            m_xr_instance,
            m_xr_system_id,
            m_xr_view_configuration_type,
            0,
            &view_count,
            nullptr
        )
    );
    if (view_count == 0)
    {
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
        xrEnumerateViewConfigurationViews(
            m_xr_instance,
            m_xr_system_id,
            m_xr_view_configuration_type,
            view_count,
            &view_count,
            m_xr_view_configuration_views.data()
        )
    );

    log_xr->info("View configuration views:");
    std::size_t index = 0;
    for (const auto& view_configuration_view : m_xr_view_configuration_views)
    {
        log_xr->info(
            "    View {}: Size recommended = {} x {}, Max = {} x {}, sample count = {}, image count recommended = {}, max = {}",
            index++,
            view_configuration_view.recommendedImageRectWidth,
            view_configuration_view.recommendedImageRectHeight,
            view_configuration_view.maxImageRectWidth,
            view_configuration_view.maxImageRectHeight,
            view_configuration_view.recommendedSwapchainSampleCount,
            view_configuration_view.maxSwapchainSampleCount,
            view_configuration_view.maxSwapchainSampleCount
        );
    }

    return true;
}

// /user/hand/left  represents the user�s left hand. It might be tracked using a controller
//                  or other device in the user�s left hand, or tracked without the user
//                  holding anything, e.g. using computer vision.
// /user/hand/right represents the user�s right hand in analog to the left hand.
// /user/head       represents inputs on the user�s head, often from a device such as a
//                  head-mounted display. To reason about the user�s head, see the
//                  XR_REFERENCE_SPACE_TYPE_VIEW reference space.
// /user/gamepad    is a two-handed gamepad device held by the user.
// /user/treadmill  is a treadmill or other locomotion-targeted input device.


// Standard identifiers

// trackpad         A 2D input source that usually includes click and touch component.
//
// thumbstick       A small 2D joystick that is meant to be used with the user�s thumb.
//                  These sometimes include click and/or touch components.
//
// joystick         A 2D joystick that is meant to be used with the user�s entire hand,
//                  such as a flight stick. These generally do not have click component,
//                  but might have touch components.
//
// trigger          A 1D analog input component that returns to a rest state when the user stops
//                  interacting with it. These sometimes include touch and/or click components.
//
// throttle         A 1D analog input component that remains in position when the user stops
//                  interacting with it.
//
// trackball        A 2D relative input source. These sometimes include click components.
//
// pedal            A 1D analog input component that is similar to a trigger but meant to be
//                  operated by a foot
//
// system           A button with the specialised meaning that it enables the user to access
//                  system-level functions and UI. Input data from system buttons is generally
//                  used internally by runtimes and may not be available to applications.
//
// dpad_up          A set of buttons arranged in a plus shape.
// dpad_down
// dpad_left
// dpad_right
//
// diamond_up       Gamepads often have a set of four buttons arranged in a diamond shape.
// diamond_down     The labels on those buttons vary from gamepad to gamepad, but their
// diamond_left     arrangement is consistent. These names are used for the A/B/X/Y buttons
// diamond_right    on a Xbox controller, and the square/cross/circle/triangle button on
//                  a PlayStation controller.
//
// a                Standalone buttons are named for their physical labels. These are the
// b                standard identifiers for such buttons. Extensions may add new identifiers
// x                as detailed in the next section. Groups of four buttons in a diamond shape
// y                should use the diamond-prefix names above instead of using the labels on
// start            the buttons themselves.
//
// home             Some other standard controls are often identified by icons.
// end              These are their standard names.
// select
// volume_up
// volume_down
// mute_mic
// play_pause
// menu
// view
//
// thumbrest        Some controllers have a place for the user to rest their thumb.
//
// shoulder         A button that is usually pressed with the index finger and is often
//                  positioned above a trigger.
//
// squeeze          An input source that indicates that the user is squeezing their fist
//                  closed. This could be a simple button or act more like a trigger.
//                  Sources with this identifier should either follow button or trigger
//                  conventions for their components.
//
// wheel            A steering wheel.

// Input sources whose orientation and/or position are tracked also expose pose identifiers.

// Standard pose identifiers for tracked hands or motion controllers as
// represented by /user/hand/left and /user/hand/right are:
//
// grip
//      A pose that allows applications to reliably render a virtual object
//      held in the user�s hand, whether it is tracked directly or by a motion
//      controller. The grip pose is defined as follows:
//
//          The grip position:
//
//              For tracked hands:
//                  The user�s palm centroid when closing the fist,
//                  at the surface of the palm.
//
//              For handheld motion controllers:
//                  A fixed position within the controller that generally lines up
//                  with the palm centroid when held by a hand in a neutral position.
//                  This position should be adjusted left or right to center the
//                  position within the controller�s grip.
//
//          The grip orientation�s +X axis:
//                  When you completely open your hand to form a flat 5-finger pose,
//                  the ray that is normal to the user�s palm (away from the palm
//                  in the left hand, into the palm in the right hand).
//
//          The grip orientation�s -Z axis:
//                  When you close your hand partially (as if holding the controller),
//                  the ray that goes through the center of the tube formed by your
//                  non-thumb fingers, in the direction of little finger to thumb.
//
//          The grip orientation�s +Y axis:
//                  orthogonal to +Z and +X using the right-hand rule.
//
// aim
//      A pose that allows applications to point in the world using the input source,
//      according to the platform�s conventions for aiming with that kind of source.
//      The aim pose is defined as follows:
//
//          For tracked hands:
//              The ray that follows platform conventions for how the user aims at
//              objects in the world with their entire hand, with +Y up, +X to the
//              right, and -Z forward. The ray chosen will be runtime-dependent,
//              for example, a ray emerging from the palm parallel to the forearm.
//
//          For handheld motion controllers:
//              The ray that follows platform conventions for how the user targets
//              objects in the world with the motion controller, with +Y up, +X to
//              the right, and -Z forward. This is usually for applications that
//              are rendering a model matching the physical controller, as an
//              application rendering a virtual object in the user�s hand likel
//              prefers to point based on the geometry of that virtual object.
//              The ray chosen will be runtime-dependent, although this will often
//              emerge from the frontmost tip of a motion controller.


// Standard locations

// When a single device contains multiple input sources that use the same identifier,
// a location suffix is added to create a unique identifier for that input source.
//
// Standard locations are:
//
// left
// right
// left_upper
// left_lower
// right_upper
// right_lower
// upper
// lower

// Standard components

// Components are named for the specific boolean, scalar, or other value of the
// input source. Standard components are:
//
//  click
//      A physical switch has been pressed by the user.
//      This is valid for all buttons, and is common for trackpads,
//      thumbsticks, triggers, and dpads. "click" components are always boolean.
//
//  touch
//      The user has touched the input source. This is valid for all trackpads,
//      and may be present for any other kind of input source if the device
//      includes the necessary sensor. "touch" components are always boolean.
//
//  force
//      A 1D scalar value that represents the user applying force to the input.
//      It varies from 0 to 1, with 0 being the rest state. This is present
//          for any input source with a force sensor.
//
//  value
//      A 1D scalar value that varies from 0 to 1, with 0 being the rest state.
//      This is present for triggers, throttles, and pedals. It may also be
//      present for squeeze or other components.
//
//  x, y
//      scalar components of 2D values. These vary in value from -1 to 1.
//      These represent the 2D position of the input source with 0 being
//      the rest state on each axis. -1 means all the way left for x axis
//      or all the way down for y axis. +1 means all the way right for x
//      axis or all the way up for y axis. x and y components are present
//      for trackpads, thumbsticks, and joysticks.
//
// twist
//      Some sources, such as flight sticks, have a sensor that allows the
//      user to twist the input left or right. For this component -1 means
//      all the way left and 1 means all the way right.
//
// pose
//      The orientation and/or position of this input source. This component
//      may exist for dedicated pose identifiers like grip and aim, or may
//      be defined on other identifiers such as trackpad to let applications
//      reason about the surface of that part.
//

// 6.4.1. Khronos Simple Controller Profile

// Path:
//      /interaction_profiles/khr/simple_controller
//
// Valid for user paths:
//      /user/hand/left
//      /user/hand/right
//
// This interaction profile provides basic pose, button, and haptic support
// for applications with simple input needs. There is no hardware associated
// with the profile, and runtimes which support this profile should map the
// input paths provided to whatever the appropriate paths are on the actual hardware.
//
// Supported component paths:
//      .../input/select/click
//      .../input/menu/click
//      .../input/grip/pose
//      .../input/aim/pose
//      .../output/haptic

// 6.4.3. HTC Vive Controller Profile

// Path:
//      /interaction_profiles/htc/vive_controller
//
// Valid for user paths:
//      /user/hand/left
//      /user/hand/right
//
// This interaction profile represents the input sources and haptics on the Vive Controller.
//
// Supported component paths:
//      .../input/system/click (may not be available for application use)
//      .../input/squeeze/click
//      .../input/menu/click
//      .../input/trigger/click
//      .../input/trigger/value
//      .../input/trackpad/x
//      .../input/trackpad/y
//      .../input/trackpad/click
//      .../input/trackpad/touch
//      .../input/grip/pose
//      .../input/aim/pose
//      .../output/haptic
//constexpr const char* c_interaction_profile_simple_controller = "/interaction_profiles/khr/simple_controller";
constexpr const char* c_interaction_profile_vive_controller   = "/interaction_profiles/htc/vive_controller";
//constexpr const char* c_interaction_profile_index_controller  = "/interaction_profiles/valve/index_controller";
//constexpr const char* c_interaction_profile_vive_pro          = "/interaction_profiles/htc/vive_pro";

constexpr const char* c_user_hand_left  = "/user/hand/left";
constexpr const char* c_user_hand_right = "/user/hand/right";

constexpr const char* c_trigger_value   = "/user/hand/right/input/trigger/value";
constexpr const char* c_squeeze_click   = "/user/hand/right/input/squeeze/click";
constexpr const char* c_aim_pose        = "/user/hand/right/input/aim/pose";

Xr_path::Xr_path() = default;

Xr_path::Xr_path(XrInstance instance, const char* path)
{
    check(
        xrStringToPath(
            instance,
            path,
            &xr_path
        )
    );
}

auto Xr_instance::path(const char* path) -> Xr_path
{
    return Xr_path(m_xr_instance, path);
}

auto Xr_instance::initialize_actions() -> bool
{
    ERHE_PROFILE_FUNCTION

    const XrActionSetCreateInfo action_set_info
    {
        .type                   = XR_TYPE_ACTION_SET_CREATE_INFO,
        .next                   = nullptr,
        .actionSetName          = { 'e', 'r', 'h', 'e', '\0' },
        .localizedActionSetName = { 'e', 'r', 'h', 'e', '\0' },
        .priority               = 0,
    };
    ERHE_XR_CHECK(
        xrCreateActionSet(
            m_xr_instance,
            &action_set_info,
            &actions.action_set
        )
    );

    const XrActionCreateInfo trigger_value_action_create_info{
        .type                = XR_TYPE_ACTION_CREATE_INFO,
        .next                = nullptr,
        .actionName          = { 't', 'r', 'i', 'g', 'g', 'e', 'r', '\0' },
        .actionType          = XR_ACTION_TYPE_FLOAT_INPUT,
        .countSubactionPaths = 0,
        .subactionPaths      = nullptr,
        .localizedActionName = { 't', 'r', 'i', 'g', 'g', 'e', 'r', '\0' }
    };
    ERHE_XR_CHECK(
        xrCreateAction(
            actions.action_set,
            &trigger_value_action_create_info,
            &actions.trigger_value
        )
    );

    const XrActionCreateInfo squeeze_click_action_create_info{
        .type                = XR_TYPE_ACTION_CREATE_INFO,
        .next                = nullptr,
        .actionName          = { 's', 'q', 'u', 'e', 'e', 'z', 'e', '\0' },
        .actionType          = XR_ACTION_TYPE_BOOLEAN_INPUT,
        .countSubactionPaths = 0,
        .subactionPaths      = nullptr,
        .localizedActionName = { 's', 'q', 'u', 'e', 'e', 'z', 'e', '\0' }
    };
    ERHE_XR_CHECK(
        xrCreateAction(
            actions.action_set,
            &squeeze_click_action_create_info,
            &actions.squeeze_click
        )
    );

    const XrActionCreateInfo aim_pose_action_create_info{
        .type                = XR_TYPE_ACTION_CREATE_INFO,
        .next                = nullptr,
        .actionName          = { 'a', 'i', 'm', '\0' },
        .actionType          = XR_ACTION_TYPE_POSE_INPUT,
        .countSubactionPaths = 0,
        .subactionPaths      = nullptr,
        .localizedActionName = { 'a', 'i', 'm', '\0' }
    };
    ERHE_XR_CHECK(
        xrCreateAction(
            actions.action_set,
            &aim_pose_action_create_info,
            &actions.aim_pose
        )
    );

    paths.user_hand_left                      = path(c_user_hand_left);
    paths.user_hand_right                     = path(c_user_hand_right);
    paths.trigger_value                       = path(c_trigger_value);
    paths.squeeze_click                       = path(c_squeeze_click);
    paths.aim_pose                            = path(c_aim_pose);
    paths.interaction_profile_vive_controller = path(c_interaction_profile_vive_controller);

    const std::array<XrActionSuggestedBinding, 3> vive_controller_trigger_value_suggested_bindings{
        {
            {
                .action  = actions.trigger_value,
                .binding = paths.trigger_value.xr_path
            },
            {
                .action  = actions.squeeze_click,
                .binding = paths.squeeze_click.xr_path
            },
            {
                .action  = actions.aim_pose,
                .binding = paths.aim_pose.xr_path
            }
        }
    };

    const XrInteractionProfileSuggestedBinding interaction_profile_suggested_binding
    {
        .type                   = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
        .next                   = nullptr,
        .interactionProfile     = paths.interaction_profile_vive_controller.xr_path,
        .countSuggestedBindings = static_cast<uint32_t>(vive_controller_trigger_value_suggested_bindings.size()),
        .suggestedBindings      = vive_controller_trigger_value_suggested_bindings.data()
    };

    ERHE_XR_CHECK(
        xrSuggestInteractionProfileBindings(
            m_xr_instance,
            &interaction_profile_suggested_binding
        )
    );

    const XrSessionActionSetsAttachInfo session_action_sets_attach_info{
        .type            = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
        .next            = nullptr,
        .countActionSets = 1,
        .actionSets      = &actions.action_set
    };

    actions.trigger_value_state = {
        .type                 = XR_TYPE_ACTION_STATE_FLOAT,
        .next                 = nullptr,
        .currentState         = 0.0f,
        .changedSinceLastSync = false,
        .lastChangeTime       = {},
        .isActive             = XR_FALSE
    };

    actions.squeeze_click_state = {
        .type                 = XR_TYPE_ACTION_STATE_BOOLEAN,
        .next                 = nullptr,
        .currentState         = XR_FALSE,
        .changedSinceLastSync = false,
        .lastChangeTime       = {},
        .isActive             = XR_FALSE
    };

    return true;
}

auto Xr_instance::update_actions(Xr_session& session) -> bool
{
    ERHE_PROFILE_FUNCTION

    const XrActiveActionSet active_action_set
    {
        .actionSet     = actions.action_set,
        .subactionPath = XR_NULL_PATH
    };

    const XrActionsSyncInfo actions_sync_info
    {
        .type                  = XR_TYPE_ACTIONS_SYNC_INFO,
        .next                  = nullptr,
        .countActiveActionSets = 1,
        .activeActionSets      = &active_action_set
    };

    {
        const auto result = xrSyncActions(
            session.get_xr_session(),
            &actions_sync_info
        );
        switch (result)
        {
            case XR_SUCCESS:
                break;

            case XR_SESSION_LOSS_PENDING:
            case XR_SESSION_NOT_FOCUSED:
            {
                // TODO
                actions.trigger_value_state.isActive = XR_FALSE;
                actions.squeeze_click_state.isActive = XR_FALSE;
                return true;
            }

            default:
            {
                log_xr->error("xrSyncActions() returned error {}", c_str(result));
                return false;
            }
        }
    }

    {
        const XrActionStateGetInfo action_state_get_info{
            .type          = XR_TYPE_ACTION_STATE_GET_INFO,
            .next          = nullptr,
            .action        = actions.trigger_value,
            .subactionPath = XR_NULL_PATH
        };

        ERHE_XR_CHECK(
            xrGetActionStateFloat(
                session.get_xr_session(),
                &action_state_get_info,
                &actions.trigger_value_state
            )
        );
    }

    {
        const XrActionStateGetInfo action_state_get_info{
            .type          = XR_TYPE_ACTION_STATE_GET_INFO,
            .next          = nullptr,
            .action        = actions.squeeze_click,
            .subactionPath = XR_NULL_PATH
        };

        ERHE_XR_CHECK(
            xrGetActionStateBoolean(
                session.get_xr_session(),
                &action_state_get_info,
                &actions.squeeze_click_state
            )
        );
    }

    {
        const XrActionStateGetInfo action_state_get_info{
            .type          = XR_TYPE_ACTION_STATE_GET_INFO,
            .next          = nullptr,
            .action        = actions.aim_pose,
            .subactionPath = XR_NULL_PATH
        };

        ERHE_XR_CHECK(
            xrGetActionStatePose(
                session.get_xr_session(),
                &action_state_get_info,
                &actions.aim_pose_state
            )
        );
    }

    const XrTime time = session.get_xr_frame_state().predictedDisplayTime;
    XrSpaceLocation location{
        .type          = XR_TYPE_SPACE_LOCATION,
        .next          = nullptr,
        .locationFlags = 0,
        .pose = {
            .orientation = {
                .x = 0.0f,
                .y = 0.0f,
                .z = 0.0f,
                .w = 1.0f
            },
            .position = {
                .x = 0.0f,
                .y = 0.0f,
                .z = 0.0f
            }
        }
    };

    ERHE_XR_CHECK(
        xrLocateSpace(
            actions.aim_pose_space,
            session.get_xr_reference_space_local(),
            time,
            &location
        )
    );

    actions.aim_pose_space_location = location;

    return true;
}

auto Xr_instance::get_current_interaction_profile(Xr_session& session) -> bool
{
    ERHE_PROFILE_FUNCTION

    XrInteractionProfileState interation_profile_state{
        .type               = XR_TYPE_INTERACTION_PROFILE_STATE,
        .next               = nullptr,
        .interactionProfile = XR_NULL_PATH
    };

    ERHE_XR_CHECK(
        xrGetCurrentInteractionProfile(
            session.get_xr_session(),
            paths.user_hand_left.xr_path,
            &interation_profile_state
        )
    );

    if (interation_profile_state.interactionProfile == XR_NULL_PATH)
    {
        log_xr->info("Current interaction profile: <nothing>");
        return true;
    }

    std::array<char, 256> profile_name{};
    uint32_t profile_name_length = 0;
    ERHE_XR_CHECK(
        xrPathToString(
            m_xr_instance,
            interation_profile_state.interactionProfile,
            static_cast<uint32_t>(profile_name.size()),
            &profile_name_length,
            profile_name.data()
        )
    );

    log_xr->info("Current interaction profile: {}", profile_name.data());
    return true;
}

auto Xr_instance::poll_xr_events(Xr_session& session) -> bool
{
    ERHE_PROFILE_FUNCTION

    for (;;)
    {
        XrEventDataBuffer buffer{};
        const auto result = xrPollEvent(m_xr_instance, &buffer);
        if (result == XR_SUCCESS)
        {
            log_xr->trace("XR event {}", c_str(buffer.type));

            if (buffer.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED)
            {
                get_current_interaction_profile(session);
            }

            // if (buffer.type == XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR)
            // {
            // }

            continue;
        }
        else if (result == XR_EVENT_UNAVAILABLE)
        {
            break;
        }

        log_xr->error("xrPollEvent() returned error {}", c_str(result));
        return false;
    }

    return true;
}

}
