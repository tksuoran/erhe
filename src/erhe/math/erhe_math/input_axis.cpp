#include "erhe_math/input_axis.hpp"
#include "erhe_math/math_log.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::math {

// https://graphtoy.com/?f1(x,t)=1%20-%20pow(1.5,%20-x)&v1=true&f2(x,t)=1%20-%20pow(2,%20-x)&v2=true&f3(x,t)=1%20-%20pow(4,%20-x)&v3=true&f4(x,t)=1%20-%20pow(8%20-x)&v4=true&f5(x,t)=1%20-%20pow(16,%20-x)&v5=false&f6(x,t)=1%20-%20pow(32,%20-x)&v6=true&grid=1&coords=2.1583584911582627,-0.01832978696472251,2.3741360268016223
// https://www.wolframalpha.com/input?i=solve+v+%3D+1+-+pow%28a%2C+-t%29+for+t

Input_axis::Input_axis(std::string_view name)
    : m_name{name}
{
    reset();
}

auto Input_axis::sign(double a) -> double
{
    if (a == 0.0) {
        return 0.0;
    }
    return a < 0.0 ? -1.0 : 1.0;
}

auto Input_axis::checked_pow(double base, double exponent) -> double
{
    ERHE_VERIFY(base > 0.0);
    if (exponent == 0.0) {
        return 1.0;
    }
    ERHE_VERIFY(std::isfinite(base));
    ERHE_VERIFY(std::isfinite(exponent));
    double result = std::pow(base, exponent);
    ERHE_VERIFY(std::isfinite(result));
    return result;
}
auto Input_axis::checked_log(double a) -> double
{
    ERHE_VERIFY(std::isfinite(a));
    if (a <= 0.0) {
        return 0.0; // TODO Fix
    }
    double result = std::log(a);
    ERHE_VERIFY(std::isfinite(result));
    return result;
}

void Input_axis::reset()
{
    m_more                  = false;
    m_less                  = false;
    m_velocity              = 0.0;
    m_tick_distance         = 0.0;
    m_segment_velocity[0]   = 0.0;
    m_segment_velocity[1]   = 0.0;
    m_segment_distance[0]   = 0.0;
    m_segment_distance[1]   = 0.0;
    m_segment_state_time[0] = 0.0;
    m_segment_state_time[1] = 0.0;
    m_tick_distance         = 0.0;
    m_direction             = 0.0;
    m_state_time            = 0.0;
    m_base_velocity         = 0.0;
}

auto Input_axis::get_name() const -> std::string_view
{
    return m_name;
}

auto Input_axis::get_power_base() const -> float
{
    return static_cast<float>(m_base);
}

auto Input_axis::get_more() const -> bool
{
    return m_more;
}

auto Input_axis::get_less() const -> bool
{
    return m_less;
}

void Input_axis::set_power_base(const float value)
{
    m_base = value;
    m_log_base = value == 0.0f ? 0.0 : checked_log(value);
}

void Input_axis::adjust(std::chrono::steady_clock::time_point timestamp, const float delta)
{
    adjust(timestamp, static_cast<double>(delta));
}

void Input_axis::adjust(std::chrono::steady_clock::time_point timestamp, const double delta)
{
    // TODO Consider segments
    update(timestamp);
    m_base_velocity = m_velocity + delta;
    m_velocity = m_base_velocity;
    m_direction = 0.0;
    m_state_time = 0.0;
    // TODO consider current m_direction != 0.0
    //if (m_direction == 0.0) {
    //    m_state_time = 0.0;
    //} else {
    //    // t(v) = log(1 / (1 - v)) / log(a)
    //    m_state_time = checked_log(1.0 / (1.0 - m_velocity)) / m_log_base;
    //    if (!std::isfinite(m_state_time)) {
    //        m_state_time = 65536.0; // TODO hack
    //        log_input_axis->warn("state time clamped for {} in adjust()", m_name);
    //    }
    //}
}

void Input_axis::tick(std::chrono::steady_clock::time_point timestamp)
{
    update(timestamp);
}

