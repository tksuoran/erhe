#pragma once

#include "erhe_xr/xr.hpp"
#include "erhe_xr/xr_swapchain_image.hpp"
#include <openxr/openxr.h>

#include "igl/TextureFormat.h"

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
    Xr_session    (Xr_instance& instance, erhe::window::Context_window& context_window);
    ~Xr_session   () noexcept;
    Xr_session    (const Xr_session&) = delete;
    void operator=(const Xr_session&) = delete;
    Xr_session    (Xr_session&&)      = delete;
    void operator=(Xr_session&&)      = delete;

    // TODO [[nodiscard]]
    auto begin_session               () -> bool;
    auto end_session                 () -> bool;
    auto is_session_running          () const -> bool;
    auto begin_frame                 () -> bool;
    auto wait_frame                  () -> XrFrameState*;
    auto render_frame                (std::function<bool(Render_view&)> render_view_callback) -> bool;
    auto end_frame                   (const bool rendered) -> bool;
    auto get_xr_session              () const -> XrSession;
    auto get_xr_reference_space_local() const -> XrSpace;
    auto get_xr_reference_space_stage() const -> XrSpace;
    auto get_xr_reference_space_view () const -> XrSpace;
    auto get_xr_frame_state          () const -> const XrFrameState&;
    void update_hand_tracking        ();
    void update_view_pose            ();

    [[nodiscard]] auto get_hand_tracking_joint (const XrHandEXT hand, const XrHandJointEXT joint) const -> Hand_tracking_joint;
    [[nodiscard]] auto get_hand_tracking_active(const XrHandEXT hand) const -> bool;
    [[nodiscard]] auto get_view_space_location () const -> const XrSpaceLocation&;

private:
    auto create_session             () -> bool;
    auto enumerate_swapchain_formats() -> bool;
    auto enumerate_reference_spaces () -> bool;
    auto create_swapchains          () -> bool;
    auto create_reference_space     () -> bool;
    auto attach_actions             () -> bool;
    auto create_hand_tracking       () -> bool;

    class Swapchains
    {
    public:
        Swapchains(XrSwapchain color_swapchain, XrSwapchain depth_swapchain)
            : color_swapchain{color_swapchain}
            , depth_swapchain{depth_swapchain}
        {
        }

        Swapchain color_swapchain;
        Swapchain depth_swapchain;
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
    igl::TextureFormat                            m_swapchain_color_format;
    igl::TextureFormat                            m_swapchain_depth_format;
    std::vector<Swapchains>                       m_view_swapchains;
    std::vector<XrView>                           m_xr_views;
    std::vector<XrCompositionLayerProjectionView> m_xr_composition_layer_projection_views;
    std::vector<XrCompositionLayerDepthInfoKHR>   m_xr_composition_layer_depth_infos;
    std::vector<XrReferenceSpaceType>             m_xr_reference_space_types;
    XrSpace                                       m_xr_reference_space_local;
    XrSpace                                       m_xr_reference_space_stage;
    XrSpace                                       m_xr_reference_space_view;
    XrSpaceLocation                               m_view_location;
    //XrSessionState                                m_xr_session_state;
    XrFrameState                                  m_xr_frame_state;
    Hand_tracker                                  m_hand_tracker_left;
    Hand_tracker                                  m_hand_tracker_right;
    bool                                          m_session_running{false};
};

}
