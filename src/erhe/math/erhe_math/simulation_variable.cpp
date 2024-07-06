#include "erhe_math/simulation_variable.hpp"

namespace erhe::math {

Simulation_variable::Simulation_variable()
{
    reset();
}

Simulation_variable::Simulation_variable(const bool linear, const bool multiply)
{
    reset();
    set_damp_mode(linear, multiply);
}

void Simulation_variable::reset()
{
    m_current_value = 0.0f;
    m_current_delta = 0.0f;
    m_more          = false;
    m_less          = false;
    m_stop          = false;
    m_active        = false;
}

auto Simulation_variable::damp() const -> float
{
    return m_damp;
}

void Simulation_variable::set_damp(const float value)
{
    m_damp = value;
}

auto Simulation_variable::max_value() const -> float
{
    return m_max_value;
}

void Simulation_variable::set_max_value(const float value)
{
    m_max_value = value;
}

auto Simulation_variable::max_delta() const -> float
{
    return m_max_delta;
}

void Simulation_variable::set_max_delta(const float value)
{
    m_max_delta = value;
}

void Simulation_variable::update()
{
    if (m_active) {
        adjust(m_current_delta);
    }

    dampen();
}

void Simulation_variable::adjust(const float delta)
{
    m_current_value += delta;
    if (m_current_value > m_max_value) {
        m_current_value = m_max_value;
    } else if (m_current_value < m_min_value) {
        m_current_value = m_min_value;
    }
}

void Simulation_variable::adjust(const double delta)
{
    adjust(static_cast<float>(delta));
}

void Simulation_variable::dampen()
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

auto Simulation_variable::more() const -> bool
{
    return m_more;
}

void Simulation_variable::set_more(const bool value)
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

auto Simulation_variable::less() const -> bool
{
    return m_less;
}

void Simulation_variable::set_less(const bool value)
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

auto Simulation_variable::stop() const -> bool
{
    return m_stop;
}

void Simulation_variable::set(
    const Simulation_variable_control control,
    const bool                        value
)
{
    switch (control) {
        //using enum Simulation_variable_item;
        case Simulation_variable_control::less: set_less(value); break;
        case Simulation_variable_control::more: set_more(value); break;
        case Simulation_variable_control::stop: set_stop(value); break;
        default: break;
    }
}

void Simulation_variable::set_stop(const bool value)
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

auto Simulation_variable::current_value() const -> float
{
    return m_current_value;
}

void Simulation_variable::set_damp_mode(const bool linear, const bool multiply)
{
    m_dampen_linear   = linear;
    m_dampen_multiply = multiply;
}

void Simulation_variable::set_damp_and_max_delta(const float damp, const float max_delta)
{
    m_damp      = damp;
    m_max_delta = max_delta;
}

} // namespace erhe::math
