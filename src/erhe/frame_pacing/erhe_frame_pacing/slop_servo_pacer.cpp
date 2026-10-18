#include "erhe_frame_pacing/slop_servo_pacer.hpp"

#include <algorithm>
#include <cmath>

namespace erhe::frame_pacing {

Slop_servo_pacer::Slop_servo_pacer(const Slop_servo_tunables& tunables, const double refresh_period)
    : m_tunables      {tunables}
    , m_refresh_period{refresh_period}
{
}

void Slop_servo_pacer::update(const double slop, const double loop_duration)
{
    // Backpressure still absorbed the sleep when the loop blocked for more
    // than the headroom: the loop period is then the plant's (display or
    // GPU bound) whatever the sleep, and it enters the cadence estimate.
    // A loop that did not block is sleep plus work, longer than the plant
    // period, so it only bounds the estimate from above.
    const bool bound = slop > m_tunables.headroom;
    if (bound) {
        track_cadence(loop_duration);
    } else if (m_loop_period_estimate > loop_duration) {
        m_loop_period_estimate = loop_duration;
    }
    const bool overshoot = loop_duration > (get_cadence_period() * (1.0 + m_tunables.overshoot_frac));
    if (overshoot && !bound && (m_sleep > 0.0)) {
        // Overshot the cadence period without blocking: the sleep ate into
        // the frame budget - back off multiplicatively (the fast lane; the
        // additive branch alone would bleed headroom for many frames).
        m_sleep *= m_tunables.backoff;
    } else {
        m_sleep += m_tunables.gain * (slop - m_tunables.headroom);
    }
    m_sleep = std::clamp(m_sleep, 0.0, get_cadence_period() - m_tunables.headroom);
    if (m_sleep < s_min_sleep) {
        // Below the release timer's resolution: a wait would cost more than
        // it sleeps.
        m_sleep = 0.0;
    }
}

void Slop_servo_pacer::track_cadence(const double loop_duration)
{
    if (m_loop_period_estimate <= 0.0) {
        m_loop_period_estimate = loop_duration;
    } else {
        m_loop_period_estimate += m_tunables.cadence_smoothing * (loop_duration - m_loop_period_estimate);
    }
}

auto Slop_servo_pacer::get_sleep() const -> double
{
    return m_sleep;
}

auto Slop_servo_pacer::get_cadence() const -> int
{
    const double periods = (m_refresh_period > 0.0) ? (get_cadence_period() / m_refresh_period) : 1.0;
    return std::clamp(static_cast<int>(std::lround(periods)), 1, 8);
}

auto Slop_servo_pacer::get_cadence_period() const -> double
{
    // The loop period the plant produced, never less than one refresh
    // period: a loop faster than the display is paced by the display.
    return std::max(m_loop_period_estimate, m_refresh_period);
}

void Slop_servo_pacer::set_refresh_period(const double refresh_period)
{
    // Called with the current period every frame by a host that reads it
    // from the device: only a change is a display change.
    if (refresh_period == m_refresh_period) {
        return;
    }
    m_refresh_period       = refresh_period;
    m_loop_period_estimate = 0.0; // re-converge the cadence from the new grid
    m_sleep                = std::min(m_sleep, get_cadence_period() - m_tunables.headroom);
}

} // namespace erhe::frame_pacing
