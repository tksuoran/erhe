#include "erhe_frame_pacing/slop_servo_pacer.hpp"

#include <algorithm>

namespace erhe::frame_pacing {

Slop_servo_pacer::Slop_servo_pacer(const Slop_servo_tunables& tunables, const double refresh_period)
    : m_tunables      {tunables}
    , m_refresh_period{refresh_period}
{
}

void Slop_servo_pacer::update(const double slop, const double loop_duration)
{
    if (loop_duration > m_refresh_period * (1.0 + m_tunables.overshoot_frac)) {
        // Overshot the refresh period: the sleep ate into the frame budget -
        // back off multiplicatively (the fast lane; the additive branch
        // alone would bleed headroom for many frames).
        m_sleep *= m_tunables.backoff;
    } else {
        m_sleep += m_tunables.gain * (slop - m_tunables.headroom);
    }
    m_sleep = std::clamp(m_sleep, 0.0, m_refresh_period - m_tunables.headroom);
}

auto Slop_servo_pacer::get_sleep() const -> double
{
    return m_sleep;
}

void Slop_servo_pacer::set_refresh_period(const double refresh_period)
{
    m_refresh_period = refresh_period;
    m_sleep          = std::min(m_sleep, m_refresh_period - m_tunables.headroom);
}

} // namespace erhe::frame_pacing
