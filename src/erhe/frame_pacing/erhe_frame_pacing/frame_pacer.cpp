#include "erhe_frame_pacing/frame_pacer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace erhe::frame_pacing {

namespace {

[[nodiscard]] auto ceil_div_time(const double value, const double period) -> std::int64_t
{
    return static_cast<std::int64_t>(std::ceil(value / period));
}

} // anonymous namespace

void Sample_window::init(const std::size_t capacity)
{
    m_samples.assign(capacity, 0.0);
    m_scratch.reserve(capacity);
    m_head  = 0;
    m_count = 0;
}

void Sample_window::clear()
{
    m_head  = 0;
    m_count = 0;
}

void Sample_window::push(const double value)
{
    if (m_samples.empty()) {
        return;
    }
    m_samples[m_head] = value;
    m_head = (m_head + 1) % m_samples.size();
    if (m_count < m_samples.size()) {
        ++m_count;
    }
}

auto Sample_window::quantile(const double p, const double fallback) const -> double
{
    if (m_count == 0) {
        return fallback;
    }
    m_scratch.clear();
    const std::size_t capacity = m_samples.size();
    const std::size_t start    = (m_head + capacity - m_count) % capacity;
    for (std::size_t i = 0; i < m_count; ++i) {
        m_scratch.push_back(m_samples[(start + i) % capacity]);
    }
    std::sort(m_scratch.begin(), m_scratch.end());
    const double      raw_index = std::ceil(p * static_cast<double>(m_count)) - 1.0;
    const std::size_t clamped   = static_cast<std::size_t>(
        std::clamp(raw_index, 0.0, static_cast<double>(m_count - 1))
    );
    return m_scratch[clamped];
}

Frame_pacer::Frame_pacer(const Frame_pacer_tunables& tunables, const double nominal_grid_phase)
    : m_tunables{tunables}
{
    const double t    = m_tunables.refresh_period;
    m_hysteresis      = m_tunables.hysteresis_frac * t;
    m_margin_min      = m_tunables.margin_min_frac * t;
    m_margin_max      = m_tunables.margin_max_frac * t;
    m_margin_attack   = m_tunables.margin_attack_frac * t;
    m_margin_decay    = m_tunables.margin_decay_frac * t;
    m_grid_phase      = nominal_grid_phase;
    m_grid_period     = t;
    m_grid_period_ref = t;
    m_deadline_offset = m_tunables.deadline_offset_initial;
    m_deadline_offset_max = std::max(m_tunables.deadline_offset_initial, m_tunables.deadline_offset_max_frac * t);
    m_vsyncs          = m_tunables.min_vsyncs;
    m_min_vsyncs      = m_tunables.min_vsyncs;
    m_margin          = m_tunables.margin_initial_frac * t;
    m_streak_attribution = std::numeric_limits<double>::infinity();
    m_dwell_required     = m_tunables.upshift_dwell_time;

    m_cpu_window.init(m_tunables.history);
    m_gpu_window.init(m_tunables.history);
    m_period_sample_window.init(m_tunables.grid_reseed_window);
    m_deadline_samples.resize(m_tunables.history);
    m_deadline_samples_scratch.reserve(m_tunables.history);
    m_latency_windows.resize(static_cast<std::size_t>(m_tunables.max_vsyncs) + 1);
    for (Sample_window& window : m_latency_windows) {
        window.init(m_tunables.history);
    }
}

auto Frame_pacer::slot_time(const std::int64_t slot) const -> double
{
    return m_grid_phase + (static_cast<double>(slot) * m_grid_period);
}

auto Frame_pacer::latency_estimate() const -> double
{
    if (m_forced) {
        return m_forced_latency;
    }
    const double p        = m_tunables.quantile_p;
    double       fallback = m_cpu_window.quantile(p, 0.0) + m_gpu_window.quantile(p, 0.0);
    if (fallback <= 0.0) {
        fallback = m_tunables.refresh_period;
    }
    const Sample_window& window = m_latency_windows[static_cast<std::size_t>(m_vsyncs)];
    if (!window.empty()) {
        return window.quantile(p, fallback);
    }
    return fallback;
}

