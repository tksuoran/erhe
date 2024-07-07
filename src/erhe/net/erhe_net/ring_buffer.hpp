#pragma once

#include <cstdint>
#include <vector>

namespace erhe::net {

class Ring_buffer
{
public:
    explicit Ring_buffer(std::size_t capacity);

    Ring_buffer   (const Ring_buffer&) = delete;
    void operator=(const Ring_buffer&) = delete;
    Ring_buffer   (Ring_buffer&& other) noexcept;
    auto operator=(Ring_buffer&& other) noexcept -> Ring_buffer&;

    void reset                   ();
    auto empty                   () const -> bool;
    auto full                    () const -> bool;
    auto max_size                () const -> std::size_t;
    auto size                    () const -> std::size_t;
    auto size_available_for_write() const -> std::size_t;
    auto size_available_for_read () const -> std::size_t;

    // For recv - like write()
    auto begin_produce           (
        std::size_t& writable_byte_count_before_wrap,
        std::size_t& writable_byte_count_after_wrap
    ) -> uint8_t*;
    void end_produce             (std::size_t byte_count);

    auto write                   (const uint8_t* src, std::size_t byte_count) -> std::size_t;

    // For send
    auto begin_consume           (
        std::size_t& readable_byte_count_before_wrap,
        std::size_t& readable_byte_count_after_wrap
    ) -> const uint8_t*;
    void end_consume             (std::size_t byte_count);

    auto read                    (uint8_t* dst, std::size_t byte_count) -> std::size_t;
    auto peek                    (uint8_t* dst, std::size_t byte_count) -> std::size_t;

    auto discard                 (std::size_t byte_count) -> std::size_t;

    void rotate                  ();
    void rotate                  (std::size_t amount);

private:
    std::vector<uint8_t> m_buffer;
    std::size_t          m_read_offset {0};
    std::size_t          m_write_offset{0};
    std::size_t          m_max_size    {0};
    bool                 m_full        {false};
};

} // namespace erhe::net
