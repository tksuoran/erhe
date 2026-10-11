#pragma once

#include <cstdint>

namespace erhe::xr {

// The result of one xrWaitFrame (Headset::wait_frame()) and of the matching
// xrBeginFrame. Plain data with no OpenXR types, so it is available in every
// ERHE_XR_LIBRARY configuration and can travel between threads in a frame
// packet (doc/editor/render_thread.md section 2.2).
class Frame_timing
{
public:
    uint64_t predicted_display_time  {0};
    uint64_t predicted_display_pediod{0};
    bool     wait_ok                 {false}; // set by Headset::wait_frame()
    bool     begin_ok                {false}; // set by the caller from Headset::begin_frame()
    bool     should_render           {false};
};

} // namespace erhe::xr