auto Frame_pacer::latency_typical() const -> double
{
    // Median latency (deviation 13, claim C18): slot REACHABILITY is judged
    // by what a typical frame achieves, not the q95 budget - a spike-tail-
    // inflated quantile must not skip slots the typical frame makes.
    if (m_forced) {
        return m_forced_latency;
    }
    double fallback = m_cpu_window.quantile(0.5, 0.0) + m_gpu_window.quantile(0.5, 0.0);
    if (fallback <= 0.0) {
        fallback = m_tunables.refresh_period;
    }
    const Sample_window& window = m_latency_windows[static_cast<std::size_t>(m_vsyncs)];
    if (!window.empty()) {
        return window.quantile(0.5, fallback);
    }
    return fallback;
}

auto Frame_pacer::margin_in_use() const -> double
{
    return m_forced ? m_forced_margin : m_margin;
}

auto Frame_pacer::throughput_statistic() const -> double
{
    const double p = m_tunables.quantile_p;
    return std::max(m_cpu_window.quantile(p, 0.0), m_gpu_window.quantile(p, 0.0)) + m_tunables.guard;
}

auto Frame_pacer::entry_for(const std::int64_t frame_id) -> Frame_entry&
{
    Frame_entry& entry = m_frame_ring[static_cast<std::size_t>(frame_id) % s_frame_ring_size];
    if (entry.frame_id != frame_id) {
        entry = Frame_entry{};
        entry.frame_id = frame_id;
    }
    return entry;
}

auto Frame_pacer::find_entry(const std::int64_t frame_id) const -> const Frame_entry*
{
    if (frame_id < 0) {
        return nullptr;
    }
    const Frame_entry& entry = m_frame_ring[static_cast<std::size_t>(frame_id) % s_frame_ring_size];
    return (entry.frame_id == frame_id) ? &entry : nullptr;
}

auto Frame_pacer::get_frame_stats(const std::int64_t frame_id) const -> Frame_stats
{
    const Frame_entry* entry = find_entry(frame_id);
    return (entry != nullptr) ? entry->stats : Frame_stats{};
}

void Frame_pacer::set_forced_override(const bool enabled, const double forced_latency, const double forced_margin)
{
    m_forced         = enabled;
    m_forced_latency = forced_latency;
    m_forced_margin  = forced_margin;
}

void Frame_pacer::set_min_vsyncs(const int min_vsyncs)
{
    m_min_vsyncs = std::max(1, min_vsyncs);
    if (m_vsyncs < m_min_vsyncs) {
        change_cadence(m_min_vsyncs, false);
    }
}

void Frame_pacer::notify_swapchain_recreated(const double nominal_grid_phase)
{
    // Reset presentation-side state only; keep load estimates and margin
    // (doc/frame_pacing/behavior.md section 8). The tracked grid PERIOD is
    // retained: it is a property of the display, not of the swapchain.
    m_grid_phase          = nominal_grid_phase;
    m_has_feedback_slot   = false;
    m_has_raw_feedback    = false;
    m_period_sample_window.clear();
    m_has_next_slot       = false;
    m_has_feedback        = false;
    // The deadline offset belongs to the presentation path, which a
    // recreation may change (control_model.md section 9).
    m_deadline_offset       = m_tunables.deadline_offset_initial;
    m_deadline_sample_head  = 0;
    m_deadline_sample_count = 0;
    m_consecutive_misses  = 0;
    m_streak_attribution  = std::numeric_limits<double>::infinity();
    reset_dwell();
}