void Input_axis::on_frame_begin()
{
    ERHE_VERIFY(m_segment_velocity[0]   == 0.0);
    ERHE_VERIFY(m_segment_velocity[1]   == 0.0);
    ERHE_VERIFY(m_segment_distance[0]   == 0.0);
    ERHE_VERIFY(m_segment_distance[1]   == 0.0);
    ERHE_VERIFY(m_segment_state_time[0] == 0.0);
    ERHE_VERIFY(m_segment_state_time[1] == 0.0);
    ERHE_VERIFY(m_tick_distance         == 0.0);
}

void Input_axis::on_frame_end()
{
    m_segment_velocity[0]   = 0.0;
    m_segment_velocity[1]   = 0.0;
    m_segment_distance[0]   = 0.0;
    m_segment_distance[1]   = 0.0;
    m_segment_state_time[0] = 0.0;
    m_segment_state_time[1] = 0.0;
    m_tick_distance         = 0.0;
}

auto Input_axis::get_value() const -> float
{
    return static_cast<float>(m_velocity);
}

auto Input_axis::get_velocity() const -> float
{
    return static_cast<float>(m_velocity);
}

auto Input_axis::get_segment_timestamp(std::size_t i) const -> std::chrono::steady_clock::time_point
{
    return m_segment_timestamp.at(i);
}

auto Input_axis::get_segment_velocity(std::size_t i) const -> float
{
    return static_cast<float>(m_segment_velocity.at(i));
}

auto Input_axis::get_segment_distance(std::size_t i) const -> float
{
    return static_cast<float>(m_segment_distance.at(i));
}

auto Input_axis::get_segment_state_time(std::size_t i) const -> float
{
    return static_cast<float>(m_segment_state_time.at(i));
}

auto Input_axis::get_tick_distance() const -> float
{
    return static_cast<float>(m_tick_distance);
}

auto Input_axis::get_base_velocity() const -> float
{
    return static_cast<float>(m_base_velocity);
}

// When accelerating (more is pressed, or less is pressed, or both are pressed)
// 
// v(t) = 1 - pow(a, -t)
// s(t) = pow(a, -t) / log(a) + t
// t(v) = log(1 / (1 - v)) / log(a)
//
// When decelerating (neither more or less is active)
//
// v(t) = v0 * pow(a, -t)
// s(t) = -v0 * pow(a, -t) / log(a)
// t(v) = log(v0/v) / log(a)
void Input_axis::update(std::chrono::steady_clock::time_point timestamp)
{
    if (m_log_base == 0.0) {
        return;
    }
    if (!m_last_timestamp.has_value() || (timestamp == m_last_timestamp.value())) {
        m_last_timestamp = timestamp;
        return;
    }
    const std::chrono::duration<double> duration = timestamp - m_last_timestamp.value();
    double dt = duration.count();
    m_segment_base_velocity = m_base_velocity;
    m_segment_direction     = m_direction;
    m_segment_timestamp[0]  = m_last_timestamp.value();
    m_segment_timestamp[1]  = timestamp;
    m_segment_state_time[0] = m_state_time;
    m_segment_state_time[1] = m_state_time + dt;
    ERHE_VERIFY(dt >= 0.0);
    m_state_time += dt;
    if (m_direction == 0.0) {
        m_segment_velocity[0] =  m_base_velocity * checked_pow(m_base, -m_segment_state_time[0]);
        m_segment_velocity[1] =  m_base_velocity * checked_pow(m_base, -m_segment_state_time[1]);
        m_segment_distance[0] = -m_base_velocity * checked_pow(m_base, -m_segment_state_time[0]) / m_log_base;
        m_segment_distance[1] = -m_base_velocity * checked_pow(m_base, -m_segment_state_time[1]) / m_log_base;
        m_tick_distance += m_segment_distance[1] - m_segment_distance[0];
    } else {
        m_segment_velocity[0] = m_direction * (1.0 - checked_pow(m_base, -m_segment_state_time[0]));
        m_segment_velocity[1] = m_direction * (1.0 - checked_pow(m_base, -m_segment_state_time[1]));
        m_segment_distance[0] = m_direction * (checked_pow(m_base, -m_segment_state_time[0]) / m_log_base + m_segment_state_time[0]);
        m_segment_distance[1] = m_direction * (checked_pow(m_base, -m_segment_state_time[1]) / m_log_base + m_segment_state_time[1]);
        m_tick_distance += m_segment_distance[1] - m_segment_distance[0]; 
    }
    m_velocity = m_segment_velocity[1];
    m_last_timestamp = timestamp;
}

