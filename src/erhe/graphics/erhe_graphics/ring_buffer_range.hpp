#pragma once

#include "erhe_graphics/enums.hpp"

namespace erhe::graphics {

class Ring_buffer;

class Ring_buffer_range final
{
public:
    Ring_buffer_range();
    Ring_buffer_range(
        Ring_buffer&         ring_buffer,
        Ring_buffer_usage    usage,
        std::span<std::byte> span,
        std::size_t          wrap_count,
        size_t               start_byte_offset_in_buffer
    );
    Ring_buffer_range(Ring_buffer_range&& old) noexcept;
    Ring_buffer_range& operator=(Ring_buffer_range&) = delete;
    Ring_buffer_range& operator=(Ring_buffer_range&& old) noexcept;
    ~Ring_buffer_range();

    void bytes_gpu_used                 (std::size_t byte_count);
    void bytes_written                  (std::size_t byte_count);
    void flush                          (std::size_t byte_write_position_in_span);
    void close                          ();
    void release                        ();
    void cancel                         ();
    auto get_span                       () const -> std::span<std::byte>;
    auto get_byte_start_offset_in_buffer() const -> std::size_t;
    auto get_writable_byte_count        () const -> std::size_t;
    auto get_written_byte_count         () const -> std::size_t;
    auto is_closed                      () const -> bool;

    [[nodiscard]] auto get_buffer() const -> Ring_buffer*;

private:
    Ring_buffer*           m_ring_buffer{nullptr};
    std::vector<std::byte> m_cpu_buffer;
    std::span<std::byte>   m_span;
    std::size_t            m_wrap_count{0};
    size_t                 m_byte_span_start_offset_in_buffer{0};
    size_t                 m_byte_write_position_in_span{0};
    size_t                 m_byte_flush_position_in_span{0};
    Ring_buffer_usage      m_usage{Ring_buffer_usage::None};
    bool                   m_is_closed{false};
    bool                   m_is_released{false};
    bool                   m_is_cancelled{false};
};

} // namespace erhe::graphics
