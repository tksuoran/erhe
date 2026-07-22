#include "windows/frame_pacing_window.hpp"

#include "erhe_frame_pacing/frame_pacing_observer.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace editor {

namespace {

constexpr float  s_graph_height    = 84.0f;
constexpr float  s_timeline_height = 34.0f;
constexpr double s_min_view        = 0.01;
constexpr double s_max_view        = 30.0;

constexpr ImU32 s_color_background   = IM_COL32( 24,  24,  28, 255);
constexpr ImU32 s_color_base_line    = IM_COL32(140, 140, 140, 160);
constexpr ImU32 s_color_refresh_line = IM_COL32(120, 120, 140,  70);
constexpr ImU32 s_color_planned      = IM_COL32(235, 235, 235, 255);
constexpr ImU32 s_color_actual       = IM_COL32(255, 160,  40, 255);
constexpr ImU32 s_color_present_delta= IM_COL32(255, 160,  40, 255);
constexpr ImU32 s_color_cpu_delta    = IM_COL32(100, 150, 255, 160);
constexpr ImU32 s_color_latency      = IM_COL32(120, 220, 255, 255);
constexpr ImU32 s_color_acquire      = IM_COL32(230, 220,  60, 255);
constexpr ImU32 s_color_present      = IM_COL32( 60, 220, 220, 255);
constexpr ImU32 s_color_highlight    = IM_COL32(255, 255, 255, 255);

// Base color per frame-in-flight slot; follows the engine's two frames in
// flight. Extend the table if that count ever changes.
[[nodiscard]] auto slot_color(const std::int64_t frame_id, const float brightness) -> ImU32
{
    const bool even = (frame_id % 2) == 0;
    const float r = even ? 200.0f : 50.0f;
    const float g = even ? 60.0f  : 180.0f;
    const float b = 60.0f;
    return IM_COL32(
        static_cast<int>(r * brightness),
        static_cast<int>(g * brightness),
        static_cast<int>(b * brightness),
        255
    );
}

} // anonymous namespace

auto Frame_pacing_window::Frame_history::at(const std::size_t index) -> Frame_sample&
{
    return (*m_chunks[index / s_chunk_size])[index % s_chunk_size];
}

auto Frame_pacing_window::Frame_history::at(const std::size_t index) const -> const Frame_sample&
{
    return (*m_chunks[index / s_chunk_size])[index % s_chunk_size];
}

auto Frame_pacing_window::Frame_history::find(const std::int64_t frame_id) -> Frame_sample*
{
    if ((m_count == 0) || (frame_id < m_first_frame_id)) {
        return nullptr;
    }
    const std::size_t index = static_cast<std::size_t>(frame_id - m_first_frame_id);
    return (index < m_count) ? &at(index) : nullptr;
}

auto Frame_pacing_window::Frame_history::find(const std::int64_t frame_id) const -> const Frame_sample*
{
    if ((m_count == 0) || (frame_id < m_first_frame_id)) {
        return nullptr;
    }
    const std::size_t index = static_cast<std::size_t>(frame_id - m_first_frame_id);
    return (index < m_count) ? &at(index) : nullptr;
}

auto Frame_pacing_window::Frame_history::append(const std::int64_t frame_id, const double append_time) -> Frame_sample&
{
    if (m_count == 0) {
        m_first_frame_id = frame_id;
    }
    // Fill any id gap since the previous append (observer dormant frames)
    // with placeholders so lookup stays a dense index; placeholders draw as
    // gaps unless the record rescan still finds their data.
    while (m_first_frame_id + static_cast<std::int64_t>(m_count) <= frame_id) {
        const std::size_t index = m_count;
        const std::size_t chunk = index / s_chunk_size;
        if (chunk >= m_chunks.size()) {
            m_chunks.push_back(std::make_unique<std::array<Frame_sample, s_chunk_size>>());
        }
        Frame_sample& sample = at(index);
        sample             = Frame_sample{};
        sample.frame_id    = m_first_frame_id + static_cast<std::int64_t>(index);
        sample.append_time = append_time;
        ++m_count;
    }
    return *find(frame_id);
}