auto Input_axis::evaluate_velocity_at_state_time(float state_time) const -> float
{
    if (m_segment_direction == 0.0) {
        return static_cast<float>(m_segment_base_velocity * checked_pow(m_base, -state_time));
    } else {
        return static_cast<float>(m_segment_direction * (1.0 - checked_pow(m_base, -state_time)));
    }
}

void Input_axis::set_direction(double direction)
{
    ERHE_VERIFY(m_log_base != 0.0);
    double old_base_velocity = m_base_velocity;
    double old_velocity      = m_velocity;
    double old_state_time    = m_state_time;
    double old_direction     = m_direction;
    if (m_direction == direction) {
        log_input_axis->warn("{} set_direction {} - redundant", m_name, direction);
        return;
    }
    if (sign(direction) * sign(m_velocity) == -1.0) {
        log_input_axis->info("{} reverse direction -> instant velocity to 0.0", m_name);
        m_velocity = 0.0;
    }
    m_base_velocity = m_velocity;
    m_direction = direction;
    if (direction != 0.0) {
        // t(v) = log(1 / (1 - v)) / log(a)
        //double abs_v = (m_direction < 0) ? -m_velocity : m_velocity;
        m_state_time = checked_log(1.0 / (1.0 - m_velocity)) / m_log_base;
        if (!std::isfinite(m_state_time)) { // Can happen if m_tick_velocity is very close to 1.0
            m_state_time = 65536.0; // Some "large" value
            log_input_axis->warn("state time clamped for {} set_direction {} -> {}, v0 {} -> {}, v {} -> {}, t {} -> {}",
                m_name,
                old_direction,     m_direction,
                old_base_velocity, m_base_velocity,
                old_velocity,      m_velocity,
                old_state_time,    m_state_time
            );
        }
    } else {
        m_state_time = 0.0f;
        //m_tick_distance = 0.0f;
    }
    log_input_axis->info("{} set_direction {} -> {}, v0 {} -> {}, v {} -> {}, state time {} -> {}",
        m_name, 
        old_direction,     m_direction,
        old_base_velocity, m_base_velocity,
        old_velocity,      m_velocity,
        old_state_time,    m_state_time
    );
}

void Input_axis::set_more(std::chrono::steady_clock::time_point timestamp, const bool value)
{
    ERHE_VERIFY(m_log_base != 0.0);
    log_input_axis->info("{} begin set_more {}", m_name, value ? "true" : "false");
    update(timestamp);
    m_more = value;
    if (m_more) {
        set_direction(1.0);
    } else if (m_less) {
        set_direction(-1.0);
    } else {
        set_direction(0.0);
    }
    log_input_axis->info("{} end set_more {}", m_name, value ? "true" : "false");
}

void Input_axis::set_less(std::chrono::steady_clock::time_point timestamp, const bool value)
{
    ERHE_VERIFY(m_log_base != 0.0);
    log_input_axis->info("{} begin set_less {}", m_name, value ? "true" : "false");
    update(timestamp);
    m_less = value;
    if (m_less) {
        set_direction(-1.0);
    } else if (m_more) {
        set_direction(1.0);
    } else {
        set_direction(0.0);
    }
    log_input_axis->info("{} end set_less {}", m_name, value ? "true" : "false");
}

void Input_axis::set(std::chrono::steady_clock::time_point timestamp, const Input_axis_control control, const bool value)
{
    ERHE_VERIFY(m_log_base != 0.0);
    switch (control) {
        //using enum Input_axis_item;
        case Input_axis_control::less: set_less(timestamp, value); break;
        case Input_axis_control::more: set_more(timestamp, value); break;
        default: break;
    }
}

} // namespace erhe::math
