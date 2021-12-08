#pragma once

//#include <openxr/openxr.h>

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

#include <vector>

namespace erhe::xr {

class Xr_session;

class Xr_path
{
public:
    Xr_path();
    Xr_path(XrInstance instance, const char* path);

    XrPath xr_path{XR_NULL_PATH};
};

class Xr_instance
{
public:
    Xr_instance   ();
    ~Xr_instance  ();
    Xr_instance   (const Xr_instance&) = delete;
    void operator=(const Xr_instance&) = delete;
    Xr_instance   (Xr_instance&&)      = delete;
    void operator=(Xr_instance&&)      = delete;

    auto is_available                   () const -> bool;
    auto poll_xr_events                 (Xr_session& session) -> bool;
    auto get_xr_instance                () const -> XrInstance;
    auto get_xr_system_id               () const -> XrSystemId;
    auto get_xr_view_configuration_type () const -> XrViewConfigurationType;
    auto get_xr_view_configuration_views() const -> const std::vector<XrViewConfigurationView>&;
    auto get_xr_environment_blend_mode  () const -> XrEnvironmentBlendMode;
    auto update_actions                 (Xr_session& session) -> bool;
    auto get_current_interaction_profile(Xr_session& session) -> bool;

    //void set_environment_depth_estimation(XrSession xr_session, bool enabled);

    auto initialize_actions             () -> bool;

    auto debug_utils_messenger_callback(
        XrDebugUtilsMessageSeverityFlagsEXT         messageSeverity,
        XrDebugUtilsMessageTypeFlagsEXT             messageTypes,
        const XrDebugUtilsMessengerCallbackDataEXT* callbackData
    ) const -> XrBool32;

    class Paths
    {
    public:
        Xr_path user_hand_left;
        Xr_path user_hand_right;
        Xr_path interaction_profile_vive_controller;
        Xr_path head;
        Xr_path menu_click;
        Xr_path trigger_click;
        Xr_path trigger_value;
        Xr_path squeeze_click;
        Xr_path trackpad_x;
        Xr_path trackpad_y;
        Xr_path trackpad_click;
        Xr_path trackpad_touch;
        Xr_path grip_pose;
        Xr_path aim_pose;
        Xr_path haptic;
    };

    class Actions
    {
    public:
        XrActionSet          action_set;
        XrAction             trigger_value;
        XrActionStateFloat   trigger_value_state;
        XrAction             squeeze_click;
        XrActionStateBoolean squeeze_click_state;
        XrAction             aim_pose;
        XrActionStatePose    aim_pose_state;
        XrSpace              aim_pose_space;
        XrSpaceLocation      aim_pose_space_location;
    };

    Paths   paths;
    Actions actions;

    PFN_xrCreateDebugUtilsMessengerEXT     xrCreateDebugUtilsMessengerEXT    {nullptr};
    PFN_xrGetVisibilityMaskKHR             xrGetVisibilityMaskKHR            {nullptr};
    PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetOpenGLGraphicsRequirementsKHR{nullptr};

    PFN_xrCreateHandTrackerEXT             xrCreateHandTrackerEXT            {nullptr};
    PFN_xrDestroyHandTrackerEXT            xrDestroyHandTrackerEXT           {nullptr}; 
    PFN_xrLocateHandJointsEXT              xrLocateHandJointsEXT             {nullptr};

private:
    auto get_proc_addr(const char* function) const -> PFN_xrVoidFunction;

    template <typename T>
    auto get_proc_addr(const char* function) const -> T
    {
        return reinterpret_cast<T>(get_proc_addr(function));
    }

    auto enumerate_layers               () -> bool;
    auto enumerate_extensions           () -> bool;
    auto create_instance                () -> bool;
    auto get_system_info                () -> bool;
    auto enumerate_blend_modes          () -> bool;
    auto enumerate_view_configurations  () -> bool;
    auto path                           (const char* path) -> Xr_path;

    //Xr_session*                          m_session                   {nullptr};
    XrInstance                           m_xr_instance               {XR_NULL_HANDLE};
    XrSystemGetInfo                      m_xr_system_info;
    XrSystemId                           m_xr_system_id{0};
    XrViewConfigurationType              m_xr_view_configuration_type{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO};
    XrEnvironmentBlendMode               m_xr_environment_blend_mode {XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
    std::vector<XrViewConfigurationView> m_xr_view_configuration_views;
    std::vector<XrExtensionProperties>   m_xr_extensions;
    std::vector<XrApiLayerProperties>    m_xr_api_layer_properties;
    std::vector<XrEnvironmentBlendMode>  m_xr_environment_blend_modes;
    XrDebugUtilsMessengerEXT             m_debug_utils_messenger;
    //PFN_xrSetEnvironmentDepthEstimationVARJO m_xrSetEnvironmentDepthEstimationVARJO{nullptr};
};

}
