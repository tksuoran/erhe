#pragma once

// Frame pacer core (implementation plan step P1.1).
//
// Pure, engine-independent port of the verified Python reference model
// scripts/frame_pacing_sim.py. Design documents:
//   doc/frame_pacing/algorithm.md      - algorithm (normative)
//   doc/frame_pacing/control_model.md  - control-theory model, claims C1..C9
//   doc/frame_pacing/inputs.md         - input definitions (service times!)
//
// The pacer never calls the OS, Vulkan, or a clock: all times arrive as
// arguments (seconds, one monotonic clock domain) and all decisions are
// returned as values. This is what makes it unit-testable against a virtual
// clock (test/ replicates the Python plant and claims C1..C9) and portable
// across backends.
//
// Input fidelity contract (doc/frame_pacing/algorithm.md section 1):
// - cpu_duration / gpu_duration are stage SERVICE times: involuntary waits
//   (pacer wait, fence, acquire) and GPU idle bubbles excluded.
// - on_gpu_done() should be driven by per-frame polling of the GPU frame
//   timestamp bracket; the lazy fence-read path degrades gracefully (C9).

#include <array>
#include <cstdint>
#include <vector>

namespace erhe::frame_pacing {

// How the presentation queue treats images (doc/frame_pacing/algorithm.md
// section 3.1, claims C13 and C24).
enum class Present_queue_kind : unsigned int {
    // A newer image ready for the same vsync supersedes an older one
    // (VK_KHR_present_mode_fifo_latest_ready): an in-flight target further
    // ahead than its cadence must not move the slot cursor (C13).
    dropping = 0,
    // Every queued image displays, at its target or later (Metal tier M):
    // the slot cursor follows every in-flight target (C24).
    fifo
};

// Where the pacer's presentation deadline offset delta_pe comes from
// (doc/frame_pacing/algorithm.md "Deadline offset estimation").
enum class Deadline_offset_source : unsigned int {
    // deadline_offset_initial throughout: presentation paths whose late
    // displays are not deadline evidence (an engine that ignores target
    // present times, paced by the C15 request holdback).
    fixed = 0,
    // Learned from presentation feedback: the engine honors target present
    // times and takes an image when its GPU work ends, so a frame displayed
    // after its free target slot proves the deadline lies before its
    // completion (Metal tier M, the macOS compositor).
    estimated
};

// The display the host presents to (doc/frame_pacing/algorithm.md
// deviation 24, claim C30).
enum class Present_display_kind : unsigned int {
    // A periodic vsync grid: a compositor, or a fixed-rate display mode. The
    // pacer tracks the grid from presentation feedback (deviations 10, 12).
    fixed_grid = 0,
    // A variable refresh rate (Adaptive-Sync) display driven directly: it
    // shows an image when its target time arrives (or when the image is
    // ready, if later), no earlier than its minimum refresh interval
    // (refresh_period) after the previous image, and repeats the previous
    // image after max_refresh_interval. There is no grid to track.
    variable_refresh
};

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
    double      upshift_quantile  {0.9};        // q_up: share of the dwell's frames that must have headroom
    std::size_t cadence_min_samples{30};        // stage samples at the current cadence the predictive downshift needs (C24)
    // GPU pre-warm before an upshift (C26): the host can run extra GPU work
    // after a frame's work (Metal tier M: an ALU-only kernel ordered after
    // the presenting command buffer); upshift_prewarm_seconds is the fill to
    // start from (0 = no pre-warm). A completed upshift dwell first requests
    // the fill for up to upshift_prewarm_frames frames and switches once a
    // frame's own times fit N-1's budget. A pre-warm that ends without the
    // GPU clock reaching upshift_prewarm_clock doubles the fill (bounded by
    // upshift_prewarm_max_frac of the cadence period) - the governor needs
    // more; one that reached it but still does not fit is a failed upshift
    // (backoff). The fill that worked stays for later upshifts.
    double      upshift_prewarm_seconds  {0.0};
    int         upshift_prewarm_frames   {8};
    double      upshift_prewarm_clock    {0.9};
    double      upshift_prewarm_max_frac {0.5};
    // The clock records (C25) hold each cadence's steady clock (C27): a
    // cadence's samples start clock_settle_time after its first GPU end -
    // the governor's ramp after a change is the previous cadence's state
    // (live ~60 ms to 0.94 at N=1); frames delivered after their cadence was
    // left and frames that ran a pre-warm are not samples of it. Ramp frames
    // recorded as N=1's clock once locked the longer cadence; longer
    // settling starves short stays at a borderline cadence of evidence.
    double      clock_settle_time        {0.15};
    // A clock record sample keeps the frame's work (time x clock), and the
    // clock at N-1 is judged from the samples whose work lies within this
    // factor of the frame's (C29): the governor answers the workload, so a
    // light workload's clock (the empty editor before a scene opens: 0.21 at
    // N=1) is no evidence of a heavier one's. Without such samples the
    // cadence is judged at full clock; the pre-warm (C26) measures the frame
    // at the raised clock before switching.
    double      clock_record_work_band   {1.5};
    int         reactive_miss_threshold{2};     // M consecutive observed self-misses
    double      margin_initial_frac{0.20};      // m0
    double      margin_min_frac   {0.02};
    double      margin_max_frac   {0.50};
    double      margin_attack_frac{0.10};       // additive increase on (near-)miss
    double      margin_decay_frac {0.001};      // decay per frame with ample slack
    int         target_queued_images{1};        // Q*
    std::size_t cold_start_seed_frames{8};      // K
    double      deadline_offset_initial{0.0025};// assumed presentation deadline offset (delta_pe floor)
    Deadline_offset_source deadline_offset_source{Deadline_offset_source::fixed};
    Present_queue_kind present_queue{Present_queue_kind::dropping};
    // delta_pe estimator (doc/frame_pacing/algorithm.md "Deadline offset
    // estimation"), Deadline_offset_source::estimated only: learned from
    // (completion margin, displayed late) samples of the last `history`
    // frames with presentation feedback.
    double      deadline_offset_max_frac{2.0};  // estimate ceiling, fraction of T
    double      deadline_offset_max{0.0};       // estimate ceiling (s), measured by the host (present calibration); 0.0: deadline_offset_max_frac * T
    std::size_t deadline_evidence_band{4};      // samples (by margin) in a deadline band
    std::size_t deadline_evidence_min_late{2};  // late frames a deadline band needs
    double      deadline_offset_decay{0.0000001};// decay per feedback toward the evidence (s)
    double      grid_tracker_gain {0.2};        // vsync grid phase PLL gain
    double      grid_period_gain  {0.01};       // vsync grid period tracker gain (per cycle)
    double      grid_period_max_dev{0.005};     // tracked period clamp, fraction of the reference period
    std::size_t grid_reseed_window{16};         // feedback-delta samples backing the gross-error re-seed
    double      grid_reseed_dev   {0.02};       // re-seed when median delta-derived period deviates this much
    // The display (C30). On variable_refresh refresh_period is the display's
    // minimum refresh interval and stays the grid period (no re-seed, no
    // period tracking), a late display re-anchors the slot cursor only, no
    // target lies more than max_refresh_interval after the previous one, the
    // cadence tests drop the guard allowance (a late frame costs its
    // lateness, not a refresh), and a frame the display could not show at
    // its target (a repeat in between) is no deadline offset evidence.
    Present_display_kind display             {Present_display_kind::fixed_grid};
    double               max_refresh_interval{0.0}; // variable_refresh: the longest interval the display holds an image (s), > 0
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
    double       prewarm_seconds   {0.0}; // C26: run this much extra GPU work (wall time) after
                                          // this frame's work, ordered after it (0 = none)
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
    double deadline_offset{0.0}; // delta_pe estimate the frame was judged against
    int    vsyncs     {1};
};

