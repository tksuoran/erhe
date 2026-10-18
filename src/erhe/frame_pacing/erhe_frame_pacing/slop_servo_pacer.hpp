#pragma once

// Slop-servo fallback pacer (capability tier S, implementation plan P4.1).
//
// The Games-by-Mason FramePacer.zig method (see
// doc/frame_pacing/capability_tiers.md section 2): no presentation-timing
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
// Cadence: with FIFO backpressure a load heavier than one refresh period
// settles on a longer loop period (a GPU-bound frame of 2.6 periods
// displays every third vblank: a loop of 3 T). The servo tracks the loop
// period it observes when no sleep of its own can be blamed (EWMA) and uses
// that cadence period as the overshoot reference and the sleep clamp:
// measured against T alone every frame of such a load reads as an overshoot
// and the servo is inert exactly where the blocking it should convert into
// sleep is largest. The estimate is the loop period itself, not a rounded
// vsync count, so a load that is GPU-bound without a whole-period lock
// (IMMEDIATE present, partial locks) is paced the same way. A frame that
// blocked for more than the headroom had its sleep absorbed by the
// backpressure, so its loop period is the plant's and is tracked; a frame
// that did not block ran sleep plus work, which only bounds the estimate
// from above, and an overshoot in such a frame is the sleep's fault.
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
    double overshoot_frac{0.10};  // loop duration > cadence period * (1 + this) = overshoot
    double cadence_smoothing{0.1}; // weight of one loop period in the cadence estimate
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

    // Refresh periods per frame the loop is settled on, rounded (>= 1).
    [[nodiscard]] auto get_cadence() const -> int;

    // The loop period the servo paces against: the tracked plant loop
    // period, at least one refresh period.
    [[nodiscard]] auto get_cadence_period() const -> double;

    // Display change (mode set, monitor move); a call with the current
    // period is a no-op. Restarts the cadence estimate and clamps the
    // sleep to the new grid: the servo re-converges from there.
    void set_refresh_period(double refresh_period);

private:
    void track_cadence(double loop_duration);

    // A sleep below this is zero: below the release timer's resolution
    // (a wait of a few microseconds measured ~0.5 ms).
    static constexpr double s_min_sleep{0.0005};

    Slop_servo_tunables m_tunables;
    double              m_refresh_period;
    double              m_sleep              {0.0};
    double              m_loop_period_estimate{0.0}; // EWMA of the loop periods the plant produced
};

} // namespace erhe::frame_pacing
