#pragma once

namespace erhe::application
{

enum class Controller_item : unsigned int
{
    less = 0,
    more = 1,
    stop = 2,
};

class Controller
{
public:
    Controller();
    Controller(const bool linear, const bool multiply);

    [[nodiscard]] auto damp         () const -> float;
    [[nodiscard]] auto max_value    () const -> float;
    [[nodiscard]] auto max_delta    () const -> float;
    [[nodiscard]] auto more         () const -> bool;
    [[nodiscard]] auto less         () const -> bool;
    [[nodiscard]] auto stop         () const -> bool;
    [[nodiscard]] auto current_value() const -> float;
    void set_damp              (float value);
    void set_max_value         (float value);
    void set_max_delta         (float value);
    void update                ();
    void adjust                (float delta);
    void adjust                (double delta);
    void set_more              (bool value);
    void set_less              (bool value);
    void set_stop              (bool value);
    void set                   (Controller_item item, bool value);
    void reset                 ();
    void set_damp_mode         (bool linear, bool multiply);
    void set_damp_and_max_delta(float damp, float max_delta);

private:
    void dampen();

    bool  m_more           {false};
    bool  m_less           {false};
    bool  m_stop           {false};
    bool  m_active         {false};
    bool  m_dampen_linear  {false};
    bool  m_dampen_multiply{true};
    float m_damp           { 0.950f};
    float m_max_delta      { 0.004f};
    float m_max_value      { 1.000f};
    float m_min_value      {-1.000f};
    float m_current_delta  { 0.000f};
    float m_current_value  { 0.000f};
};

} // namespace erhe::application