auto Frame_pacer::schedule(const std::int64_t frame_id, const double now) -> Schedule_decision
{
    std::int64_t slot    = 0;
    double       release = now;

    if (m_completed_frames < static_cast<std::int64_t>(m_tunables.cold_start_seed_frames)) {
        // Cold start seed phase: open loop, release immediately (FR7).
        slot = std::max(m_last_slot + 1, ceil_div_time(now - m_grid_phase, m_grid_period));
    } else {
        const double budget = latency_estimate() + margin_in_use();
        if (m_has_feedback) {
            // Presentation feedback is AUTHORITATIVE for the slot cursor: the
            // anchor (latest achieved present plus the scheduled cadence of
            // every in-flight frame) is where the display queue actually is.
            // Using it as a mere lower bound lets the cursor ratchet ahead of
            // reality without correction when the app runs unpaced (observer
            // mode finding, step P2.1; claim C11).
            // Each in-flight frame displays at its own target slot, but no
            // earlier than one slot after the frame before it (FIFO) and no
            // later than its cadence after it: a target further ahead comes
            // from an earlier schedule, and on a dropping path (latest_ready)
            // taking it would ratchet the cursor (C13). Adding N per
            // in-flight frame regardless of the target assumed every
            // in-flight frame sits N slots after its predecessor: once two
            // frames shared a slot (a cold-start miss), that rule re-created
            // the collision every few frames - a limit cycle of late
            // displays and a margin pinned at its maximum (C10, deviation 16).
            std::int64_t anchor = m_feedback_slot;
            for (std::int64_t i = m_feedback_frame_id + 1; i < frame_id; ++i) {
                const Frame_entry* entry = find_entry(i);
                anchor = (entry != nullptr)
                    ? std::max(anchor + 1, std::min(entry->target_slot, anchor + entry->vsyncs))
                    : (anchor + m_vsyncs);
            }
            anchor += m_vsyncs;
            // The last_slot+1 floor must NOT apply here (deviation 11): on a
            // dropping presentation path (fifo_latest_ready) an app running
            // faster than refresh consumes less than one display slot per
            // frame, so a per-frame slot floor ratchets the cursor ahead
            // without bound (~10 s observed live, claim C13). The anchor's
            // per-in-flight +N already makes targets strictly increasing
            // between feedbacks.
            slot = anchor;
        } else {
            slot = m_has_next_slot ? m_next_slot : ceil_div_time(now - m_grid_phase, m_grid_period);
            slot = std::max(slot, m_last_slot + 1);
        }
        // Re-anchor forward if the slot is no longer reachable. The
        // reachability test uses the TYPICAL (median) latency plus margin,
        // not the q95 budget (deviation 13, claim C18): a spike-tail-
        // inflated quantile would skip slots the typical frame makes - with
        // the decision point gated late (the C15 holdback runs inline on
        // the producer thread), that chronic +1 slid the whole schedule
        // into a stable one-slot-longer cadence (the live 120 Hz N=3 ->
        // 4-slot lock / N=1 sawtooth). A tail frame that does miss slips
        // one slot and re-anchors - bounded, priced by the margin AIMD.
        const double typical = latency_typical() + margin_in_use();
        while ((slot_time(slot) - m_deadline_offset - typical) < (now - m_tunables.guard)) {
            ++slot;
        }
        release = std::max(now, slot_time(slot) - m_deadline_offset - budget);
    }

    m_next_slot     = slot + m_vsyncs;
    m_has_next_slot = true;
    m_last_slot     = slot;

    Frame_entry& entry = entry_for(frame_id);
    entry.target_slot = slot;
    entry.target_time = slot_time(slot);
    entry.vsyncs      = m_vsyncs;

    Schedule_decision decision;
    decision.frame_id           = frame_id;
    decision.release_time       = release;
    decision.target_slot        = slot;
    decision.target_time        = slot_time(slot);
    decision.predicted_display  = slot_time(slot);
    decision.predicted_duration = static_cast<double>(m_vsyncs) * m_grid_period;
    decision.vsyncs_per_frame   = m_vsyncs;
    // FR5 clamp: at most Q* images between completion and display, plus
    // the images a deadline offset of one or more cadence periods holds in
    // the queue by itself (each must be complete delta_pe before its vsync,
    // so frame k is released before frame k - 1 - Q* can have displayed).
    const std::int64_t deadline_images = static_cast<std::int64_t>(
        std::floor(m_deadline_offset / (static_cast<double>(m_vsyncs) * m_grid_period))
    );
    decision.wait_id            = frame_id - 1 - static_cast<std::int64_t>(m_tunables.target_queued_images) - deadline_images;
    // Present-request holdback deadline (C15 mitigation): hold the request
    // into the target slot's cycle. Computed HERE so it uses the tracked
    // period (deviation 12). Deliberately NOT capped to the next frame's
    // release point: the capped policy re-admits early displays whenever
    // the budget exceeds (N+1)*T - dpe - 2*guard (claim C18).
    if (m_completed_frames >= static_cast<std::int64_t>(m_tunables.cold_start_seed_frames)) {
        decision.hold_until = slot_time(slot) - m_grid_period + m_tunables.guard;
    }
    return decision;
}

