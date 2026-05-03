#pragma once

#include "erhe_xr/xr_action.hpp"
#include "erhe_xr/generated/headset_config.hpp"

#if defined(ERHE_OS_WINDOWS)
#   include <unknwn.h>
#endif

#if defined(ERHE_GRAPHICS_API_VULKAN)
# include "erhe_graphics/vulkan_external_creators.hpp"
#endif

// jni.h must precede <openxr/openxr_platform.h> on Android: the platform
// header references jobject (XrLoaderInitInfoAndroidKHR, XrInstanceCreateInfoAndroidKHR).
#if defined(XR_USE_PLATFORM_ANDROID)
# include <jni.h>
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "etl/vector.h"

#include <optional>
#include <string_view>
#include <vector>

namespace erhe::xr {

class Xr_session;

class Xr_path
{
public:
    XrPath xr_path{XR_NULL_PATH};
};

enum class Message_severity : unsigned int {
    verbose = 0,
    info    = 1,
    warning = 2,
    error   = 3
};

[[nodiscard]] auto c_str(Message_severity message_severity) -> const char*;

using Message_callback = std::function<void(Message_severity severity, const std::string& message, const std::string& callstack)>;

class Xr_instance
{
public:
    explicit Xr_instance(
        const Headset_config& configuration,
        Message_callback      message_callback
    );
    ~Xr_instance  () noexcept;
    Xr_instance   (const Xr_instance&) = delete;
    void operator=(const Xr_instance&) = delete;
    Xr_instance   (Xr_instance&&)      = delete;
    void operator=(Xr_instance&&)      = delete;

    [[nodiscard]] auto is_available                   () const -> bool;
    [[nodiscard]] auto poll_xr_events                 (Xr_session& session) -> bool;
    [[nodiscard]] auto get_xr_instance                () const -> XrInstance;
    [[nodiscard]] auto get_xr_system_id               () const -> XrSystemId;

#if defined(ERHE_GRAPHICS_API_VULKAN)
    // Build a Vulkan_external_creators struct whose hooks wrap the
    // XR_KHR_vulkan_enable2 entry points. The returned struct is meant to be
    // passed to erhe::graphics::Device so that instance / physical device /
    // device creation goes through the OpenXR runtime.
    [[nodiscard]] auto make_vulkan_external_creators() const -> erhe::graphics::Vulkan_external_creators;
#endif

    [[nodiscard]] auto get_xr_view_configuration_type () const -> XrViewConfigurationType;
    [[nodiscard]] auto get_xr_view_configuration_views() const -> const std::vector<XrViewConfigurationView>&;
    [[nodiscard]] auto get_xr_environment_blend_mode  () const -> XrEnvironmentBlendMode;
    [[nodiscard]] auto update_actions                 (Xr_session& session) -> bool;
                  auto get_current_interaction_profile(Xr_session& session) -> bool;
    [[nodiscard]] auto get_configuration              () const -> const Headset_config&;

    // XR_EXT_performance_settings: apply CPU/GPU performance level. Either
    // value may be XR_PERF_SETTINGS_LEVEL_*_EXT, or -1 to skip the call for
    // that domain (= keep runtime default). No-ops when the extension is
    // not enabled.
    void apply_performance_level(XrSession session, int cpu_level, int gpu_level);

    // Drain the most recent XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT received
    // since the last call. The editor polls this each tick to decide
    // whether to step the CPU/GPU level down per Headset_config::boost_on_thermal_warning.
    [[nodiscard]] auto take_latest_thermal_warning() -> std::optional<XrEventDataPerfSettingsEXT>;

    //void set_environment_depth_estimation(XrSession xr_session, bool enabled);

    auto initialize_actions             () -> bool;
    void update_action_bindings         ();

    void message(Message_severity severity, const std::string& message) const;

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

    struct Extensions {
        bool KHR_composition_layer_depth       {false};
        bool KHR_visibility_mask               {false};
        bool EXT_debug_utils                   {false};
        bool EXT_hand_tracking                 {false};
        bool EXT_performance_settings          {false};
        bool FB_passthrough                    {false};
        bool FB_color_space                    {false};
        bool FB_display_refresh_rate           {false};
        bool META_performance_metrics            {false};
        bool VARJO_quad_views                  {false};
        bool VARJO_environment_depth_estimation{false};
    };
    Extensions extensions{};

    PFN_xrCreateDebugUtilsMessengerEXT     xrCreateDebugUtilsMessengerEXT    {nullptr};
    PFN_xrGetVisibilityMaskKHR             xrGetVisibilityMaskKHR            {nullptr};
#if defined(ERHE_GRAPHICS_API_OPENGL)
    PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetOpenGLGraphicsRequirementsKHR{nullptr};
#endif
#if defined(ERHE_GRAPHICS_API_VULKAN)
    PFN_xrGetVulkanGraphicsRequirements2KHR xrGetVulkanGraphicsRequirements2KHR{nullptr};
    PFN_xrCreateVulkanInstanceKHR           xrCreateVulkanInstanceKHR          {nullptr};
    PFN_xrGetVulkanGraphicsDevice2KHR       xrGetVulkanGraphicsDevice2KHR      {nullptr};
    PFN_xrCreateVulkanDeviceKHR             xrCreateVulkanDeviceKHR            {nullptr};
#endif

