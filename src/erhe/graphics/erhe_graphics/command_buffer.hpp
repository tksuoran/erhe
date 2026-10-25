#pragma once

#include "erhe_utility/debug_label.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace erhe::dataformat {
    enum class Format : unsigned int;
}

namespace erhe::graphics {

class Buffer;
class Command_buffer_impl;
class Device;
class Frame_begin_info;
class Frame_end_info;
class Frame_state;
class Render_pass;
class Texture;
class Texture_subresource_range;
enum class Barrier_scope : unsigned int;
enum class Image_layout : unsigned int;
enum class Memory_barrier_mask : unsigned int;

// Public erhe::graphics handle for a backend command buffer. Callers
// (notably erhe::xr) hold an explicit Command_buffer instead of relying on
// an implicit single-cb-per-frame model, so submission timing is decoupled
// from Device::end_frame().
class Command_buffer final
{
public:
    explicit Command_buffer(Device& device, erhe::utility::Debug_label debug_label = {});
    ~Command_buffer        () noexcept;
    Command_buffer         (const Command_buffer&)            = delete;
    Command_buffer& operator=(const Command_buffer&)          = delete;
    Command_buffer         (Command_buffer&& other) noexcept;
    Command_buffer& operator=(Command_buffer&& other) noexcept;

    [[nodiscard]] auto get_debug_label() const noexcept -> erhe::utility::Debug_label;

    // The device frame this cb was obtained for (Device::get_frame_index()
    // of the calling role at Device::get_command_buffer()). The cb belongs
    // to that frame's frame-in-flight slot and is submitted in that frame.
    [[nodiscard]] auto get_frame_id() const noexcept -> std::uint64_t;

    void begin();
    void end  ();

    // True between begin() and end(). Used by callers that may have already
    // ended + submitted the cb deeper in the call stack (e.g. OpenXR's
    // submit-before-xrEndFrame requirement) to avoid double end / double
    // submit.
    [[nodiscard]] auto is_recording() const noexcept -> bool;

    // Swapchain-frame lifecycle (moved off Device). wait_for_swapchain
    // pumps the per-frame-in-flight pacing on the backing Swapchain
    // (pool reset, fence recycle on Vulkan; acquire of next image and
    // setup of acquire/present semaphores) and fills Frame_state with
    // predicted display timing. begin_swapchain / end_swapchain bracket
    // recording that targets the swapchain image so the backend can
    // track which submit branch this Command_buffer feeds.
    [[nodiscard]] auto wait_for_swapchain(Frame_state& out_frame_state) -> bool;
    [[nodiscard]] auto begin_swapchain   (const Frame_begin_info& frame_begin_info, Frame_state& out_frame_state) -> bool;
    void               end_swapchain     (const Frame_end_info& frame_end_info);

    // Cross-Command_buffer synchronization. Each Command_buffer carries
    // an implicit GPU-side primitive (Vulkan: binary VkSemaphore;
    // Metal: MTL::Event) and an implicit CPU-observable primitive
    // (Vulkan: VkFence; Metal: MTL::SharedEvent), allocated by
    // Device_impl when the cb is handed out by get_command_buffer().
    // The primitives are never exposed; the four methods below register
    // dependency edges that the backend collects at submit time.
    //
    // A GPU-to-GPU edge from a producer cb to a consumer cb is two
    // calls, one on each side. The primitive involved is the
    // consumer's, so one producer can fan out to N consumers (a binary
    // semaphore can be waited on once per signal):
    //
    //   producer.signal_gpu_completion_to(consumer)
    //       When my GPU work completes, signal consumer's GPU primitive.
    //       Vulkan: VkSemaphore signal on my submit. Metal:
    //       encodeSignalEvent on consumer's MTL::Event.
    //   consumer.wait_for_gpu_completion_signal()
    //       Before my GPU work starts, wait for the signal a producer
    //       registered on me (no CPU stall). Vulkan: VkSemaphore wait
    //       on my submit. Metal: encodeWait on my MTL::Event; call it
    //       after begin() and before recording any work.
    //
    // A GPU-to-CPU edge uses the producer's primitive, so the signal
    // side names nobody and the wait side names the producer:
    //
    //   producer.signal_cpu_completion()
    //       Make my submit signal my CPU-observable primitive when my
    //       GPU work completes. Vulkan: VkFence on vkQueueSubmit2
    //       (one fence per submit, so at most one cb per submit batch
    //       may call this). Metal: MTL::SharedEvent encodeSignalEvent.
    //   consumer.wait_for_cpu_completion_of(producer)
    //       Block the CPU before my submit is enqueued, until producer's
    //       GPU work has completed. Vulkan: vkWaitForFences. Metal:
    //       MTL::SharedEvent::waitUntilSignaledValue.
    void signal_gpu_completion_to    (Command_buffer& consumer);
    void wait_for_gpu_completion_signal();
    void signal_cpu_completion       ();
    void wait_for_cpu_completion_of  (Command_buffer& producer);