void Frame_pacer::on_cpu_done(const std::int64_t frame_id, const double cpu_duration)
{
    static_cast<void>(frame_id);
    m_cpu_window.push(cpu_duration);
}

void Frame_pacer::on_gpu_done(
    const std::int64_t frame_id,
    const double       gpu_end_time,
    const double       cpu_duration,
    const double       gpu_duration,
    const double       latency
)
{
    m_gpu_window.push(gpu_duration);

    const Frame_entry* scheduled = find_entry(frame_id);
    const int          n_used    = (scheduled != nullptr) ? scheduled->vsyncs : m_vsyncs;
    m_latency_windows[static_cast<std::size_t>(std::clamp(n_used, 1, m_tunables.max_vsyncs))].push(latency);

    const std::int64_t target_slot = (scheduled != nullptr) ? scheduled->target_slot : m_last_slot;
    const double       deadline    = slot_time(target_slot) - m_deadline_offset;
    const double       slack       = deadline - gpu_end_time;

    double elapsed = 0.0;
    if (m_has_last_gpu_done) {
        elapsed = std::max(0.0, gpu_end_time - m_last_gpu_done);
    }
    m_last_gpu_done     = gpu_end_time;
    m_has_last_gpu_done = true;
    ++m_completed_frames;

    // Margin AIMD (FR2b under FR2a priority).
    if (!m_forced) {
        if (slack < m_tunables.guard) { // miss or near-miss: fast attack
            m_margin = std::min(m_margin_max, m_margin + m_margin_attack);
        } else {                        // ample slack: slow release
            m_margin = std::max(m_margin_min, m_margin - m_margin_decay);
        }
    }
    if (slack < m_tunables.guard) {
        m_last_pressure_frame = m_completed_frames; // deadline pressure evidence
    }

    // Miss streak with throughput attribution: the streak is attributed to
    // load only if EVERY missed frame's own stage time exceeds the budget
    // (min over streak of per-frame max) - a cascade miss behind a single
    // spike has normal stage times and breaks the attribution (claim C5).
    const bool self_miss = slack < 0.0;
    if (self_miss) {
        ++m_consecutive_misses;
        m_streak_attribution = std::min(m_streak_attribution, std::max(cpu_duration, gpu_duration));
    } else {
        m_consecutive_misses = 0;
        m_streak_attribution = std::numeric_limits<double>::infinity();
    }

    const double w_statistic = throughput_statistic();

    Frame_entry& entry      = entry_for(frame_id);
    entry.has_gpu_end       = true;
    entry.gpu_end           = gpu_end_time;
    entry.stats.valid       = true;
    entry.stats.self_miss   = self_miss;
    entry.stats.slack       = slack;
    entry.stats.w_statistic = w_statistic;
    entry.stats.margin      = m_margin;
    entry.stats.deadline_offset = m_deadline_offset;
    entry.stats.vsyncs      = m_vsyncs;

    // Outer loop (FR1). Cadence budgets quantize by the TRACKED period, not
    // the nominal (deviation 12): with a grossly wrong nominal, load in the
    // band between the true and nominal periods would otherwise look
    // feasible at a cadence it cannot sustain - and the reactive path could
    // never attribute the resulting misses (streak_attr + guard stays below
    // the inflated nominal budget).
    const int required_vsyncs = std::clamp(
        static_cast<int>(std::ceil(w_statistic / m_grid_period)),
        m_min_vsyncs,
        m_tunables.max_vsyncs
    );
    const double cadence_budget = static_cast<double>(m_vsyncs) * m_grid_period;

    // Upshift survival: no pressure-driven revert for 4*T_up after an
    // upshift means the new cadence holds - future upshifts are cheap.
    if (m_has_upshift_time && ((gpu_end_time - m_upshift_time) >= (4.0 * m_tunables.upshift_dwell_time))) {
        m_dwell_required   = m_tunables.upshift_dwell_time;
        m_has_upshift_time = false;
    }

    if (
        (m_consecutive_misses >= m_tunables.reactive_miss_threshold) &&
        ((m_streak_attribution + m_tunables.guard) > cadence_budget)
    ) {
        // Reactive downshift: sustained overload evidenced by the missed
        // frames' own stage times.
        on_pressure_downshift(gpu_end_time);
        change_cadence(std::max(m_vsyncs + 1, required_vsyncs), false);
        m_consecutive_misses = 0;
        m_streak_attribution = std::numeric_limits<double>::infinity();
    } else if (
        (required_vsyncs > m_vsyncs) &&
        ((m_completed_frames - m_last_pressure_frame) <= static_cast<std::int64_t>(m_tunables.history))
    ) {
        // Predictive downshift needs PRESSURE evidence (a recent miss or
        // near-miss), not the quantile alone: heavy-tailed load whose q95
        // exceeds the budget can still be fully sustainable (tail absorbed
        // by margin and queue elasticity - observer finding, step P2.1;
        // claim C10). Misses/near-misses are the honest signal; the margin
        // AIMD surfaces overload as near-misses well before drops.
        on_pressure_downshift(gpu_end_time);
        change_cadence(required_vsyncs, false);
    } else if (m_vsyncs > m_min_vsyncs) {
        // Upshift headroom (deviation 15): a frame beyond the next cadence's
        // period resets the dwell; a frame inside the hysteresis band below
        // it (past the budget (N - 1) * period - H) is allowed for a
        // 1 - q_up share of the frames since the dwell started. A GPU that
        // clocks down at the longer cadence (Apple GPUs) pushes a tail of
        // frames into that band, and a per-frame test then never completes
        // its dwell (C23); a heavy tail beyond the period still keeps the
        // longer cadence (C10). The frames of the dwell, not the window:
        // the window stays stale for ~2 s after a load drop (C3).
        const double stage_time     = std::max(cpu_duration, gpu_duration);
        const double next_period    = static_cast<double>(m_vsyncs - 1) * m_grid_period;
        const bool   beyond_period  = stage_time > next_period;
        const bool   in_band        = !beyond_period && (stage_time > (next_period - m_hysteresis));
        ++m_dwell_frames;
        if (in_band) {
            ++m_dwell_frames_loaded;
        }
        const double allowed_loaded = std::floor((1.0 - m_tunables.upshift_quantile) * static_cast<double>(m_dwell_frames));
        if (beyond_period || (static_cast<double>(m_dwell_frames_loaded) > allowed_loaded)) {
            reset_dwell();
        } else {
            m_dwell += elapsed;
            if (m_dwell >= m_dwell_required) {
                change_cadence(m_vsyncs - 1, true);
                m_has_upshift_time = true;
                m_upshift_time     = gpu_end_time;
            }
        }
    } else {
        reset_dwell();
    }
}

