#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace MTL  { class Buffer; class ComputePipelineState; class Event; }
namespace MTL4 { class CommitFeedback; class ComputeCommandEncoder; }

namespace erhe::graphics {

// GPU pre-warm (doc/plans/frame_pacing_metal.md, claim C26): the Apple GPU
// holds a low clock at a longer frame cadence, and after the frame pacer's
// upshift its clock needs ~6 frames to ramp - frames slip meanwhile. Before
// switching, the pacer asks for extra GPU work after each frame: an
// ALU-only kernel (a register-resident FMA loop over 65536 threads, one
// never-taken write, no memory traffic) in its own command buffer, ordered
// after the frame's presenting command buffer through an MTLEvent so it runs
// in the idle gap instead of overlapping the frame (unordered it stretched
// the frame's span). Under MTL4 the queue signals the event after the
// presenting commit and waits on it before the pre-warm commit (which holds
// back every later commit too, doc/erhe/metal4_backend.md "Submission").
// 2.3 ms of it per 16.7 ms frame raised the clock from ~0.5 to ~0.97 of its
// maximum (probe on an M4 Pro).
//
// The kernel duration is requested in seconds; the iteration count follows
// the measured duration per iteration of earlier dispatches. Thread-safe:
// the render thread dispatches, Metal's feedback threads update the rate.
class Device_impl;
class Recording;
class Gpu_prewarm
{
public:
    explicit Gpu_prewarm(Device_impl& device_impl);
    ~Gpu_prewarm() noexcept;
    Gpu_prewarm(const Gpu_prewarm&) = delete;
    auto operator=(const Gpu_prewarm&) -> Gpu_prewarm& = delete;

    [[nodiscard]] auto is_available() const -> bool;
    // Right after committing the presenting command buffer, with the submit
    // mutex held: the queue signals the event the following dispatch_locked()
    // waits on.
    void signal_after_last_commit();
    // Then: run the kernel for about seconds, ordered after it.
    void dispatch_locked(double seconds);
    // Init calibration: runs iterations standalone and waits; returns the
    // kernel's GPU time in seconds (0.0 on failure) and updates the rate.
    [[nodiscard]] auto run_blocking(std::uint32_t iterations) -> double;
    [[nodiscard]] auto get_seconds_per_iteration() const -> double;

private:
    // Duration per iteration of earlier dispatches, updated by their commit
    // feedback (the device waits for all feedback before destroying this).
    class Rate
    {
    public:
        void note(std::uint32_t iterations, double seconds);
        [[nodiscard]] auto get() const -> double;
    private:
        mutable std::mutex m_mutex;
        double             m_seconds_per_iteration{0.0}; // 0 = not measured
    };

    // With the submit mutex held. blocking_seconds (may be null) receives
    // the kernel's GPU time from the commit feedback.
    [[nodiscard]] auto commit_locked(std::uint32_t iterations, double* blocking_seconds) -> std::uint64_t;
    // Recording_feedback_function of the pre-warm commits.
    static void on_feedback(Recording& recording, MTL4::CommitFeedback* feedback);

    static constexpr std::size_t s_iteration_slots = 16;

    Device_impl&                m_device_impl;
    MTL::ComputePipelineState*  m_pipeline{nullptr};
    std::size_t                 m_iteration_slot{0};
    MTL::Buffer*                m_output  {nullptr};
    MTL::Event*                 m_event   {nullptr};
    std::uint64_t               m_event_value{0}; // render thread only
    Rate                        m_rate;
};

} // namespace erhe::graphics