auto Frame_pacing_window::Frame_history::lower_bound(const double t) const -> std::size_t
{
    std::size_t low  = 0;
    std::size_t high = m_count;
    while (low < high) {
        const std::size_t mid = (low + high) / 2;
        if (at(mid).append_time < t) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low;
}

void Frame_pacing_window::Frame_history::clear()
{
    m_chunks.clear();
    m_first_frame_id = 0;
    m_count          = 0;
}

Frame_pacing_window::Frame_pacing_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Frame Pacing", "frame_pacing"}
{
    set_min_size(480.0f, 320.0f);
}

void Frame_pacing_window::collect(
    erhe::frame_pacing::Frame_time_recorder&         recorder,
    const erhe::frame_pacing::Frame_pacing_observer& observer,
    const double                                     refresh_period,
    const double                                     now
)
{
    m_refresh_period = refresh_period;
    m_now            = now;

    const std::int64_t latest = recorder.get_latest_frame_id();
    if (latest < 0) {
        return;
    }

    const erhe::frame_pacing::Frame_pacer* const pacer_for_grid = observer.get_pacer();
    const erhe::frame_pacing::Schedule_decision& decision = observer.get_last_decision();
    const bool capture_full =
        (m_max_capture_frames > 0) &&
        (m_history.size() >= m_max_capture_frames);
    if (!capture_full && (decision.frame_id == latest) && (latest > m_latest_frame_id)) {
        Frame_sample& sample = m_history.append(latest, now);
        sample.planned_vsyncs    = decision.vsyncs_per_frame;
        sample.release_time      = decision.release_time;
        sample.predicted_display = decision.predicted_display;
        sample.wait_id           = decision.wait_id;
        if (pacer_for_grid != nullptr) {
            sample.grid_phase  = pacer_for_grid->get_grid_phase();
            sample.grid_period = pacer_for_grid->get_grid_period();
        }
        m_latest_frame_id        = latest;
    }

    // Re-snapshot recent frames: GPU spans and presentation feedback land
    // several frames after the CPU slot closes. A frame that scrolls out of
    // the recorder's ring keeps whatever it had (gaps, not fabricated
    // values); the capture itself is persistent (U9).
    const erhe::frame_pacing::Frame_pacer* const pacer = observer.get_pacer();
    const std::int64_t first = std::max<std::int64_t>(0, latest - 64);
    for (std::int64_t frame_id = first; frame_id <= latest; ++frame_id) {
        Frame_sample* const sample = m_history.find(frame_id);
        if ((sample == nullptr) || sample->complete) {
            continue;
        }
        const erhe::frame_pacing::Frame_time_record* const record = recorder.find(frame_id);
        if (record != nullptr) {
            sample->record = *record;
            if (record->achieved_present_time > 0.0) {
                m_grid_phase_time = std::max(m_grid_phase_time, record->achieved_present_time);
            }
        }
        if (pacer != nullptr) {
            const erhe::frame_pacing::Frame_stats stats = pacer->get_frame_stats(frame_id);
            if (stats.valid) {
                sample->stats_valid = true;
                sample->self_miss   = stats.self_miss;
                sample->slack       = stats.slack;
            }
        }
        sample->complete =
            (sample->record.cpu_slot_end          > 0.0) &&
            (sample->record.gpu_frame_end         > 0.0) &&
            (sample->record.achieved_present_time > 0.0);
    }
}

void Frame_pacing_window::run_simulated_workload()
{
    const double min_s = static_cast<double>(m_wait_min_ms) * 0.001;
    const double max_s = static_cast<double>(std::max(m_wait_min_ms, m_wait_max_ms)) * 0.001;
    double duration = 0.0;
    if (m_spike_requested) {
        duration          = max_s;
        m_spike_requested = false;
    } else if (max_s > min_s) {
        duration = std::uniform_real_distribution<double>{min_s, max_s}(m_random);
    } else {
        duration = min_s;
    }
    if (duration <= 0.0) {
        return;
    }
    // Busy-wait: precise (sleep_for wake error makes the dialed value a lie,
    // see P0.5) and burns CPU, which is exactly what simulated load should do.
    const double t0 = erhe::frame_pacing::Frame_time_recorder::now();
    while ((erhe::frame_pacing::Frame_time_recorder::now() - t0) < duration) {
    }
}

void Frame_pacing_window::set_workload_range_ms(const float min_ms, const float max_ms)
{
    m_wait_min_ms = std::clamp(min_ms, 0.0f, 60.0f);
    m_wait_max_ms = std::clamp(std::max(max_ms, m_wait_min_ms), 0.0f, 60.0f);
}

void Frame_pacing_window::clear_capture()
{
    m_history.clear();
    m_latest_frame_id        = -1;
    m_hovered_frame          = -1;
    m_hovered_frame_previous = -1;
}

auto Frame_pacing_window::find_sample(const std::int64_t frame_id) const -> const Frame_sample*
{
    return m_history.find(frame_id);
}

auto Frame_pacing_window::get_capture_frame_count() const -> std::size_t
{
    return m_history.size();
}

auto Frame_pacing_window::get_capture_first_frame_id() const -> std::int64_t
{
    return m_history.empty() ? -1 : m_history.first_frame_id();
}

auto Frame_pacing_window::get_capture_sample(const std::int64_t frame_id) const -> const Frame_sample*
{
    return m_history.find(frame_id);
}

auto Frame_pacing_window::next_achieved_after(const std::int64_t frame_id) const -> double
{
    for (std::int64_t next = frame_id + 1; next <= std::min(m_latest_frame_id, frame_id + 16); ++next) {
        const Frame_sample* const sample = find_sample(next);
        if ((sample != nullptr) && (sample->record.achieved_present_time > 0.0)) {
            return sample->record.achieved_present_time;
        }
    }
    return 0.0;
}

auto Frame_pacing_window::actual_vsyncs(const Frame_sample& sample) const -> int
{
    if ((sample.record.achieved_present_time <= 0.0) || (m_refresh_period <= 0.0)) {
        return 0;
    }
    const double next = next_achieved_after(sample.frame_id);
    if (next <= sample.record.achieved_present_time) {
        return 0;
    }
    return std::max(1, static_cast<int>(std::lround((next - sample.record.achieved_present_time) / m_refresh_period)));
}

auto Frame_pacing_window::release_of(const Frame_sample& sample) const -> double
{
    return (sample.record.pacer_wait_end > 0.0) ? sample.record.pacer_wait_end : sample.record.cpu_slot_begin;
}

auto Frame_pacing_window::Region::time_to_x(const double t) const -> float
{
    const double u = (t - t_begin) / (t_end - t_begin);
    return x0 + static_cast<float>(u) * (x1 - x0);
}

auto Frame_pacing_window::visible_index_range(const Region& region) const -> std::pair<std::size_t, std::size_t>
{
    // Pad the time span so bars that begin before the view but extend into
    // it (long frames, multi-refresh display spans) are still drawn.
    constexpr double pad = 0.5;
    const std::size_t begin = m_history.lower_bound(region.t_begin - pad);
    const std::size_t end   = m_history.lower_bound(region.t_end + pad);
    return {begin, end};
}

auto Frame_pacing_window::begin_region(const char* id, const float height) -> Region
{
    Region region;
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float  width    = std::max(64.0f, ImGui::GetContentRegionAvail().x);
    region.x0 = position.x;
    region.x1 = position.x + width;
    region.y0 = position.y;
    region.y1 = position.y + height;

    const double t_end = m_paused ? m_view_end : m_now;
    region.t_end   = t_end;
    region.t_begin = t_end - m_view_duration;

    ImGui::InvisibleButton(id, ImVec2{width, height});
    region.hovered = ImGui::IsItemHovered();

    ImDrawList* const draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(ImVec2{region.x0, region.y0}, ImVec2{region.x1, region.y1}, s_color_background);

    // Refresh cycle boundaries (U8): from the pacer's DYNAMIC grid as
    // captured with the sample nearest the visible range - locally accurate
    // even for old paused data. The real period deviates from the queried
    // one (~11 ppm measured), so a grid anchored once to a recent present
    // drifts ~0.6 ms per minute of distance from the anchor.
    double grid_phase  = m_grid_phase_time;   // fallback: latest achieved present
    double grid_period = m_refresh_period;
    if (!m_history.empty()) {
        std::size_t index = m_history.lower_bound(region.t_end);
        if (index >= m_history.size()) {
            index = m_history.size() - 1;
        }
        const Frame_sample& sample = m_history.at(index);
        if (sample.grid_period > 0.0) {
            grid_phase  = sample.grid_phase;
            grid_period = sample.grid_period;
        }
    }
    if ((grid_period > 0.0) && ((grid_phase != 0.0) || !m_history.empty())) {
        const double first_n = std::ceil((region.t_begin - grid_phase) / grid_period);
        for (double n = first_n; ; n += 1.0) {
            const double t = grid_phase + n * grid_period;
            if (t > region.t_end) {
                break;
            }
            const float x = region.time_to_x(t);
            draw_list->AddLine(ImVec2{x, region.y0}, ImVec2{x, region.y1}, s_color_refresh_line);
        }
    }

    // Single horizontal base line.
    draw_list->AddLine(ImVec2{region.x0, region.y1 - 1.0f}, ImVec2{region.x1, region.y1 - 1.0f}, s_color_base_line);

    // Pan / zoom (U8): interacting implicitly pauses.
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && (io.MouseDelta.x != 0.0f)) {
        if (!m_paused) {
            m_paused   = true;
            m_view_end = region.t_end;
        }
        m_view_end -= static_cast<double>(io.MouseDelta.x) * m_view_duration / static_cast<double>(width);
        m_view_end  = std::min(m_view_end, m_now);
    }
    if (region.hovered && (io.MouseWheel != 0.0f)) {
        if (!m_paused) {
            m_paused   = true;
            m_view_end = region.t_end;
        }
        const double anchor = region.t_begin + (region.t_end - region.t_begin) * static_cast<double>((io.MousePos.x - region.x0) / width);
        const double factor = std::pow(0.8, static_cast<double>(io.MouseWheel));
        m_view_duration = std::clamp(m_view_duration * factor, s_min_view, s_max_view);
        m_view_end      = std::min(anchor + (m_view_end - anchor) * (m_view_duration / (region.t_end - region.t_begin)), m_now);
    }

    return region;
}

