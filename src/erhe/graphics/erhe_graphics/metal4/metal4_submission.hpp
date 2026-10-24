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
namespace MTL4 { class CommandEncoder; }
namespace MTL4 { class CommitFeedback; }
namespace MTL4 { class ComputeCommandEncoder; }
namespace MTL4 { class RenderCommandEncoder; }
namespace NS   { class Object; }

// The MTL4 submission core of the Metal 4 backend (doc/erhe/metal4_backend.md
// "Submission", "Residency", "Binding", "Barriers"): the residency set that
// holds every allocation the backend creates, the pooled recordings
// (command buffer + allocator + commit options + everything the recording
// holds until the GPU has completed it), the pools of argument tables and
// inline constant chunks, and the stage mapping of the barriers
// (doc/erhe/metal4_backend.md "Encoders and barriers").

namespace erhe::graphics {

enum class Image_layout : unsigned int;
enum class Memory_barrier_mask : unsigned int;

// Binding indices of the argument tables (the MSL contract of
// doc/erhe/metal_backend.md "Explicit Metal Buffer Index Layout").
constexpr std::size_t c_argument_table_buffer_count  = 31;
constexpr std::size_t c_argument_table_texture_count = 31;
constexpr std::size_t c_argument_table_sampler_count = 16;

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
    // The work of a presenting cb before its drawable's first use
    // (Command_buffer_impl::acquire_drawable_for_use), committed before the
    // drawable is taken; its GPU start opens the frame's GPU bracket.
    bool                        present_prefix  {false};
    // A presenting recording whose cb committed such a prefix: its GPU end
    // closes the frame's GPU bracket.
    bool                        has_present_prefix{false};
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

private:
    MTL::Device*                        m_mtl_device{nullptr};
    Residency&                          m_residency;
    std::mutex                          m_mutex;
    std::vector<std::unique_ptr<Recording>> m_all_recordings;
    std::vector<Recording*>             m_free_recordings;
    std::vector<MTL4::ArgumentTable*>   m_free_argument_tables;
    std::vector<MTL::Buffer*>           m_free_inline_bytes_chunks;
    std::vector<MTL::Buffer*>           m_all_inline_bytes_chunks;
};

// Barriers (MTL4 tracks no hazards; doc/erhe/metal4_backend.md "Encoders
// and barriers"). The one table that maps every synchronization declaration
// of the application to MTL::Stages bits (returned as uint64_t): the usages
// of texture_barrier, render pass attachments and cmd_texture_barrier
// (image), of buffer_barrier (buffer), the layouts of
// transition_texture_layout and the bits of memory_barrier. 0: no GPU stage
// of this queue (none, present, host accesses).
[[nodiscard]] auto get_image_usage_stages   (std::uint64_t usage) -> std::uint64_t;
[[nodiscard]] auto get_buffer_usage_stages  (std::uint64_t usage) -> std::uint64_t;
[[nodiscard]] auto get_image_layout_stages  (Image_layout layout) -> std::uint64_t;
[[nodiscard]] auto get_memory_barrier_stages(Memory_barrier_mask mask) -> std::uint64_t;
// MTL::Stages bits (MTLCommandEncoder.hpp; metal4_submission.cpp verifies
// them): single stages the backend's own recordings name, the stages a
// render encoder runs, the stages a compute encoder runs (dispatch, blit,
// acceleration structure), every stage of the queue (the source of
// memory_barrier and of a layout transition, as Vulkan's ALL_COMMANDS) and
// the shader stages (vertex | fragment | dispatch).
constexpr std::uint64_t c_stage_vertex                 = std::uint64_t{1} << 0;
constexpr std::uint64_t c_stage_fragment               = std::uint64_t{1} << 1;
constexpr std::uint64_t c_stage_tile                   = std::uint64_t{1} << 2;
constexpr std::uint64_t c_stage_object                 = std::uint64_t{1} << 3;
constexpr std::uint64_t c_stage_mesh                   = std::uint64_t{1} << 4;
constexpr std::uint64_t c_stage_dispatch               = std::uint64_t{1} << 27;
constexpr std::uint64_t c_stage_blit                   = std::uint64_t{1} << 28;
constexpr std::uint64_t c_stage_acceleration_structure = std::uint64_t{1} << 29;
constexpr std::uint64_t c_render_encoder_stages  = c_stage_vertex | c_stage_fragment | c_stage_tile | c_stage_object | c_stage_mesh;
constexpr std::uint64_t c_compute_encoder_stages = c_stage_dispatch | c_stage_blit | c_stage_acceleration_structure;
constexpr std::uint64_t c_all_stages             = c_render_encoder_stages | c_compute_encoder_stages;
constexpr std::uint64_t c_shader_stages          = c_stage_vertex | c_stage_fragment | c_stage_dispatch;
// The two MTL4 queue barriers (MTL4CommandEncoder.h), with
// VisibilityOptionDevice:
// - consumer barrier, barrierAfterQueueStages(after, before): the work of the
//   CURRENT encoder in the before stages waits for the after stages of all
//   work before the current encoder (earlier encoders of its command buffer
//   and earlier commits). Later encoders are not ordered by it.
// - producer barrier, barrierAfterStages(after, before): the work of every
//   LATER encoder (of this command buffer and of later commits) in the before
//   stages waits for the after stages of the work encoded so far in the
//   current encoder and in prior encoders.
void encode_consumer_barrier(MTL4::CommandEncoder* encoder, std::uint64_t after_stages, std::uint64_t before_stages);
void encode_producer_barrier(MTL4::CommandEncoder* encoder, std::uint64_t after_stages, std::uint64_t before_stages);
// Dispatches and blits inside one MTL4 compute encoder run concurrently:
// a barrier between consecutive ones (Metal 3 compute encoders were serial).
// Kept for every dispatch after the first until the application states the
// dependencies between dispatches (doc/plans/resource_barriers.md).
void encode_intra_encoder_barrier(MTL4::ComputeCommandEncoder* encoder);

} // namespace erhe::graphics
