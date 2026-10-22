#include "erhe_frame_pacing/frame_pacer.hpp"
#include "erhe_verify/verify.hpp"

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

void Clock_record::init(const std::size_t capacity)
{
    m_clocks.assign(capacity, 0.0);
    m_works.assign(capacity, 0.0);
    m_scratch.reserve(capacity);
    m_head  = 0;
    m_count = 0;
}

void Clock_record::push(const double clock_ratio, const double work)
{
    if (m_clocks.empty()) {
        return;
    }
    m_clocks[m_head] = clock_ratio;
    m_works [m_head] = work;
    m_head = (m_head + 1) % m_clocks.size();
    if (m_count < m_clocks.size()) {
        ++m_count;
    }
}

auto Clock_record::clock_for_work(
    const double      work,
    const double      band,
    const double      p,
    const std::size_t min_samples,
    const double      fallback
) const -> double
{
    m_scratch.clear();
    const std::size_t capacity = m_clocks.size();
    const std::size_t start    = (capacity > 0) ? ((m_head + capacity - m_count) % capacity) : 0;
    const double      low      = work / band;
    const double      high     = work * band;
    for (std::size_t i = 0; i < m_count; ++i) {
        const std::size_t index = (start + i) % capacity;
        if ((m_works[index] >= low) && (m_works[index] <= high)) {
            m_scratch.push_back(m_clocks[index]);
        }
    }
    if ((m_scratch.size() < min_samples) || m_scratch.empty()) {
        return fallback;
    }
    std::sort(m_scratch.begin(), m_scratch.end());
    const double      raw_index = std::ceil(p * static_cast<double>(m_scratch.size())) - 1.0;
    const std::size_t clamped   = static_cast<std::size_t>(
        std::clamp(raw_index, 0.0, static_cast<double>(m_scratch.size() - 1))
    );
    return m_scratch[clamped];
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
    // C30: a variable refresh display needs its maximum refresh interval.
    ERHE_VERIFY((m_tunables.display != Present_display_kind::variable_refresh) || (m_tunables.max_refresh_interval > 0.0));
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
    m_deadline_offset_max = std::max(
        m_tunables.deadline_offset_initial,
        (m_tunables.deadline_offset_max > 0.0) ? m_tunables.deadline_offset_max : (m_tunables.deadline_offset_max_frac * t)
    );
    m_vsyncs          = m_tunables.min_vsyncs;
    m_min_vsyncs      = m_tunables.min_vsyncs;
    m_margin          = m_tunables.margin_initial_frac * t;
    m_streak_attribution = std::numeric_limits<double>::infinity();
    m_dwell_required     = m_tunables.upshift_dwell_time;
    m_prewarm_fill       = m_tunables.upshift_prewarm_seconds;

    m_cpu_window.init(m_tunables.history);
    m_gpu_window.init(m_tunables.history);
    m_cadence_cpu_window.init(m_tunables.history);
    m_cadence_gpu_window.init(m_tunables.history);
    m_clock_windows.resize(static_cast<std::size_t>(m_tunables.max_vsyncs) + 1);
    m_cpu_clock_windows.resize(static_cast<std::size_t>(m_tunables.max_vsyncs) + 1);
    for (Clock_record& record : m_clock_windows) {
        record.init(m_tunables.history);
    }
    for (Clock_record& record : m_cpu_clock_windows) {
        record.init(m_tunables.history);
    }
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
    if ((m_latency_seed > 0.0) && (window.count() < m_tunables.cold_start_seed_frames)) {
        // The visit's own samples are too few yet: the seed floors them (C28).
        return std::max(m_latency_seed, window.quantile(p, 0.0));
    }
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
    if ((m_latency_seed > 0.0) && (window.count() < m_tunables.cold_start_seed_frames)) {
        return std::max(m_latency_seed, window.quantile(0.5, 0.0));
    }
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
    return std::max(m_cpu_window.quantile(p, 0.0), m_gpu_window.quantile(p, 0.0)) + cadence_guard();
}

auto Frame_pacer::is_variable_refresh() const -> bool
{
    return m_tunables.display == Present_display_kind::variable_refresh;
}

auto Frame_pacer::cadence_guard() const -> double
{
    // The jitter allowance of the cadence tests (W, the reactive
    // attribution): on a fixed grid a frame that misses its vsync by a
    // little costs a whole refresh; on a VRR display it costs only its own
    // lateness (C30), so the allowance is 0 there.
    return is_variable_refresh() ? 0.0 : m_tunables.guard;
}

