#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace erhe::math {

enum class Input_axis_control : unsigned int {
    less = 0,
    more = 1,
    stop = 2,
};

class Input_axis_segment
{
public:
    double velocity   {0.0};
    double translation{0.0};
};

class Input_axis
{
public:
    explicit Input_axis(std::string_view name);

    [[nodiscard]] auto get_name              () const -> std::string_view;
    [[nodiscard]] auto get_power_base        () const -> float;
    [[nodiscard]] auto get_more              () const -> bool;
    [[nodiscard]] auto get_less              () const -> bool;
    [[nodiscard]] auto get_value             () const -> float;
    [[nodiscard]] auto get_velocity          () const -> float;
    [[nodiscard]] auto get_tick_distance     () const -> float;
    [[nodiscard]] auto get_base_velocity     () const -> float;
    [[nodiscard]] auto get_segment_timestamp (std::size_t i) const -> std::chrono::steady_clock::time_point;
    [[nodiscard]] auto get_segment_velocity  (std::size_t i) const -> float;
    [[nodiscard]] auto get_segment_distance  (std::size_t i) const -> float;
    [[nodiscard]] auto get_segment_state_time(std::size_t i) const -> float;
    [[nodiscard]] auto evaluate_velocity_at_state_time(float state_time) const -> float;

    void set_power_base(float base);
    void on_frame_begin();
    void on_frame_end  ();
    void tick          (std::chrono::steady_clock::time_point timestamp);
    void update        (std::chrono::steady_clock::time_point timestamp);
    void adjust        (std::chrono::steady_clock::time_point timestamp, float delta);
    void adjust        (std::chrono::steady_clock::time_point timestamp, double delta);
    void set_more      (std::chrono::steady_clock::time_point timestamp, bool value);
    void set_less      (std::chrono::steady_clock::time_point timestamp, bool value);
    void set           (std::chrono::steady_clock::time_point timestamp, Input_axis_control item, bool value);
    void reset         ();

private:
    static auto sign       (double a) -> double;
    static auto checked_pow(double base, double exponent) -> double;
    static auto checked_log(double a) -> double;

    void set_direction(double value);
    bool   m_more         {false};
    bool   m_less         {false};
    double m_base         {4.0};
    double m_log_base     {4.0};
    double m_direction    {0.0};
    double m_state_time   {0.0};
    double m_base_velocity{0.0}; // used for velocity calculation when decelerating and when state time is zero when acceleratng
    double m_velocity     {0.0}; // velocity at end of last segment
    double m_tick_distance{0.0}; // distance travelled during last tick
    std::array<std::chrono::steady_clock::time_point, 2> m_segment_timestamp;
    double                                               m_segment_base_velocity = 0.0;
    double                                               m_segment_direction     = 0.0;
    std::array<double, 2>                                m_segment_velocity      = {0.0, 0.0};
    std::array<double, 2>                                m_segment_distance      = {0.0, 0.0};
    std::array<double, 2>                                m_segment_state_time    = {0.0, 0.0};
    std::optional<std::chrono::steady_clock::time_point> m_last_timestamp;
    std::string m_name;
};

} // namespace erhe::math