void Frame_pacing_window::workload_ui()
{
    ImGui::TextUnformatted("Simulated workload (extra per-frame wait, busy-wait)");
    // Each slider is clamped by the other: dragging one never moves the
    // other, it just stops at it.
    ImGui::SliderFloat("Min ms", &m_wait_min_ms, 0.0f, 60.0f, "%.2f");
    m_wait_min_ms = std::min(m_wait_min_ms, m_wait_max_ms);
    ImGui::SliderFloat("Max ms", &m_wait_max_ms, 0.0f, 60.0f, "%.2f");
    m_wait_max_ms = std::max(m_wait_max_ms, m_wait_min_ms);
    ImGui::SameLine();
    if (ImGui::Button("Spike")) {
        m_spike_requested = true;
    }
    ImGui::SetItemTooltip("Inject the configured maximum for a single frame (impulse scenario)");
    if ((m_wait_min_ms > 0.0f) || (m_wait_max_ms > 0.0f)) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4{1.0f, 0.6f, 0.2f, 1.0f}, "active");
    }
}

void Frame_pacing_window::time_axis_ui()
{
    if (ImGui::Button(m_paused ? "Unpause" : "Pause")) {
        if (m_paused) {
            m_paused = false;
        } else {
            m_paused   = true;
            m_view_end = m_now;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        clear_capture();
    }
    ImGui::SetItemTooltip("Drop the captured history (the capture grows without bound until cleared)");
    ImGui::SameLine();
    {
        int max_frames = static_cast<int>(m_max_capture_frames);
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragInt("Max frames to record", &max_frames, 16.0f, 0, 1000000)) {
            m_max_capture_frames = static_cast<std::size_t>(std::max(0, max_frames));
        }
        ImGui::SetItemTooltip(
            "Recording stops once the capture holds this many frames (0 = unbounded).\n"
            "Bounds memory and the capture-size-dependent draw cost; combine with\n"
            "Clear for a bounded 'record a run' capture."
        );
    }
    ImGui::SameLine();
    ImGui::Text(
        "%s | view %.0f ms | capture %zu frames (%.1f MB) | drag to pan, wheel to zoom (pauses)",
        m_paused ? "paused" : "following",
        m_view_duration * 1000.0,
        m_history.size(),
        static_cast<double>(m_history.size() * sizeof(Frame_sample)) / (1024.0 * 1024.0)
    );
}

