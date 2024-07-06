#pragma once

#include "erhe_xr/xr_action.hpp"

#ifdef _WIN32
#   include <unknwn.h>
#   define XR_USE_PLATFORM_WIN32      1
#endif

#ifdef linux
#   define XR_USE_PLATFORM_LINUX      1
#endif

#define XR_USE_GRAPHICS_API_OPENGL 1
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "etl/vector.h"

#include <string_view>
#include <vector>

namespace erhe::xr {

class Xr_session;

class Xr_path
{
public:
    XrPath xr_path{XR_NULL_PATH};
};

class Xr_configuration
{
public:
    bool debug            {false};
    bool quad_view        {false};
    bool depth            {false};
    bool visibility_mask  {false};
    bool hand_tracking    {false};
    bool composition_alpha{false};
};

class Xr_instance
{
public:
    explicit Xr_instance(const Xr_configuration& configuration);
    ~Xr_instance  () noexcept;
    Xr_instance   (const Xr_instance&) = delete;
    void operator=(const Xr_instance&) = delete;
    Xr_instance   (Xr_instance&&)      = delete;
    void operator=(Xr_instance&&)      = delete;

    [[nodiscard]] auto is_available                   () const -> bool;
    [[nodiscard]] auto poll_xr_events                 (Xr_session& session) -> bool;
    [[nodiscard]] auto get_xr_instance                () const -> XrInstance;
    [[nodiscard]] auto get_xr_system_id               () const -> XrSystemId;
    [[nodiscard]] auto get_xr_view_configuration_type () const -> XrViewConfigurationType;
    [[nodiscard]] auto get_xr_view_configuration_views() const -> const std::vector<XrViewConfigurationView>&;
    [[nodiscard]] auto get_xr_environment_blend_mode  () const -> XrEnvironmentBlendMode;
    [[nodiscard]] auto update_actions                 (Xr_session& session) -> bool;
                  auto get_current_interaction_profile(Xr_session& session) -> bool;
    [[nodiscard]] auto get_configuration              () const -> const Xr_configuration&;

    //void set_environment_depth_estimation(XrSession xr_session, bool enabled);

    auto initialize_actions             () -> bool;
    void update_action_bindings         ();

    auto debug_utils_messenger_callback(
        XrDebugUtilsMessageSeverityFlagsEXT         messageSeverity,
        XrDebugUtilsMessageTypeFlagsEXT             messageTypes,
        const XrDebugUtilsMessengerCallbackDataEXT* callbackData
    ) const -> XrBool32;

    [[nodiscard]] auto get_path_string(XrPath path) -> std::string;

    class Paths
    {
    public:
        XrPath user_head                                  {XR_NULL_PATH};
        XrPath user_hand_left                             {XR_NULL_PATH};
        XrPath user_hand_right                            {XR_NULL_PATH};
        XrPath interaction_profile_khr_simple_controller  {XR_NULL_PATH};
        XrPath interaction_profile_htc_vive_controller    {XR_NULL_PATH};
        XrPath interaction_profile_oculus_touch_controller{XR_NULL_PATH};
        XrPath interaction_profile_valve_index_controller {XR_NULL_PATH};
    };

    Xr_actions actions_left;
    Xr_actions actions_right;

    [[nodiscard]] auto create_boolean_action (unsigned int profile_mask, const std::string_view path_name) -> Xr_action_boolean*;
    [[nodiscard]] auto create_float_action   (unsigned int profile_mask, const std::string_view path_name) -> Xr_action_float*;
    [[nodiscard]] auto create_vector2f_action(unsigned int profile_mask, const std::string_view path_name) -> Xr_action_vector2f*;
    [[nodiscard]] auto create_pose_action    (unsigned int profile_mask, const std::string_view path_name) -> Xr_action_pose*;

    static constexpr unsigned int max_action_count = 50;
    [[nodiscard]] auto get_boolean_actions () -> etl::vector<Xr_action_boolean,  max_action_count>&;
    [[nodiscard]] auto get_float_actions   () -> etl::vector<Xr_action_float,    max_action_count>&;
    [[nodiscard]] auto get_vector2f_actions() -> etl::vector<Xr_action_vector2f, max_action_count>&;
    [[nodiscard]] auto get_pose_actions    () -> etl::vector<Xr_action_pose,     max_action_count>&;

    auto attach_actions(const XrSession session) -> bool;

    Paths                                  paths;

    PFN_xrCreateDebugUtilsMessengerEXT     xrCreateDebugUtilsMessengerEXT    {nullptr};
    PFN_xrGetVisibilityMaskKHR             xrGetVisibilityMaskKHR            {nullptr};
    PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetOpenGLGraphicsRequirementsKHR{nullptr};

    PFN_xrCreateHandTrackerEXT             xrCreateHandTrackerEXT            {nullptr};
    PFN_xrDestroyHandTrackerEXT            xrDestroyHandTrackerEXT           {nullptr};
    PFN_xrLocateHandJointsEXT              xrLocateHandJointsEXT             {nullptr};

private:
    [[nodiscard]] auto get_proc_addr(const char* function) const -> PFN_xrVoidFunction;

    template <typename T>
    [[nodiscard]] auto get_proc_addr(const char* function) const -> T
    {
        return reinterpret_cast<T>(get_proc_addr(function));
    }

    auto enumerate_layers               () -> bool;
    auto enumerate_extensions           () -> bool;
    auto create_instance                () -> bool;
    auto get_system_info                () -> bool;
    auto enumerate_blend_modes          () -> bool;
    auto enumerate_view_configurations  () -> bool;

    void get_paths();
    void get_path(const char* path, XrPath& xr_path);

    void collect_bindings(const Xr_action& action);

    Xr_configuration                     m_configuration;

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

    XrActionSet                                       m_action_set{};
    etl::vector<Xr_action_boolean,  max_action_count> m_boolean_actions;
    etl::vector<Xr_action_float,    max_action_count> m_float_actions;
    etl::vector<Xr_action_vector2f, max_action_count> m_vector2f_actions;
    etl::vector<Xr_action_pose,     max_action_count> m_pose_actions;
    std::vector<XrActionSuggestedBinding> m_khr_simple_bindings;
    std::vector<XrActionSuggestedBinding> m_oculus_touch_bindings;
    std::vector<XrActionSuggestedBinding> m_valve_index_bindings;
    std::vector<XrActionSuggestedBinding> m_htc_vive_bindings;

    //PFN_xrSetEnvironmentDepthEstimationVARJO m_xrSetEnvironmentDepthEstimationVARJO{nullptr};
};

} // namespace erhe::xr
