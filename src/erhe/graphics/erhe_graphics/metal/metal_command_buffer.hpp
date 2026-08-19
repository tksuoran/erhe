#pragma once

#include "erhe_utility/debug_label.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace MTL { class CommandBuffer; }
namespace MTL { class Event; }
namespace MTL { class Fence; }
namespace MTL { class SharedEvent; }

namespace erhe::dataformat {
    enum class Format : unsigned int;
}

namespace erhe::graphics {

class Buffer;
class Command_buffer;
class Device;
class Frame_begin_info;
class Frame_end_info;
class Frame_state;
class Swapchain_impl;
class Texture;
enum class Image_layout : unsigned int;
enum class Memory_barrier_mask : unsigned int;

// Metal backend Command_buffer implementation. Owns one
// MTL::CommandBuffer allocated from the device's MTL::CommandQueue in
// begin(), plus a per-cb MTL::Fence used to serialize encoders inside
// the cb and a pair of GPU/CPU sync primitives (MTL::Event /
// MTL::SharedEvent) used by wait_for_gpu / wait_for_cpu / signal_gpu /
// signal_cpu for cross-cb synchronization.
class Command_buffer_impl final
{
public:
    Command_buffer_impl        (Device& device, erhe::utility::Debug_label debug_label);
    ~Command_buffer_impl       () noexcept;
    Command_buffer_impl        (const Command_buffer_impl&)            = delete;
    Command_buffer_impl& operator=(const Command_buffer_impl&)         = delete;
    Command_buffer_impl        (Command_buffer_impl&& other) noexcept;
    Command_buffer_impl& operator=(Command_buffer_impl&& other) noexcept;

    [[nodiscard]] auto get_debug_label() const noexcept -> erhe::utility::Debug_label;

    void begin();
    void end  ();

    [[nodiscard]] auto wait_for_swapchain(Frame_state& out_frame_state) -> bool;
    [[nodiscard]] auto begin_swapchain   (const Frame_begin_info& frame_begin_info, Frame_state& out_frame_state) -> bool;
    void               end_swapchain     (const Frame_end_info& frame_end_info);

    void wait_for_cpu(Command_buffer& other);
    void wait_for_gpu(Command_buffer& other);
    void signal_gpu  (Command_buffer& other);
    void signal_cpu  (Command_buffer& other);

    void upload_to_buffer         (const Buffer& buffer, std::size_t offset, const void* data, std::size_t length);
    void upload_to_texture        (const Texture& texture, int level, int x, int y, int width, int height, erhe::dataformat::Format pixelformat, const void* data, int row_stride);
    void clear_texture            (const Texture& texture, std::array<double, 4> clear_value);
    void transition_texture_layout(const Texture& texture, Image_layout new_layout);
    void cmd_texture_barrier      (std::uint64_t usage_before, std::uint64_t usage_after);
    void memory_barrier           (Memory_barrier_mask barriers);

    // Backend access: encoders (Render_pass_impl, Compute_command_encoder_impl,
    // Blit_command_encoder_impl) and Device_impl::submit_command_buffers
    // record/commit on this MTL::CommandBuffer. Returns nullptr if begin()
    // has not been called.
    [[nodiscard]] auto get_mtl_command_buffer  () const noexcept -> MTL::CommandBuffer*;
    // Drops the reference taken in begin(). Called by
    // Device_impl::submit_command_buffers right after commit: Metal keeps
    // the cb alive until its completion handlers have run, and the queue
    // frees the in-flight slot the cb holds only when it deallocates.
    void release_mtl_command_buffer() noexcept;
    // Per-cb MTL::Fence for serializing encoders inside this cb. Replaces
    // the legacy device-frame-fence path on Device_impl. Each encoder
    // waitForFence at start and updateFence at end.
    [[nodiscard]] auto get_inter_encoder_fence() const noexcept -> MTL::Fence*;

    // Implicit per-cb sync primitives. The "GPU event" backs wait_for_gpu /
    // signal_gpu (MTL::Event encoded on the MTL::CommandBuffer). The "CPU
    // event" backs wait_for_cpu / signal_cpu (MTL::SharedEvent that the
    // CPU can block on via waitUntilSignaledValue).
    [[nodiscard]] auto get_implicit_gpu_event              () const noexcept -> MTL::Event*;
    [[nodiscard]] auto get_implicit_gpu_event_signal_value () const noexcept -> std::uint64_t;
    [[nodiscard]] auto get_implicit_cpu_event              () const noexcept -> MTL::SharedEvent*;
    [[nodiscard]] auto get_implicit_cpu_event_signal_value () const noexcept -> std::uint64_t;

    // Called by Device_impl::get_command_buffer right after construction
    // to hand this cb its implicit GPU/CPU sync primitives. The
    // primitives are owned by this Command_buffer_impl from this point on
    // and are released in the destructor. Mirrors the Vulkan
    // set_implicit_sync pattern.
    void set_implicit_sync(MTL::Event* gpu_event, MTL::SharedEvent* cpu_event) noexcept;

    // CPU-side wait phase of submit. For each wait_for_cpu(other)
    // registered, blocks on other's MTL::SharedEvent until the captured
    // signal value is reached. Called by Device_impl::submit_command_buffers
    // before the cb is committed.
    void pre_submit_wait();

    // Encode pending signal events onto m_mtl_command_buffer for each
    // signal_gpu / signal_cpu registered. Must be called after every
    // encoder for this cb has called endEncoding (Metal disallows event
    // encode while an encoder is open) and before commit. wait_for_gpu
    // is encoded immediately at call time (Metal positional cb model);
    // see the method comment.
    void encode_synchronization();

    // Swapchain that this cb engaged via begin_swapchain, or nullptr.
    // Device_impl::submit_command_buffers reads this to drive
    // presentDrawable() before commit.
    [[nodiscard]] auto get_swapchain_used() const noexcept -> Swapchain_impl*;
    void               clear_swapchain_used() noexcept;

private:
    Device*                    m_device{nullptr};
    erhe::utility::Debug_label m_debug_label;
    MTL::CommandBuffer*        m_mtl_command_buffer{nullptr};
    MTL::Fence*                m_inter_encoder_fence{nullptr};
    MTL::Event*                m_implicit_gpu_event{nullptr};
    std::uint64_t              m_implicit_gpu_event_signal_value{0};
    MTL::SharedEvent*          m_implicit_cpu_event{nullptr};
    std::uint64_t              m_implicit_cpu_event_signal_value{0};

    // Sync registrations populated by wait_for_cpu / signal_gpu /
    // signal_cpu. wait_for_gpu is NOT recorded here -- Metal positional
    // event encoding requires the wait to land before the user's
    // encoders, so wait_for_gpu encodes immediately at call time.
    class Sync_entry
    {
    public:
        Command_buffer* command_buffer{nullptr};
        std::uint64_t   value{0};
    };
    std::vector<Sync_entry> m_wait_for_cpu_pending;
    std::vector<Sync_entry> m_signal_gpu_pending;
    std::vector<Sync_entry> m_signal_cpu_pending;

    // Set by begin_swapchain when the per-frame swapchain lookup
    // succeeds. Cleared by Device_impl::submit_command_buffers after
    // presentDrawable runs. Mirrors Vulkan_command_buffer's
    // m_swapchain_used.
    Swapchain_impl* m_swapchain_used{nullptr};
};

} // namespace erhe::graphics
