#include "erhe/toolkit/headset.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/xr_instance.hpp"
#include "erhe/toolkit/xr_session.hpp"

namespace erhe::xr {

Headset::Headset(erhe::toolkit::Context_window* context_window)
{
    VERIFY(context_window != nullptr);

    m_xr_instance = std::make_unique<Xr_instance>();
    m_xr_session  = std::make_unique<Xr_session>(*m_xr_instance.get(), *context_window);

    m_xr_instance->get_current_interaction_profile(*m_xr_session.get());
}

Headset::~Headset()
{
}

auto Headset::trigger_value() const -> float
{
    return m_xr_instance->actions.trigger_position_state.currentState;
}

auto Headset::begin_frame() -> Frame_timing
{
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
    return m_xr_session->end_frame();
}

}