// The upshift path's state, exposed for telemetry: why a longer cadence
// is kept.
class Upshift_state
{
public:
    double      dwell                 {0.0}; // accumulated headroom time, seconds
    double      dwell_required        {0.0}; // the dwell the next upshift needs (backoff, C10)
    std::size_t dwell_frames          {0};
    std::size_t dwell_frames_loaded   {0};
    double      last_stage_time       {0.0}; // the last upshift test's stage time at N-1, seconds
    double      next_period           {0.0}; // N-1's budget the stage time was judged against
    double      gpu_clock_next        {0.0}; // the clock ratio N-1 is judged at for the last frame's work (0 = no record of that workload)
    double      cpu_clock_next        {0.0};
    std::size_t gpu_clock_next_samples{0}; // all of N-1's samples, any workload
    int         prewarm_left          {0};
    double      prewarm_fill          {0.0}; // seconds
};

// The clocks measured at one cadence, each with the frame's work (its stage
// time times the clock, i.e. the time at full clock) - claim C29.
// Capacity is allocated once at init; steady state performs no allocations.
class Clock_record
{
public:
    void init(std::size_t capacity);
    void push(double clock_ratio, double work);
    [[nodiscard]] auto count() const -> std::size_t { return m_count; }
    // The p-quantile of the clocks of the samples whose work lies within
    // [work / band, work * band]; fallback when fewer than min_samples.
    [[nodiscard]] auto clock_for_work(double work, double band, double p, std::size_t min_samples, double fallback) const -> double;

private:
    std::vector<double>         m_clocks;
    std::vector<double>         m_works;
    mutable std::vector<double> m_scratch;
    std::size_t                 m_head {0};
    std::size_t                 m_count{0};
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
    // gpu_clock_ratio / cpu_clock_ratio: the GPU's / the CPU's average
    // active clock over the frame as a ratio of its maximum, where the host
    // measures it; 0.0 = not measured (claim C25).
    void on_gpu_done        (std::int64_t frame_id, double gpu_end_time, double cpu_duration, double gpu_duration, double latency, double gpu_clock_ratio = 0.0, double cpu_clock_ratio = 0.0);
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
    [[nodiscard]] auto get_deadline_offset () const -> double { return m_deadline_offset; }
    [[nodiscard]] auto get_frame_stats     (std::int64_t frame_id) const -> Frame_stats;
    [[nodiscard]] auto get_upshift_state   () const -> Upshift_state;

