#include "erhe_graphics/metal/metal_gpu_timer.hpp"
#include "erhe_graphics/metal/metal_command_buffer.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>

namespace erhe::graphics {

auto Gpu_timer_command_buffer_samples::begin_encoder(
    Gpu_timer_sample_pool&     pool,
    MTL::CounterSampleBuffer*& out_buffer,
    std::size_t&               out_start,
    std::size_t&               out_end
) -> bool
{
    ERHE_VERIFY(open_encoder_start < 0);
    if (!pool.has_timers()) {
        return false;
    }
    if (chunk_index < 0) {
        chunk_index = pool.acquire_chunk();
        if (chunk_index < 0) {
            return false;
        }
    }
    if (used_sample_count + 2 > Gpu_timer_sample_pool::samples_per_chunk) {
        return false;
    }
    open_encoder_start = static_cast<int>(used_sample_count);
    open_encoder_end   = static_cast<int>(used_sample_count + 1);
    used_sample_count += 2;

    // Ranges begun with no encoder open start at this encoder.
    for (std::size_t i = 0; i < record_count; ++i) {
        Gpu_timer_record& record = records[i];
        if (record.begin_pending) {
            record.begin_sample  = open_encoder_start;
            record.begin_pending = false;
        }
    }

    const std::size_t base = pool.get_chunk_base(chunk_index);
    out_buffer = pool.get_chunk_buffer(chunk_index);
    out_start  = base + static_cast<std::size_t>(open_encoder_start);
    out_end    = base + static_cast<std::size_t>(open_encoder_end);
    return true;
}

void Gpu_timer_command_buffer_samples::end_encoder()
{
    if (open_encoder_start < 0) {
        return;
    }
    last_ended_end     = open_encoder_end;
    open_encoder_start = -1;
    open_encoder_end   = -1;
}

void Gpu_timer_command_buffer_samples::write_begin(const uint64_t timer_serial)
{
    if (record_count >= max_records) {
        return;
    }
    Gpu_timer_record& record = records[record_count++];
    record = Gpu_timer_record{};
    record.timer_serial = timer_serial;
    if (open_encoder_start >= 0) {
        record.begin_sample = open_encoder_start;
    } else {
        record.begin_pending = true;
    }
}

void Gpu_timer_command_buffer_samples::write_end(const uint64_t timer_serial)
{
    // The latest record of this timer still waiting for its end.
    for (std::size_t i = record_count; i > 0; --i) {
        Gpu_timer_record& record = records[i - 1];
        if ((record.timer_serial != timer_serial) || (record.end_sample >= 0)) {
            continue;
        }
        if (record.begin_pending) {
            // No sampled encoder ran inside the range: nothing to measure.
            record.begin_pending = false;
            return;
        }
        record.end_sample = (open_encoder_end >= 0) ? open_encoder_end : last_ended_end;
        return;
    }
}

auto Gpu_timer_command_buffer_samples::has_chunk() const -> bool
{
    return chunk_index >= 0;
}

Gpu_timer_sample_pool::Gpu_timer_sample_pool(MTL::Device* const mtl_device)
    : m_mtl_device{mtl_device}
{
    if (m_mtl_device == nullptr) {
        return;
    }
    // Held until the pool is destroyed: completion handlers still resolving
    // on Metal's dispatch threads call sampleTimestamps.
    m_mtl_device->retain();
    if (!m_mtl_device->supportsCounterSampling(MTL::CounterSamplingPointAtStageBoundary)) {
        log_startup->warn("Metal: device does not support counter sampling at stage boundaries - GPU timers read 0");
        return;
    }
    Scoped_transient_object_pool object_pool{};
    const NS::Array* const counter_sets = m_mtl_device->counterSets();
    if (counter_sets == nullptr) {
        return;
    }
    for (NS::UInteger i = 0, end = counter_sets->count(); i < end; ++i) {
        MTL::CounterSet* const counter_set = counter_sets->object<MTL::CounterSet>(i);
        if (counter_set->name()->isEqualToString(MTL::CommonCounterSetTimestamp)) {
            m_timestamp_counter_set = counter_set;
            m_timestamp_counter_set->retain();
            break;
        }
    }
    if (m_timestamp_counter_set == nullptr) {
        log_startup->warn("Metal: device has no timestamp counter set - GPU timers read 0");
        return;
    }
    MTL::Timestamp cpu_timestamp = 0;
    MTL::Timestamp gpu_timestamp = 0;
    m_mtl_device->sampleTimestamps(&cpu_timestamp, &gpu_timestamp);
    m_calibration_cpu_ns = cpu_timestamp;
    m_calibration_gpu    = gpu_timestamp;
    m_sample_buffers.reserve(max_buffers);
    m_free_chunks.reserve(max_buffers * chunks_per_buffer);
    m_timers.reserve(256);
}

Gpu_timer_sample_pool::~Gpu_timer_sample_pool() noexcept
{
    // Completion handlers of submitted command buffers return their chunks;
    // the device has waited for the GPU to go idle before this runs, so only
    // handlers still finishing on Metal's dispatch threads are waited for.
    {
        std::unique_lock<std::mutex> lock{m_mutex};
        m_chunks_returned.wait(lock, [this]() { return m_chunks_out == 0; });
    }
    for (MTL::CounterSampleBuffer* const sample_buffer : m_sample_buffers) {
        sample_buffer->release();
    }
    m_sample_buffers.clear();
    if (m_timestamp_counter_set != nullptr) {
        m_timestamp_counter_set->release();
        m_timestamp_counter_set = nullptr;
    }
    if (m_mtl_device != nullptr) {
        m_mtl_device->release();
        m_mtl_device = nullptr;
    }
}

auto Gpu_timer_sample_pool::is_supported() const -> bool
{
    return m_timestamp_counter_set != nullptr;
}

auto Gpu_timer_sample_pool::has_timers() const -> bool
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return (m_timestamp_counter_set != nullptr) && !m_timers.empty();
}

