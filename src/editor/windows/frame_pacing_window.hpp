#pragma once

// Frame pacing verification UI (doc/frame_pacing_user_interface.md).
//
// One ImGui window that (a) provides an adjustable simulated CPU workload
// (U2) and (b) visualizes the frame time records and pacer decisions as
// graphs and CPU/GPU/display timelines (U3-U8), so a human can verify
// pacer behavior on real hardware without reading logs.
//
// Data path (U9): consumes the existing frame record ring and the
// observer's schedule decisions only; copies them into a PERSISTENT
// capture owned by the window. The capture is append-only and unbounded
// (the 120-frame decision ring recycles quickly; a paused or panned view
// must never lose the data under inspection). "Clear" is the only way
// data is dropped. Missing data (feedback not yet delivered, tier OFF)
// renders as gaps, never as fabricated values.
//
// Future serialization note: the capture is a dense frame-id-indexed
// sequence of fixed-size samples. A compact on-disk format should store
// per-field columns with delta encoding - frame begin times as deltas
// between consecutive frames, all other timestamps as float milliseconds
// relative to their frame's begin - rather than dumping the structs.

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_frame_pacing/frame_pacer.hpp"
#include "erhe_frame_pacing/frame_time_recorder.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <utility>
#include <vector>

namespace erhe::frame_pacing { class Frame_pacing_observer; }

namespace editor {

class Frame_pacing_window : public erhe::imgui::Imgui_window
{
public:
    Frame_pacing_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows);

    // Called once per frame from the editor tick, right after the observer
    // has computed this frame's schedule decision: appends the new frame to
    // the capture and re-snapshots recent records whose late fields (GPU
    // span, present feedback) are still arriving.
    void collect(
        erhe::frame_pacing::Frame_time_recorder&      recorder,
        const erhe::frame_pacing::Frame_pacing_observer& observer,
        double                                        refresh_period,
        double                                        now
    );

    // Simulated workload (U2): busy-waits for a per-frame duration drawn
    // uniformly from [min, max] ms (single-frame spike of the maximum when
    // the spike button was pressed). Must be called inside the measured
    // per-frame work span - after the pacer waits - so the records count it
    // as CPU service time. No-op when the range is 0/0.
    void run_simulated_workload();

    // Implements Imgui_window
    void imgui() override;

    class Frame_sample
    {
    public:
        std::int64_t frame_id      {-1};
        // Capture time of the sample (collect()'s now). Monotonic across
        // the capture - the binary-search key for visible-range lookup -
        // and set even for gap placeholder samples that never got data.
        double       append_time   {0.0};
        int          planned_vsyncs{0};
        double       release_time  {0.0};
        double       predicted_display{0.0};
        std::int64_t wait_id       {-1};
        // The pacer's dynamic vsync grid at capture time (deviation 10):
        // vsync boundaries near this sample are at grid_phase + n *
        // grid_period. The UI must draw refresh lines from the sample-local
        // grid, not from a static anchor - the real period deviates from
        // the queried one (~11 ppm measured), so a static grid drifts
        // ~0.6 ms per minute across a paused view.
        double       grid_phase    {0.0};
        double       grid_period   {0.0};
        bool         stats_valid   {false};
        bool         self_miss     {false};
        double       slack         {0.0};
        bool         complete      {false}; // stop re-snapshotting the record
        erhe::frame_pacing::Frame_time_record record{};
    };

