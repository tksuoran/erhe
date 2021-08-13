#pragma once

#include "erhe/graphics/gl_objects.hpp"
#include "erhe/graphics/span.hpp"

#include <gsl/span>

#include <mutex>

namespace erhe::graphics
{

class Buffer final
{
public:
    Buffer () = default;
    ~Buffer();

    Buffer(const gl::Buffer_target       target,
           const size_t                  capacity_bytes_count,
           const gl::Buffer_storage_mask storage_mask) noexcept;

    Buffer(const gl::Buffer_target          target,
           const size_t                     capacity_byte_count,
           const gl::Buffer_storage_mask    storage_mask,
           const gl::Map_buffer_access_mask map_buffer_access_mask) noexcept;

    Buffer        (const Buffer&) = delete;
    void operator=(const Buffer&) = delete;
    Buffer        (Buffer&& other) noexcept;
    auto operator=(Buffer&& other) noexcept -> Buffer&;

    auto map                  ()                -> gsl::span<std::byte>;
    auto debug_label          () const noexcept -> const std::string&;
    auto capacity_byte_count  () const noexcept -> size_t;
    auto allocate_bytes       (const size_t byte_count, const size_t alignment = 64) noexcept -> size_t;
    void unmap                () noexcept;
    void flush_bytes          (const size_t byte_offset, const size_t byte_count) noexcept;
    void flush_and_unmap_bytes(const size_t byte_count) noexcept;
    auto free_capacity_bytes  () const noexcept -> size_t;
    auto target               () const noexcept -> gl::Buffer_target;
    void set_debug_label      (std::string_view label) noexcept;
    void dump                 () const noexcept;
    auto gl_name              () const noexcept -> unsigned int;

    template <typename T>
    auto map_elements(const size_t                     element_offset,
                      const size_t                     element_count,
                      const gl::Map_buffer_access_mask access_mask) noexcept
    -> gsl::span<T>
    {
        const size_t byte_offset = element_offset * sizeof(T);
        const size_t byte_count  = element_count * sizeof(T);
        auto raw_map = map_bytes(byte_offset, byte_count, access_mask);
        return gsl::span(reinterpret_cast<T*>(raw_map.data()),
                                              raw_map.size_bytes() / sizeof(T));
    }

    auto map_all_bytes(const gl::Map_buffer_access_mask access_mask) noexcept
    -> gsl::span<std::byte>;

    auto map_bytes(const size_t                     byte_offset,
                   const size_t                     byte_count,
                   const gl::Map_buffer_access_mask access_mask) noexcept
    -> gsl::span<std::byte>;

    friend class Vertex_input_state;
    friend class Texture;

private:
    Gl_buffer               m_handle;
    std::string             m_debug_label;
    gl::Buffer_target       m_target             {gl::Buffer_target::array_buffer};
    size_t                  m_capacity_byte_count{0};
    size_t                  m_next_free_byte     {0};
    gl::Buffer_storage_mask m_storage_mask       {0};
    std::mutex              m_allocate_mutex;

    // Last MapBuffer
    gsl::span<std::byte>        m_map;
    size_t                      m_map_byte_offset       {0};
    gl::Map_buffer_access_mask  m_map_buffer_access_mask{0};
};

class Ring_buffer
{
    Ring_buffer(const size_t capacity)
    {
        m_buffer.resize(capacity);
        m_max_size = capacity;
        reset();
    }

    void reset()
    {
        m_write_offset = 0;
        m_read_offset = 0;
        m_full = false;
    }

    bool empty() const
    {
        return !m_full && (m_read_offset == m_write_offset);
    }

    bool full() const
    {
        return m_full;
    }

    size_t max_size() const
    {
        return m_max_size;
    }

    auto size() const -> size_t
    {
        if (full())
        {
            return m_max_size;
        }
        if (m_write_offset >= m_read_offset)
        {
            return m_write_offset - m_read_offset;
        }
        return m_max_size + m_write_offset - m_read_offset;
    }

    auto size_available_for_write() const -> size_t
    {
        return m_max_size - size();
    }

    auto size_available_for_read() const -> size_t
    {
        return size();
    }

    auto write(const uint8_t* src, const size_t byte_count) -> size_t
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        size_t can_write_count = std::min(size_available_for_write(), byte_count);
        if (can_write_count == 0)
        {
            return 0;
        }
        size_t max_count_before_wrap = m_max_size - m_write_offset;
        size_t count_before_wrap = std::min(can_write_count, max_count_before_wrap);
        size_t count_after_wrap = (count_before_wrap < can_write_count) ? (can_write_count - count_before_wrap) : 0;
        memcpy(&m_buffer[m_write_offset], src, count_before_wrap);
        if (count_after_wrap > 0)
        {
            memcpy(&m_buffer[0], src + count_before_wrap, count_after_wrap);
        }