void Frame_pacing_window::draw_cadence_graph(const Region& region)
{
    ImDrawList* const draw_list = ImGui::GetWindowDrawList();
    const float y_max_value = 9.0f;
    const auto value_to_y = [&](const float v) { return region.y1 - (v / y_max_value) * (region.y1 - region.y0); };

    const auto [index_begin, index_end] = visible_index_range(region);
    for (std::size_t index = index_begin; index < index_end; ++index) {
        const Frame_sample& sample = m_history.at(index);
        const double begin = sample.record.cpu_slot_begin;
        if (begin <= 0.0) {
            continue; // gap placeholder
        }
        const double end = (sample.record.cpu_slot_end > begin) ? sample.record.cpu_slot_end : begin + 0.001;
        if ((begin > region.t_end) || (end < region.t_begin)) {
            continue;
        }
        const float x0 = region.time_to_x(std::max(begin, region.t_begin));
        const float x1 = region.time_to_x(std::min(end,   region.t_end));
        if (sample.planned_vsyncs > 0) {
            const float y = value_to_y(static_cast<float>(sample.planned_vsyncs));
            draw_list->AddLine(ImVec2{x0, y}, ImVec2{x1, y}, s_color_planned, 3.0f);
        }
        const int actual = actual_vsyncs(sample);
        if (actual > 0) {
            const float y = value_to_y(static_cast<float>(actual));
            draw_list->AddLine(ImVec2{x0, y}, ImVec2{x1, y}, s_color_actual, 1.5f);
        }
    }
    draw_list->AddText(ImVec2{region.x0 + 4.0f, region.y0 + 2.0f}, s_color_planned, "cadence: planned (white) / actual (orange), vsyncs per frame");
}

