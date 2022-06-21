#pragma once

#include <mutex>

namespace erhe::graphics
{

class Ring_buffer
{
public:
    explicit Ring_buffer(const std::size_t capacity);

    void reset                   ();
    auto empty                   () const -> bool;
    auto full                    () const -> bool;
    auto max_size                () const -> std::size_t;
    auto size                    () const -> std::size_t;
    auto size_available_for_write() const -> std::size_t;
    auto size_available_for_read () const -> std::size_t;
    auto write                   (const uint8_t* src, const std::size_t byte_count) -> std::size_t;
    auto read                    (uint8_t* dst, const std::size_t byte_count) -> std::size_t;
    auto discard                 (const std::size_t byte_count) -> std::size_t;

private:
    std::mutex           m_mutex;
    std::vector<uint8_t> m_buffer;
    std::size_t          m_read_offset {0};
    std::size_t          m_write_offset{0};
    std::size_t          m_max_size    {0};
    bool                 m_full        {false};
};


} // namespace erhe::graphics
