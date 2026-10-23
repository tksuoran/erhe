#pragma once

#include <cstddef>

namespace erhe::graphics {

class Present_calibration;

// The backend-neutral part of the present calibration (present_calibration.hpp,
// doc/erhe/metal_backend.md "Frame pacing"): the grid fit, the request
// window search and the deadline split over clear-only probes. A backend
// supplies the probes through IPresent_calibration_presenter: Metal presents
// drawables with presentDrawable:atTime: (metal_device_present_calibration.cpp),
// Vulkan presents swapchain images with VK_GOOGLE_display_timing
// desiredPresentTime (vulkan_device_present_calibration.cpp). Times are
// seconds of the Frame_time_recorder clock.

class Present_calibration_probe_feedback
{
public:
    double presented_time{0.0};   // 0.0 when not displayed
    double gpu_end       {0.0};   // the probe's GPU completion
    bool   presented     {false}; // feedback arrived (presented_time 0.0: not displayed)
};

class IPresent_calibration_presenter
{
public:
    virtual ~IPresent_calibration_presenter() noexcept;

    // Presents one clear-only image to be displayed at request_time (0.0: a
    // plain present), its GPU work committed at commit_time (0.0: now).
    // Returns the probe's index (consecutive from 0), or -1 on failure.
    [[nodiscard]] virtual auto present(double request_time, double commit_time) -> std::ptrdiff_t = 0;
    // The probe's feedback once it arrived, or as it stands at the timeout.
    [[nodiscard]] virtual auto wait(std::size_t index, double timeout_seconds) -> Present_calibration_probe_feedback = 0;
    // Called between the grid phase's presents: lets the window system show
    // a window ordered on screen during init (macOS: a Core Animation commit
    // needs the main run loop).
    virtual void service_window() = 0;
    // How many presents may be queued before the oldest one's feedback is
    // due (the swapchain image / drawable pool size).
    [[nodiscard]] virtual auto get_queue_depth() const -> std::size_t = 0;
};

// Runs the grid, request window and deadline phases and fills the result's
// period, request window, deadline offset, samples, valid and status (the
// caller sets done and prewarm_seconds_per_iteration). Returns nullptr when
// valid, else why not (also in status).
[[nodiscard]] auto run_present_calibration(
    IPresent_calibration_presenter& presenter,
    double                          nominal_period,
    Present_calibration&            result
) -> const char*;

// Sleeps to within a millisecond of the time, then spins: probe commit times
// must be accurate to well under the T/8 margin step.
void present_calibration_wait_until(double time);

} // namespace erhe::graphics
