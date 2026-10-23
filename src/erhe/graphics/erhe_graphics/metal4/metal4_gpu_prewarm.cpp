#include "erhe_graphics/metal4/metal4_gpu_prewarm.hpp"
#include "erhe_graphics/metal4/metal4_device.hpp"
#include "erhe_graphics/metal4/metal4_submission.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <condition_variable>
#include <memory>

namespace erhe::graphics {

namespace {

constexpr const char* c_prewarm_msl = R"(
#include <metal_stdlib>
using namespace metal;

kernel void gpu_prewarm(
    device float* out        [[buffer(0)]],
    constant uint& iterations [[buffer(1)]],
    uint           thread_id  [[thread_position_in_grid]])
{
    float a = float(thread_id) * 1.0e-6;
    float b = 1.0001;
    float c = 0.9999;
    for (uint i = 0; i < iterations; ++i) {
        a = fma(a, b, c);
        b = fma(b, c, a * 1.0e-9);
    }
    if (a == 12345.0) {
        out[0] = a + b; // never taken; keeps the loop alive
    }
}
)";

constexpr std::uint32_t s_threadgroups          = 256;
constexpr std::uint32_t s_threads_per_group     = 256;
constexpr std::uint32_t s_max_iterations        = 400000;
constexpr double        s_rate_smoothing        = 0.3;

} // anonymous namespace

Gpu_prewarm::Gpu_prewarm(Device_impl& device_impl)
    : m_device_impl{device_impl}
{
    MTL::Device* const device = device_impl.get_mtl_device();
    Scoped_transient_object_pool object_pool{};
    NS::Error* error = nullptr;
    MTL::Library* const library = device->newLibrary(NS::String::string(c_prewarm_msl, NS::UTF8StringEncoding), nullptr, &error);
    if (library == nullptr) {
        log_context->warn("GPU pre-warm: kernel compile failed: {}", (error != nullptr) ? error->localizedDescription()->utf8String() : "(no error)");
        return;
    }
    MTL::Function* const function = library->newFunction(NS::String::string("gpu_prewarm", NS::UTF8StringEncoding));
    m_pipeline = (function != nullptr) ? device->newComputePipelineState(function, &error) : nullptr;
    if (function != nullptr) {
        function->release();
    }
    library->release();
    if (m_pipeline == nullptr) {
        log_context->warn("GPU pre-warm: pipeline failed: {}", (error != nullptr) ? error->localizedDescription()->utf8String() : "(no error)");
        return;
    }
    // The output (never written) and the iteration counts, one per dispatch
    // (the queue runs at most a few pre-warms ahead; s_iteration_slots
    // entries 256 bytes apart).
    m_output = device->newBuffer(16 + (s_iteration_slots * 256), MTL::ResourceStorageModeShared);
    m_event  = device->newEvent();
    if (m_output != nullptr) {
        device_impl.get_residency().add(m_output);
    }
}

Gpu_prewarm::~Gpu_prewarm() noexcept
{
    if (m_event != nullptr) {
        m_event->release();
    }
    if (m_output != nullptr) {
        m_device_impl.get_residency().remove(m_output);
        m_output->release();
    }
    if (m_pipeline != nullptr) {
        m_pipeline->release();
    }
}

auto Gpu_prewarm::is_available() const -> bool
{
    return (m_pipeline != nullptr) && (m_output != nullptr) && (m_event != nullptr);
}

void Gpu_prewarm::Rate::note(const std::uint32_t iterations, const double seconds)
{
    if ((iterations == 0) || (seconds <= 0.0)) {
        return;
    }
    const double                      per_iteration = seconds / static_cast<double>(iterations);
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_seconds_per_iteration = (m_seconds_per_iteration > 0.0)
        ? (((1.0 - s_rate_smoothing) * m_seconds_per_iteration) + (s_rate_smoothing * per_iteration))
        : per_iteration;
}

auto Gpu_prewarm::Rate::get() const -> double
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_seconds_per_iteration;
}

auto Gpu_prewarm::get_seconds_per_iteration() const -> double
{
    return m_rate.get();
}

void Gpu_prewarm::signal_after_last_commit()
{
    m_device_impl.get_mtl_command_queue()->signalEvent(m_event, ++m_event_value);
}

auto Gpu_prewarm::commit_locked(const std::uint32_t iterations, double* const blocking_seconds) -> uint64_t
{
    Scoped_transient_object_pool object_pool{};
    Recording* const recording = m_device_impl.begin_internal_recording();
    recording->command_buffer->setLabel(NS::String::string("GPU pre-warm", NS::UTF8StringEncoding));

    // The iteration count goes into the next slot of the output buffer (the
    // MTL4 replacement for setBytes); a slot is reused only s_iteration_slots
    // dispatches later.
    const std::size_t slot_offset = 256 + ((m_iteration_slot % s_iteration_slots) * 256);
    ++m_iteration_slot;
    std::memcpy(static_cast<std::byte*>(m_output->contents()) + slot_offset, &iterations, sizeof(iterations));

    MTL4::ArgumentTable* const argument_table = m_device_impl.get_submission_pools().acquire_argument_table();
    recording->argument_tables.push_back(argument_table);
    argument_table->setAddress(m_output->gpuAddress(),               0);
    argument_table->setAddress(m_output->gpuAddress() + slot_offset, 1);
    // No queue barrier: the kernel reads and writes nothing earlier work
    // touches; the event wait orders it after the presenting commit.
    MTL4::ComputeCommandEncoder* const encoder = recording->command_buffer->computeCommandEncoder();
    encoder->setComputePipelineState(m_pipeline);
    encoder->setArgumentTable(argument_table);
    encoder->dispatchThreadgroups(MTL::Size{s_threadgroups, 1, 1}, MTL::Size{s_threads_per_group, 1, 1});
    encoder->endEncoding();
    recording->command_buffer->endCommandBuffer();

    recording->on_feedback = &Gpu_prewarm::on_feedback;
    recording->user        = this;
    recording->user_result = blocking_seconds;
    recording->user_value  = iterations;
    return m_device_impl.commit_locked(*recording);
}

void Gpu_prewarm::on_feedback(Recording& recording, MTL4::CommitFeedback* const feedback)
{
    Gpu_prewarm* const prewarm = static_cast<Gpu_prewarm*>(recording.user);
    const double seconds = (feedback != nullptr) ? (feedback->GPUEndTime() - feedback->GPUStartTime()) : 0.0;
    prewarm->m_rate.note(static_cast<std::uint32_t>(recording.user_value), seconds);
    if (recording.user_result != nullptr) {
        *static_cast<double*>(recording.user_result) = seconds;
    }
}

void Gpu_prewarm::dispatch_locked(const double seconds)
{
    const double per_iteration = get_seconds_per_iteration();
    if ((seconds <= 0.0) || (per_iteration <= 0.0)) {
        return;
    }
    const std::uint32_t iterations = static_cast<std::uint32_t>(std::clamp(seconds / per_iteration, 1.0, static_cast<double>(s_max_iterations)));
    m_device_impl.get_mtl_command_queue()->wait(m_event, m_event_value);
    static_cast<void>(commit_locked(iterations, nullptr));
}

auto Gpu_prewarm::run_blocking(const std::uint32_t iterations) -> double
{
    double   seconds = 0.0;
    uint64_t serial  = 0;
    {
        const std::unique_lock<std::mutex> submit_lock = m_device_impl.lock_submit();
        serial = commit_locked(iterations, &seconds);
    }
    m_device_impl.wait_for_commit(serial);
    return seconds; // written by the feedback handler, which has run
}

} // namespace erhe::graphics