void Frame_pacer::reset_dwell()
{
    m_dwell               = 0.0;
    m_dwell_frames        = 0;
    m_dwell_frames_loaded = 0;
}

void Frame_pacer::on_pressure_downshift(const double gpu_end_time)
{
    // A pressure-driven downshift shortly after an upshift means the upshift
    // probed an infeasible cadence: double the price of the next probe.
    // Geometric backoff converges (finitely many probes) at persistently
    // borderline load while staying adaptive to real drops (claim C10).
    if (m_has_upshift_time && ((gpu_end_time - m_upshift_time) < (4.0 * m_tunables.upshift_dwell_time))) {
        m_dwell_required  *= 2.0;
        m_has_upshift_time = false;
    }
}

void Frame_pacer::change_cadence(const int new_vsyncs, const bool is_upshift)
{
    const int clamped = std::clamp(new_vsyncs, m_min_vsyncs, m_tunables.max_vsyncs);
    if (clamped == m_vsyncs) {
        return;
    }
    const int old_vsyncs = m_vsyncs;
    m_vsyncs = clamped;
    reset_dwell();
    Sample_window& window = m_latency_windows[static_cast<std::size_t>(m_vsyncs)];
    if (window.empty()) {
        // Seed the new cadence's latency estimate: carry-over is a safe
        // overestimate when downshifting; the structural sum floors it when
        // upshifting (doc/frame_pacing/algorithm.md section 5).
        const double p     = m_tunables.quantile_p;
        const double carry = m_latency_windows[static_cast<std::size_t>(old_vsyncs)].quantile(p, 0.0);
        const double structural =
            m_cpu_window.quantile(p, 0.0) + m_gpu_window.quantile(p, 0.0);
        double initial = std::max(carry, structural);
        if (initial <= 0.0) {
            initial = m_tunables.refresh_period;
        }
        window.push(initial);
    }
    if (is_upshift) {
        // Pre-emptive safety boost while the new cadence's latency
        // distribution is learned.
        m_margin = std::min(m_margin_max, m_margin + m_margin_attack);
    }
}