auto Gpu_timer_sample_pool::register_timer(Gpu_timer_impl* const timer) -> uint64_t
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    const uint64_t serial = m_next_timer_serial++;
    m_timers.push_back(Registered_timer{.serial = serial, .timer = timer});
    return serial;
}

void Gpu_timer_sample_pool::unregister_timer(const uint64_t timer_serial)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    std::erase_if(m_timers, [timer_serial](const Registered_timer& entry) { return entry.serial == timer_serial; });
}

auto Gpu_timer_sample_pool::acquire_chunk() -> int
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (m_timestamp_counter_set == nullptr) {
        return -1;
    }
    if (m_free_chunks.empty()) {
        if (m_sample_buffers.size() >= max_buffers) {
            return -1;
        }
        Scoped_transient_object_pool object_pool{};
        MTL::CounterSampleBufferDescriptor* const descriptor = MTL::CounterSampleBufferDescriptor::alloc()->init();
        descriptor->setCounterSet(m_timestamp_counter_set);
        descriptor->setStorageMode(MTL::StorageModeShared);
        descriptor->setSampleCount(samples_per_buffer);
        descriptor->setLabel(NS::String::string("erhe Gpu_timer samples", NS::UTF8StringEncoding));
        NS::Error* error = nullptr;
        MTL::CounterSampleBuffer* const sample_buffer = m_mtl_device->newCounterSampleBuffer(descriptor, &error);
        descriptor->release();
        if (sample_buffer == nullptr) {
            log_context->error(
                "Metal: newCounterSampleBuffer failed: {}",
                (error != nullptr) ? error->localizedDescription()->utf8String() : "(no error object)"
            );
            // Stop trying: every later acquire would fail the same way.
            m_timestamp_counter_set->release();
            m_timestamp_counter_set = nullptr;
            return -1;
        }
        const int first_chunk = static_cast<int>(m_sample_buffers.size() * chunks_per_buffer);
        m_sample_buffers.push_back(sample_buffer);
        for (int i = static_cast<int>(chunks_per_buffer) - 1; i >= 0; --i) {
            m_free_chunks.push_back(first_chunk + i);
        }
    }
    const int chunk_index = m_free_chunks.back();
    m_free_chunks.pop_back();
    ++m_chunks_out;
    return chunk_index;
}

void Gpu_timer_sample_pool::release_chunk(const int chunk_index)
{
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        ERHE_VERIFY(m_chunks_out > 0);
        m_free_chunks.push_back(chunk_index);
        --m_chunks_out;
    }
    m_chunks_returned.notify_all();
}

auto Gpu_timer_sample_pool::get_chunk_buffer(const int chunk_index) const -> MTL::CounterSampleBuffer*
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_sample_buffers.at(static_cast<std::size_t>(chunk_index) / chunks_per_buffer);
}

auto Gpu_timer_sample_pool::get_chunk_base(const int chunk_index) const -> std::size_t
{
    return (static_cast<std::size_t>(chunk_index) % chunks_per_buffer) * samples_per_chunk;
}