    PFN_xrCreateHandTrackerEXT             xrCreateHandTrackerEXT            {nullptr};
    PFN_xrDestroyHandTrackerEXT            xrDestroyHandTrackerEXT           {nullptr};
    PFN_xrLocateHandJointsEXT              xrLocateHandJointsEXT             {nullptr};

    PFN_xrEnumerateColorSpacesFB           xrEnumerateColorSpacesFB          {nullptr};
    PFN_xrSetColorSpaceFB                  xrSetColorSpaceFB                 {nullptr};

    PFN_xrCreatePassthroughFB              xrCreatePassthroughFB             {nullptr};
    PFN_xrDestroyPassthroughFB             xrDestroyPassthroughFB            {nullptr};
    PFN_xrPassthroughStartFB               xrPassthroughStartFB              {nullptr};
    PFN_xrPassthroughPauseFB               xrPassthroughPauseFB              {nullptr};
    PFN_xrCreatePassthroughLayerFB         xrCreatePassthroughLayerFB        {nullptr};
    PFN_xrDestroyPassthroughLayerFB        xrDestroyPassthroughLayerFB       {nullptr};
    PFN_xrPassthroughLayerPauseFB          xrPassthroughLayerPauseFB         {nullptr};
    PFN_xrPassthroughLayerResumeFB         xrPassthroughLayerResumeFB        {nullptr};
    PFN_xrPassthroughLayerSetStyleFB       xrPassthroughLayerSetStyleFB      {nullptr};
    PFN_xrCreateGeometryInstanceFB         xrCreateGeometryInstanceFB        {nullptr};
    PFN_xrDestroyGeometryInstanceFB        xrDestroyGeometryInstanceFB       {nullptr};
    PFN_xrGeometryInstanceSetTransformFB   xrGeometryInstanceSetTransformFB  {nullptr};

    // XR_EXT_performance_settings
    PFN_xrPerfSettingsSetPerformanceLevelEXT xrPerfSettingsSetPerformanceLevelEXT{nullptr};

    // XR_META_performance_metrics
    PFN_xrEnumeratePerformanceMetricsCounterPathsMETA xrEnumeratePerformanceMetricsCounterPathsMETA{nullptr};
    PFN_xrSetPerformanceMetricsStateMETA              xrSetPerformanceMetricsStateMETA             {nullptr};
    PFN_xrGetPerformanceMetricsStateMETA              xrGetPerformanceMetricsStateMETA             {nullptr};
    PFN_xrQueryPerformanceMetricsCounterMETA          xrQueryPerformanceMetricsCounterMETA         {nullptr};

    [[nodiscard]] auto debug_utils_messenger_callback(
        XrDebugUtilsMessageSeverityFlagsEXT         message_severity,
        XrDebugUtilsMessageTypeFlagsEXT             message_types,
        const XrDebugUtilsMessengerCallbackDataEXT* callback_data
    ) -> XrBool32;

private:
    [[nodiscard]] auto get_proc_addr(const char* function) const -> PFN_xrVoidFunction;

    template <typename T>
    [[nodiscard]] auto get_proc_addr(const char* function) const -> T
    {
        return reinterpret_cast<T>(get_proc_addr(function));
    }

    [[nodiscard]] auto has_layer    (const char* layer_name) const -> bool;
    [[nodiscard]] auto has_extension(const char* extension_name) const -> bool;

    auto enumerate_layers               () -> bool;
    auto enumerate_extensions           () -> bool;
    auto create_instance                () -> bool;
    auto get_system_info                () -> bool;
    auto enumerate_blend_modes          () -> bool;
    auto enumerate_view_configurations  () -> bool;

    void get_paths();
    void get_path(const char* path, XrPath& xr_path);

    void collect_bindings(const Xr_action& action);

    Headset_config                       m_configuration;

    XrInstance                           m_xr_instance               {XR_NULL_HANDLE};
    XrSystemGetInfo                      m_xr_system_info;
    XrSystemId                           m_xr_system_id{0};
    XrViewConfigurationType              m_xr_view_configuration_type{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO};
    XrEnvironmentBlendMode               m_xr_environment_blend_mode {XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
    std::vector<XrViewConfigurationView> m_xr_view_configuration_views;
    std::vector<XrExtensionProperties>   m_xr_extensions;
    std::vector<XrApiLayerProperties>    m_xr_api_layer_properties;
    std::vector<XrEnvironmentBlendMode>  m_xr_environment_blend_modes;
    XrDebugUtilsMessengerEXT             m_debug_utils_messenger{XR_NULL_HANDLE};
    std::vector<const char*>             m_enabled_extensions;

    XrActionSet                                       m_action_set{};
    etl::vector<Xr_action_boolean,  max_action_count> m_boolean_actions;
    etl::vector<Xr_action_float,    max_action_count> m_float_actions;
    etl::vector<Xr_action_vector2f, max_action_count> m_vector2f_actions;
    etl::vector<Xr_action_pose,     max_action_count> m_pose_actions;
    std::vector<XrActionSuggestedBinding>             m_khr_simple_bindings;
    std::vector<XrActionSuggestedBinding>             m_oculus_touch_bindings;
    std::vector<XrActionSuggestedBinding>             m_valve_index_bindings;
    std::vector<XrActionSuggestedBinding>             m_htc_vive_bindings;

    Message_callback m_message_callback{};
    //PFN_xrSetEnvironmentDepthEstimationVARJO m_xrSetEnvironmentDepthEstimationVARJO{nullptr};

    std::optional<XrEventDataPerfSettingsEXT> m_latest_thermal_warning;
};

} // namespace erhe::xr
