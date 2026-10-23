#include "erhe_graphics/metal4/metal4_gpu_timer.hpp"
#include "erhe_graphics/metal4/metal4_command_buffer.hpp"
#include "erhe_graphics/metal4/metal4_device.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

#if defined(ERHE_PROFILE_LIBRARY_TRACY) && defined(TRACY_ENABLE)
#   include <client/TracyProfiler.hpp>
#   include <common/TracyQueue.hpp>
#   define ERHE_METAL_TRACY_GPU_ZONES 1
#endif

#include <algorithm>
#include <cstring>

namespace erhe::graphics {

auto Gpu_timer_command_buffer_samples::begin_encoder(
    Gpu_timer_sample_pool&     pool,
    MTL4::CounterHeap*&        out_heap,
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
    out_heap   = pool.get_chunk_buffer(chunk_index);
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
        if (record.is_tracy_zone || (record.timer_serial != timer_serial) || (record.end_sample >= 0)) {
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

auto Gpu_timer_command_buffer_samples::write_tracy_zone_begin(const uint16_t query_id, const uint64_t connection_id) -> bool
{
    if (record_count >= max_records) {
        return false;
    }
    Gpu_timer_record& record = records[record_count++];
    record = Gpu_timer_record{};
    record.is_tracy_zone       = true;
    record.tracy_query_id      = query_id;
    record.tracy_connection_id = connection_id;
    if (open_encoder_start >= 0) {
        record.begin_sample = open_encoder_start;
    } else {
        record.begin_pending = true;
    }
    return true;
}

auto Gpu_timer_command_buffer_samples::write_tracy_zone_end(const uint16_t query_id) -> const Gpu_timer_record*
{
    for (std::size_t i = record_count; i > 0; --i) {
        Gpu_timer_record& record = records[i - 1];
        if (!record.is_tracy_zone || (record.tracy_query_id != query_id)) {
            continue;
        }
        // A zone with no sampled encoder inside keeps begin_sample -1; it
        // still gets its GPU times (send_tracy_zone_times).
        record.begin_pending = false;
        if (record.begin_sample >= 0) {
            record.end_sample = (open_encoder_end >= 0) ? open_encoder_end : last_ended_end;
        }
        return &record;
    }
    return nullptr;
}

auto Gpu_timer_command_buffer_samples::has_chunk() const -> bool
{
    return chunk_index >= 0;
}

auto Gpu_timer_command_buffer_samples::needs_resolve() const -> bool
{
    return (chunk_index >= 0) || (record_count > 0);
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
    const uint64_t timestamp_frequency = m_mtl_device->queryTimestampFrequency();
    if (timestamp_frequency == 0) {
        log_startup->warn("Metal: device reports no timestamp frequency - GPU timers read 0");
        return;
    }
    m_supported   = true;
    m_ns_per_tick = 1.0e9 / static_cast<double>(timestamp_frequency);
    m_sample_buffers.reserve(max_buffers);
    m_free_chunks.reserve(max_buffers * chunks_per_buffer);
    m_timers.reserve(256);
    create_tracy_context();
}

void Gpu_timer_sample_pool::create_tracy_context()
{
#if defined(ERHE_METAL_TRACY_GPU_ZONES)
    // As tracy/TracyMetal.hmm does: the context's GPU time base is a
    // sampleTimestamps pair taken now, and the period is 1 ns: the zone
    // times sent are counter heap ticks converted to nanoseconds
    // (ticks_to_nanoseconds), the time base sampleTimestamps reports.
    MTL::Timestamp cpu_timestamp = 0;
    MTL::Timestamp gpu_timestamp = 0;
    m_mtl_device->sampleTimestamps(&cpu_timestamp, &gpu_timestamp);
    m_tracy_context_id = tracy::NextGpuContextId();
    {
        tracy::QueueItem* const item = tracy::Profiler::QueueSerial();
        tracy::MemWrite(&item->hdr.type,              tracy::QueueType::GpuNewContext);
        tracy::MemWrite(&item->gpuNewContext.cpuTime, tracy::Profiler::GetTime());
        tracy::MemWrite(&item->gpuNewContext.gpuTime, static_cast<int64_t>(gpu_timestamp));
        tracy::MemWrite(&item->gpuNewContext.thread,  uint32_t{0});
        tracy::MemWrite(&item->gpuNewContext.period,  1.0f);
        tracy::MemWrite(&item->gpuNewContext.context, static_cast<uint8_t>(m_tracy_context_id));
        tracy::MemWrite(&item->gpuNewContext.flags,   tracy::GpuContextFlags{0});
        tracy::MemWrite(&item->gpuNewContext.type,    tracy::GpuContextType::Metal);
#   if defined(TRACY_ON_DEMAND)
        tracy::GetProfiler().DeferItem(*item);
#   endif
        tracy::Profiler::QueueSerialFinish();
    }
    {
        static constexpr char context_name[] = "erhe Metal queue";
        constexpr std::size_t name_length    = sizeof(context_name) - 1;
        char* const name_copy = static_cast<char*>(tracy::tracy_malloc(name_length));
        std::memcpy(name_copy, context_name, name_length);
        tracy::QueueItem* const item = tracy::Profiler::QueueSerial();
        tracy::MemWrite(&item->hdr.type,                  tracy::QueueType::GpuContextName);
        tracy::MemWrite(&item->gpuContextNameFat.context, static_cast<uint8_t>(m_tracy_context_id));
        tracy::MemWrite(&item->gpuContextNameFat.ptr,     reinterpret_cast<uint64_t>(name_copy));
        tracy::MemWrite(&item->gpuContextNameFat.size,    static_cast<uint16_t>(name_length));
#   if defined(TRACY_ON_DEMAND)
        tracy::GetProfiler().DeferItem(*item);
#   endif
        tracy::Profiler::QueueSerialFinish();
    }
    log_startup->info("Tracy Metal GPU profiling context created");
#endif
}

auto Gpu_timer_sample_pool::begin_tracy_zone(
    Gpu_timer_command_buffer_samples& samples,
    const uint32_t                    line,
    const char* const                 file,
    const char* const                 function,
    const char* const                 name,
    uint16_t&                         out_query_id
) -> bool
{
#if defined(ERHE_METAL_TRACY_GPU_ZONES)
    if (m_tracy_context_id < 0) {
        return false;
    }
#   if defined(TRACY_ON_DEMAND)
    if (!tracy::GetProfiler().IsConnected()) {
        return false;
    }
    const uint64_t connection_id = tracy::GetProfiler().ConnectionId();
#   else
    const uint64_t connection_id = 0;
#   endif
    uint16_t query_id = 0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        query_id = m_tracy_next_query_id;
        m_tracy_next_query_id = static_cast<uint16_t>(m_tracy_next_query_id + 2);
    }
    if (!samples.write_tracy_zone_begin(query_id, connection_id)) {
        return false;
    }
    const uint64_t source_location = tracy::Profiler::AllocSourceLocation(
        line,
        file,     std::strlen(file),
        function, std::strlen(function),
        name,     std::strlen(name)
    );
    tracy::QueueItem* const item = tracy::Profiler::QueueSerial();
    tracy::MemWrite(&item->hdr.type,             tracy::QueueType::GpuZoneBeginAllocSrcLocSerial);
    tracy::MemWrite(&item->gpuZoneBegin.cpuTime, tracy::Profiler::GetTime());
    tracy::MemWrite(&item->gpuZoneBegin.srcloc,  source_location);
    tracy::MemWrite(&item->gpuZoneBegin.thread,  tracy::GetThreadHandle());
    tracy::MemWrite(&item->gpuZoneBegin.queryId, query_id);
    tracy::MemWrite(&item->gpuZoneBegin.context, static_cast<uint8_t>(m_tracy_context_id));
    tracy::Profiler::QueueSerialFinish();
    out_query_id = query_id;
    return true;
#else
    static_cast<void>(samples);
    static_cast<void>(line);
    static_cast<void>(file);
    static_cast<void>(function);
    static_cast<void>(name);
    static_cast<void>(out_query_id);
    return false;
#endif
}

void Gpu_timer_sample_pool::end_tracy_zone(Gpu_timer_command_buffer_samples& samples, const uint16_t query_id)
{
#if defined(ERHE_METAL_TRACY_GPU_ZONES)
    const Gpu_timer_record* const record = samples.write_tracy_zone_end(query_id);
    ERHE_VERIFY(record != nullptr); // a Scoped_gpu_zone must not span its command buffer's submit
#   if defined(TRACY_ON_DEMAND)
    // A zone begun on an earlier connection ends silently, as Tracy's own
    // GPU zone scopes do (send_tracy_zone_times drops its GPU times too).
    if (record->tracy_connection_id != tracy::GetProfiler().ConnectionId()) {
        return;
    }
#   endif
    tracy::QueueItem* const item = tracy::Profiler::QueueSerial();
    tracy::MemWrite(&item->hdr.type,           tracy::QueueType::GpuZoneEndSerial);
    tracy::MemWrite(&item->gpuZoneEnd.cpuTime, tracy::Profiler::GetTime());
    tracy::MemWrite(&item->gpuZoneEnd.thread,  tracy::GetThreadHandle());
    tracy::MemWrite(&item->gpuZoneEnd.queryId, static_cast<uint16_t>(query_id + 1));
    tracy::MemWrite(&item->gpuZoneEnd.context, static_cast<uint8_t>(m_tracy_context_id));
    tracy::Profiler::QueueSerialFinish();
#else
    static_cast<void>(samples);
    static_cast<void>(query_id);
#endif
}

void Gpu_timer_sample_pool::send_tracy_zone_times(
    const Gpu_timer_record& record,
    const uint64_t* const   values,
    const std::size_t       sample_count,
    const uint64_t          now_gpu_time,
    uint64_t&               last_gpu_time
)
{
#if defined(ERHE_METAL_TRACY_GPU_ZONES)
    // Every zone begin sent gets both GPU times: a sample the GPU did not
    // write (no sampled encoder inside the zone, an encoder without work,
    // no sample chunk, a command buffer dropped without submit) is replaced
    // so the zone closes, zero length, at the latest sample of the command
    // buffer resolved so far, or at the resolve time when there is none.
    const auto read_sample = [this, values, sample_count](const int index) -> uint64_t {
        if ((values == nullptr) || (index < 0) || (static_cast<std::size_t>(index) >= sample_count)) {
            return 0;
        }
        const uint64_t value = values[static_cast<std::size_t>(index)];
        return ((value == 0) || (value == MTL::CounterErrorValue)) ? 0 : ticks_to_nanoseconds(value);
    };
    uint64_t begin_time = read_sample(record.begin_sample);
    uint64_t end_time   = read_sample(record.end_sample);
    if ((begin_time == 0) && (end_time == 0)) {
        begin_time = (last_gpu_time != 0) ? last_gpu_time : now_gpu_time;
        end_time   = begin_time;
    } else if (begin_time == 0) {
        begin_time = end_time;
    } else if ((end_time == 0) || (end_time < begin_time)) {
        end_time = begin_time;
    }
    last_gpu_time = std::max(last_gpu_time, end_time);

#   if defined(TRACY_ON_DEMAND)
    if (record.tracy_connection_id != tracy::GetProfiler().ConnectionId()) {
        return; // begun on an earlier connection
    }
#   endif
    const uint16_t query_ids [2] = {record.tracy_query_id, static_cast<uint16_t>(record.tracy_query_id + 1)};
    const uint64_t gpu_times [2] = {begin_time, end_time};
    for (std::size_t i = 0; i < 2; ++i) {
        tracy::QueueItem* const item = tracy::Profiler::QueueSerial();
        tracy::MemWrite(&item->hdr.type,         tracy::QueueType::GpuTime);
        tracy::MemWrite(&item->gpuTime.gpuTime,  static_cast<int64_t>(gpu_times[i]));
        tracy::MemWrite(&item->gpuTime.queryId,  query_ids[i]);
        tracy::MemWrite(&item->gpuTime.context,  static_cast<uint8_t>(m_tracy_context_id));
        tracy::Profiler::QueueSerialFinish();
    }
#else
    static_cast<void>(record);
    static_cast<void>(values);
    static_cast<void>(sample_count);
    static_cast<void>(now_gpu_time);
    static_cast<void>(last_gpu_time);
#endif
}

void Gpu_timer_sample_pool::discard_command_buffer_samples(const Gpu_timer_command_buffer_samples& samples)
{
    if (!samples.needs_resolve()) {
        return;
    }
    // The GPU never ran the command buffer: no samples. Its Tracy zones were
    // sent to Tracy while it was recorded and still need their GPU times.
    MTL::Timestamp cpu_timestamp = 0;
    MTL::Timestamp gpu_timestamp = 0;
    m_mtl_device->sampleTimestamps(&cpu_timestamp, &gpu_timestamp);
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        uint64_t last_gpu_time = 0;
        for (std::size_t i = 0; i < samples.record_count; ++i) {
            const Gpu_timer_record& record = samples.records[i];
            if (record.is_tracy_zone) {
                send_tracy_zone_times(record, nullptr, 0, gpu_timestamp, last_gpu_time);
            }
        }
    }
    if (samples.has_chunk()) {
        release_chunk(samples.chunk_index);
    }
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
    for (MTL4::CounterHeap* const counter_heap : m_sample_buffers) {
        counter_heap->release();
    }
    m_sample_buffers.clear();
    if (m_mtl_device != nullptr) {
        m_mtl_device->release();
        m_mtl_device = nullptr;
    }
}

auto Gpu_timer_sample_pool::is_supported() const -> bool
{
    return m_supported;
}

auto Gpu_timer_sample_pool::has_timers() const -> bool
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (!m_supported) {
        return false;
    }
    if (!m_timers.empty()) {
        return true;
    }
#if defined(ERHE_METAL_TRACY_GPU_ZONES)
#   if defined(TRACY_ON_DEMAND)
    return (m_tracy_context_id >= 0) && tracy::GetProfiler().IsConnected();
#   else
    return m_tracy_context_id >= 0;
#   endif
#else
    return false;
#endif
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
    if (!m_supported) {
        return -1;
    }
    if (m_free_chunks.empty()) {
        if (m_sample_buffers.size() >= max_buffers) {
            return -1;
        }
        Scoped_transient_object_pool object_pool{};
        MTL4::CounterHeapDescriptor* const descriptor = MTL4::CounterHeapDescriptor::alloc()->init();
        descriptor->setType (MTL4::CounterHeapTypeTimestamp);
        descriptor->setCount(samples_per_buffer);
        NS::Error* error = nullptr;
        MTL4::CounterHeap* const counter_heap = m_mtl_device->newCounterHeap(descriptor, &error);
        descriptor->release();
        if (counter_heap == nullptr) {
            log_context->error(
                "Metal 4: newCounterHeap failed: {}",
                (error != nullptr) ? error->localizedDescription()->utf8String() : "(no error object)"
            );
            // Stop trying: every later acquire would fail the same way.
            m_supported = false;
            return -1;
        }
        counter_heap->setLabel(NS::String::string("erhe Gpu_timer samples", NS::UTF8StringEncoding));
        const int first_chunk = static_cast<int>(m_sample_buffers.size() * chunks_per_buffer);
        m_sample_buffers.push_back(counter_heap);
        for (int i = static_cast<int>(chunks_per_buffer) - 1; i >= 0; --i) {
            m_free_chunks.push_back(first_chunk + i);
        }
    }
    const int chunk_index = m_free_chunks.back();
    m_free_chunks.pop_back();
    ++m_chunks_out;
    // Entries the GPU does not write this time (an encoder without work)
    // must not read a previous use's timestamps.
    m_sample_buffers.at(static_cast<std::size_t>(chunk_index) / chunks_per_buffer)->invalidateCounterRange(
        NS::Range::Make(get_chunk_base(chunk_index), samples_per_chunk)
    );
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

auto Gpu_timer_sample_pool::get_chunk_buffer(const int chunk_index) const -> MTL4::CounterHeap*
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_sample_buffers.at(static_cast<std::size_t>(chunk_index) / chunks_per_buffer);
}

