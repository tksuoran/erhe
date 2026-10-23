#include "erhe_graphics/metal4/metal4_gpu_prewarm.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>
#include <cmath>
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

Gpu_prewarm::Gpu_prewarm(MTL::Device* const device, MTL::CommandQueue* const queue)
    : m_queue{queue}
{
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
    m_output = device->newBuffer(16, MTL::ResourceStorageModePrivate);
    m_event  = device->newEvent();
}

Gpu_prewarm::~Gpu_prewarm() noexcept
{
    if (m_event != nullptr) {
        m_event->release();
    }
    if (m_output != nullptr) {
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

void Gpu_prewarm::encode(MTL::CommandBuffer* const command_buffer, const std::uint32_t iterations)
{
    MTL::ComputeCommandEncoder* const encoder = command_buffer->computeCommandEncoder();
    encoder->setComputePipelineState(m_pipeline);
    encoder->setBuffer(m_output, 0, 0);
    encoder->setBytes(&iterations, sizeof(iterations), 1);
    encoder->dispatchThreadgroups(MTL::Size{s_threadgroups, 1, 1}, MTL::Size{s_threads_per_group, 1, 1});
    encoder->endEncoding();
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
    return m_rate->get();
}

void Gpu_prewarm::signal_after(MTL::CommandBuffer* const presenting_command_buffer)
{
    presenting_command_buffer->encodeSignalEvent(m_event, ++m_event_value);
}

void Gpu_prewarm::dispatch(const double seconds)
{
    const double per_iteration = get_seconds_per_iteration();
    if ((seconds <= 0.0) || (per_iteration <= 0.0)) {
        return;
    }
    const std::uint32_t iterations = static_cast<std::uint32_t>(std::clamp(seconds / per_iteration, 1.0, static_cast<double>(s_max_iterations)));
    Scoped_transient_object_pool object_pool{};
    MTL::CommandBuffer* const command_buffer = m_queue->commandBuffer();
    if (command_buffer == nullptr) {
        return;
    }
    command_buffer->setLabel(NS::String::string("GPU pre-warm", NS::UTF8StringEncoding));
    command_buffer->encodeWait(m_event, m_event_value);
    encode(command_buffer, iterations);
    std::shared_ptr<Rate> rate = m_rate;
    command_buffer->addCompletedHandler(
        [rate, iterations](MTL::CommandBuffer* completed) {
            rate->note(iterations, completed->GPUEndTime() - completed->GPUStartTime());
        }
    );
    command_buffer->commit();
}

auto Gpu_prewarm::run_blocking(const std::uint32_t iterations) -> double
{
    Scoped_transient_object_pool object_pool{};
    MTL::CommandBuffer* const command_buffer = m_queue->commandBuffer();
    if (command_buffer == nullptr) {
        return 0.0;
    }
    command_buffer->setLabel(NS::String::string("GPU pre-warm calibration", NS::UTF8StringEncoding));
    encode(command_buffer, iterations);
    command_buffer->commit();
    command_buffer->waitUntilCompleted();
    const double seconds = command_buffer->GPUEndTime() - command_buffer->GPUStartTime();
    m_rate->note(iterations, seconds);
    return seconds;
}

} // namespace erhe::graphics
