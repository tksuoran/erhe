#pragma once

#include "erhe_graphics/metal4/metal4_gpu_timer.hpp"
#include "erhe_graphics/metal4/metal4_submission.hpp"
#include "erhe_utility/debug_label.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace MTL  { class Buffer; }
namespace MTL  { class Event; }
namespace MTL  { class SharedEvent; }
namespace MTL4 { class ArgumentTable; }
namespace MTL4 { class CommandBuffer; }
namespace MTL4 { class CommandEncoder; }
namespace MTL4 { class CommandQueue; }
namespace MTL4 { class ComputeCommandEncoder; }
namespace MTL4 { class RenderCommandEncoder; }
namespace NS   { class Object; }

namespace erhe::dataformat {
    enum class Format : unsigned int;
}

namespace erhe::graphics {

class Buffer;
class Command_buffer;
class Device;
class Device_impl;
class Frame_begin_info;
class Frame_end_info;
class Frame_state;
class Swapchain_impl;
class Texture;
enum class Image_layout : unsigned int;
class Texture_subresource_range;
enum class Barrier_scope : unsigned int;
enum class Memory_barrier_mask : unsigned int;

// Metal 4 backend Command_buffer implementation. begin() takes a Recording
// (MTL4::CommandBuffer + MTL4::CommandAllocator + commit options) from the
// device's Submission_pools and records into it; Device_impl::
// submit_command_buffers ends and commits it, and its commit feedback
// returns everything the recording took. Cross command buffer sync
// (signal_gpu_completion_to / wait_for_gpu_completion_signal /
// signal_cpu_completion / wait_for_cpu_completion_of) uses an MTL::Event /
// MTL::SharedEvent pair, waited on and signaled on the MTL4 queue around the
// commit.
class Command_buffer_impl final
{
public:
    Command_buffer_impl        (Device& device, erhe::utility::Debug_label debug_label);
    ~Command_buffer_impl       () noexcept;
    Command_buffer_impl        (const Command_buffer_impl&)    = delete;
    Command_buffer_impl& operator=(const Command_buffer_impl&) = delete;
    Command_buffer_impl        (Command_buffer_impl&&)         = delete;
    Command_buffer_impl& operator=(Command_buffer_impl&&)      = delete;

    [[nodiscard]] auto get_debug_label() const noexcept -> erhe::utility::Debug_label;
    // The device frame this cb was obtained for; stamped by
    // Device_impl::get_command_buffer.
    [[nodiscard]] auto get_frame_id   () const noexcept -> std::uint64_t;
    [[nodiscard]] auto get_completion_frame_index() const noexcept -> std::uint64_t;
    void               set_frame_id   (std::uint64_t frame_id) noexcept;

    void begin();
    void end  ();

    [[nodiscard]] auto wait_for_swapchain(Frame_state& out_frame_state) -> bool;
    [[nodiscard]] auto begin_swapchain   (const Frame_begin_info& frame_begin_info, Frame_state& out_frame_state) -> bool;
    void               end_swapchain     (const Frame_end_info& frame_end_info);

    void signal_gpu_completion_to    (Command_buffer& consumer);
    void wait_for_gpu_completion_signal();
    void signal_cpu_completion       ();
    void wait_for_cpu_completion_of  (Command_buffer& producer);

    void upload_to_buffer         (const Buffer& buffer, std::size_t offset, const void* data, std::size_t length);
    [[nodiscard]] auto try_upload_to_buffer(const Buffer& buffer, std::size_t offset, const void* data, std::size_t length, std::size_t staging_capacity_byte_count) -> bool;
    void upload_to_texture        (const Texture& texture, int level, int x, int y, int width, int height, erhe::dataformat::Format pixelformat, const void* data, int row_stride);
    void clear_texture            (const Texture& texture, std::array<double, 4> clear_value);
    void transition_texture_layout(const Texture& texture, Image_layout new_layout);
    void texture_barrier          (const Texture& texture, const Texture_subresource_range& range, std::uint64_t usage_before, std::uint64_t usage_after, Barrier_scope scope);
    void buffer_barrier           (const Buffer& buffer, std::uint64_t usage_before, std::uint64_t usage_after, Barrier_scope scope);
    void cmd_texture_barrier      (std::uint64_t usage_before, std::uint64_t usage_after);
    void memory_barrier           (Memory_barrier_mask barriers);

