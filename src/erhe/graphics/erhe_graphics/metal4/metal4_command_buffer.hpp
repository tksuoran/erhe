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
namespace MTL4 { class CommandQueue; }
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
    void upload_to_texture        (const Texture& texture, int level, int x, int y, int width, int height, erhe::dataformat::Format pixelformat, const void* data, int row_stride);
    void clear_texture            (const Texture& texture, std::array<double, 4> clear_value);
    void transition_texture_layout(const Texture& texture, Image_layout new_layout);
    void cmd_texture_barrier      (std::uint64_t usage_before, std::uint64_t usage_after);
    void memory_barrier           (Memory_barrier_mask barriers);

    // Backend access: encoders (Render_pass_impl, Compute_command_encoder_impl,
    // Blit_command_encoder_impl) record into this MTL4::CommandBuffer.
    // nullptr if begin() has not been called (or failed).
    [[nodiscard]] auto get_mtl4_command_buffer() const noexcept -> MTL4::CommandBuffer*;

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
    MTL::Buffer*               m_inline_bytes_chunk {nullptr}; // the chunk being filled (also in m_recording)
    std::size_t                m_inline_bytes_offset{0};

    MTL::Event*                m_implicit_gpu_event{nullptr};
    std::uint64_t              m_implicit_gpu_event_signal_value{0};
    MTL::SharedEvent*          m_implicit_cpu_event{nullptr};
    std::uint64_t              m_implicit_cpu_event_signal_value{0};

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
