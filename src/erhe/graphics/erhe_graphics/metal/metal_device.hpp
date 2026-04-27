#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/shader_monitor.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

typedef int GLint;

namespace MTL { class ArgumentEncoder; }
namespace MTL { class CommandBuffer; }
namespace MTL { class Device; }
namespace MTL { class CommandQueue; }
namespace MTL { class Fence; }
namespace MTL { class LogState; }
namespace MTL { class SamplerState; }
namespace CA  { class MetalDrawable; }

namespace erhe::dataformat {
    enum class Format : unsigned int;
}

namespace erhe::graphics {

class Frame_state;
class Frame_end_info;
class Render_pass_impl;
class Ring_buffer_client;
class Surface;
class Swapchain;

// Mirrors Vulkan's Device_frame_state. Tracks lifecycle of new-style
// begin_swapchain_frame / end_swapchain_frame API, and backs
// is_in_device_frame / is_in_swapchain_frame.
enum class Device_frame_state : uint8_t
{
    idle,
    waited,
    recording,
    in_swapchain_frame
};

class Device;
class Device_impl final
{
public:
    Device_impl   (Device& device, const Surface_create_info& surface_create_info, const Graphics_config& graphics_config = {});
    Device_impl   (const Device_impl&) = delete;
    void operator=(const Device_impl&) = delete;
    Device_impl   (Device_impl&&)      = delete;
    void operator=(Device_impl&&)      = delete;
    ~Device_impl() noexcept;

    [[nodiscard]] auto wait_frame () -> bool;
    [[nodiscard]] auto begin_frame() -> bool;
    [[nodiscard]] auto end_frame  () -> bool;
    [[nodiscard]] auto begin_frame(const Frame_begin_info& frame_begin_info) -> bool;
    [[nodiscard]] auto end_frame  (const Frame_end_info& frame_end_info) -> bool;

    void               wait_idle            ();
    [[nodiscard]] auto is_in_swapchain_frame() const -> bool;

    // Transitional: see Vulkan Device_impl. Command_buffer_impl took
    // over the swapchain-frame entry points and still needs to flip
    // m_had_swapchain_frame / m_pending_drawable for the legacy
    // end_frame path.
    friend class Command_buffer_impl;

    void resize_swapchain_to_window();
    void start_frame_capture       ();
    void end_frame_capture         ();

    // Active render pass tracking
    static Render_pass_impl* s_active_render_pass;
    void memory_barrier            (Memory_barrier_mask barriers);
    void clear_texture             (const Texture& texture, std::array<double, 4> clear_value);
    void transition_texture_layout (const Texture& texture, Image_layout new_layout);
    void cmd_texture_barrier       (uint64_t usage_before, uint64_t usage_after);
    [[nodiscard]] auto get_command_buffer(unsigned int thread_slot) -> Command_buffer&;
    void submit_command_buffers    (std::span<Command_buffer* const> command_buffers);
    void upload_to_buffer          (const Buffer& buffer, size_t offset, const void* data, size_t length);
    void upload_to_texture         (const Texture& texture, int level, int x, int y, int width, int height, erhe::dataformat::Format pixelformat, const void* data, int row_stride);
    void add_completion_handler    (std::function<void(Device_impl&)> callback);
    void on_thread_enter           ();