void Frame_pacing_window::draw_delta_graph(const Region& region)
{
    ImDrawList* const draw_list = ImGui::GetWindowDrawList();
    const auto [index_begin, index_end] = visible_index_range(region);

    // Auto-scale to the largest delta in view, floored to 3 refresh periods.
    double y_max = (m_refresh_period > 0.0) ? 3.0 * m_refresh_period * 1000.0 : 30.0;
    for (std::size_t index = std::max<std::size_t>(index_begin, 1); index < index_end; ++index) {
        const Frame_sample& sample   = m_history.at(index);
        const Frame_sample& previous = m_history.at(index - 1);
        if ((sample.record.achieved_present_time > 0.0) && (previous.record.achieved_present_time > 0.0)) {
            y_max = std::max(y_max, (sample.record.achieved_present_time - previous.record.achieved_present_time) * 1000.0);
        }
    }
    const auto value_to_y = [&](const double v_ms) {
        return region.y1 - static_cast<float>(v_ms / (y_max * 1.15)) * (region.y1 - region.y0);
    };

    for (std::size_t index = std::max<std::size_t>(index_begin, 1); index < index_end; ++index) {
        const Frame_sample& sample   = m_history.at(index);
        const Frame_sample& previous = m_history.at(index - 1);
        const double begin = sample.record.cpu_slot_begin;
        if ((begin <= 0.0) || (begin > region.t_end)) {
            continue;
        }
        const double end = (sample.record.cpu_slot_end > begin) ? sample.record.cpu_slot_end : begin + 0.001;
        const float x0 = region.time_to_x(std::max(begin, region.t_begin));
        const float x1 = region.time_to_x(std::min(end,   region.t_end));
        if (previous.record.cpu_slot_begin > 0.0) {
            const double cpu_delta_ms = (begin - previous.record.cpu_slot_begin) * 1000.0;
            const float y = value_to_y(cpu_delta_ms);
            draw_list->AddLine(ImVec2{x0, y}, ImVec2{x1, y}, s_color_cpu_delta, 1.0f);
        }
        if ((sample.record.achieved_present_time > 0.0) && (previous.record.achieved_present_time > 0.0)) {
            const double present_delta_ms = (sample.record.achieved_present_time - previous.record.achieved_present_time) * 1000.0;
            const float y = value_to_y(present_delta_ms);
            draw_list->AddLine(ImVec2{x0, y}, ImVec2{x1, y}, s_color_present_delta, 2.0f);
        }
    }
    char label[128];
    std::snprintf(label, sizeof(label), "frame delta ms: present-to-present (orange) / cpu-to-cpu (blue), scale 0..%.1f", y_max * 1.15);
    draw_list->AddText(ImVec2{region.x0 + 4.0f, region.y0 + 2.0f}, s_color_planned, label);
}

