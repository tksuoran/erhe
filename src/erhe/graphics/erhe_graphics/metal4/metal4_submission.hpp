#pragma once

#include "erhe_graphics/metal4/metal4_gpu_timer.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace MTL  { class Allocation; }
namespace MTL  { class Buffer; }
namespace MTL  { class Device; }
namespace MTL  { class ResidencySet; }
namespace MTL4 { class ArgumentTable; }
namespace MTL4 { class CommandAllocator; }
namespace MTL4 { class CommandBuffer; }
namespace MTL4 { class CommitFeedback; }
namespace MTL4 { class ComputeCommandEncoder; }
namespace MTL4 { class RenderCommandEncoder; }
namespace NS   { class Object; }

// The MTL4 submission core of the Metal 4 backend (doc/erhe/metal4_backend.md
// "Submission", "Residency", "Binding", "Barriers"): the residency set that
// holds every allocation the backend creates, the pooled recordings
// (command buffer + allocator + commit options + everything the recording
// holds until the GPU has completed it), the pools of argument tables and
// inline constant chunks, the immutable draw id table the push constant
// points into, and the conservative barriers every encoder starts with.

namespace erhe::graphics {

// Binding indices of the argument tables (the MSL contract of
// doc/erhe/metal_backend.md "Explicit Metal Buffer Index Layout").
constexpr std::size_t c_argument_table_buffer_count  = 31;
constexpr std::size_t c_argument_table_texture_count = 31;
constexpr std::size_t c_argument_table_sampler_count = 16;
constexpr std::size_t c_metal_push_constant_index    = 15;

class Device_impl;
class Recording;

// Feedback of a backend-internal recording (the GPU pre-warm, the headless
// swapchain readback, the present calibration probes), called on the Metal
// feedback thread before the recording returns to its pool.
using Recording_feedback_function = void (*)(Recording& recording, MTL4::CommitFeedback* feedback);

// One MTL4::CommandBuffer and the MTL4::CommandAllocator its commands are
// recorded into; the feedback handler of its commit
// (Device_impl::commit_locked) calls Device_impl::on_commit_feedback with
// this recording. Taken from
// Submission_pools for one recording and returned (allocator reset, lists
// cleared with their capacity kept) once the GPU has completed the commit,
// so steady-state commits allocate nothing.
class Recording final
{
public:
    Recording() = default;
    ~Recording() noexcept;
    Recording(const Recording&) = delete;
    auto operator=(const Recording&) -> Recording& = delete;

    MTL4::CommandBuffer*    command_buffer{nullptr};
    MTL4::CommandAllocator* allocator     {nullptr};

    // What the recording holds until the GPU has completed it: argument
    // tables and inline constant chunks (returned to the pools), objects its
    // commands reference (MTL4 command buffers retain nothing), callbacks
    // (staging buffer releases), timer samples.
    std::vector<MTL4::ArgumentTable*>  argument_tables;
    std::vector<MTL::Buffer*>          inline_bytes_chunks;
    std::vector<NS::Object*>           retained_objects;
    std::vector<std::function<void()>> callbacks;
    Gpu_timer_command_buffer_samples   gpu_timer_samples;

    // The commit, set by the submit path.
    uint64_t                    serial          {0};
    bool                        counts_frame    {false}; // a Command_buffer's commit, charged to frame_index
    uint64_t                    frame_index     {0};
    bool                        presents        {false};
    int64_t                     present_frame_id{0};
    // Backend-internal recordings: their feedback and its arguments.
    Recording_feedback_function on_feedback     {nullptr};
    void*                       user            {nullptr};
    void*                       user_result     {nullptr};
    uint64_t                    user_value      {0};
};

// The device's MTL::ResidencySet, attached to the MTL4 command queue. Every
// buffer, texture, indirect command buffer and acceleration structure the
// backend allocates is added at creation and removed at destruction; the
// set is committed at most once per submit, when it changed (commit() is
// not free). Texture views are not added: they share their parent's
// allocation. Thread-safe.
class Residency final
{
public:
    explicit Residency(MTL::Device* mtl_device);
    ~Residency() noexcept;
    Residency(const Residency&) = delete;
    auto operator=(const Residency&) -> Residency& = delete;

    [[nodiscard]] auto get_residency_set() const -> MTL::ResidencySet*;

