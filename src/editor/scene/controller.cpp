#include "scene/controller.hpp"

namespace editor
{

auto Controller::damp() const -> float
{
    return m_damp;
}

void Controller::set_damp(const float value)
{
    m_damp = value;
}

auto Controller::max_value() const -> float
{
    return m_max_value;
}

void Controller::set_max_value(const float value)
{
    m_max_value = value;
}

auto Controller::max_delta() const -> float
{
    return m_max_delta;
}

void Controller::set_max_delta(const float value)
{
    m_max_delta = value;
}

void Controller::update()
{
    if (m_active /*&& m_focus*/)
    {
        adjust(m_current_delta);
    }

    dampen();
}

void Controller::adjust(const float delta)
{
    m_current_value += delta;
    if (m_current_value > m_max_value)
    {
        m_current_value = m_max_value;
    }
    else if (m_current_value < m_min_value)
    {
        m_current_value = m_min_value;
    }
}

void Controller::adjust(const double delta)
{
    adjust(static_cast<float>(delta));
}

void Controller::dampen()
{
    // Dampening by multiplying by a constant
    if (m_dampen_multiply)
    {
        const float old_value = m_current_value;
        m_current_value = m_current_value * m_damp;

        if (m_current_value == old_value)
        {
            m_current_value = 0.0f;
        }
    }
    else if (m_dampen_linear && !m_active) // Constant velocity dampening
    {
        if (m_current_value > m_max_delta)
        {
            m_current_value -= m_max_delta;
            if (m_current_value < m_max_delta)
            {
                m_current_value = 0.0f;
            }
        }
        else if (m_current_value < -m_max_delta)
        {
            m_current_value += m_max_delta;
            if (m_current_value > -m_max_delta)
            {
                m_current_value = 0.0f;
            }
        }
        else // Close to 0.0
        {
            const float old_value = m_current_value;
            m_current_value *= m_damp;
            if (m_current_value == old_value)
            {
                m_current_value = 0.0f;
            }
        }
    }
}

//void Controller::set_focus(const bool focus)
//{
//    m_focus = focus;
//}

auto Controller::more() const -> bool
{
    return m_more;
}

void Controller::set_more(const bool value)
{
    m_more = value;
    if (m_more)
    {
        m_active        = true;
        m_current_delta = m_max_delta;
    }
    else
    {
        if (m_less)
        {
            m_current_delta = -m_max_delta;
        }
        else
        {
            m_active        = false;
            m_current_delta = 0.0f;
        }
    }
}

auto Controller::less() const -> bool
{
    return m_less;
}

void Controller::set_less(const bool value)
{
    m_less = value;
    if (m_less)
    {
        m_active = true;
        m_current_delta = -m_max_delta;
    }
    else
    {
        if (m_more)
        {
            m_current_delta = m_max_delta;
        }
        else
        {
            m_active = false;
            m_current_delta = 0.0f;
        }
    }
}

auto Controller::stop() const -> bool
{
    return m_stop;
}

void Controller::set(const Controller_item item, const bool value)
{
    switch (item)
    {
    case Controller_item::less: set_less(value); break;
    case Controller_item::more: set_more(value); break;
    case Controller_item::stop: set_stop(value); break;
    }
}

void Controller::set_stop(const bool value)
{
    m_stop = value;
    if (m_stop)
    {
        if (m_current_value > 0.0f)
        {
            m_current_delta = -m_max_delta;
        }
        else if (m_current_value < 0.0f)
        {
            m_current_delta = m_max_delta;
        }
    }
    else
    {
        if (m_less && !m_more)
        {
            m_current_delta = -m_max_delta;
        }
        else if (!m_less && m_more)
        {
            m_current_delta = m_max_delta;
        }
    }
}

Controller::Controller()
{
    clear();
}

Controller::Controller(const bool linear, const bool multiply)
{
    clear();
    set_damp_mode(linear, multiply);
}

void Controller::clear()
{
    m_damp            =  0.950f;
    m_max_delta       =  0.004f;
    m_max_value       =  1.000f;
    m_min_value       = -1.000f;
    m_current_value   =  0.0f;
    m_current_delta   =  0.0f;
    m_more            = false;
    m_less            = false;
    m_stop            = false;
    m_active          = false;
    m_dampen_linear   = false;
    m_dampen_multiply = true;
}

auto Controller::current_value() const -> float
{
    return m_current_value;
}

void Controller::set_damp_mode(const bool linear, const bool multiply)
{
    m_dampen_linear   = linear;
    m_dampen_multiply = multiply;
}

void Controller::set_damp_and_max_delta(const float damp, const float max_delta)
{
    m_damp      = damp;
    m_max_delta = max_delta;
}

} // namespace editor
