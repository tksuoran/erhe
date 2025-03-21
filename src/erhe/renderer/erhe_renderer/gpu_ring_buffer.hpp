#pragma once

#include "erhe_renderer/buffer_writer.hpp"
#include "erhe_graphics/buffer.hpp"

#include <chrono>
#include <deque>
#include <limits>
#include <optional>

namespace erhe::renderer {

class GPU_ring_buffer;

struct Sync_entry
{
    std::size_t     wrap_count {0};
    size_t          byte_offset{0};
    size_t          byte_count {0};
    void*           fence_sync {nullptr};
    gl::Sync_status result     {gl::Sync_status::timeout_expired};
};

enum class Ring_buffer_usage : unsigned int
{
    None       = 0,
    CPU_write  = 1,
    GPU_access = 2
};

class Buffer_range
{
public:
    Buffer_range();
    Buffer_range(
        GPU_ring_buffer&     ring_buffer,
        Ring_buffer_usage    usage,
        std::span<std::byte> span,
        std::size_t          wrap_count,
        size_t               start_byte_offset
    );
    Buffer_range(Buffer_range&& old);
    Buffer_range& operator=(Buffer_range&) = delete;
    Buffer_range& operator=(Buffer_range&& old);
    ~Buffer_range();

    void flush                          (std::size_t byte_write_position_in_span);
    void close                          (std::size_t byte_write_position_in_span);
    auto bind                           () -> bool;
    void submit                         ();
    void cancel                         ();
    auto get_span                       () const -> std::span<std::byte>;
    auto get_byte_start_offset_in_buffer() const -> std::size_t;
    auto get_writable_byte_count        () const -> std::size_t;
    auto get_written_byte_count         () const -> std::size_t;
    auto is_closed                      () const -> bool;

private:
    GPU_ring_buffer*     m_ring_buffer{nullptr};
    std::span<std::byte> m_span;
    std::size_t          m_wrap_count{0};
    size_t               m_byte_span_start_offset_in_buffer{0};
    size_t               m_byte_write_position_in_span{0};
    size_t               m_byte_flush_position_in_span{0};
    Ring_buffer_usage    m_usage{Ring_buffer_usage::None};
    bool                 m_is_closed{false};
    bool                 m_is_submitted{false};
    bool                 m_is_cancelled{false};
};

class GPU_ring_buffer_create_info
{
public:
    gl::Buffer_target target       {0};
    unsigned int      binding_point{std::numeric_limits<unsigned int>::max()};
    std::size_t       size         {0};
    const char*       debug_label  {nullptr};
};

class GPU_ring_buffer
{
public:
    GPU_ring_buffer(erhe::graphics::Instance& graphics_instance, const GPU_ring_buffer_create_info& create_info);
    ~GPU_ring_buffer();

    void sanity_check();
    void get_size_available_for_write(
        std::size_t& out_alignment_byte_count_without_wrap,
        std::size_t& out_available_byte_count_without_wrap,
        std::size_t& out_available_byte_count_with_wrap
    ) const;
    auto open                        (Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range;
    auto bind                        (const Buffer_range& range) -> bool;

    // For Buffer_range
    void flush                       (std::size_t byte_offset, std::size_t byte_count);
    void close                       (std::size_t byte_offset, std::size_t byte_write_count);
    void push(Sync_entry&& entry);

    [[nodiscard]] auto get_buffer() -> erhe::graphics::Buffer&;
    [[nodiscard]] auto get_name  () const -> const std::string&;

private:
    void wrap_write();
    void update_entries();

    erhe::graphics::Instance& m_instance;
    unsigned int              m_binding_point{0};
    erhe::graphics::Buffer    m_buffer;

    std::size_t               m_map_offset           {0};
    std::size_t               m_write_position       {0};
    std::size_t               m_write_wrap_count     {1};
    std::size_t               m_last_write_wrap_count{1}; // for handling write wraps wraps
    std::size_t               m_read_wrap_count      {0};
    std::size_t               m_read_offset          {0}; // This is the first offset where we cannot write
    std::span<std::byte>      m_map;

    std::string               m_name;

    std::deque<Sync_entry>    m_sync_entries;

    std::optional<std::chrono::steady_clock::time_point> m_last_warning_time;
};

} // namespace erhe::renderer