auto Frame_pacer::vrr_slots_max() const -> std::int64_t
{
    // C30: the most slots between two targets the display shows without a
    // repeat of the previous image in between.
    return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::floor((m_tunables.max_refresh_interval / m_grid_period) + 1e-9)));
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
    // C30: the slots between consecutive targets. On a VRR display a cadence
    // beyond the display's maximum refresh interval is spaced at the bound
    // (the display would repeat the previous image in between); the
    // cadence N itself still drives the cadence tests.
    const std::int64_t spacing = is_variable_refresh()
        ? std::min<std::int64_t>(m_vsyncs, vrr_slots_max())
        : static_cast<std::int64_t>(m_vsyncs);

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
            // On a FIFO queue every in-flight target holds (C24): a target
            // the reachability test skipped forward lies beyond its cadence,
            // and capping it there gave the next frame the same slot - a
            // late display on a path that drops nothing.
            const bool   fifo   = (m_tunables.present_queue == Present_queue_kind::fifo);
            std::int64_t anchor = m_feedback_slot;
            for (std::int64_t i = m_feedback_frame_id + 1; i < frame_id; ++i) {
                const Frame_entry* entry = find_entry(i);
                if (entry == nullptr) {
                    anchor += m_vsyncs;
                } else if (fifo) {
                    anchor = std::max(anchor + 1, entry->target_slot);
                } else {
                    anchor = std::max(anchor + 1, std::min(entry->target_slot, anchor + entry->vsyncs));
                }
            }
            anchor += spacing;
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
        if (is_variable_refresh() && (m_last_slot >= 0)) {
            // C30: a target more than the display's maximum refresh interval
            // after the previous one makes the display repeat the previous
            // image in between and hold this one to the repeat plus a
            // minimum interval. A frame that cannot make the nearer slot
            // displays when it is ready instead, which on a VRR display
            // costs only its lateness. The bound counts from the previous
            // target or from now, whichever is later: after a stall the
            // stale target would pin every target behind the release (the
            // cursor catching up one slot per frame).
            const std::int64_t now_slot = static_cast<std::int64_t>(std::floor((now - m_grid_phase) / m_grid_period));
            const std::int64_t base     = std::max(m_last_slot, now_slot);
            slot = std::max(m_last_slot + 1, std::min(slot, base + vrr_slots_max()));
        }
        release = std::max(now, slot_time(slot) - m_deadline_offset - budget);
    }

    m_next_slot     = slot + spacing;
    m_has_next_slot = true;
    m_last_slot     = slot;

    Frame_entry& entry = entry_for(frame_id);
    entry.target_slot = slot;
    entry.target_time = slot_time(slot);
    entry.vsyncs      = m_vsyncs;
    entry.prewarm     = (m_prewarm_left > 0);

    Schedule_decision decision;
    decision.frame_id           = frame_id;
    decision.release_time       = release;
    decision.target_slot        = slot;
    decision.target_time        = slot_time(slot);
    decision.predicted_display  = slot_time(slot);
    decision.predicted_duration = static_cast<double>(spacing) * m_grid_period;
    decision.vsyncs_per_frame   = m_vsyncs;
    // FR5 clamp: at most Q* images between completion and display, plus
    // the images a deadline offset of one or more cadence periods holds in
    // the queue by itself (each must be complete delta_pe before its vsync,
    // so frame k is released before frame k - 1 - Q* can have displayed).
    const std::int64_t deadline_images = static_cast<std::int64_t>(
        std::floor(m_deadline_offset / (static_cast<double>(spacing) * m_grid_period))
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
    decision.prewarm_seconds = (m_prewarm_left > 0) ? m_prewarm_fill : 0.0;
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
    const double       latency,
    const double       gpu_clock_ratio,
    const double       cpu_clock_ratio
)
{
    m_gpu_window.push(gpu_duration);

    const Frame_entry* scheduled = find_entry(frame_id);
    const int          n_used    = (scheduled != nullptr) ? scheduled->vsyncs : m_vsyncs;
    m_latency_windows[static_cast<std::size_t>(std::clamp(n_used, 1, m_tunables.max_vsyncs))].push(latency);
    if (n_used == m_vsyncs) {
        m_cadence_cpu_window.push(cpu_duration);
        m_cadence_gpu_window.push(gpu_duration);
    }
    // Clock records only from the cadence's steady state (C27).
    if ((n_used == m_vsyncs) && !m_has_cadence_start) {
        m_has_cadence_start = true;
        m_cadence_start     = gpu_end_time;
    }
    const bool clock_steady =
        (n_used == m_vsyncs) &&
        ((gpu_end_time - m_cadence_start) >= m_tunables.clock_settle_time) &&
        ((scheduled == nullptr) || !scheduled->prewarm);
    // Each with the frame's work (C29).
    m_last_gpu_work = gpu_duration * gpu_clock_ratio;
    m_last_cpu_work = cpu_duration * cpu_clock_ratio;
    if (clock_steady && (gpu_clock_ratio > 0.0)) {
        m_clock_windows[static_cast<std::size_t>(std::clamp(n_used, 1, m_tunables.max_vsyncs))].push(gpu_clock_ratio, m_last_gpu_work);
    }
    if (clock_steady && (cpu_clock_ratio > 0.0)) {
        m_cpu_clock_windows[static_cast<std::size_t>(std::clamp(n_used, 1, m_tunables.max_vsyncs))].push(cpu_clock_ratio, m_last_cpu_work);
    }

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
        if (n_used == m_vsyncs) {
            m_cadence_pressure = true;
        }
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
    const double cadence_budget = static_cast<double>(m_vsyncs) * m_grid_period;
    // The predictive downshift judges the current cadence by its own stage
    // times and pressure (C24): after an upshift the window still holds the
    // longer cadence's slower GPU frames and near-misses, and judged by
    // those the pacer went straight back to the longer cadence. The reactive
    // path guards the transition meanwhile.
    int required_vsyncs_here = m_vsyncs;
    if (m_cadence_gpu_window.count() >= m_tunables.cadence_min_samples) {
        const double p = m_tunables.quantile_p;
        const double w_here =
            std::max(m_cadence_cpu_window.quantile(p, 0.0), m_cadence_gpu_window.quantile(p, 0.0)) + cadence_guard();
        required_vsyncs_here = std::clamp(
            static_cast<int>(std::ceil(w_here / m_grid_period)),
            m_min_vsyncs,
            m_tunables.max_vsyncs
        );
    }

    // Upshift survival: no pressure-driven revert for 4*T_up after an
    // upshift means the new cadence holds - future upshifts are cheap.
    if (m_has_upshift_time && ((gpu_end_time - m_upshift_time) >= (4.0 * m_tunables.upshift_dwell_time))) {
        m_dwell_required   = m_tunables.upshift_dwell_time;
        m_has_upshift_time = false;
    }

    if (
        (m_consecutive_misses >= m_tunables.reactive_miss_threshold) &&
        ((m_streak_attribution + cadence_guard()) > cadence_budget)
    ) {
        // Reactive downshift: sustained overload evidenced by the missed
        // frames' own stage times.
        on_pressure_downshift(gpu_end_time);
        change_cadence(std::max(m_vsyncs + 1, required_vsyncs_here), false);
        m_consecutive_misses = 0;
        m_streak_attribution = std::numeric_limits<double>::infinity();
    } else if (
        (required_vsyncs_here > m_vsyncs) &&
        m_cadence_pressure &&
        ((m_completed_frames - m_last_pressure_frame) <= static_cast<std::int64_t>(m_tunables.history))
    ) {
        // Predictive downshift needs PRESSURE evidence (a recent miss or
        // near-miss), not the quantile alone: heavy-tailed load whose q95
        // exceeds the budget can still be fully sustainable (tail absorbed
        // by margin and queue elasticity - observer finding, step P2.1;
        // claim C10). Misses/near-misses are the honest signal; the margin
        // AIMD surfaces overload as near-misses well before drops.
        on_pressure_downshift(gpu_end_time);
        change_cadence(required_vsyncs_here, false);
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
        // Where the host measures the GPU and CPU clocks (C25), the stage
        // times at the next shorter cadence are predicted from the work (time
        // * clock ratio) at the clock last seen there - its low decile, or
        // the full clock before that cadence has a record: a processor that
        // clocks down at the longer cadence (Apple GPUs at N=2: 41-97 % of
        // the maximum against 93-98 % at N=1; P-cores ~53 % against 92-97 %)
        // makes its times there no evidence of the shorter cadence's cost,
        // and one that clocks down at the shorter cadence too must not be
        // judged at full clock. The clock seen there is the one seen for a
        // similar workload (C29): the governor answers the work, and the
        // empty editor's idle clock at N=1 once predicted a scene's work at
        // twice the period there.
        const std::size_t next_index = static_cast<std::size_t>(std::clamp(m_vsyncs - 1, 1, m_tunables.max_vsyncs));
        const auto at_next = [this](const double duration, const double ratio, const Clock_record& record) -> double {
            if (ratio <= 0.0) {
                return duration;
            }
            const double work       = duration * ratio;
            const double ratio_next = record.clock_for_work(work, m_tunables.clock_record_work_band, 0.1, m_tunables.cadence_min_samples, 1.0);
            return work / ratio_next;
        };
        const double next_period    = static_cast<double>(m_vsyncs - 1) * m_grid_period;
        if (m_prewarm_left > 0) {
            // Pre-warm (C26): the frame's own GPU time is measured at the
            // raised clock; switch once it - and the CPU's at the clock
            // recorded at N-1 - fits N-1's budget. A pre-warm that does not
            // get there is a failed upshift (backoff, C10).
            const double prewarmed_stage = std::max(at_next(cpu_duration, cpu_clock_ratio, m_cpu_clock_windows[next_index]), gpu_duration);
            if (prewarmed_stage <= (next_period - m_hysteresis)) {
                m_prewarm_left = 0;
                change_cadence(m_vsyncs - 1, true);
                m_has_upshift_time = true;
                m_upshift_time     = gpu_end_time;
            } else {
                --m_prewarm_left;
                if (m_prewarm_left == 0) {
                    if ((gpu_clock_ratio > 0.0) && (gpu_clock_ratio < m_tunables.upshift_prewarm_clock)) {
                        // The governor did not raise the clock: learn a larger fill.
                        m_prewarm_fill = std::min(2.0 * m_prewarm_fill, m_tunables.upshift_prewarm_max_frac * static_cast<double>(m_vsyncs) * m_grid_period);
                    } else {
                        m_dwell_required = m_dwell_required * 2.0;
                    }
                }
            }
            return;
        }
        const double stage_time = std::max(
            at_next(cpu_duration, cpu_clock_ratio, m_cpu_clock_windows[next_index]),
            at_next(gpu_duration, gpu_clock_ratio, m_clock_windows[next_index])
        );
        m_last_upshift_stage_time = stage_time;
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
                if ((m_prewarm_fill > 0.0) && (gpu_clock_ratio > 0.0)) {
                    m_prewarm_left = m_tunables.upshift_prewarm_frames;
                    reset_dwell();
                } else {
                    change_cadence(m_vsyncs - 1, true);
                    m_has_upshift_time = true;
                    m_upshift_time     = gpu_end_time;
                }
            }
        }
    } else {
        reset_dwell();
    }
}