auto Gpu_timer_sample_pool::ticks_to_nanoseconds(const uint64_t ticks) -> uint64_t
{
    return static_cast<uint64_t>(static_cast<double>(ticks) * m_ns_per_tick);
}

void Gpu_timer_sample_pool::resolve_command_buffer_samples(const Gpu_timer_command_buffer_samples& samples)
{
    if (samples.chunk_index < 0) {
        return;
    }
    Scoped_transient_object_pool object_pool{}; // drains the NS::Data resolveCounterRange returns
    MTL::CounterSampleBuffer* const sample_buffer = get_chunk_buffer(samples.chunk_index);
    const std::size_t               base          = get_chunk_base(samples.chunk_index);
    const NS::Data* const data = (samples.used_sample_count > 0)
        ? sample_buffer->resolveCounterRange(NS::Range::Make(base, samples.used_sample_count))
        : nullptr;
    const std::size_t sample_count = (data != nullptr) ? (data->length() / sizeof(uint64_t)) : 0;
    const uint64_t* const values = (data != nullptr) ? static_cast<const uint64_t*>(data->bytes()) : nullptr;

    // GPU timestamps are in GPU ticks; MTL::Device::sampleTimestamps pairs
    // them with CPU nanoseconds. The ratio over the span since the pool was
    // created converts ticks to nanoseconds (1:1 on Apple silicon).
    MTL::Timestamp cpu_timestamp = 0;
    MTL::Timestamp gpu_timestamp = 0;
    m_mtl_device->sampleTimestamps(&cpu_timestamp, &gpu_timestamp);

    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if ((gpu_timestamp > m_calibration_gpu) && (cpu_timestamp > m_calibration_cpu_ns)) {
            m_ns_per_tick =
                static_cast<double>(cpu_timestamp - m_calibration_cpu_ns) /
                static_cast<double>(gpu_timestamp - m_calibration_gpu);
        }
        for (std::size_t i = 0; i < samples.record_count; ++i) {
            const Gpu_timer_record& record = samples.records[i];
            if ((record.begin_sample < 0) || (record.end_sample < 0)) {
                continue;
            }
            const std::size_t begin_index = static_cast<std::size_t>(record.begin_sample);
            const std::size_t end_index   = static_cast<std::size_t>(record.end_sample);
            if ((begin_index >= sample_count) || (end_index >= sample_count)) {
                continue;
            }
            const uint64_t begin_ticks = values[begin_index];
            const uint64_t end_ticks   = values[end_index];
            // An encoder with no work samples nothing (reads 0); failed
            // samples read MTL::CounterErrorValue.
            const bool valid =
                (begin_ticks != 0) && (end_ticks != 0) &&
                (begin_ticks != MTL::CounterErrorValue) && (end_ticks != MTL::CounterErrorValue) &&
                (end_ticks >= begin_ticks);
            if (!valid) {
                continue;
            }
            const auto entry = std::find_if(
                m_timers.begin(), m_timers.end(),
                [&record](const Registered_timer& registered) { return registered.serial == record.timer_serial; }
            );
            if (entry == m_timers.end()) {
                continue; // timer destroyed while its command buffer was in flight
            }
            entry->timer->store_last_result_ns(ticks_to_nanoseconds(end_ticks - begin_ticks));
        }
    }
    release_chunk(samples.chunk_index);
}

Gpu_timer_impl::Gpu_timer_impl(Device& device, const char* label)
    : m_pool {&device.get_impl().get_gpu_timer_sample_pool()}
    , m_label{label}
{
    m_serial = m_pool->register_timer(this);
}

Gpu_timer_impl::~Gpu_timer_impl() noexcept
{
    m_pool->unregister_timer(m_serial);
}

void Gpu_timer_impl::write_begin_timestamp(Command_buffer& command_buffer)
{
    command_buffer.get_impl().get_gpu_timer_samples().write_begin(m_serial);
}

void Gpu_timer_impl::write_end_timestamp(Command_buffer& command_buffer)
{
    command_buffer.get_impl().get_gpu_timer_samples().write_end(m_serial);
}

void Gpu_timer_impl::store_last_result_ns(const uint64_t nanoseconds)
{
    const std::lock_guard<std::mutex> lock{m_result_mutex};
    m_last_result = nanoseconds;
}

auto Gpu_timer_impl::last_result() -> uint64_t
{
    const std::lock_guard<std::mutex> lock{m_result_mutex};
    return m_last_result;
}

auto Gpu_timer_impl::label() const -> const char*
{
    return (m_label != nullptr) ? m_label : "(unnamed)";
}

} // namespace erhe::graphics
