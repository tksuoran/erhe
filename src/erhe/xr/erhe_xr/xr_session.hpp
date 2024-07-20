#pragma once

#include "erhe_xr/xr.hpp"
#include "erhe_xr/xr_swapchain_image.hpp"
#include <openxr/openxr.h>

#include <functional>
#include <vector>

namespace erhe::window {
    class Context_window;
}

namespace erhe::xr {

class Xr_instance;

class Xr_session
{
public:
    Xr_session    (Xr_instance& instance, erhe::window::Context_window& context_window, bool mirror_mode);
    ~Xr_session   () noexcept;
    Xr_session    (const Xr_session&) = delete;
    void operator=(const Xr_session&) = delete;
    Xr_session    (Xr_session&&)      = delete;
    void operator=(Xr_session&&)      = delete;

    [[nodiscard]] auto begin_session               () -> bool;
    [[nodiscard]] auto end_session                 () -> bool;
    [[nodiscard]] auto is_session_running          () const -> bool;
    [[nodiscard]] auto begin_frame                 () -> bool;
    [[nodiscard]] auto wait_frame                  () -> XrFrameState*;
    [[nodiscard]] auto render_frame                (std::function<bool(Render_view&)> render_view_callback) -> bool;
    [[nodiscard]] auto end_frame                   (const bool rendered) -> bool;
    [[nodiscard]] auto get_xr_session              () const -> XrSession;
    [[nodiscard]] auto get_xr_reference_space_local() const -> XrSpace;
    [[nodiscard]] auto get_xr_reference_space_stage() const -> XrSpace;
    [[nodiscard]] auto get_xr_reference_space_view () const -> XrSpace;
    [[nodiscard]] auto get_xr_frame_state          () const -> const XrFrameState&;
    void update_hand_tracking();
    void update_view_pose    ();
    void set_state           (XrSessionState state);

    [[nodiscard]] auto get_hand_tracking_joint (const XrHandEXT hand, const XrHandJointEXT joint) const -> Hand_tracking_joint;
    [[nodiscard]] auto get_hand_tracking_active(const XrHandEXT hand) const -> bool;
    [[nodiscard]] auto get_view_space_location () const -> const XrSpaceLocation&;
    [[nodiscard]] auto get_state               () const -> XrSessionState;

private:
    [[nodiscard]] auto color_format_score(const gl::Internal_format image_format) const -> int;
    [[nodiscard]] auto create_session             () -> bool;
    [[nodiscard]] auto enumerate_swapchain_formats() -> bool;
    [[nodiscard]] auto enumerate_reference_spaces () -> bool;
    [[nodiscard]] auto create_swapchains          () -> bool;
    [[nodiscard]] auto create_reference_space     () -> bool;
    [[nodiscard]] auto attach_actions             () -> bool;
    [[nodiscard]] auto create_hand_tracking       () -> bool;

    class Swapchains
    {
    public:
        Swapchains(XrSwapchain color_swapchain, XrSwapchain depth_stencil_swapchain)
            : color_swapchain{color_swapchain}
            , depth_stencil_swapchain{depth_stencil_swapchain}
        {
        }

        Swapchain color_swapchain;
        Swapchain depth_stencil_swapchain;
    };

    class Hand_tracker
    {
    public:
        XrHandTrackerEXT         hand_tracker    {XR_NULL_HANDLE};
        XrHandJointLocationEXT   joint_locations [XR_HAND_JOINT_COUNT_EXT];
        XrHandJointVelocityEXT   joint_velocities[XR_HAND_JOINT_COUNT_EXT];
        XrHandJointVelocitiesEXT velocities;
        XrHandJointLocationsEXT  locations;
    };

    Xr_instance&                                  m_instance;
    erhe::window::Context_window&                 m_context_window;
    XrSession                                     m_xr_session;
    bool                                          m_mirror_mode{false};
    gl::Internal_format                           m_swapchain_color_format;
    gl::Internal_format                           m_swapchain_depth_stencil_format;
    std::vector<Swapchains>                       m_view_swapchains;
    std::vector<XrView>                           m_xr_views;
    std::vector<XrCompositionLayerProjectionView> m_xr_composition_layer_projection_views;
    std::vector<XrCompositionLayerDepthInfoKHR>   m_xr_composition_layer_depth_infos;
    std::vector<XrReferenceSpaceType>             m_xr_reference_space_types;
    XrSpace                                       m_xr_reference_space_local;
    XrSpace                                       m_xr_reference_space_stage;
    XrSpace                                       m_xr_reference_space_view;
    XrSpaceLocation                               m_view_location;
    XrSessionState                                m_xr_session_state;
    XrFrameState                                  m_xr_frame_state;
    Hand_tracker                                  m_hand_tracker_left;
    Hand_tracker                                  m_hand_tracker_right;
    bool                                          m_session_running{false};
};

} // namespace erhe::xr