    // Test hook (claim C6): force latency estimate and margin to given values.
    void set_forced_override(bool enabled, double forced_latency, double forced_margin);

private:
    // Bookkeeping for recently scheduled frames, ring-indexed by frame id.
    class Frame_entry
    {
    public:
        std::int64_t frame_id     {-1};
        std::int64_t target_slot  {0};
        double       target_time  {0.0};
        int          vsyncs       {1};
        bool         prewarm      {false}; // a GPU pre-warm was requested (C26)
        Frame_stats  stats        {};
        bool         has_gpu_end  {false};
        double       gpu_end      {0.0};
        bool         has_achieved {false};
        double       achieved_time{0.0};
    };

    // One delta_pe estimator sample: how long before its target time the
    // frame's GPU work ended, and whether it displayed after its target.
    class Deadline_sample
    {
    public:
        double completion_margin{0.0};
        bool   late             {false};
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
    void               reset_dwell          ();
    void               update_deadline_offset(std::int64_t frame_id, double achieved_present_time, double previous_display_time);
    void               on_variable_refresh_feedback(std::int64_t frame_id, double achieved_present_time);
    [[nodiscard]] auto is_variable_refresh  () const -> bool;
    [[nodiscard]] auto cadence_guard        () const -> double; // C30: the cadence tests' jitter allowance
    [[nodiscard]] auto vrr_slots_max        () const -> std::int64_t; // C30: most slots between two targets without a repeat
    [[nodiscard]] auto deadline_evidence    () -> double;

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
    double m_deadline_offset_max{0.0};
    std::vector<Deadline_sample> m_deadline_samples;        // ring, capacity tunables.history
    std::vector<Deadline_sample> m_deadline_samples_scratch; // sort scratch for deadline_evidence()
    std::size_t                  m_deadline_sample_head {0};
    std::size_t                  m_deadline_sample_count{0};

    // Outer loop state.
    int    m_vsyncs      {1};   // N
    int    m_min_vsyncs  {1};   // application-set floor
    double m_dwell       {0.0}; // accumulated upshift headroom time
    std::size_t m_dwell_frames         {0}; // frames of the current dwell
    std::size_t m_dwell_frames_loaded  {0}; // of them without upshift headroom
    double m_last_upshift_stage_time{0.0}; // telemetry (get_upshift_state)
    double m_last_gpu_work{0.0};           // telemetry: the last frame's work (time x clock)
    double m_last_cpu_work{0.0};
    int    m_consecutive_misses{0};
    double m_streak_attribution{0.0}; // min over streak of max(c, g); +inf when no streak
    std::int64_t m_last_pressure_frame{-1000000000}; // completed-count of last (near-)miss
    // Upshift backoff: a pressure-reverted upshift doubles the next probe's
    // required dwell (geometric convergence at infeasible-borderline load);
    // a surviving upshift resets it.
    double m_dwell_required  {0.0};
    bool   m_has_upshift_time{false};
    double m_upshift_time    {0.0};
    int    m_prewarm_left    {0};   // GPU pre-warm frames left (C26); 0 = none
    double m_prewarm_fill    {0.0}; // the learned pre-warm fill (C26)
    double m_latency_seed    {0.0};    // C28: latency floor until the visit has cold_start_seed_frames samples; 0 = none
    bool   m_has_cadence_start{false}; // C27: the current cadence's first GPU end is known ...
    double m_cadence_start   {0.0};    // ... and this is it

    // Inner loop state.
    double        m_margin{0.0};
    Sample_window m_cpu_window;
    Sample_window m_gpu_window;
    // Stage times of the frames scheduled at the current cadence (C24): the
    // GPU cost depends on the cadence (an Apple GPU clocks down at N=2), so
    // the predictive downshift judges a cadence by its own samples and its
    // own (near-)misses. Cleared on every cadence change.
    Sample_window m_cadence_cpu_window;
    Sample_window m_cadence_gpu_window;
    bool          m_cadence_pressure{false};
    // GPU and CPU clock ratios measured at each cadence N (index
    // 0..max_vsyncs), kept across cadence changes: the clock a processor
    // shows at a cadence predicts its stage time there (C25).
    std::vector<Clock_record> m_clock_windows;
    std::vector<Clock_record> m_cpu_clock_windows;
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
