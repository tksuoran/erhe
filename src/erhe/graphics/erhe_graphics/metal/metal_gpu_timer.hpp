#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace MTL { class CounterSampleBuffer; }
namespace MTL { class CounterSet; }
namespace MTL { class Device; }

namespace erhe::graphics {

class Command_buffer;
class Device;
class Gpu_timer_impl;
class Gpu_timer_sample_pool;

// GPU timers on Metal (doc/erhe/metal_backend.md "GPU timers").
//
// Apple GPUs sample timestamps only at stage boundaries
// (MTL::CounterSamplingPointAtStageBoundary): a timestamp is taken at the
// start or the end of an encoder, through the sampleBufferAttachments of the
// pass descriptor the encoder is created from. A Gpu_timer's begin / end
// therefore resolve to encoder boundaries:
//
// - Every render pass and compute encoder created on a command buffer while
//   any Gpu_timer exists samples its start (render: start of vertex) and end
//   (render: end of fragment) into the command buffer's chunk of a
//   device-owned MTL::CounterSampleBuffer.
// - begin(): the start of the encoder open on the command buffer, or, with
//   none open, the start of the next sampled encoder.
// - end(): the end of the encoder open on the command buffer, or, with none
//   open, the end of the last sampled encoder that ended.
//
// A render-pass bound timer is exact (its begin and end land inside the
// pass's own encoder). Explicit ranges bracket whole encoders; ranges inside
// one compute encoder all measure that whole encoder. An encoder with no
// work records no timestamps (the samples read 0) and leaves the timer's
// previous result in place.
//
// The samples are resolved in the command buffer's completion handler
// (resolve_command_buffer_samples), on a Metal dispatch thread.

// One timer measurement on a command buffer: sample indices within the
// command buffer's chunk; -1 while not (yet) known.
class Gpu_timer_record
{
public:
    uint64_t timer_serial{0};
    int      begin_sample{-1};
    int      end_sample  {-1};
    bool     begin_pending{false};
};

// The timer state of one command buffer. Copied by value into the command
// buffer's completion handler at submit, which returns the chunk to the pool.
class Gpu_timer_command_buffer_samples
{
public:
    static constexpr std::size_t max_records = 64;

    // Called when an encoder is created on the command buffer. Returns false
    // when the encoder is not sampled (no live timers, or no sample chunk);
    // otherwise out_buffer / out_start / out_end are the sample buffer and
    // the absolute sample indices to attach to the pass descriptor.
    [[nodiscard]] auto begin_encoder(
        Gpu_timer_sample_pool&       pool,
        MTL::CounterSampleBuffer*&   out_buffer,
        std::size_t&                 out_start,
        std::size_t&                 out_end
    ) -> bool;
    void end_encoder();

    void write_begin(uint64_t timer_serial);
    void write_end  (uint64_t timer_serial);

    [[nodiscard]] auto has_chunk() const -> bool;

    int                                       chunk_index        {-1};
    std::size_t                               used_sample_count  {0};
    int                                       open_encoder_start {-1};
    int                                       open_encoder_end   {-1};
    int                                       last_ended_end     {-1};
    std::size_t                               record_count       {0};
    std::array<Gpu_timer_record, max_records> records            {};
};

// Device-owned: the counter sample buffers (split into per-command-buffer
// chunks), the registry of live timers, and the GPU-tick to nanosecond
// calibration.
class Gpu_timer_sample_pool
{
public:
    static constexpr std::size_t samples_per_chunk  = 256;
    // MTL::CounterSampleBuffer is limited to 32 KiB: 4096 timestamps.
    static constexpr std::size_t samples_per_buffer = 4096;
    static constexpr std::size_t chunks_per_buffer  = samples_per_buffer / samples_per_chunk;
    static constexpr std::size_t max_buffers        = 8;

    explicit Gpu_timer_sample_pool(MTL::Device* mtl_device);
    ~Gpu_timer_sample_pool() noexcept;

    Gpu_timer_sample_pool(const Gpu_timer_sample_pool&) = delete;
    auto operator=       (const Gpu_timer_sample_pool&) = delete;
    Gpu_timer_sample_pool(Gpu_timer_sample_pool&&)      = delete;
    auto operator=       (Gpu_timer_sample_pool&&)      = delete;

    [[nodiscard]] auto is_supported() const -> bool;
    // True while at least one Gpu_timer exists: encoders are sampled only then.
    [[nodiscard]] auto has_timers  () const -> bool;

    [[nodiscard]] auto register_timer  (Gpu_timer_impl* timer) -> uint64_t;
                  void unregister_timer(uint64_t timer_serial);

    [[nodiscard]] auto acquire_chunk() -> int;
                  void release_chunk(int chunk_index);
    [[nodiscard]] auto get_chunk_buffer(int chunk_index) const -> MTL::CounterSampleBuffer*;
    [[nodiscard]] auto get_chunk_base  (int chunk_index) const -> std::size_t;

    // Completion handler of a command buffer: reads the chunk's samples,
    // stores each complete record's duration into its timer (if the timer
    // still exists) and returns the chunk.
    void resolve_command_buffer_samples(const Gpu_timer_command_buffer_samples& samples);

private:
    [[nodiscard]] auto ticks_to_nanoseconds(uint64_t ticks) -> uint64_t;

    class Registered_timer
    {
    public:
        uint64_t        serial{0};
        Gpu_timer_impl* timer {nullptr};
    };

    MTL::Device*                           m_mtl_device        {nullptr};
    MTL::CounterSet*                       m_timestamp_counter_set{nullptr};
    mutable std::mutex                     m_mutex;
    std::condition_variable                m_chunks_returned;
    std::vector<MTL::CounterSampleBuffer*> m_sample_buffers;
    std::vector<int>                       m_free_chunks;
    std::size_t                            m_chunks_out        {0};
    std::vector<Registered_timer>          m_timers;
    uint64_t                               m_next_timer_serial {1};
    uint64_t                               m_calibration_cpu_ns{0};
    uint64_t                               m_calibration_gpu   {0};
    double                                 m_ns_per_tick       {1.0};
};

class Gpu_timer_impl
{
public:
    Gpu_timer_impl(Device& device, const char* label);
    ~Gpu_timer_impl() noexcept;

    Gpu_timer_impl(const Gpu_timer_impl&) = delete;
    auto operator=(const Gpu_timer_impl&) = delete;
    Gpu_timer_impl(Gpu_timer_impl&&)      = delete;
    auto operator=(Gpu_timer_impl&&)      = delete;

    [[nodiscard]] auto last_result() -> uint64_t;
    [[nodiscard]] auto label      () const -> const char*;

    void write_begin_timestamp(Command_buffer& command_buffer);
    void write_end_timestamp  (Command_buffer& command_buffer);

    // Called by Gpu_timer_sample_pool::resolve_command_buffer_samples, with
    // the pool mutex held (unregister_timer takes the same mutex, so the
    // timer is never written while being destroyed).
    void store_last_result_ns(uint64_t nanoseconds);

private:
    Gpu_timer_sample_pool* m_pool        {nullptr};
    const char*            m_label       {nullptr};
    uint64_t               m_serial      {0};
    std::mutex             m_result_mutex;
    uint64_t               m_last_result {0};
};

} // namespace erhe::graphics
