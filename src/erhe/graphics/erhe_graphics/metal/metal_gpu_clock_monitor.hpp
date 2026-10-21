#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace erhe::frame_pacing { class Frame_time_recorder; }

namespace erhe::graphics {

// The GPU's clock per presented frame (doc/plans/frame_pacing_metal.md,
// claim C25): an Apple GPU clocks down at a longer frame cadence, so its
// spans there are no evidence of a shorter cadence's cost; the frame pacer
// judges an upshift by the work at the clock the GPU shows at the shorter
// cadence. macOS has no public API for the GPU clock: this reads the
// private IOReport framework ("GPU Stats" / "GPU Performance States",
// channel GPUPH - residency per DVFS state, resolved with dlopen) and the
// DVFS frequency table of the pmgr IORegistry entry (voltage-states9). A
// sample costs 0.15-0.25 ms (up to 1.7 ms), so a sampler thread takes it,
// woken once per presented frame.
//
// The presenting command buffer's completion handler calls on_frame_gpu_done
// with the frame's GPU bracket; the sampler writes gpu_frame_begin,
// gpu_frame_end and gpu_clock_ratio (average active clock since the previous
// sample over the maximum DVFS clock) into the frame record together, so the
// pacer never sees a frame's span without its clock. Without IOReport (a
// missing symbol, an unexpected state table) is_available() is false and the
// handler writes the bracket alone. Thread-safe.
class Gpu_clock_monitor
{
public:
    explicit Gpu_clock_monitor(erhe::frame_pacing::Frame_time_recorder& recorder);
    ~Gpu_clock_monitor() noexcept;
    Gpu_clock_monitor(const Gpu_clock_monitor&) = delete;
    auto operator=(const Gpu_clock_monitor&) -> Gpu_clock_monitor& = delete;

    [[nodiscard]] auto is_available() const -> bool;
    void on_frame_gpu_done(std::int64_t frame_id, double gpu_begin, double gpu_end);
    // Stops the sampler and cuts the monitor from the recorder (device
    // teardown); completion handlers that still hold it write nothing.
    void detach();

private:
    class Pending_frame
    {
    public:
        std::int64_t frame_id {0};
        double       gpu_begin{0.0};
        double       gpu_end  {0.0};
    };

    void sampler_loop();

    class Ioreport; // the resolved private API and the subscription
    std::unique_ptr<Ioreport>               m_ioreport;
    std::vector<double>                     m_state_mhz;   // per GPUPH state index; 0 = not an active state
    double                                  m_max_mhz{0.0};

    std::mutex                              m_mutex;
    std::condition_variable                 m_condition;
    std::deque<Pending_frame>               m_pending;
    bool                                    m_stop{false};
    erhe::frame_pacing::Frame_time_recorder* m_recorder{nullptr};
    std::thread                             m_thread;
};

} // namespace erhe::graphics
