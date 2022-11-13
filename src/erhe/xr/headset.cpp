#include "erhe/xr/headset.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/xr/xr_instance.hpp"
#include "erhe/xr/xr_session.hpp"
#include "erhe/toolkit/profile.hpp"

namespace erhe::xr {

Headset::Headset(erhe::toolkit::Context_window* context_window, const Xr_configuration& configuration)
{
    ERHE_PROFILE_FUNCTION

    ERHE_VERIFY(context_window != nullptr);

    m_xr_instance = std::make_unique<Xr_instance>(configuration);
    if (!m_xr_instance->is_available())
    {
        m_xr_instance.reset();
        return;
    }

    m_xr_session = std::make_unique<Xr_session>(*m_xr_instance.get(), *context_window);

    m_xr_instance->get_current_interaction_profile(*m_xr_session.get());
}

Headset::~Headset()
{
}

auto Headset::controller_pose() const -> Pose
{
    return m_controller_pose;
}

auto Headset::get_hand_tracking_joint(const XrHandEXT hand, const XrHandJointEXT joint) const -> Hand_tracking_joint
{
    return m_xr_session
        ? m_xr_session->get_hand_tracking_joint(hand, joint)
        : Hand_tracking_joint{
            .location = {
                .locationFlags = 0
            },
            .velocity = {
                .velocityFlags = 0
            }
        };
}

auto Headset::get_hand_tracking_active(const XrHandEXT hand) const -> bool
{
    return m_xr_session
        ? m_xr_session->get_hand_tracking_active(hand)
        : false;
}

[[nodiscard]] auto Headset::get_view_in_world() const -> glm::mat4
{
    if (!m_xr_session)
    {
        return glm::mat4{1.0f};
    }
    const auto& space_location = m_xr_session->get_view_space_location();

    glm::vec3 position{0.0f, 0.0f, 0.0f};
    if ((space_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == XR_SPACE_LOCATION_POSITION_VALID_BIT)
    {
        position = to_glm(space_location.pose.position);
    }
    glm::quat orientation{};
    if ((space_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
    {
        orientation = to_glm(space_location.pose.orientation);
    }

    const glm::mat4 m_orientation = glm::mat4_cast(orientation);
    const glm::mat4 m_translation = glm::translate(glm::mat4{ 1 }, position);
    const glm::mat4 m             = m_translation * m_orientation;
    return m;
}

auto Headset::trigger_value() const -> float
{
    return m_xr_instance
        ? m_xr_instance->actions.trigger_value_state.currentState
        : 0.0f;
}

auto Headset::menu_click() const -> bool
{
    return m_xr_instance
        ? (m_xr_instance->actions.menu_click_state.currentState == XR_TRUE)
        : false;
}

auto Headset::squeeze_click() const -> bool
{
    return m_xr_instance
        ? (m_xr_instance->actions.squeeze_click_state.currentState == XR_TRUE)
        : false;
}

auto Headset::begin_frame() -> Frame_timing
{
    ERHE_PROFILE_FUNCTION

    Frame_timing result{0, 0, false};
    if (!m_xr_instance)
    {
        return result;
    }
    if (!m_xr_instance->poll_xr_events(*m_xr_session.get()))
    {
        return result;
    }

    if (!m_xr_instance->update_actions(*m_xr_session.get()))
    {
        return result;
    }

    m_xr_session->update_hand_tracking();

    m_xr_session->update_view_pose();

    if ((m_xr_instance->actions.aim_pose_space_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == XR_SPACE_LOCATION_POSITION_VALID_BIT)
    {
        m_controller_pose.position    = to_glm(m_xr_instance->actions.aim_pose_space_location.pose.position);
    }
    if ((m_xr_instance->actions.aim_pose_space_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
    {
        m_controller_pose.orientation = to_glm(m_xr_instance->actions.aim_pose_space_location.pose.orientation);
    }

    auto* xr_frame_state = m_xr_session->wait_frame();
    if (xr_frame_state == nullptr)
    {
        return result;
    }

    if (!m_xr_session->begin_frame())
    {
        return result;
    }

    result.predicted_display_time   = xr_frame_state->predictedDisplayTime;
    result.predicted_display_pediod = xr_frame_state->predictedDisplayPeriod;
    result.should_render            = xr_frame_state->shouldRender;

    return result;
}

auto Headset::render(std::function<bool(Render_view&)> render_view_callback) -> bool
{
    ERHE_PROFILE_FUNCTION

    if (!m_xr_session)
    {
        return false;
    }
    if (!m_xr_session->render_frame(render_view_callback))
    {
        return false;
    }
    return true;
}

auto Headset::end_frame() -> bool
{
    ERHE_PROFILE_FUNCTION

    if (!m_xr_instance)
    {
        return false;
    }

    return m_xr_session->end_frame();
}

}