void Frame_pacing_window::draw_latency_graph(const Region& region)
{
    ImDrawList* const draw_list = ImGui::GetWindowDrawList();
    const auto [index_begin, index_end] = visible_index_range(region);

    double y_max = (m_refresh_period > 0.0) ? 4.0 * m_refresh_period * 1000.0 : 40.0;
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t index = index_begin; index < index_end; ++index) {
            const Frame_sample& sample = m_history.at(index);
            const double begin = sample.record.cpu_slot_begin;
            if ((begin <= 0.0) || (begin > region.t_end)) {
                continue;
            }
            const int actual = actual_vsyncs(sample);
            const double release = release_of(sample);
            if ((actual <= 0) || (release <= 0.0) || (sample.record.achieved_present_time <= 0.0)) {
                continue; // feedback not (yet) available: gap, not a guess
            }
            const double midpoint   = sample.record.achieved_present_time + (actual - 1) * m_refresh_period * 0.5;
            const double latency_ms = (midpoint - release) * 1000.0;
            if (pass == 0) {
                y_max = std::max(y_max, latency_ms);
            } else {
                const double end = (sample.record.cpu_slot_end > begin) ? sample.record.cpu_slot_end : begin + 0.001;
                const float x0 = region.time_to_x(std::max(begin, region.t_begin));
                const float x1 = region.time_to_x(std::min(end,   region.t_end));
                const float y  = region.y1 - static_cast<float>(latency_ms / (y_max * 1.15)) * (region.y1 - region.y0);
                draw_list->AddLine(ImVec2{x0, y}, ImVec2{x1, y}, s_color_latency, 2.0f);
            }
        }
    }
    char label[128];
    std::snprintf(label, sizeof(label), "input latency ms: release to visibility midpoint, scale 0..%.1f", y_max * 1.15);
    draw_list->AddText(ImVec2{region.x0 + 4.0f, region.y0 + 2.0f}, s_color_planned, label);
}

void Frame_pacing_window::frame_tooltip(const Frame_sample& sample, const char* const section)
{
    // Inspecting implicitly pauses (U8).
    if (!m_paused) {
        m_paused   = true;
        m_view_end = m_now;
    }
    m_hovered_frame = sample.frame_id;

    const erhe::frame_pacing::Frame_time_record& r = sample.record;
    const double base = r.cpu_slot_begin;
    const auto rel_ms = [base](const double t) { return (t > 0.0) ? (t - base) * 1000.0 : -1.0; };
    ImGui::BeginTooltip();
    ImGui::Text("frame %lld  slot %lld%s%s", static_cast<long long>(sample.frame_id), static_cast<long long>(sample.frame_id % 2), (section != nullptr) ? "  " : "", (section != nullptr) ? section : "");
    ImGui::Separator();
    // Label/value alignment via a table: space padding cannot line up with
    // a proportional font.
    if (ImGui::BeginTable("##frame_tooltip", 2, ImGuiTableFlags_SizingFixedFit)) {
        const auto row = [](const char* const label, const char* const format, auto... args) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            ImGui::Text(format, args...);
        };
        row("planned vsyncs", "%d", sample.planned_vsyncs);
        const int actual = actual_vsyncs(sample);
        if (actual > 0) {
            row("actual vsyncs", "%d", actual);
        }
        row("wait_id", "%lld", static_cast<long long>(sample.wait_id));
        if (sample.stats_valid) {
            row("slack", "%.3f ms%s", sample.slack * 1000.0, sample.self_miss ? "  SELF-MISS" : "");
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("relative to");
        ImGui::TableNextColumn();
        ImGui::TextDisabled("cpu slot begin, ms (-1 = not recorded)");
        row("cpu slot",         "%.3f .. %.3f (service %.3f)", 0.0, rel_ms(r.cpu_slot_end), r.cpu_service_time() * 1000.0);
        row("pacer wait",       "%.3f .. %.3f", rel_ms(r.pacer_wait_begin), rel_ms(r.pacer_wait_end));
        row("fence waits",      "%.3f (accumulated)", r.fence_wait_duration * 1000.0);
        row("acquire",          "%.3f .. %.3f", rel_ms(r.acquire_begin), rel_ms(r.acquire_end));
        row("submit",           "%.3f", rel_ms(r.submit_time));
        row("present request",  "%.3f .. %.3f", rel_ms(r.present_request_time), rel_ms(r.present_return_time));
        row("gpu",              "%.3f .. %.3f (span %.3f)", rel_ms(r.gpu_frame_begin), rel_ms(r.gpu_frame_end), (r.gpu_frame_end > r.gpu_frame_begin) ? (r.gpu_frame_end - r.gpu_frame_begin) * 1000.0 : -1.0);
        row("achieved present", "%.3f", rel_ms(r.achieved_present_time));
        if ((actual > 0) && (r.achieved_present_time > 0.0)) {
            const double midpoint = r.achieved_present_time + (actual - 1) * m_refresh_period * 0.5;
            row("input latency", "%.3f ms", (midpoint - release_of(sample)) * 1000.0);
        }
        ImGui::EndTable();
    }
    ImGui::EndTooltip();
}