    // Every encoder recorded into this cb calls one of these right after
    // it is created, before encoding work (doc/erhe/metal4_backend.md
    // "Encoders and barriers"). With barriers pending (add_stage_barrier
    // between encoders) it encodes the consumer barrier for their consumers
    // in this encoder, merged with the encoder's own consumer pair
    // (consumer_after_stages -> consumer_before_stages: a render pass's
    // begin barrier, a transfer's wait for earlier accesses), and the
    // producer barrier for their consumers in every later encoder, then
    // clears them; without, only the encoder's own pair (none: no barrier).
    // use: draw for a render pass, transfer for a transfer recorded as a
    // render encoder (blit_framebuffer, depth / stencil clears), whose
    // render stages also stand for pending blit-stage consumers.
    enum class Render_encoder_use : unsigned int { draw, transfer };
    void encode_start_barriers(MTL4::RenderCommandEncoder*  encoder, Render_encoder_use use, std::uint64_t consumer_after_stages = 0, std::uint64_t consumer_before_stages = 0);
    void encode_start_barriers(MTL4::ComputeCommandEncoder* encoder, std::uint64_t consumer_after_stages = 0, std::uint64_t consumer_before_stages = 0);

    // A dependency from the producer stages (after_stages) to the consumer
    // stages (before_stages), MTL::Stages bits (metal4_submission.hpp).
    // Between encoders it is pending for the next encoder of this cb
    // (encode_start_barriers; end_recording encodes what is left). With a
    // compute encoder of this cb open on the calling thread it is encoded
    // there at once: the encoder's later dispatches wait (encoder barrier,
    // and a consumer barrier for producers in earlier encoders or commits)
    // and a producer barrier orders every later encoder. Every application
    // declaration lands here: texture_barrier / buffer_barrier,
    // memory_barrier, transition_texture_layout, cmd_texture_barrier.
    void add_stage_barrier      (std::uint64_t after_stages, std::uint64_t before_stages, Barrier_scope scope);
    // add_stage_barrier with the stages of two image usages
    // (get_image_usage_stages).
    void add_image_usage_barrier(std::uint64_t usage_before, std::uint64_t usage_after, Barrier_scope scope);

    // Backend access: encoders (Render_pass_impl, Compute_command_encoder_impl,
    // Blit_command_encoder_impl) record into this MTL4::CommandBuffer.
    // nullptr if begin() has not been called (or failed). Marks the current
    // recording as holding work (acquire_drawable_for_use) - also for
    // callers that only query it without encoding, which then cost an
    // extra (empty) commit before the drawable.
    [[nodiscard]] auto get_mtl4_command_buffer() noexcept -> MTL4::CommandBuffer*;

    // Called before every encoder that uses swapchain's current texture
    // (the swapchain render pass, a blit_framebuffer from or to it). At the
    // first such use of a frame whose drawable is still pending (windowed,
    // Swapchain_impl::is_drawable_pending) this cb, when it engaged that
    // swapchain and has recorded work, ends the recording so far and
    // commits it (Device_impl::commit_drawable_prefix), continues in a new
    // recording, and then takes the drawable (nextDrawable, the acquire of
    // the frame time record): the work before the drawable runs on the GPU
    // while the CPU waits for the drawable, and the queue's drawable wait at
    // submit holds back only what follows (doc/erhe/metal4_backend.md
    // "Present").
    void acquire_drawable_for_use(Swapchain_impl& swapchain);
    // Submit path: whether the work before the drawable was committed
    // ahead (the frame's GPU bracket then starts at that commit). Cleared
    // by end_recording.
    [[nodiscard]] auto has_committed_drawable_prefix() const noexcept -> bool;

    // An argument table for one encoder, held until the GPU has completed
    // this command buffer (MTL4 encoders snapshot a table's bindings at each
    // draw and dispatch; a table is never shared between encoders, so no
    // in-flight encoder reads one that is being rewritten).
    [[nodiscard]] auto acquire_argument_table() -> MTL4::ArgumentTable*;
    // Copies byte_count bytes into this command buffer's inline constant
    // chunks and returns their GPU address (the MTL4 replacement for
    // setBytes / setVertexBytes / setFragmentBytes).
    [[nodiscard]] auto allocate_inline_bytes(const void* data, std::size_t byte_count) -> uint64_t;
    // Takes over one reference to object, released once the GPU has
    // completed this command buffer (temporary pipeline states, depth
    // stencil states and texture views its commands use).
    void retain_until_completed(NS::Object* object);
    // Runs once the GPU has completed this command buffer (or at
    // destruction when it was never committed).
    void add_completed_callback(std::function<void()> callback);

    // Submit path (Device_impl::submit_command_buffers), in this order:
    // CPU waits of wait_for_cpu_completion_of, the queue waits of
    // wait_for_gpu_completion_signal (before the commit), end of recording
    // and the hand-over of the Recording, then the queue signals (after the
    // commit).
    void pre_submit_wait   ();
    // VERIFYs that every producer this cb waits for (wait_for_cpu_completion_of,
    // wait_for_gpu_completion_signal) has been committed already, so the
    // waits cannot hang. Called with the submit mutex held before a commit
    // that happens ahead of submit (Device_impl::commit_drawable_prefix).
    void verify_waited_producers_committed() const;
    void encode_queue_waits(MTL4::CommandQueue* queue);
    // Ends recording and hands the Recording (with this cb's timer samples)
    // to the caller, who commits it; nullptr when there is nothing to commit
    // (begin() was not called).
    [[nodiscard]] auto end_recording() -> Recording*;
    void encode_queue_signals(MTL4::CommandQueue* queue);