auto Frame_pacer::get_upshift_state() const -> Upshift_state
{
    const std::size_t next_index = static_cast<std::size_t>(std::clamp(m_vsyncs - 1, 1, m_tunables.max_vsyncs));
    const Clock_record& gpu_record = m_clock_windows[next_index];
    const Clock_record& cpu_record = m_cpu_clock_windows[next_index];
    const double        band       = m_tunables.clock_record_work_band;
    const std::size_t   min_count  = m_tunables.cadence_min_samples;
    return Upshift_state{
        .dwell                  = m_dwell,
        .dwell_required         = m_dwell_required,
        .dwell_frames           = m_dwell_frames,
        .dwell_frames_loaded    = m_dwell_frames_loaded,
        .last_stage_time        = m_last_upshift_stage_time,
        .next_period            = static_cast<double>(std::max(m_vsyncs - 1, 1)) * m_grid_period,
        .gpu_clock_next         = gpu_record.clock_for_work(m_last_gpu_work, band, 0.1, min_count, 0.0),
        .cpu_clock_next         = cpu_record.clock_for_work(m_last_cpu_work, band, 0.1, min_count, 0.0),
        .gpu_clock_next_samples = gpu_record.count(),
        .prewarm_left           = m_prewarm_left,
        .prewarm_fill           = m_prewarm_fill
    };
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
    // The latency seed of the cadence entered (C28): the estimate at the
    // cadence left; the structural sum floors it only where that visit has
    // no measured estimate yet (the whole-history windows can still hold an
    // earlier load, such as the startup spike).
    const double p             = m_tunables.quantile_p;
    const bool   seed_measured = m_latency_windows[static_cast<std::size_t>(m_vsyncs)].count() >= m_tunables.cold_start_seed_frames;
    double       seed          = m_forced ? 0.0 : latency_estimate();
    if (!seed_measured) {
        seed = std::max(seed, m_cpu_window.quantile(p, 0.0) + m_gpu_window.quantile(p, 0.0));
    }
    m_vsyncs = clamped;
    reset_dwell();
    m_prewarm_left      = 0;
    m_has_cadence_start = false;
    m_cadence_cpu_window.clear();
    m_cadence_gpu_window.clear();
    m_cadence_pressure = false;
    // A cadence's latency window holds the current visit only (C28):
    // samples of an earlier visit (the startup spike at N=1) kept the release
    // budget at the old latency for a whole window after an upshift, and a
    // seed kept as a window sample carried such a spike down a chain of
    // short visits. Until the visit has cold_start_seed_frames samples the
    // seed floors the estimate (carry-over is a safe overestimate when
    // downshifting; doc/frame_pacing/algorithm.md section 5).
    m_latency_windows[static_cast<std::size_t>(m_vsyncs)].clear();
    m_latency_seed = (seed > 0.0) ? seed : m_tunables.refresh_period;
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
    if (is_variable_refresh()) {
        m_has_raw_feedback      = true;
        m_raw_feedback_previous = achieved_present_time;
        on_variable_refresh_feedback(frame_id, achieved_present_time);
        update_deadline_offset(frame_id, achieved_present_time, previous_display_time);
        return;
    }
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

void Frame_pacer::on_variable_refresh_feedback(const std::int64_t frame_id, const double achieved_present_time)
{
    // C30: a VRR display has no grid. The period stays refresh_period (the
    // minimum refresh interval): presented deltas are the pacer's own
    // targets or a late frame's ready time, never period evidence, so
    // neither the gross-error re-seed nor the PLL period branch runs. The
    // phase follows on-time displays only (innovation within guard).
    const Frame_entry* entry = find_entry(frame_id);
    std::int64_t achieved_slot = 0;
    if ((entry != nullptr) && (achieved_present_time > (entry->target_time + m_tunables.guard))) {
        // A late display re-anchors the slot cursor only: the late frame's
        // own slot S' is the first slot at or after its display, and the
        // next image goes at S' + 1 (the display shows nothing sooner than a
        // minimum interval after it). Re-basing the phase on the display
        // instead moves every in-flight target and costs more refreshes
        // (measured in the plant: mean present delta 8.59 against 8.50 ms
        // with a 20 ms frame every 50).
        achieved_slot = entry->target_slot + static_cast<std::int64_t>(
            std::ceil(((achieved_present_time - entry->target_time) / m_grid_period) - 1e-9)
        );
    } else {
        const double nearest_slot = std::round((achieved_present_time - m_grid_phase) / m_grid_period);
        const double error        = achieved_present_time - (m_grid_phase + (nearest_slot * m_grid_period));
        if (std::abs(error) < m_tunables.guard) {
            m_grid_phase += m_tunables.grid_tracker_gain * error;
        }
        achieved_slot = static_cast<std::int64_t>(std::round((achieved_present_time - m_grid_phase) / m_grid_period));
    }
    if (!m_has_feedback || (frame_id > m_feedback_frame_id)) {
        m_has_feedback      = true;
        m_feedback_frame_id = frame_id;
        m_feedback_slot     = achieved_slot;
    }
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
    if (
        is_variable_refresh() &&
        (previous_display_time > 0.0) &&
        (
            (previous_display_time > (entry.target_time - m_grid_period)) ||
            ((entry.target_time - previous_display_time) > m_tunables.max_refresh_interval)
        )
    ) {
        // C30: the VRR display could not show the frame at its target -
        // within a minimum interval of the previous image, or after a repeat
        // of it (more than the maximum interval between them, which holds
        // the frame to the repeat plus a minimum interval). A cascade, not a
        // deadline outcome.
        return;
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