    [[nodiscard]] auto get_surface                        () -> Surface*;
    [[nodiscard]] auto get_native_handles                 () const -> Native_device_handles;
    [[nodiscard]] auto get_handle                         (const Texture& texture, const Sampler& sampler) const -> uint64_t;
    [[nodiscard]] auto create_dummy_texture               (Command_buffer& init_command_buffer, erhe::dataformat::Format format) -> std::shared_ptr<Texture>;
    [[nodiscard]] auto get_buffer_alignment               (Buffer_target target) -> std::size_t;
    [[nodiscard]] auto get_frame_index                    () const -> uint64_t;
    [[nodiscard]] auto allocate_ring_buffer_entry         (Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range;
    [[nodiscard]] auto make_blit_command_encoder          (Command_buffer& command_buffer) -> Blit_command_encoder;
    [[nodiscard]] auto make_compute_command_encoder       (Command_buffer& command_buffer) -> Compute_command_encoder;
    [[nodiscard]] auto make_render_command_encoder        (Command_buffer& command_buffer) -> Render_command_encoder;
    [[nodiscard]] auto get_format_properties              (erhe::dataformat::Format format) const -> Format_properties;
    [[nodiscard]] auto probe_image_format_support         (erhe::dataformat::Format format, uint64_t usage_mask) const -> bool;
    [[nodiscard]] auto get_supported_depth_stencil_formats() const -> std::vector<erhe::dataformat::Format>;
                  void sort_depth_stencil_formats         (std::vector<erhe::dataformat::Format>& formats, unsigned int sort_flags, int requested_sample_count) const;
    [[nodiscard]] auto choose_depth_stencil_format        (const std::vector<erhe::dataformat::Format>& formats) const -> erhe::dataformat::Format;
    [[nodiscard]] auto choose_depth_stencil_format        (unsigned int sort_flags, int requested_sample_count) const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_shader_monitor                 () -> Shader_monitor&;
    [[nodiscard]] auto get_info                           () const -> const Device_info&;
    [[nodiscard]] auto get_graphics_config                () const -> const Graphics_config&;

    void reset_shader_stages_state_tracker();
    [[nodiscard]] auto get_draw_id_uniform_location() const -> GLint;

    [[nodiscard]] auto get_mtl_device              () const -> MTL::Device*;
    [[nodiscard]] auto get_mtl_command_queue       () const -> MTL::CommandQueue*;
    [[nodiscard]] auto get_default_sampler_state   () const -> MTL::SamplerState*;

    // Returns the command buffer opened by begin_frame() and committed
    // by end_frame(). nullptr if no device frame is active. All in-frame
    // recording (render pass, blit, compute, upload_to_*, clear_texture)
    // should route through this so the frame lands in a single commit.
    // Unlike Vulkan there is no render-pass-active veto: multiple Metal
    // encoders (render / blit / compute) can be created sequentially
    // against the same cb, they just have to end before the next one begins.
    [[nodiscard]] auto get_device_frame_command_buffer() const -> MTL::CommandBuffer*;

    // Per-device-frame MTL::Fence used to serialize encoders against each
    // other. Each encoder on the device cb should waitForFence(fence) at
    // start and updateFence(fence) at end. Metal's automatic hazard
    // tracking within a single cb is per-MTLTexture-identity and does
    // NOT track aliased newTextureView pairs, so a render pass writing
    // to a parent texture at level K does not synchronize automatically
    // with a later render pass that samples from a view of that same
    // mip. This fence restores the pass-to-pass ordering the old
    // per-pass-cb model got implicitly from queue submission order.
    [[nodiscard]] auto get_device_frame_fence() const -> MTL::Fence*;

    // Serializes recording against the active device-frame cb across
    // worker threads (init taskflow etc.). Matches the Vulkan backend's
    // m_recording_mutex.
    std::mutex m_recording_mutex;

    // Texture argument buffer support
    [[nodiscard]] auto get_texture_argument_encoder() const -> MTL::ArgumentEncoder*;
                  void set_texture_argument_encoder(MTL::ArgumentEncoder* encoder);

    // Argument buffer resource index mapping (GLSL binding -> arg buffer id)
    struct Texture_arg_buffer_layout
    {
        uint32_t shadow_compare_texture_id    {0xFFFFFFFFu};
        uint32_t shadow_compare_sampler_id    {0xFFFFFFFFu};
        uint32_t shadow_no_compare_texture_id {0xFFFFFFFFu};
        uint32_t shadow_no_compare_sampler_id {0xFFFFFFFFu};
        uint32_t material_texture_base_id     {0xFFFFFFFFu};
        uint32_t material_sampler_base_id     {0xFFFFFFFFu};
        uint32_t argument_buffer_index        {2}; // [[buffer(N)]]
        bool     valid{false};
    };
    [[nodiscard]] auto get_texture_arg_buffer_layout() const -> const Texture_arg_buffer_layout&;
                  void set_texture_arg_buffer_layout(const Texture_arg_buffer_layout& layout);

private:
    void frame_completed(uint64_t completed_frame);

    class Completion_handler
    {
    public:
        uint64_t                          frame_number;
        std::function<void(Device_impl&)> callback;
    };

    Device&                          m_device;
    std::unique_ptr<Surface>         m_surface;
    Shader_monitor                   m_shader_monitor;
    Device_info                      m_info;
    Graphics_config                  m_graphics_config;
    uint64_t                         m_frame_index{1};
    Device_frame_state               m_state{Device_frame_state::idle};
    bool                             m_had_swapchain_frame{false};
    std::vector<Completion_handler>  m_completion_handlers;
    // GPU-completion tracking driven by MTL::CommandBuffer
    // addCompletedHandler callbacks (which run on an arbitrary Metal
    // dispatch thread). Each frame that successfully commits its
    // device cb enqueues its frame index here; begin_frame drains the
    // list and calls frame_completed() on each, which recycles the
    // ring buffer ranges pinned to that frame. Replaces the previous
    // "2-frame delay" heuristic that assumed frame N-2 was GPU-done.
    std::mutex                       m_completion_mutex;
    std::vector<uint64_t>            m_pending_completed_frames;
    MTL::Device*             m_mtl_device          {nullptr};
    MTL::CommandQueue*       m_mtl_command_queue   {nullptr};
    MTL::SamplerState*       m_default_sampler_state{nullptr};
    MTL::LogState*           m_mtl_log_state        {nullptr};
    MTL::ArgumentEncoder*    m_texture_argument_encoder{nullptr};
    // Active device-frame command buffer. Opened in begin_frame(),
    // committed in end_frame(). nullptr outside of a device frame;
    // callers must route through get_device_frame_command_buffer()
    // which returns nullptr in that case and expects the caller to
    // fall back to a per-op cb.
    MTL::CommandBuffer*      m_active_command_buffer{nullptr};
    // Drawable latched at begin_swapchain_frame for end_frame to present.
    CA::MetalDrawable*       m_pending_drawable     {nullptr};
    // Device-frame fence. Created in begin_frame, released in end_frame.
    MTL::Fence*              m_frame_fence          {nullptr};
    Texture_arg_buffer_layout m_texture_arg_buffer_layout;
    std::vector<std::unique_ptr<Ring_buffer>> m_ring_buffers;
    static constexpr std::size_t m_min_buffer_size{4 * 1024 * 1024};
};

void install_metal_error_handler(Device& device);

} // namespace erhe::graphics
