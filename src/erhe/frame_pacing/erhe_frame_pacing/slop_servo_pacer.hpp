#pragma once

// Slop-servo fallback pacer (capability tier S, implementation plan P4.1).
//
// The Games-by-Mason FramePacer.zig method (see
// doc/frame_pacing_capability_tiers.md section 2): no presentation-timing
// extensions, no vsync grid, no cadence - sense BACKPRESSURE instead of
// display times. Per frame, measure "slop" (time the CPU spent involuntarily
// blocked on the GPU fence / swapchain acquire; erhe's frame records already
// isolate this as the involuntary-wait subtraction behind cpu_service_time)
// and servo a sleep inserted before input polling: additive correction toward
// a static headroom of residual blocking, multiplicative back-off when the
// loop overshoots the refresh period, clamped to [0, period - headroom].
// Equilibrium: residual blocking ~= headroom - the servo self-tunes toward
// the vblank deadline without ever knowing where it is.
//
// Hard requirement: plain-FIFO backpressure (plain VK_PRESENT_MODE_FIFO_KHR).
// On fifo_latest_ready superseded images drop instead of blocking, slop reads
// ~0 and the servo winds to nothing (verified: claim C21a). Documented
// failure mode: heavy-tailed load oscillates - every tail frame overshoots
// the loop period and collapses the sleep (claim C21b); the full Frame_pacer
// (tier W) absorbs the same load through its quantile budget.
//
// Like Frame_pacer, this class is pure: no OS / clock / GPU access, all
// times arrive as arguments in one monotonic clock domain (seconds).
// scripts/frame_pacing_sim.py class SlopServo is the reference model; keep
// them in sync (parity claims C19..C21).

namespace erhe::frame_pacing {

class Slop_servo_tunables
{
public:
    double headroom      {0.001}; // target residual blocking per frame [s]
    double gain          {0.1};   // additive sleep correction per unit slop error
    double backoff       {0.5};   // multiplicative sleep back-off on overshoot
    double overshoot_frac{0.10};  // loop duration > period * (1 + this) = overshoot
};

class Slop_servo_pacer
{
public:
    Slop_servo_pacer(const Slop_servo_tunables& tunables, double refresh_period);

    // Per-frame servo update. slop = involuntary blocking measured this
    // frame; loop_duration = decision-to-decision loop time (sleep + work +
    // blocking).
    void update(double slop, double loop_duration);

    // Sleep to insert before input polling for the next frame.
    [[nodiscard]] auto get_sleep() const -> double;

    // Display change (mode set, monitor move). Resets nothing else: the
    // servo re-converges from the current sleep.
    void set_refresh_period(double refresh_period);

private:
    Slop_servo_tunables m_tunables;
    double              m_refresh_period;
    double              m_sleep{0.0};
};

} // namespace erhe::frame_pacing
