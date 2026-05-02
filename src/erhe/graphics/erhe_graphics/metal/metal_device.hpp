#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/shader_monitor.hpp"

#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

typedef int GLint;

namespace MTL { class ArgumentEncoder; }
namespace MTL { class Device; }
namespace MTL { class CommandQueue; }
namespace MTL { class Event; }
namespace MTL { class LogState; }
namespace MTL { class SamplerState; }
namespace MTL { class SharedEvent; }

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
    [[nodiscard]] auto recreate_surface_for_new_window() -> bool;
    [[nodiscard]] auto is_in_swapchain_frame() const -> bool;

    // Called from MTL::CommandBuffer addCompletedHandler lambdas (which
    // run on an arbitrary Metal dispatch thread) to enqueue a frame
    // index for completion processing. begin_frame() drains the queue
    // under m_completion_mutex on the next frame and recycles ring
    // buffer ranges pinned to those frames.
    void notify_command_buffer_completed(uint64_t frame_index);

    void resize_swapchain_to_window();
    void start_frame_capture       ();
    void end_frame_capture         ();

    // Active render pass tracking
    static Render_pass_impl* s_active_render_pass;
    [[nodiscard]] auto get_command_buffer(unsigned int thread_slot) -> Command_buffer&;
    void submit_command_buffers    (std::span<Command_buffer* const> command_buffers);
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

    // Per-(thread_slot) bag of cbs allocated this frame. Each entry is
    // tagged with the frame index of allocation so frame_completed() can
    // drop entries whose frame's GPU work has retired. Mirrors Vulkan's
    // Per_thread_command_pool except there is no MTLCommandPool to reset
    // -- MTL::CommandBuffer allocation goes straight through the queue.
    class Per_thread_command_buffers
    {
    public:
        class Allocated_command_buffer
        {
        public:
            uint64_t                        frame_index{0};
            std::unique_ptr<Command_buffer> command_buffer;
        };
        std::vector<Allocated_command_buffer> allocated;
    };

    static constexpr unsigned int s_number_of_thread_slots = 8;

    Device&                          m_device;
    std::unique_ptr<Surface>         m_surface;
    Shader_monitor                   m_shader_monitor;
    Device_info                      m_info;
    Graphics_config                  m_graphics_config;
    uint64_t                         m_frame_index{1};
    Device_frame_state               m_state{Device_frame_state::idle};
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
    Texture_arg_buffer_layout m_texture_arg_buffer_layout;
    std::vector<std::unique_ptr<Ring_buffer>> m_ring_buffers;
    static constexpr std::size_t m_min_buffer_size{4 * 1024 * 1024};

    // Per-thread Command_buffer storage. get_command_buffer(slot)
    // allocates a Command_buffer here; frame_completed sweeps entries
    // whose frame_index has retired. Guarded by m_per_thread_mutex.
    std::array<Per_thread_command_buffers, s_number_of_thread_slots> m_per_thread_command_buffers;
    std::mutex m_per_thread_mutex;
};

void install_metal_error_handler(Device& device);

} // namespace erhe::graphics