        m_write_offset = (m_write_offset + can_write_count) % m_max_size;
        m_full = (m_write_offset == m_read_offset);
        return can_write_count;
    }

    auto read(uint8_t* dst, const size_t byte_count) -> size_t
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        size_t can_read_count = std::min(size_available_for_read(), byte_count);
        if (can_read_count == 0)
        {
            return 0;
        }
        size_t max_count_before_wrap = m_max_size - m_read_offset;
        size_t count_before_wrap = std::min(can_read_count, max_count_before_wrap);
        size_t count_after_wrap = (count_before_wrap < can_read_count) ? (can_read_count - count_before_wrap) : 0;
        memcpy(dst, &m_buffer[m_read_offset], count_before_wrap);
        if (count_after_wrap > 0)
        {
            memcpy(dst + count_before_wrap, &m_buffer[0], count_after_wrap);
        }

        m_read_offset = (m_read_offset + can_read_count) % m_max_size;
        m_full = false;
        return can_read_count;
    }

    auto discard(const size_t byte_count) -> size_t
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        size_t can_discard_count = std::min(size_available_for_read(), byte_count);
        if (can_discard_count == 0)
        {
            return 0;
        }
        m_read_offset = (m_read_offset + can_discard_count) % m_max_size;
        m_full = false;
        return can_discard_count;
    }

private:
    std::mutex           m_mutex;
    std::vector<uint8_t> m_buffer;
    size_t               m_read_offset{0};
    size_t               m_write_offset{0};
    size_t               m_max_size{0};
    bool                 m_full{false};
};

class Buffer_transfer_queue final
{
public:
    Buffer_transfer_queue ();
    ~Buffer_transfer_queue();
    Buffer_transfer_queue (Buffer_transfer_queue&) = delete;
    auto operator=        (Buffer_transfer_queue&) -> Buffer_transfer_queue& = delete;

    class Transfer_entry
    {
    public:
        Transfer_entry(Buffer*                target,
                       const size_t           target_offset,
                       std::vector<uint8_t>&& data)
            : target       {target}
            , target_offset{target_offset}
            , data         {data}
        {
        }

        Transfer_entry(Transfer_entry&) = delete;
        void operator=(Transfer_entry&) = delete;

        Transfer_entry(Transfer_entry&& other) noexcept
            : target       {other.target}
            , target_offset{other.target_offset}
            , data         {std::move(other.data)}
        {
        }

        Transfer_entry& operator=(Transfer_entry&& other) noexcept
        {
            target        = other.target;
            target_offset = other.target_offset;
            data          = std::move(other.data);
            return *this;
        }

        Buffer*              target{nullptr};
        size_t               target_offset{0};
        std::vector<uint8_t> data;
    };

    void flush();
    void enqueue(Buffer* buffer, const size_t offset, std::vector<uint8_t>&& data);

private:
    std::mutex                  m_mutex;
    std::vector<Transfer_entry> m_queued;
};

template <typename T>
class Scoped_buffer_mapping
{
public:
    Scoped_buffer_mapping(Buffer&                          buffer,
                          const size_t                     element_offset,
                          const size_t                     element_count,
                          const gl::Map_buffer_access_mask access_mask)
        : m_buffer{buffer}
        , m_span  {buffer.map_elements<T>(element_offset, element_count, access_mask)}
    {
    }

    ~Scoped_buffer_mapping()
    {
        m_buffer.unmap();
    }

    Scoped_buffer_mapping(const Scoped_buffer_mapping&) = delete;
    auto operator=       (const Scoped_buffer_mapping&) -> Scoped_buffer_mapping& = delete;

    auto span() const -> const gsl::span<T>&
    {
        return m_span;
    }

private:
    Buffer&      m_buffer;
    gsl::span<T> m_span;
};

class Buffer_hash
{
public:
    auto operator()(const Buffer& buffer) const noexcept
    -> size_t
    {
        return static_cast<size_t>(buffer.gl_name());
    }
};

auto operator==(const Buffer& lhs, const Buffer& rhs) noexcept
-> bool;

auto operator!=(const Buffer& lhs, const Buffer& rhs) noexcept
-> bool;

} // namespace erhe::graphics