    // Swapchain that this cb engaged via begin_swapchain, or nullptr.
    // Device_impl::submit_command_buffers reads this to present the drawable
    // around the commit.
    [[nodiscard]] auto get_swapchain_used() const noexcept -> Swapchain_impl*;
    void               clear_swapchain_used() noexcept;

    // Gpu_timer measurements recorded on this cb (metal4_gpu_timer.hpp).
    // Encoders sample their start / end through it; Gpu_timer_impl records
    // begin / end against it.
    [[nodiscard]] auto get_gpu_timer_samples() noexcept -> Gpu_timer_command_buffer_samples&;
    [[nodiscard]] auto get_gpu_timer_sample_pool() const noexcept -> Gpu_timer_sample_pool*;

    // Called by Device_impl::get_command_buffer right after construction
    // to hand this cb its implicit GPU/CPU sync primitives, owned from then
    // on and released in the destructor.
    void set_implicit_sync(MTL::Event* gpu_event, MTL::SharedEvent* cpu_event) noexcept;

private:
    Device*                    m_device{nullptr};
    // Held directly, like the timer pool: the destructor of a cb never
    // committed returns its recording to the pools.
    Device_impl*               m_device_impl{nullptr};
    erhe::utility::Debug_label m_debug_label;
    std::uint64_t              m_frame_id{0};
    // The device frame begin() counted this command buffer against (its
    // feedback handler reports it); submit_command_buffers() counts the
    // commit against the same frame.
    std::uint64_t              m_completion_frame_index{0};

    // The pooled recording this cb records into (nullptr before begin() and
    // after end_recording()); it holds what the recording needs until GPU
    // completion.
    Recording*                 m_recording{nullptr};
    // True once an encoder was asked for m_recording's command buffer.
    bool                       m_recording_used{false};
    // acquire_drawable_for_use committed the work before the drawable.
    bool                       m_drawable_prefix_committed{false};
    MTL::Buffer*               m_inline_bytes_chunk {nullptr}; // the chunk being filled (also in m_recording)
    std::size_t                m_inline_bytes_offset{0};

    // Barriers (add_stage_barrier) recorded between encoders and not yet
    // encoded: the unions of their producer (after) and consumer (before)
    // stages as MTL::Stages bits. The next encoder encodes and clears them
    // (encode_start_barriers); end_recording encodes what is left in an
    // empty compute encoder (end_recording_barriers).
    class Pending_barrier
    {
    public:
        std::uint64_t after_stages {0};
        std::uint64_t before_stages{0};
    };
    void encode_pending_barriers(MTL4::CommandEncoder* encoder, std::uint64_t encoder_stages, std::uint64_t consumer_after_stages, std::uint64_t consumer_before_stages);
    void end_recording_barriers ();
    Pending_barrier            m_pending_barrier;

    MTL::Event*                m_implicit_gpu_event{nullptr};
    std::uint64_t              m_implicit_gpu_event_signal_value{0};
    MTL::SharedEvent*          m_implicit_cpu_event{nullptr};
    std::uint64_t              m_implicit_cpu_event_signal_value{0};
    // The highest values of the two events a committed producer has queued
    // a signal for (encode_queue_signals, under the submit mutex).
    std::uint64_t              m_implicit_gpu_event_queued_value{0};
    std::uint64_t              m_implicit_cpu_event_queued_value{0};

    class Sync_entry
    {
    public:
        Command_buffer* command_buffer{nullptr};
        std::uint64_t   value{0};
    };
    // Producers whose CPU event this cb's submit blocks on.
    std::vector<Sync_entry> m_wait_for_cpu_pending;
    // Consumers whose GPU event this cb signals after its commit.
    std::vector<Sync_entry> m_signal_gpu_pending;
    // Values of this cb's own GPU event the queue waits for before its
    // commit (wait_for_gpu_completion_signal).
    std::vector<std::uint64_t> m_gpu_waits_pending;
    // This cb signals its own CPU event after its commit
    // (signal_cpu_completion); the value is captured at registration.
    bool                    m_signal_own_cpu_completion{false};
    std::uint64_t           m_signal_own_cpu_completion_value{0};

    // Set by begin_swapchain when the per-frame swapchain lookup
    // succeeds. Cleared by Device_impl::submit_command_buffers after
    // the present. Mirrors Vulkan_command_buffer's m_swapchain_used.
    Swapchain_impl* m_swapchain_used{nullptr};

    // Held directly: ~Device tears down Device_impl (and its per-thread
    // cbs) after Device's impl pointer has been reset.
    Gpu_timer_sample_pool*           m_gpu_timer_sample_pool{nullptr};
    Gpu_timer_command_buffer_samples m_gpu_timer_samples;
};

} // namespace erhe::graphics
