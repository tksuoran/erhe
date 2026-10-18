#pragma once

#include <string>

namespace erhe::window {

// One display as the window system reports it (Context_window::enumerate_displays).
class Display_info
{
public:
    std::string name;
    int         width                 {0};   // current mode, pixels
    int         height                {0};
    float       refresh_rate          {0.0f}; // current mode, Hz; 0 when unknown
    double      refresh_period_seconds{0.0};  // exact, from the mode's numerator / denominator when reported
    bool        primary               {false};
    bool        hosts_window          {false}; // the display the window is on
};

} // namespace erhe::window