void Frame_pacing_window::draw_cpu_timeline(const Region& region)
{
    ImDrawList* const draw_list = ImGui::GetWindowDrawList();
    const float y0 = region.y0 + 2.0f;
    const float y1 = region.y1 - 3.0f;
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    const auto [index_begin, index_end] = visible_index_range(region);
    for (std::size_t index = index_begin; index < index_end; ++index) {
        const Frame_sample& sample = m_history.at(index);
        const erhe::frame_pacing::Frame_time_record& r = sample.record;
        const double begin = r.cpu_slot_begin;
        if (begin <= 0.0) {
            continue;
        }
        const double end = (r.cpu_slot_end > begin) ? r.cpu_slot_end : m_now;
        if ((begin > region.t_end) || (end < region.t_begin)) {
            continue;
        }
        const float x0 = region.time_to_x(std::max(begin, region.t_begin));
        const float x1 = std::max(x0 + 1.0f, region.time_to_x(std::min(end, region.t_end)));

        // Active work at full brightness; the pacer wait span dimmed (U4).
        draw_list->AddRectFilled(ImVec2{x0, y0}, ImVec2{x1, y1}, slot_color(sample.frame_id, 1.0f));
        if (r.pacer_wait_end > r.pacer_wait_begin) {
            const float wx0 = region.time_to_x(std::clamp(r.pacer_wait_begin, region.t_begin, region.t_end));
            const float wx1 = region.time_to_x(std::clamp(r.pacer_wait_end,   region.t_begin, region.t_end));
            if (wx1 > wx0) {
                draw_list->AddRectFilled(ImVec2{wx0, y0}, ImVec2{wx1, y1}, slot_color(sample.frame_id, 0.4f));
            }
        }
        // Involuntary-wait sections marked with outline colors. The fence
        // wait is recorded as an accumulated duration only (no span) and is
        // therefore shown in the tooltip, not drawn.
        if (r.acquire_end > r.acquire_begin) {
            const float ax0 = region.time_to_x(std::clamp(r.acquire_begin, region.t_begin, region.t_end));
            const float ax1 = region.time_to_x(std::clamp(r.acquire_end,   region.t_begin, region.t_end));
            if (ax1 > ax0) {
                draw_list->AddRect(ImVec2{ax0, y0}, ImVec2{ax1, y1}, s_color_acquire);
            }
        }
        if (r.present_return_time > r.present_request_time) {
            const float px0 = region.time_to_x(std::clamp(r.present_request_time, region.t_begin, region.t_end));
            const float px1 = region.time_to_x(std::clamp(r.present_return_time,  region.t_begin, region.t_end));
            if (px1 > px0) {
                draw_list->AddRect(ImVec2{px0, y0}, ImVec2{px1, y1}, s_color_present);
            }
        }
        if (sample.frame_id == m_hovered_frame_previous) {
            draw_list->AddRect(ImVec2{x0 - 1.0f, y0 - 1.0f}, ImVec2{x1 + 1.0f, y1 + 1.0f}, s_color_highlight);
        }
        if (region.hovered && (mouse.x >= x0) && (mouse.x < x1) && (mouse.y >= y0) && (mouse.y < y1)) {
            const char* section = "section: active work";
            if ((r.pacer_wait_end > r.pacer_wait_begin)) {
                const double t = region.t_begin + (region.t_end - region.t_begin) * static_cast<double>((mouse.x - region.x0) / (region.x1 - region.x0));
                if ((t >= r.pacer_wait_begin) && (t < r.pacer_wait_end)) {
                    section = "section: pacer wait";
                } else if ((t >= r.acquire_begin) && (t < r.acquire_end)) {
                    section = "section: acquire";
                } else if ((t >= r.present_request_time) && (t < r.present_return_time)) {
                    section = "section: present";
                }
            }
            frame_tooltip(sample, section);
        }
    }
}

void Frame_pacing_window::draw_gpu_timeline(const Region& region)
{
    ImDrawList* const draw_list = ImGui::GetWindowDrawList();
    const float y0 = region.y0 + 2.0f;
    const float y1 = region.y1 - 3.0f;
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    const auto [index_begin, index_end] = visible_index_range(region);
    for (std::size_t index = index_begin; index < index_end; ++index) {
        const Frame_sample& sample = m_history.at(index);
        const erhe::frame_pacing::Frame_time_record& r = sample.record;
        if (r.gpu_frame_end <= r.gpu_frame_begin) {
            continue;
        }
        if ((r.gpu_frame_begin > region.t_end) || (r.gpu_frame_end < region.t_begin)) {
            continue;
        }
        const float x0 = region.time_to_x(std::max(r.gpu_frame_begin, region.t_begin));
        const float x1 = std::max(x0 + 1.0f, region.time_to_x(std::min(r.gpu_frame_end, region.t_end)));
        draw_list->AddRectFilled(ImVec2{x0, y0}, ImVec2{x1, y1}, slot_color(sample.frame_id, 1.0f));
        if (sample.frame_id == m_hovered_frame_previous) {
            draw_list->AddRect(ImVec2{x0 - 1.0f, y0 - 1.0f}, ImVec2{x1 + 1.0f, y1 + 1.0f}, s_color_highlight);
        }
        if (region.hovered && (mouse.x >= x0) && (mouse.x < x1) && (mouse.y >= y0) && (mouse.y < y1)) {
            frame_tooltip(sample, nullptr);
        }
    }
}

