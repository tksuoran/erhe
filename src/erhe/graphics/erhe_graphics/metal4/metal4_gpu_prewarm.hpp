#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

namespace MTL { class Buffer; class CommandBuffer; class CommandQueue; class ComputePipelineState; class Device; class Event; }

namespace erhe::graphics {

// GPU pre-warm (doc/plans/frame_pacing_metal.md, claim C26): the Apple GPU
// holds a low clock at a longer frame cadence, and after the frame pacer's
// upshift its clock needs ~6 frames to ramp - frames slip meanwhile. Before
// switching, the pacer asks for extra GPU work after each frame: an
// ALU-only kernel (a register-resident FMA loop over 65536 threads, one
// never-taken write, no memory traffic) in its own command buffer, ordered
// after the frame's presenting command buffer through an MTLEvent so it runs
// in the idle gap instead of overlapping the frame (unordered it stretched
// the frame's span). 2.3 ms of it per 16.7 ms frame raised the clock from
// ~0.5 to ~0.97 of its maximum (probe on an M4 Pro).
//
// The kernel duration is requested in seconds; the iteration count follows
// the measured duration per iteration of earlier dispatches. Thread-safe:
// the render thread dispatches, Metal's completion threads update the rate.
class Gpu_prewarm
{
public:
    Gpu_prewarm(MTL::Device* device, MTL::CommandQueue* queue);
    ~Gpu_prewarm() noexcept;
    Gpu_prewarm(const Gpu_prewarm&) = delete;
    auto operator=(const Gpu_prewarm&) -> Gpu_prewarm& = delete;

    [[nodiscard]] auto is_available() const -> bool;
    // Before committing the presenting command buffer: signal the event the
    // following dispatch() waits on.
    void signal_after(MTL::CommandBuffer* presenting_command_buffer);
    // After that commit: run the kernel for about seconds, ordered after it.
    void dispatch(double seconds);
    // Init calibration: runs iterations standalone and waits; returns the
    // kernel's GPU time in seconds (0.0 on failure) and updates the rate.
    [[nodiscard]] auto run_blocking(std::uint32_t iterations) -> double;
    [[nodiscard]] auto get_seconds_per_iteration() const -> double;

private:
    // Duration per iteration of earlier dispatches, shared with their
    // completion handlers (which may run after the device is gone).
    class Rate
    {
    public:
        void note(std::uint32_t iterations, double seconds);
        [[nodiscard]] auto get() const -> double;
    private:
        mutable std::mutex m_mutex;
        double             m_seconds_per_iteration{0.0}; // 0 = not measured
    };

    void encode(MTL::CommandBuffer* command_buffer, std::uint32_t iterations);

    MTL::CommandQueue*          m_queue   {nullptr};
    MTL::ComputePipelineState*  m_pipeline{nullptr};
    MTL::Buffer*                m_output  {nullptr};
    MTL::Event*                 m_event   {nullptr};
    std::uint64_t               m_event_value{0}; // render thread only
    std::shared_ptr<Rate>       m_rate{std::make_shared<Rate>()};
};

} // namespace erhe::graphics
