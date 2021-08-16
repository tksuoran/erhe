#pragma once

#include <mutex>

namespace erhe::graphics
{

class Ring_buffer
{
public:
    explicit Ring_buffer(const size_t capacity);

    void reset                   ();
    auto empty                   () const -> bool;
    auto full                    () const -> bool;
    auto max_size                () const -> size_t;
    auto size                    () const -> size_t;
    auto size_available_for_write() const -> size_t;
    auto size_available_for_read () const -> size_t;
    auto write                   (const uint8_t* src, const size_t byte_count) -> size_t;
    auto read                    (uint8_t* dst, const size_t byte_count) -> size_t;
    auto discard                 (const size_t byte_count) -> size_t;

private:
    std::mutex           m_mutex;
    std::vector<uint8_t> m_buffer;
    size_t               m_read_offset {0};
    size_t               m_write_offset{0};
    size_t               m_max_size    {0};
    bool                 m_full        {false};
};


} // namespace erhe::graphics
