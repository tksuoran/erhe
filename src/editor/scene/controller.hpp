#pragma once

namespace editor
{

class Controller
{
public:
    Controller();

    Controller(const bool linear, const bool multiply);

    auto damp() const -> float;

    void set_damp(const float value);

    auto max_value() const -> float;

    void set_max_value(const float value);

    auto max_delta() const -> float;

    void set_max_delta(const float value);

    void update();

    void adjust(const float delta);

    void adjust(const double delta)
    {
        adjust(static_cast<float>(delta));
    }

    void set_inhibit(const bool value);

    auto more() const -> bool;

    void set_more(const bool value);

    auto less() const -> bool;

    void set_less(const bool value);

    auto stop() const -> bool;

    void set_stop(const bool value);

    void clear();

    auto current_value() const -> float;

    void set_damp_mode(const bool linear, const bool multiply);

    void set_damp_and_max_delta(const float damp, const float max_delta);

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

} // namespace editor