    void add   (const MTL::Allocation* allocation);
    // The caller releases the allocation right after: a reference is held
    // until the removal is committed.
    void remove(MTL::Allocation* allocation);
    // Called by the submit path (under the device's submit mutex) before
    // each queue commit.
    void commit_if_changed();

private:
    std::mutex                    m_mutex;
    MTL::ResidencySet*            m_residency_set{nullptr};
    bool                          m_changed      {false};
    std::vector<MTL::Allocation*> m_removed;     // released after the next commit
};

// Pools of what a command buffer takes while it records. Every acquire is
// matched by a release from the commit feedback handler (or from the
// command buffer's destructor when it was never committed), so the pools
// reach a high-water mark and allocation traffic stops. Thread-safe.
class Submission_pools final
{
public:
    Submission_pools(MTL::Device* mtl_device, Residency& residency);
    ~Submission_pools() noexcept;
    Submission_pools(const Submission_pools&) = delete;
    auto operator=(const Submission_pools&) -> Submission_pools& = delete;

    // A recording with its command buffer begun.
    [[nodiscard]] auto acquire_recording() -> Recording*;
    // Resets the allocator (the GPU has completed every command recorded
    // into it, or never saw them) and the commit description. The caller
    // has already returned what the recording held
    // (Device_impl::release_recording_payload).
    void release_recording(Recording* recording);

    [[nodiscard]] auto acquire_argument_table() -> MTL4::ArgumentTable*;
    void release_argument_table(MTL4::ArgumentTable* argument_table);

    // Shared, resident chunks of c_inline_bytes_chunk_size bytes for inline
    // constants (what setBytes / setVertexBytes carried in Metal 3).
    static constexpr std::size_t c_inline_bytes_chunk_size = 64 * 1024;
    static constexpr std::size_t c_inline_bytes_alignment  = 256;
    [[nodiscard]] auto acquire_inline_bytes_chunk() -> MTL::Buffer*;
    void release_inline_bytes_chunk(MTL::Buffer* chunk);

    // The push constant (ERHE_DRAW_ID, index 15) of draw i points at entry i
    // of an immutable table, so draws set no inline data at all. Entries are
    // c_draw_id_stride bytes apart (constant buffer offset alignment).
    // Returns the address of entry 0 of a table holding at least
    // draw_count entries; a table that grows keeps the earlier ones alive
    // until the pools are destroyed (in-flight commands point into them).
    static constexpr std::size_t c_draw_id_stride = 16;
    [[nodiscard]] auto get_draw_id_table(std::size_t draw_count) -> uint64_t;
    // Entry 0 (ERHE_DRAW_ID = 0) of the first table, which stays valid for
    // the pools' lifetime: no lock, for every pipeline bind and encoder
    // start.
    [[nodiscard]] auto get_draw_id_zero() const -> uint64_t { return m_draw_id_zero; }

private:
    [[nodiscard]] auto create_draw_id_table(std::size_t entry_count) -> bool;

    MTL::Device*                        m_mtl_device{nullptr};
    Residency&                          m_residency;
    std::mutex                          m_mutex;
    std::vector<std::unique_ptr<Recording>> m_all_recordings;
    std::vector<Recording*>             m_free_recordings;
    std::vector<MTL4::ArgumentTable*>   m_free_argument_tables;
    std::vector<MTL::Buffer*>           m_free_inline_bytes_chunks;
    std::vector<MTL::Buffer*>           m_all_inline_bytes_chunks;
    std::vector<MTL::Buffer*>           m_draw_id_tables;          // newest last
    std::size_t                         m_draw_id_capacity{0};
    uint64_t                            m_draw_id_zero    {0};
};

// Hazards (MTL4 tracks none): every encoder starts with a consumer barrier
// on all earlier work of the queue (earlier encoders of its own command
// buffer and every earlier commit), which replaces the Metal 3 backend's
// inter-encoder fence and Metal's hazard tracking. Conservative; see
// doc/erhe/metal4_backend.md "Barriers".
void encode_queue_barrier(MTL4::RenderCommandEncoder*  encoder);
void encode_queue_barrier(MTL4::ComputeCommandEncoder* encoder);
// Dispatches and blits inside one MTL4 compute encoder run concurrently:
// a barrier between consecutive ones (Metal 3 compute encoders were serial).
void encode_intra_encoder_barrier(MTL4::ComputeCommandEncoder* encoder);

} // namespace erhe::graphics