auto Gpu_timer_sample_pool::get_chunk_base(const int chunk_index) const -> std::size_t
{
    return (static_cast<std::size_t>(chunk_index) % chunks_per_buffer) * samples_per_chunk;
}

auto Gpu_timer_sample_pool::ticks_to_nanoseconds(const uint64_t ticks) const -> uint64_t
{
    return static_cast<uint64_t>(static_cast<double>(ticks) * m_ns_per_tick);
}

void Gpu_timer_sample_pool::resolve_command_buffer_samples(const Gpu_timer_command_buffer_samples& samples)
{
    if (!samples.needs_resolve()) {
        return;
    }
    Scoped_transient_object_pool object_pool{}; // drains the NS::Data resolveCounterRange returns
    // Without a chunk only Tracy zone records remain, resolved with no samples.
    const bool                      has_chunk     = samples.has_chunk();
    MTL4::CounterHeap* const        sample_buffer = has_chunk ? get_chunk_buffer(samples.chunk_index) : nullptr;
    const std::size_t               base          = has_chunk ? get_chunk_base(samples.chunk_index) : 0;
    const NS::Data* const data = (has_chunk && (samples.used_sample_count > 0))
        ? sample_buffer->resolveCounterRange(NS::Range::Make(base, samples.used_sample_count))
        : nullptr;
    const std::size_t sample_count = (data != nullptr) ? (data->length() / sizeof(uint64_t)) : 0;
    const uint64_t* const values = (data != nullptr) ? static_cast<const uint64_t*>(data->bytes()) : nullptr;

    // The resolve time closes Tracy zones without samples; sampleTimestamps
    // reports it in nanoseconds, the time base of ticks_to_nanoseconds.
    MTL::Timestamp cpu_timestamp = 0;
    MTL::Timestamp gpu_timestamp = 0;
    m_mtl_device->sampleTimestamps(&cpu_timestamp, &gpu_timestamp);

    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        uint64_t last_zone_gpu_time = 0;
        for (std::size_t i = 0; i < samples.record_count; ++i) {
            const Gpu_timer_record& record = samples.records[i];
            if (record.is_tracy_zone) {
                send_tracy_zone_times(record, values, sample_count, gpu_timestamp, last_zone_gpu_time);
                continue;
            }
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
    if (has_chunk) {
        release_chunk(samples.chunk_index);
    }
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
