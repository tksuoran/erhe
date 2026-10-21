#pragma once

#include <string>
#include <vector>

namespace erhe::graphics {

// Present calibration (doc/plans/frame_pacing_metal.md "Present calibration
// at device init"): properties of a presentation path that honors target
// present times, measured at device init with clear-only frames, because
// they depend on the display mode (the macOS compositor on a ProMotion panel
// latches a period earlier than on a fixed-rate display).

enum class Present_calibration_phase : unsigned int {
    grid = 0,         // plain present: the displayed time gives period and phase
    request_window,   // early completion, request at target - parameter
    deadline          // request at the calibrated offset, completion margin = parameter
};

[[nodiscard]] auto c_str(Present_calibration_phase value) -> const char*;

class Present_calibration_sample
{
public:
    Present_calibration_phase phase{Present_calibration_phase::grid};
    double target_time   {0.0}; // seconds, the target vsync (0.0 in the grid phase)
    double parameter     {0.0}; // seconds: request offset before the target, or completion margin (target - GPU end)
    double completion_margin{0.0}; // seconds, target - GPU end
    double presented_time{0.0}; // seconds, 0.0 when not displayed
    int    slot_offset   {0};   // displayed vsync minus target vsync, in periods
};

class Present_calibration
{
public:
    bool        done           {false}; // the calibration ran (with or without a result)
    bool        valid          {false}; // request_offset and deadline_offset are measured
    std::string status;                 // why not valid, or a one-line summary
    double      period         {0.0};   // seconds, from the grid phase
    double      request_offset {0.0};   // seconds before the target vsync to request the present
    double      request_min    {0.0};   // on-target request offsets measured: [request_min, request_max]
    double      request_max    {0.0};
    double      deadline_offset{0.0};   // seconds: completion margin a frame needs to display at its target
    // GPU pre-warm kernel (doc/plans/frame_pacing_metal.md, claim C26): its
    // measured duration per iteration; 0.0 = no pre-warm on this path. The
    // fill that raises the clock is learned online by the frame pacer.
    // Measured first, independent of valid: the kernel rate does not depend
    // on the compositor properties.
    double      prewarm_seconds_per_iteration{0.0};
    std::vector<Present_calibration_sample> samples;
};

} // namespace erhe::graphics