void Frame_pacer::on_present_feedback(const std::int64_t frame_id, const double achieved_present_time)
{
    // Gross-error re-seed (deviation 12, claim C16): the queried nominal
    // period can be grossly wrong (4.2% measured after an app-driven
    // fullscreen mode set - driver report issue #2). The PLL cannot recover
    // on its own: every innovation is pinned above the outlier gate and the
    // clamp band is centered on the wrong nominal. Period samples from raw
    // feedback DELTAS (dt / round(dt/period)) are phase-independent and
    // immune to slips (a skipped slot moves dt and dn together); when their
    // median disagrees with the tracked period by far more than the clamp
    // band, re-seed the period and phase from the measurement and re-center
    // the clamp on the measured period.
    // The newest display before this one, of any earlier frame (frames a
    // latest_ready engine dropped give no feedback): the deadline estimator
    // needs to know whether this frame's target slot was still free.
    const double previous_display_time = m_has_raw_feedback ? m_raw_feedback_previous : 0.0;
    if (m_has_raw_feedback) {
        const double       dt = achieved_present_time - m_raw_feedback_previous;
        const std::int64_t dn = static_cast<std::int64_t>(std::round(dt / m_grid_period));
        if ((dn >= 1) && (dn <= 4)) {
            m_period_sample_window.push(dt / static_cast<double>(dn));
        }
    }
    m_has_raw_feedback      = true;
    m_raw_feedback_previous = achieved_present_time;
    if (m_period_sample_window.count() == m_tunables.grid_reseed_window) {
        const double median = m_period_sample_window.quantile(0.5, m_grid_period);
        if (std::abs(median - m_grid_period) > (m_tunables.grid_reseed_dev * m_grid_period_ref)) {
            const double nearest_old = std::round((achieved_present_time - m_grid_phase) / m_grid_period);
            m_grid_period     = median;
            m_grid_period_ref = median;
            m_grid_phase      = achieved_present_time - (nearest_old * median);
            m_period_sample_window.clear();
            m_has_feedback_slot = false;
        }
    }

    const double nearest_slot = std::round((achieved_present_time - m_grid_phase) / m_grid_period);
    const double error        = achieved_present_time - (m_grid_phase + (nearest_slot * m_grid_period));

    // Dynamic grid (deviation 10): second-order PLL - phase (below) plus a
    // period tracker. The frequency correction normalizes the innovation by
    // cycles elapsed since the previous good feedback, is skipped for
    // outliers (a slip or compositor hiccup produces a near +-period/2
    // innovation that would kick the period estimate hard), and the
    // estimate is clamped to a band around the reference period (the
    // nominal until a gross-error re-seed replaces it). A period
    // change RE-BASES the phase so the slot nearest to the feedback keeps
    // its time: slot indices are absolute, so an un-rebased period change
    // of dP moves slot j by j*dP - seconds, for real reference-clock epochs.
    if (std::abs(error) < m_tunables.guard) {
        if (m_has_feedback_slot) {
            const std::int64_t cycles = static_cast<std::int64_t>(nearest_slot) - m_feedback_slot_previous;
            if (cycles >= 1) {
                double period_new = m_grid_period + m_tunables.grid_period_gain * error / static_cast<double>(cycles);
                period_new = std::clamp(
                    period_new,
                    m_grid_period_ref * (1.0 - m_tunables.grid_period_max_dev),
                    m_grid_period_ref * (1.0 + m_tunables.grid_period_max_dev)
                );
                m_grid_phase += nearest_slot * (m_grid_period - period_new);
                m_grid_period = period_new;
            }
        }
        m_has_feedback_slot      = true;
        m_feedback_slot_previous = static_cast<std::int64_t>(nearest_slot);
    }

    m_grid_phase += m_tunables.grid_tracker_gain * error;

    const std::int64_t achieved_slot =
        static_cast<std::int64_t>(std::round((achieved_present_time - m_grid_phase) / m_grid_period));
    if (!m_has_feedback || (frame_id > m_feedback_frame_id)) {
        m_has_feedback      = true;
        m_feedback_frame_id = frame_id;
        m_feedback_slot     = achieved_slot;
    }

    update_deadline_offset(frame_id, achieved_present_time, previous_display_time);
}