    // Capture introspection (MCP get_frame_pacing_* tools).
    [[nodiscard]] auto get_capture_frame_count   () const -> std::size_t;
    [[nodiscard]] auto get_capture_first_frame_id() const -> std::int64_t;
    [[nodiscard]] auto get_capture_latest_frame_id() const -> std::int64_t { return m_latest_frame_id; }
    [[nodiscard]] auto get_capture_sample        (std::int64_t frame_id) const -> const Frame_sample*;
    [[nodiscard]] auto get_refresh_period        () const -> double { return m_refresh_period; }
    [[nodiscard]] auto get_workload_range_ms     () const -> std::pair<float, float> { return {m_wait_min_ms, m_wait_max_ms}; }
    // Headless twin of the U2 sliders (MCP set_frame_pacing_workload):
    // values are clamped to the sliders' [0, 60] ms range and min <= max.
    void               set_workload_range_ms     (float min_ms, float max_ms);
    // Capture cap: recording stops once the capture holds this many frames
    // (0 = unbounded). Bounds both memory and the capture-size-dependent
    // draw cost, and gives bounded "record a run" captures: set the cap,
    // clear, and the next max_frames frames are recorded.
    void               set_max_capture_frames    (std::size_t max_frames) { m_max_capture_frames = max_frames; }
    [[nodiscard]] auto get_max_capture_frames    () const -> std::size_t  { return m_max_capture_frames; }
    void               clear_capture             ();
    // Actual visible duration of the frame in refresh cycles, from
    // presentation feedback; 0 while unknown. Public for the MCP tools.
    [[nodiscard]] auto actual_vsyncs             (const Frame_sample& sample) const -> int;

private:
    // Persistent capture: dense sequence indexed by (frame_id - first id),
    // stored in fixed-size chunks so growth never reallocates or moves
    // existing samples (stable while the UI is iterating) and lookup stays
    // O(1). Grows without bound until clear().
    class Frame_history
    {
    public:
        [[nodiscard]] auto size           () const -> std::size_t  { return m_count; }
        [[nodiscard]] auto empty          () const -> bool         { return m_count == 0; }
        [[nodiscard]] auto first_frame_id () const -> std::int64_t { return m_first_frame_id; }
        [[nodiscard]] auto at             (std::size_t index) -> Frame_sample&;
        [[nodiscard]] auto at             (std::size_t index) const -> const Frame_sample&;
        [[nodiscard]] auto find           (std::int64_t frame_id) -> Frame_sample*;
        [[nodiscard]] auto find           (std::int64_t frame_id) const -> const Frame_sample*;
        // Appends the sample for frame_id (filling any id gap since the
        // previous append with placeholder samples so indexing stays dense)
        // and returns it. frame_id must be >= every previously appended id.
        [[nodiscard]] auto append         (std::int64_t frame_id, double append_time) -> Frame_sample&;
        // First index with append_time >= t.
        [[nodiscard]] auto lower_bound    (double t) const -> std::size_t;
        void               clear          ();

    private:
        static constexpr std::size_t s_chunk_size = 1024;
        std::vector<std::unique_ptr<std::array<Frame_sample, s_chunk_size>>> m_chunks;
        std::int64_t m_first_frame_id{0};
        std::size_t  m_count         {0};
    };

    [[nodiscard]] auto find_sample        (std::int64_t frame_id) const -> const Frame_sample*;
    // Achieved presentation time of the next frame that has feedback, or
    // 0.0 while unknown; bounds this frame's visibility (actual cadence).
    [[nodiscard]] auto next_achieved_after(std::int64_t frame_id) const -> double;
    [[nodiscard]] auto release_of         (const Frame_sample& sample) const -> double;

    void workload_ui   ();
    void time_axis_ui  ();
    // One shared-axis plot/timeline region: reserves height pixels, draws
    // background, refresh-cycle boundary lines and base line, handles
    // pan/zoom/hover pausing (U8), then invokes the drawing body via the
    // per-region draw functions below.
    struct Region
    {
        float  x0{0.0f}, x1{0.0f}, y0{0.0f}, y1{0.0f};
        bool   hovered{false};
        double t_begin{0.0}, t_end{0.0};
        [[nodiscard]] auto time_to_x(double t) const -> float;
    };
    [[nodiscard]] auto begin_region(const char* id, float height) -> Region;
    // Capture index range [first, second) overlapping the region's visible
    // time span (binary search on append_time, padded so bars spanning into
    // the view from outside are included).
    [[nodiscard]] auto visible_index_range(const Region& region) const -> std::pair<std::size_t, std::size_t>;

    void draw_cadence_graph (const Region& region);
    void draw_delta_graph   (const Region& region);
    void draw_latency_graph (const Region& region);
    void draw_cpu_timeline  (const Region& region);
    void draw_gpu_timeline  (const Region& region);
    void draw_display_timeline(const Region& region);
    void frame_tooltip      (const Frame_sample& sample, const char* section);

    Frame_history m_history;
    std::int64_t  m_latest_frame_id{-1};
    double        m_refresh_period {0.0};
    double        m_now            {0.0};
    double        m_grid_phase_time{0.0}; // latest achieved present, anchors refresh lines

    // Capture cap (0 = unbounded); see set_max_capture_frames().
    std::size_t  m_max_capture_frames{0};

    // Simulated workload state.
    float        m_wait_min_ms{0.0f};
    float        m_wait_max_ms{0.0f};
    bool         m_spike_requested{false};
    std::mt19937 m_random{20260722u};

    // Shared time axis (U8).
    bool         m_paused       {false};
    double       m_view_end     {0.0};  // valid while paused
    double       m_view_duration{0.25}; // seconds
    std::int64_t m_hovered_frame{-1};   // this frame, from last drawn frame's hit test
    std::int64_t m_hovered_frame_previous{-1};
};

}