    // GPU-work primitives. These record commands into the cb's
    // backend command buffer. Each one used to live on Device and
    // route through the now-removed immediate-commands path; with
    // the explicit cb model, the caller owns the cb and must
    // submit it via Device::submit_command_buffers (or batch it
    // with other cbs that the caller submits).
    void upload_to_buffer         (const Buffer& buffer, std::size_t offset, const void* data, std::size_t length);
    // Like upload_to_buffer, for uploads that may wait a frame: host-visible
    // staging, when the backend needs any, comes from
    // Device::allocate_upload_staging with the given cap. Returns false and
    // records nothing when the staging has no room now; retry next frame.
    [[nodiscard]] auto try_upload_to_buffer(const Buffer& buffer, std::size_t offset, const void* data, std::size_t length, std::size_t staging_capacity_byte_count) -> bool;
    void upload_to_texture        (const Texture& texture, int level, int x, int y, int width, int height, erhe::dataformat::Format pixelformat, const void* data, int row_stride);
    void clear_texture            (const Texture& texture, std::array<double, 4> clear_value);
    void transition_texture_layout(const Texture& texture, Image_layout new_layout);

    // Resource barriers (doc/erhe/graphics.md "Resource barriers"). The call
    // orders the resource's accesses of usage_before, recorded earlier in
    // this command buffer (or, with Barrier_scope::earlier_submissions, in
    // work submitted to the queue before it), before its accesses of
    // usage_after recorded after the call, and makes the earlier writes
    // visible to them. texture_barrier also performs the layout transition
    // the usage pair implies on backends with image layouts. Nothing is
    // inferred: a missing barrier is an application bug. Usages are
    // Image_usage_flag_bit_mask / Buffer_usage_flag_bit_mask bits, with
    // optional stage qualifiers (vertex_shader, fragment_shader,
    // compute_shader) for the shader accesses of each side. Outside of a
    // render pass only.
    void texture_barrier(
        const Texture& texture,
        std::uint64_t  usage_before,
        std::uint64_t  usage_after,
        Barrier_scope  scope
    );
    void texture_barrier(const Texture& texture, std::uint64_t usage_before, std::uint64_t usage_after);
    void texture_barrier(
        const Texture&                   texture,
        const Texture_subresource_range& range,
        std::uint64_t                    usage_before,
        std::uint64_t                    usage_after,
        Barrier_scope                    scope
    );
    void texture_barrier(
        const Texture&                   texture,
        const Texture_subresource_range& range,
        std::uint64_t                    usage_before,
        std::uint64_t                    usage_after
    );
    void buffer_barrier(const Buffer& buffer, std::uint64_t usage_before, std::uint64_t usage_after, Barrier_scope scope);
    void buffer_barrier(const Buffer& buffer, std::uint64_t usage_before, std::uint64_t usage_after);

    // Option B explicit barrier (between render passes). Inserts a
    // pipeline barrier from usage_before to usage_after into the cb.
    // Must be called outside of a render pass (between
    // Scoped_render_pass destructor and the next constructor).
    // A global (resource-less) barrier; texture_barrier names the texture
    // and its layout transition instead.
    void cmd_texture_barrier      (std::uint64_t usage_before, std::uint64_t usage_after);

    // glMemoryBarrier-shaped global memory barrier. Records a
    // vkCmdPipelineBarrier2 (Vulkan) or glMemoryBarrier (GL) into the
    // cb. Must be called outside of a render pass. The catch-all for
    // dependencies that name no single resource; texture_barrier /
    // buffer_barrier state one resource's dependency.
    void memory_barrier           (Memory_barrier_mask barriers);

    // The render pass started on this command buffer and not yet ended
    // (Render_pass::start_render_pass / end_render_pass set it); nullptr
    // outside a pass. Kept per command buffer, so passes that two threads
    // record into their own command buffers at the same time (the render
    // thread's frame and the main thread's prepare-phase standalone work)
    // do not see each other (doc/erhe/graphics.md "Thread safety").
    [[nodiscard]] auto get_active_render_pass() const noexcept -> Render_pass*;
    void               set_active_render_pass(Render_pass* render_pass) noexcept;

    [[nodiscard]] auto get_impl()       -> Command_buffer_impl&;
    [[nodiscard]] auto get_impl() const -> const Command_buffer_impl&;

private:
    std::unique_ptr<Command_buffer_impl> m_impl;
    bool                                 m_recording         {false};
    Render_pass*                         m_active_render_pass{nullptr};
};

} // namespace erhe::graphics
