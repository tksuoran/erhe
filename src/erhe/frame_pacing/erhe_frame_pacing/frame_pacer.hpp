#pragma once

// Frame pacer core (implementation plan step P1.1).
//
// Pure, engine-independent port of the verified Python reference model
// scripts/frame_pacing_sim.py. Design documents:
//   doc/frame_pacing_algorithm.md      - algorithm (normative)
//   doc/frame_pacing_control_model.md  - control-theory model, claims C1..C9
//   doc/frame_pacing_inputs.md         - input definitions (service times!)
//
// The pacer never calls the OS, Vulkan, or a clock: all times arrive as
// arguments (seconds, one monotonic clock domain) and all decisions are
// returned as values. This is what makes it unit-testable against a virtual
// clock (test/ replicates the Python plant and claims C1..C9) and portable
// across backends.
//
// Input fidelity contract (doc/frame_pacing_algorithm.md section 1):
// - cpu_duration / gpu_duration are stage SERVICE times: involuntary waits
//   (pacer wait, fence, acquire) and GPU idle bubbles excluded.
// - on_gpu_done() should be driven by per-frame polling of the GPU frame
//   timestamp bracket; the lazy fence-read path degrades gracefully (C9).

#include <array>
#include <cstdint>
#include <vector>

namespace erhe::frame_pacing {

// All quantitative behavior knobs. Durations are in seconds, fractions are of
// the display refresh period T. Defaults match the verified reference model.
class Frame_pacer_tunables
{
public:
    double      refresh_period    {1.0 / 60.0}; // T
    int         min_vsyncs        {1};          // N_min (application floor)
    int         max_vsyncs        {8};          // N_max (runaway backstop)
    double      quantile_p        {0.95};       // p for window order statistics
    std::size_t history           {120};        // sample window length (frames)
    double      guard             {0.001};      // jitter allowance / near-miss band
    double      hysteresis_frac   {0.10};       // H, fraction of T
    double      upshift_dwell_time{0.5};        // T_up
    int         reactive_miss_threshold{2};     // M consecutive observed self-misses
    double      margin_initial_frac{0.20};      // m0
    double      margin_min_frac   {0.02};
    double      margin_max_frac   {0.50};
    double      margin_attack_frac{0.10};       // additive increase on (near-)miss
    double      margin_decay_frac {0.001};      // decay per frame with ample slack
    int         target_queued_images{1};        // Q*
    std::size_t cold_start_seed_frames{8};      // K
    double      deadline_offset_initial{0.0025};// assumed presentation deadline offset
    double      grid_tracker_gain {0.2};        // vsync grid phase PLL gain
    double      grid_period_gain  {0.01};       // vsync grid period tracker gain (per cycle)
    double      grid_period_max_dev{0.005};     // tracked period clamp, fraction of the reference period
    std::size_t grid_reseed_window{16};         // feedback-delta samples backing the gross-error re-seed
    double      grid_reseed_dev   {0.02};       // re-seed when median delta-derived period deviates this much
};

// Result of Frame_pacer::schedule() for one frame.
class Schedule_decision
{
public:
    std::int64_t frame_id          {0};
    double       release_time      {0.0}; // earliest time app may start per-frame work
    std::int64_t target_slot       {0};   // vsync slot index in the pacer's grid
    double       target_time       {0.0}; // requested presentation time (FR3)
    double       predicted_display {0.0}; // FR4
    double       predicted_duration{0.0}; // FR4 (N * T)
    int          vsyncs_per_frame  {1};   // N used for this frame
    std::int64_t wait_id           {-1};  // FR5: block release until this frame displayed (<0: none)
    double       hold_until        {0.0}; // C15 mitigation: delay the present request to this
                                          // time (0 = none). Computed from the TRACKED grid
                                          // (deviation 12); see deviation 13 / claim C18 for
                                          // why it is deliberately NOT capped to the next
                                          // frame's release point.
};

// Per-frame outcome, exposed for tests and telemetry.
class Frame_stats
{
public:
    bool   valid      {false};
    bool   self_miss  {false};
    double slack      {0.0};
    double w_statistic{0.0};
    double margin     {0.0};
    int    vsyncs     {1};
};

// Fixed-capacity sliding sample window with order-statistic quantile.
// Capacity is allocated once at init; steady state performs no allocations.
class Sample_window
{
public:
    void init(std::size_t capacity);
    void clear();
    void push(double value);
    [[nodiscard]] auto empty   () const -> bool        { return m_count == 0; }
    [[nodiscard]] auto count   () const -> std::size_t { return m_count; }
    [[nodiscard]] auto quantile(double p, double fallback) const -> double;

private:
    std::vector<double>         m_samples; // ring storage
    mutable std::vector<double> m_scratch; // sort scratch for quantile()
    std::size_t                 m_head {0};
    std::size_t                 m_count{0};
};

class Frame_pacer
{
public:
    explicit Frame_pacer(const Frame_pacer_tunables& tunables, double nominal_grid_phase = 0.0);

    // Inner loop (FR2/FR3/FR4/FR5): schedule frame frame_id at current time now.
    [[nodiscard]] auto schedule(std::int64_t frame_id, double now) -> Schedule_decision;

