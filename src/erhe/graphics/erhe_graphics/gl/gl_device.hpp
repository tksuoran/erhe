#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/gl/gl_context_provider.hpp"
#include "erhe_graphics/gl/gl_state_tracker.hpp"
#include "erhe_graphics/shader_monitor.hpp"

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace erhe::graphics {

class Frame_sync
{
public:
    uint64_t        frame_number{0};
    void*           fence_sync  {nullptr};
    gl::Sync_status result      {gl::Sync_status::timeout_expired};
};

struct Ring_buffer_sync_entry
{
    uint64_t    waiting_for_frame{0};
    std::size_t wrap_count {0};
    size_t      byte_offset{0};
    size_t      byte_count {0};
};

class GPU_ring_buffer_impl final
{
public:
    GPU_ring_buffer_impl(Device& device, GPU_ring_buffer& ring_buffer, const GPU_ring_buffer_create_info& create_info);
    ~GPU_ring_buffer_impl();

    void sanity_check();
    void get_size_available_for_write(
        std::size_t  required_alignment,
        std::size_t& out_alignment_byte_count_without_wrap,
        std::size_t& out_available_byte_count_without_wrap,
        std::size_t& out_available_byte_count_with_wrap
    ) const;
    [[nodiscard]] auto acquire(std::size_t required_alignment, Ring_buffer_usage ring_buffer_usage, std::size_t byte_count) -> Buffer_range;
    [[nodiscard]] auto match  (Ring_buffer_usage ring_buffer_usage) const -> bool;

    // For Buffer_range
    void flush(std::size_t byte_offset, std::size_t byte_count);
    void close(std::size_t byte_offset, std::size_t byte_write_count);
    void make_sync_entry(std::size_t wrap_count, std::size_t byte_offset, std::size_t byte_count);

    [[nodiscard]] auto get_buffer() -> Buffer*;
    [[nodiscard]] auto get_name  () const -> const std::string&;

    // For Device
    void frame_completed(uint64_t frame);

private:
    void wrap_write();

    Device&                 m_device;
    GPU_ring_buffer&        m_ring_buffer;
    Ring_buffer_usage       m_ring_buffer_usage;

    std::unique_ptr<Buffer> m_buffer;

    std::size_t             m_map_offset           {0};
    std::size_t             m_write_position       {0};
    std::size_t             m_write_wrap_count     {1};
    std::size_t             m_last_write_wrap_count{1}; // for handling write wraps wraps
    std::size_t             m_read_wrap_count      {0};
    std::size_t             m_read_offset          {0}; // This is the first offset where we cannot write

    std::string             m_name;

    std::vector<Ring_buffer_sync_entry> m_sync_entries;
};

class Device;
class Device_impl final
{
public:
    Device_impl   (Device& device, erhe::window::Context_window& context_window);
    Device_impl   (const Device_impl&) = delete;
    void operator=(const Device_impl&) = delete;
    Device_impl   (Device_impl&&)      = delete;
    void operator=(Device_impl&&)      = delete;
    ~Device_impl  ();

    void start_of_frame        ();
    void end_of_frame          ();
    void memory_barrier        (Memory_barrier_mask barriers);
    void clear_texture         (Texture& texture, std::array<double, 4> clear_value);
    void upload_to_buffer      (Buffer& buffer, size_t offset, const void* data, size_t length);
    void add_completion_handler(std::function<void()> callback);
    void on_thread_enter       ();

    [[nodiscard]] auto get_handle                  (const Texture& texture, const Sampler& sampler) const -> uint64_t;
    [[nodiscard]] auto create_dummy_texture        () -> std::shared_ptr<Texture>;
    [[nodiscard]] auto get_buffer_alignment        (Buffer_target target) -> std::size_t;
    [[nodiscard]] auto get_frame_number            () const -> uint64_t;
    [[nodiscard]] auto allocate_ring_buffer_entry  (Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range;
    [[nodiscard]] auto make_blit_command_encoder   () -> Blit_command_encoder;
    [[nodiscard]] auto make_compute_command_encoder() -> Compute_command_encoder;
    [[nodiscard]] auto make_render_command_encoder (Render_pass& render_pass) -> Render_command_encoder;
    [[nodiscard]] auto get_format_properties       (erhe::dataformat::Format format) const -> Format_properties;
    [[nodiscard]] auto choose_depth_stencil_format (unsigned int flags, int sample_count) const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_shader_monitor          () -> Shader_monitor&;
    [[nodiscard]] auto get_info                    () const -> const Device_info&;

private:
    void frame_completed(uint64_t frame);

    using PFN_generic          = void (*) ();
    using PFN_get_proc_address = PFN_generic (*) (const char*);

    erhe::window::Context_window& m_context_window;

    friend class Blit_command_encoder;
    friend class Compute_command_encoder;
    friend class Render_command_encoder;
    friend class Render_pass_impl;

    Device&                       m_device;
    Shader_monitor                m_shader_monitor;
    OpenGL_state_tracker          m_gl_state_tracker;
    Gl_context_provider           m_gl_context_provider;
    Device_info                   m_info;

    std::unordered_map<gl::Internal_format, Format_properties> format_properties;

    std::vector<std::unique_ptr<GPU_ring_buffer>> m_ring_buffers;
    std::size_t                                   m_min_buffer_size = 2 * 1024 * 1024; // TODO

    std::array<Frame_sync, 16> m_frame_syncs;
    uint64_t                   m_frame_number{1};

    std::vector<uint64_t>      m_pending_frames;
    std::vector<uint64_t>      m_completed_frames;
    bool                       m_need_sync{false};

    class Completion_handler
    {
    public:
        uint64_t              frame_number;
        std::function<void()> callback;
    };
    std::vector<Completion_handler> m_completion_handlers;
};

// Unified API for bindless textures and texture unit cache emulating bindless textures
// using sampler arrays. Also candidate for future metal argument buffer / vulkan
// descriptor indexing based implementations
class Texture_heap_impl final
{
public:
    Texture_heap_impl(
        Device&        device,
        const Texture& fallback_texture,
        const Sampler& fallback_sampler,
        std::size_t    reserved_slot_count
    );
    ~Texture_heap_impl();

    auto assign           (std::size_t slot, const Texture* texture, const Sampler* sample) -> uint64_t;
    void reset            ();
    auto allocate         (const Texture* texture, const Sampler* sample) -> uint64_t;
    auto get_shader_handle(const Texture* texture, const Sampler* sample) -> uint64_t; // bindless ? handle : slot
    auto bind             () -> std::size_t;
    void unbind           ();

protected:
    Device&                     m_device;
    const Texture&              m_fallback_texture;
    const Sampler&              m_fallback_sampler;
    std::size_t                 m_reserved_slot_count;
    std::vector<bool>           m_assigned;
    std::vector<uint64_t>       m_gl_bindless_texture_handles;
    std::vector<bool>           m_gl_bindless_texture_resident;
    std::vector<bool>           m_reserved;
    std::vector<const Texture*> m_textures;
    std::vector<const Sampler*> m_samplers;
    std::vector<GLuint>         m_gl_textures;
    std::vector<GLuint>         m_gl_samplers;
    std::vector<GLuint>         m_zero_vector;
    std::size_t                 m_used_slot_count{0};
};

} // namespace erhe::graphics
