#include "erhe_xr/headset.hpp"
#include "erhe_xr/xr_log.hpp"
#include "erhe_xr/xr_instance.hpp"
#include "erhe_xr/xr_session.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <SDL3/SDL.h>

namespace erhe::xr {

Headset::Headset(erhe::window::Context_window& context_window, const Xr_configuration& configuration)
    : m_context_window{context_window}
    , m_configuration {configuration}
{
    auto message_callback = [this](Message_severity severity, const std::string& message, const std::string& callstack) {
        if (
            (severity == Message_severity::warning) ||
            (severity == Message_severity::error)
        ) {
            std::string clipboard_text = "=== OpenXR ===\n" + message + "\n=== Callstack ===\n" + callstack;
            SDL_SetClipboardText(clipboard_text.c_str());
            //if (severity == Message_severity::error)
            {
                ERHE_FATAL("OpenXR Error (error and callstack copied to clipboard)");
            }
        }
    };
    m_xr_instance = std::make_unique<Xr_instance>(configuration, std::move(message_callback));
    if (!m_xr_instance->is_available()) {
        log_xr->info("OpenXR instance is not available");
        m_xr_instance.reset();
        return;
    }
}

auto Headset::create_session(erhe::graphics::Device& graphics_device) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!m_xr_instance) {
        return false;
    }

    m_xr_session = std::make_unique<Xr_session>(
        *m_xr_instance.get(),
        m_context_window,
        graphics_device,
        m_configuration.mirror_mode
    );
    return static_cast<bool>(m_xr_session);
}

Headset::~Headset() noexcept = default;

auto Headset::get_actions_left() -> Xr_actions*
{
    return m_xr_instance ? &m_xr_instance->actions_left : nullptr;
}

auto Headset::get_actions_left() const -> const Xr_actions*
{
    return m_xr_instance ? &m_xr_instance->actions_left : nullptr;
}

auto Headset::get_actions_right() -> Xr_actions*
{
    return m_xr_instance ? &m_xr_instance->actions_right : nullptr;
}

auto Headset::get_actions_right() const -> const Xr_actions*
{
    return m_xr_instance ? &m_xr_instance->actions_right : nullptr;
}

auto Headset::get_hand_tracking_joint(const XrHandEXT hand, const XrHandJointEXT joint) const -> Hand_tracking_joint
{
    return m_xr_session
        ? m_xr_session->get_hand_tracking_joint(hand, joint)
        : Hand_tracking_joint{
            .location = {
                .locationFlags = 0,
                .pose          = {},
                .radius        = 0.0f
            },
            .velocity = {
                .velocityFlags   = 0,
                .linearVelocity  = XrVector3f{0.0f, 0.0f, 0.0f},
                .angularVelocity = XrVector3f{0.0f, 0.0f, 0.0f},

            }
        };
}

auto Headset::get_hand_tracking_active(const XrHandEXT hand) const -> bool
{
    return m_xr_session
        ? m_xr_session->get_hand_tracking_active(hand)
        : false;
}

auto Headset::get_headset_pose(glm::vec3& position, glm::quat& orientation) const -> bool
{
    if (!m_xr_session) {
        return false;
    }
    const auto& space_location = m_xr_session->get_view_space_location();
    if ((space_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == XR_SPACE_LOCATION_POSITION_VALID_BIT) {
        position = to_glm(space_location.pose.position);
    } else {
        return false;
    }
    if ((space_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) {
        orientation = to_glm(space_location.pose.orientation);
    }
    return true;
}

auto Headset::get_xr_instance() -> Xr_instance*
{
    return m_xr_instance.get();
}

auto Headset::get_xr_session() -> Xr_session*
{
    return m_xr_session.get();
}

auto Headset::is_valid() const -> bool
{
    return static_cast<bool>(m_xr_instance) && static_cast<bool>(m_xr_session);
}

auto Headset::is_active() const -> bool
{
    return is_valid() && m_xr_session->is_session_running();
}

auto Headset::poll_events() const -> bool
{
    if (!m_xr_instance || !m_xr_session) {
        return false;
    }
    return m_xr_instance->poll_xr_events(*m_xr_session.get());
}

auto Headset::update_actions() const -> bool
{
    if (!m_xr_instance || !m_xr_session) {
        return false;
    }
    if (!m_xr_session->is_session_running()) {
        return false;
    }

    if (!m_xr_instance->update_actions(*m_xr_session.get())) {
        return false;
    }

    m_xr_session->update_hand_tracking();

    m_xr_session->update_view_pose();
    return true;
}

auto Headset::begin_frame_() -> Frame_timing
{
    ERHE_PROFILE_FUNCTION();

    Frame_timing result{
        .predicted_display_time   = 0,
        .predicted_display_pediod = 0,
        .begin_ok                 = false,
        .should_render            = true
    };

    auto* xr_frame_state = m_xr_session->wait_frame();
    if (xr_frame_state == nullptr) {
        return result;
    }

    if (!m_xr_session->begin_frame()) {
        result.should_render = false;
        return result;
    }

    result.predicted_display_time   = xr_frame_state->predictedDisplayTime;
    result.predicted_display_pediod = xr_frame_state->predictedDisplayPeriod;
    result.begin_ok                 = true;
    result.should_render            = xr_frame_state->shouldRender;

    return result;
}

auto Headset::render(erhe::graphics::Command_buffer& command_buffer, std::function<bool(Render_view&, erhe::graphics::Command_buffer&)> render_view_callback) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!m_xr_session) {
        return false;
    }
    if (!m_xr_session->render_frame(command_buffer, render_view_callback)) {
        return false;
    }
    return true;
}

auto Headset::end_frame(const bool rendered) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!m_xr_instance) {
        return false;
    }

    return m_xr_session->end_frame(rendered);
}

} // namespace erhe::xr