    // Measurement feedback. Durations are service times (see header comment).
    void on_cpu_done        (std::int64_t frame_id, double cpu_duration);
    void on_gpu_done        (std::int64_t frame_id, double gpu_end_time, double cpu_duration, double gpu_duration, double latency);
    void on_present_feedback(std::int64_t frame_id, double achieved_present_time);

    void set_min_vsyncs           (int min_vsyncs);
    void notify_swapchain_recreated(double nominal_grid_phase = 0.0);

    // Introspection (tests, telemetry, overlay). The grid is DYNAMIC
    // (deviation 10): slot_time(j) = grid_phase + j * grid_period, both
    // tracked from presentation feedback. Consumers drawing or predicting
    // vsync boundaries must use this pair, not the nominal refresh period.
    [[nodiscard]] auto get_vsyncs_per_frame() const -> int    { return m_vsyncs; }
    [[nodiscard]] auto get_margin          () const -> double { return m_margin; }
    [[nodiscard]] auto get_grid_phase      () const -> double { return m_grid_phase; }
    [[nodiscard]] auto get_grid_period     () const -> double { return m_grid_period; }
    [[nodiscard]] auto get_frame_stats     (std::int64_t frame_id) const -> Frame_stats;

    // Test hook (claim C6): force latency estimate and margin to given values.
    void set_forced_override(bool enabled, double forced_latency, double forced_margin);

private:
    // Bookkeeping for recently scheduled frames, ring-indexed by frame id.
    class Frame_entry
    {
    public:
        std::int64_t frame_id   {-1};
        std::int64_t target_slot{0};
        int          vsyncs     {1};
        Frame_stats  stats      {};
    };

    [[nodiscard]] auto slot_time            (std::int64_t slot) const -> double;
    [[nodiscard]] auto latency_estimate     () const -> double;
    [[nodiscard]] auto latency_typical      () const -> double;
    [[nodiscard]] auto margin_in_use        () const -> double;
    [[nodiscard]] auto throughput_statistic () const -> double; // W
    [[nodiscard]] auto entry_for            (std::int64_t frame_id) -> Frame_entry&;
    [[nodiscard]] auto find_entry           (std::int64_t frame_id) const -> const Frame_entry*;
    void               change_cadence       (int new_vsyncs, bool is_upshift);
    void               on_pressure_downshift(double gpu_end_time);

    Frame_pacer_tunables m_tunables;

    // Derived absolute values (seconds), computed from tunables at init.
    double m_hysteresis   {0.0}; // H
    double m_margin_min   {0.0};
    double m_margin_max   {0.0};
    double m_margin_attack{0.0};
    double m_margin_decay {0.0};

    // Estimated vsync grid: slot_time(j) = m_grid_phase + j * m_grid_period.
    // The period is tracked from feedback (initialized from tunables); real
    // displays deviate from the queried period (measured 11 ppm, deviation 10).
    double m_grid_phase {0.0};
    double m_grid_period{0.0};
    // Gross-error re-seed (deviation 12, claim C16): the queried nominal
    // period can be grossly wrong (4.2% measured, driver report issue #2).
    // Period samples derived from raw feedback deltas re-seed the period
    // and re-center the PLL clamp (reference = nominal until re-seeded).
    double        m_grid_period_ref{0.0};
    bool          m_has_raw_feedback{false};
    double        m_raw_feedback_previous{0.0};
    Sample_window m_period_sample_window;
    bool         m_has_feedback_slot{false}; // period tracker: previous good feedback
    std::int64_t m_feedback_slot_previous{0};
    double m_deadline_offset{0.0}; // delta_pe estimate

    // Outer loop state.
    int    m_vsyncs      {1};   // N
    int    m_min_vsyncs  {1};   // application-set floor
    double m_dwell       {0.0}; // accumulated upshift headroom time
    int    m_consecutive_misses{0};
    double m_streak_attribution{0.0}; // min over streak of max(c, g); +inf when no streak
    std::int64_t m_last_pressure_frame{-1000000000}; // completed-count of last (near-)miss
    // Upshift backoff: a pressure-reverted upshift doubles the next probe's
    // required dwell (geometric convergence at infeasible-borderline load);
    // a surviving upshift resets it.
    double m_dwell_required  {0.0};
    bool   m_has_upshift_time{false};
    double m_upshift_time    {0.0};

    // Inner loop state.
    double        m_margin{0.0};
    Sample_window m_cpu_window;
    Sample_window m_gpu_window;
    std::vector<Sample_window> m_latency_windows; // per cadence N, index 0..max_vsyncs

    // Slot cursor and anchoring.
    bool         m_has_next_slot{false};
    std::int64_t m_next_slot    {0};
    std::int64_t m_last_slot    {-1};
    bool         m_has_feedback {false};
    std::int64_t m_feedback_frame_id{-1};
    std::int64_t m_feedback_slot{0};

    std::int64_t m_completed_frames{0};
    bool         m_has_last_gpu_done{false};
    double       m_last_gpu_done{0.0};

    // Test hook state.
    bool   m_forced        {false};
    double m_forced_latency{0.0};
    double m_forced_margin {0.0};

    static constexpr std::size_t s_frame_ring_size = 1024;
    std::array<Frame_entry, s_frame_ring_size> m_frame_ring{};
};

} // namespace erhe::frame_pacing
