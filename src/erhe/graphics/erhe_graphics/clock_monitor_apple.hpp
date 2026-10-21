#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace erhe::frame_pacing { class Frame_time_recorder; }

namespace erhe::graphics {

// GPU and CPU clocks per presented frame (doc/plans/frame_pacing_metal.md,
// claim C25): Apple GPUs and CPUs clock down at a longer frame cadence, so
// stage times measured there are no evidence of a shorter cadence's cost;
// the frame pacer judges an upshift by the work at the clocks seen at the
// shorter cadence. macOS has no public API for these clocks: this reads the
// private IOReport framework (resolved with dlopen) - "GPU Stats" / "GPU
// Performance States" channel GPUPH and "CPU Stats" / "CPU Core Performance
// States" channels PCPU<cluster><core>0, residency per DVFS state - and the
// DVFS frequency tables of the pmgr IORegistry entry (voltage-states9 for
// the GPU in Hz; voltage-states5-sram and voltage-states13-sram for the two
// P-core clusters in kHz). A sample costs 0.15-0.25 ms (up to 1.7 ms), so a
// sampler thread takes it, woken once per presented frame.
//
// The CPU clock is the residency-weighted average active clock of the
// P-cores: the editor's main and render threads are user-interactive and run
// there, and the busiest cores dominate the weighting.
//
// The presenting command buffer's completion handler calls on_frame_gpu_done
// with the frame's GPU bracket; the sampler writes the clock ratios (average
// active clock since the previous sample over the maximum DVFS clock) and
// then the bracket into the frame record, so the pacer never sees a frame's
// GPU span without its clocks. A part that cannot be resolved (a missing
// symbol, a state table that does not match its DVFS table) logs why and
// reports 0 (not measured); without the GPU part is_available() is false
// and the handler writes the bracket alone. Thread-safe.
class Clock_monitor
{
public:
    explicit Clock_monitor(erhe::frame_pacing::Frame_time_recorder& recorder);
    ~Clock_monitor() noexcept;
    Clock_monitor(const Clock_monitor&) = delete;
    auto operator=(const Clock_monitor&) -> Clock_monitor& = delete;

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

    // One DVFS domain: the IOReport channels that sample it and the clock of
    // each of their states (0 = not an active state).
    class Channel
    {
    public:
        std::string         name;
        std::vector<double> state_mhz; // per state index; 0 = not an active state
    };
    class Domain
    {
    public:
        std::vector<Channel> channels;
        double               max_mhz{0.0};
    };

    void sampler_loop();

    class Ioreport; // the resolved private API and the subscription
    std::unique_ptr<Ioreport>               m_ioreport;
    Domain                                  m_gpu;
    Domain                                  m_cpu;

    std::mutex                              m_mutex;
    std::condition_variable                 m_condition;
    std::deque<Pending_frame>               m_pending;
    bool                                    m_stop{false};
    erhe::frame_pacing::Frame_time_recorder* m_recorder{nullptr};
    std::thread                             m_thread;
    std::mutex                              m_ioreport_mutex; // every IOReport call on the subscription
};

} // namespace erhe::graphics