void Frame_pacer::update_deadline_offset(const std::int64_t frame_id, const double achieved_present_time, const double previous_display_time)
{
    // delta_pe estimator (algorithm.md section 3.3), for a presentation path
    // that honors target present times and takes an image at its GPU end. A
    // frame displayed after its target although the slot was free (the
    // newest earlier display lies before it) proves the image had to be
    // complete more than its completion margin before the vsync. A late
    // frame needs a second one nearby in margin (the evidence band) to count,
    // so a single disturbance does not raise the offset.
    if (m_tunables.deadline_offset_source != Deadline_offset_source::estimated) {
        return;
    }
    Frame_entry& entry = m_frame_ring[static_cast<std::size_t>(frame_id) % s_frame_ring_size];
    if ((entry.frame_id != frame_id) || !entry.has_gpu_end || entry.has_achieved) {
        return;
    }
    entry.has_achieved  = true;
    entry.achieved_time = achieved_present_time;

    // Times, not slot indices: slot indices move with grid re-seeds.
    const double half_period = 0.5 * m_grid_period;
    if (previous_display_time > (entry.target_time - half_period)) {
        return; // the target slot was taken: a cascade, not a deadline outcome
    }

    Deadline_sample& sample = m_deadline_samples[m_deadline_sample_head];
    sample.completion_margin = entry.target_time - entry.gpu_end;
    sample.late              = achieved_present_time > (entry.target_time + half_period);
    m_deadline_sample_head   = (m_deadline_sample_head + 1) % m_deadline_samples.size();
    m_deadline_sample_count  = std::min(m_deadline_sample_count + 1, m_deadline_samples.size());

    // Raise at once to the evidence (a too-small offset displays frames
    // late), decay slowly toward it (a too-large one only costs latency;
    // conservative over-estimate preferred).
    const double evidence = std::clamp(deadline_evidence(), m_tunables.deadline_offset_initial, m_deadline_offset_max);
    if (evidence > m_deadline_offset) {
        m_deadline_offset = evidence;
    } else {
        m_deadline_offset = std::max(evidence, m_deadline_offset - m_tunables.deadline_offset_decay);
    }
}

auto Frame_pacer::deadline_evidence() -> double
{
    // The largest completion margin of a late frame whose band - it and the
    // deadline_evidence_band - 1 samples of next smaller margin - holds at
    // least deadline_evidence_min_late late frames; that margin plus guard.
    // 0 without such a band. The band is local: frames late for a cause
    // below the deadline (a GPU spike) must not lend their share to an
    // uncorrelated late frame of a larger margin.
    m_deadline_samples_scratch.clear();
    for (std::size_t i = 0; i < m_deadline_sample_count; ++i) {
        m_deadline_samples_scratch.push_back(m_deadline_samples[i]);
    }
    std::sort(
        m_deadline_samples_scratch.begin(),
        m_deadline_samples_scratch.end(),
        [](const Deadline_sample& lhs, const Deadline_sample& rhs) { return lhs.completion_margin < rhs.completion_margin; }
    );
    const std::size_t band       = std::max<std::size_t>(1, m_tunables.deadline_evidence_band);
    double            evidence   = 0.0;
    std::size_t       late_count = 0; // late samples in [i - band + 1, i]
    for (std::size_t i = 0; i < m_deadline_samples_scratch.size(); ++i) {
        if (m_deadline_samples_scratch[i].late) {
            ++late_count;
        }
        if ((i >= band) && m_deadline_samples_scratch[i - band].late) {
            --late_count;
        }
        if (m_deadline_samples_scratch[i].late && (late_count >= m_tunables.deadline_evidence_min_late)) {
            evidence = m_deadline_samples_scratch[i].completion_margin + m_tunables.guard;
        }
    }
    return evidence;
}

} // namespace erhe::frame_pacing
