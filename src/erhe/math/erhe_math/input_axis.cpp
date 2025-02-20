#include "erhe_math/input_axis.hpp"

namespace erhe::math {

Input_axis::Input_axis()
{
    reset();
}

Input_axis::Input_axis(const bool linear, const bool multiply)
{
    reset();
    set_damp_mode(linear, multiply);
}

void Input_axis::reset()
{
    m_current_value = 0.0f;
    m_current_delta = 0.0f;
    m_more          = false;
    m_less          = false;
    m_stop          = false;
    m_active        = false;
}

auto Input_axis::damp() const -> float
{
    return m_damp;
}

void Input_axis::set_damp(const float value)
{
    m_damp = value;
}

auto Input_axis::max_value() const -> float
{
    return m_max_value;
}

void Input_axis::set_max_value(const float value)
{
    m_max_value = value;
}

auto Input_axis::max_delta() const -> float
{
    return m_max_delta;
}

void Input_axis::set_max_delta(const float value)
{
    m_max_delta = value;
}

void Input_axis::update()
{
    if (m_active) {
        adjust(m_current_delta);
    }

    dampen();
}

void Input_axis::adjust(const float delta)
{
    m_current_value += delta;
    if (m_current_value > m_max_value) {
        m_current_value = m_max_value;
    } else if (m_current_value < m_min_value) {
        m_current_value = m_min_value;
    }
}

void Input_axis::adjust(const double delta)
{
    adjust(static_cast<float>(delta));
}

void Input_axis::dampen()
{
    // Dampening by multiplying by a constant
    if (m_dampen_multiply) {
        const float old_value = m_current_value;
        m_current_value = m_current_value * m_damp;

        if (m_current_value == old_value) {
            m_current_value = 0.0f;
        }
    } else if (m_dampen_linear && !m_active) { // Constant velocity dampening
        if (m_current_value > m_max_delta) {
            m_current_value -= m_max_delta;
            if (m_current_value < m_max_delta) {
                m_current_value = 0.0f;
            }
        } else if (m_current_value < -m_max_delta) {
            m_current_value += m_max_delta;
            if (m_current_value > -m_max_delta) {
                m_current_value = 0.0f;
            }
        } else { // Close to 0.0
            const float old_value = m_current_value;
            m_current_value *= m_damp;
            if (m_current_value == old_value) {
                m_current_value = 0.0f;
            }
        }
    }
}

auto Input_axis::more() const -> bool
{
    return m_more;
}

void Input_axis::set_more(const bool value)
{
    m_more = value;
    if (m_more) {
        m_active        = true;
        m_current_delta = m_max_delta;
    } else {
        if (m_less) {
            m_current_delta = -m_max_delta;
        } else {
            m_active        = false;
            m_current_delta = 0.0f;
        }
    }
}

auto Input_axis::less() const -> bool
{
    return m_less;
}

void Input_axis::set_less(const bool value)
{
    m_less = value;
    if (m_less) {
        m_active = true;
        m_current_delta = -m_max_delta;
    } else {
        if (m_more) {
            m_current_delta = m_max_delta;
        } else {
            m_active = false;
            m_current_delta = 0.0f;
        }
    }
}

auto Input_axis::stop() const -> bool
{
    return m_stop;
}

void Input_axis::set(const Input_axis_control control, const bool value)
{
    switch (control) {
        //using enum Simulation_variable_item;
        case Input_axis_control::less: set_less(value); break;
        case Input_axis_control::more: set_more(value); break;
        case Input_axis_control::stop: set_stop(value); break;
        default: break;
    }
}

void Input_axis::set_stop(const bool value)
{
    m_stop = value;
    if (m_stop) {
        if (m_current_value > 0.0f) {
            m_current_delta = -m_max_delta;
        } else if (m_current_value < 0.0f) {
            m_current_delta = m_max_delta;
        }
    } else {
        if (m_less && !m_more) {
            m_current_delta = -m_max_delta;
        } else if (!m_less && m_more) {
            m_current_delta = m_max_delta;
        }
    }
}

auto Input_axis::current_value() const -> float
{
    return m_current_value;
}

void Input_axis::set_damp_mode(const bool linear, const bool multiply)
{
    m_dampen_linear   = linear;
    m_dampen_multiply = multiply;
}

void Input_axis::set_damp_and_max_delta(const float damp, const float max_delta)
{
    m_damp      = damp;
    m_max_delta = max_delta;
}

} // namespace erhe::math