void Frame_pacing_window::draw_display_timeline(const Region& region)
{
    ImDrawList* const draw_list = ImGui::GetWindowDrawList();
    const float y0 = region.y0 + 2.0f;
    const float y1 = region.y1 - 3.0f;
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    const auto [index_begin, index_end] = visible_index_range(region);
    for (std::size_t index = index_begin; index < index_end; ++index) {
        const Frame_sample& sample = m_history.at(index);
        const double achieved = sample.record.achieved_present_time;
        if (achieved <= 0.0) {
            continue;
        }
        const double next = next_achieved_after(sample.frame_id);
        const double end  = (next > achieved) ? next : ((m_refresh_period > 0.0) ? achieved + m_refresh_period : achieved);
        if ((achieved > region.t_end) || (end < region.t_begin)) {
            continue;
        }
        // First refresh cycle full brightness, remaining cycles dimmed (U6).
        const double first_end = std::min(end, achieved + ((m_refresh_period > 0.0) ? m_refresh_period : 0.0));
        const float x0 = region.time_to_x(std::max(achieved, region.t_begin));
        const float xm = std::max(x0 + 1.0f, region.time_to_x(std::clamp(first_end, region.t_begin, region.t_end)));
        const float x1 = std::max(xm, region.time_to_x(std::min(end, region.t_end)));
        draw_list->AddRectFilled(ImVec2{x0, y0}, ImVec2{xm, y1}, slot_color(sample.frame_id, 1.0f));
        if (x1 > xm) {
            draw_list->AddRectFilled(ImVec2{xm, y0}, ImVec2{x1, y1}, slot_color(sample.frame_id, 0.5f));
        }
        if (sample.frame_id == m_hovered_frame_previous) {
            draw_list->AddRect(ImVec2{x0 - 1.0f, y0 - 1.0f}, ImVec2{x1 + 1.0f, y1 + 1.0f}, s_color_highlight);
        }
        if (region.hovered && (mouse.x >= x0) && (mouse.x < x1) && (mouse.y >= y0) && (mouse.y < y1)) {
            frame_tooltip(sample, nullptr);
        }
    }
}

void Frame_pacing_window::imgui()
{
    workload_ui();
    ImGui::Separator();

    if (m_history.empty() || (m_now <= 0.0)) {
        ImGui::TextUnformatted("No frame pacing data (pacer dormant: display refresh period not known yet).");
        return;
    }

    time_axis_ui();

    m_hovered_frame = -1;

    // Distribute the remaining window height over the six plot rows: each
    // graph gets 2.5 units, each timeline 1 unit. The configured constants
    // are minimums - a small window scrolls instead of collapsing the plots.
    const ImGuiStyle& style        = ImGui::GetStyle();
    const float       label_height = ImGui::GetTextLineHeightWithSpacing();
    const float       available    =
        ImGui::GetContentRegionAvail().y
        - (3.0f * label_height)          // timeline label rows
        - (6.0f * style.ItemSpacing.y);  // inter-region spacing
    const float unit            = available / 10.5f; // 3 * 2.5 + 3 * 1
    const float graph_height    = std::max(s_graph_height,    2.5f * unit);
    const float timeline_height = std::max(s_timeline_height, unit);

    draw_cadence_graph (begin_region("##cadence",  graph_height));
    draw_delta_graph   (begin_region("##delta",    graph_height));
    draw_latency_graph (begin_region("##latency",  graph_height));
    ImGui::TextUnformatted("CPU");
    draw_cpu_timeline  (begin_region("##cpu",      timeline_height));
    ImGui::TextUnformatted("GPU");
    draw_gpu_timeline  (begin_region("##gpu",      timeline_height));
    ImGui::TextUnformatted("Display");
    draw_display_timeline(begin_region("##display", timeline_height));

    m_hovered_frame_previous = m_hovered_frame;
}

}
