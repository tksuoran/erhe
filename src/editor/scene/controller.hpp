#pragma once

namespace sample
{

class Controller
{
public:
    Controller();

    Controller(bool linear, bool multiply);

    auto damp() const -> float;

    void set_damp(float value);

    auto max_value() const -> float;

    void set_max_value(float value);

    auto max_delta() const -> float;

    void set_max_delta(float value);

    void update();

    void adjust(float delta);

    void adjust(double delta)
    {
        adjust(static_cast<float>(delta));
    }

    void set_inhibit(bool value);

    auto more() const -> bool;

    void set_more(bool value);

    auto less() const -> bool;

    void set_less(bool value);

    auto stop() const -> bool;

    void set_stop(bool value);

    void clear();

    auto current_value() const -> float;

    void set_damp_mode(bool linear, bool multiply);

    void set_damp_and_max_delta(float damp, float max_delta);

private:
    void dampen();

    bool  m_more           {false};
    bool  m_less           {false};
    bool  m_stop           {false};
    bool  m_active         {false};
    bool  m_inhibit        {false};
    bool  m_dampen_linear  {false};
    bool  m_dampen_multiply{true};
    float m_damp           { 0.950f};
    float m_max_delta      { 0.004f};
    float m_max_value      { 1.000f};
    float m_min_value      {-1.000f};
    float m_current_delta  { 0.000f};
    float m_current_value  { 0.000f};
};

} // namespace sample
