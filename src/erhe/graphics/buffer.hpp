#ifndef buffer_hpp_erhe_graphics
#define buffer_hpp_erhe_graphics

#include "erhe/graphics/gl_objects.hpp"
#include "erhe/graphics/span.hpp"

#include <gsl/span>

namespace erhe::graphics
{

class Buffer
{
public:
    Buffer() = default;

    Buffer(gl::Buffer_target       target,
           size_t                  capacity_bytes_count,
           gl::Buffer_storage_mask storage_mask) noexcept;

    Buffer(gl::Buffer_target          target,
           size_t                     capacity_byte_count,
           gl::Buffer_storage_mask    storage_mask,
           gl::Map_buffer_access_mask map_buffer_access_mask) noexcept;

    Buffer(const Buffer& other) = delete;

    auto operator=(const Buffer&)
    -> Buffer& = delete;

    Buffer(Buffer&& other) noexcept
    {
        m_handle                 = std::move(other.m_handle);
        m_debug_label            = std::move(other.m_debug_label);
        m_target                 = other.m_target;
        m_capacity_byte_count    = other.m_capacity_byte_count;
        m_next_free_byte         = other.m_next_free_byte;
        m_storage_mask           = other.m_storage_mask;
        m_map                    = other.m_map;
        m_map_byte_offset        = other.m_map_byte_offset;
        m_map_buffer_access_mask = other.m_map_buffer_access_mask;
    }

    auto operator=(Buffer&& other) noexcept
    -> Buffer&
    {
        m_handle                 = std::move(other.m_handle);
        m_debug_label            = std::move(other.m_debug_label);
        m_target                 = other.m_target;
        m_capacity_byte_count    = other.m_capacity_byte_count;
        m_next_free_byte         = other.m_next_free_byte;
        m_storage_mask           = other.m_storage_mask;
        m_map                    = other.m_map;
        m_map_byte_offset        = other.m_map_byte_offset;
        m_map_buffer_access_mask = other.m_map_buffer_access_mask;
        return *this;
    }

    ~Buffer() = default;

    auto map()
    -> gsl::span<std::byte>
    {
        return m_map;
    }

    auto debug_label() const noexcept
    -> const std::string&;

    auto capacity_byte_count() const noexcept
    -> size_t;

    auto allocate_bytes(size_t byte_count, size_t alignment = 64) noexcept
    -> size_t;

    template <typename T>
    auto map_elements(size_t                     element_offset,
                      size_t                     element_count,
                      gl::Map_buffer_access_mask access_mask) noexcept
    -> gsl::span<T>
    {
        size_t byte_offset = element_offset * sizeof(T);
        size_t byte_count  = element_count * sizeof(T);
        auto raw_map = map_bytes(byte_offset, byte_count, access_mask);
        return gsl::span(reinterpret_cast<T*>(raw_map.data()),
                                              raw_map.size_bytes() / sizeof(T));
    }

    auto map_all_bytes(gl::Map_buffer_access_mask access_mask) noexcept
    -> gsl::span<std::byte>;

    auto map_bytes(size_t                     byte_offset,
                   size_t                     byte_count,
                   gl::Map_buffer_access_mask access_mask) noexcept
    -> gsl::span<std::byte>;

    void unmap() noexcept;

    void flush_bytes(size_t byte_offset, size_t byte_count) noexcept;

    void flush_and_unmap_bytes(size_t byte_count) noexcept;

    auto free_capacity_bytes() const noexcept
    -> size_t;

    auto target() const noexcept
    -> gl::Buffer_target
    {
        return m_target;
    }

    void set_debug_label(std::string label) noexcept
    {
        m_debug_label = std::move(label);
        gl::object_label(gl::Object_identifier::buffer,
                         gl_name(), static_cast<GLsizei>(m_debug_label.length()), m_debug_label.c_str());
    }

    void dump() const noexcept;

    auto gl_name() const noexcept
    -> unsigned int;

    friend class Vertex_input_state;
    friend class Texture;

private:
    Gl_buffer               m_handle;
    std::string             m_debug_label;
    gl::Buffer_target       m_target             {gl::Buffer_target::array_buffer};
    size_t                  m_capacity_byte_count{0};
    size_t                  m_next_free_byte     {0};
    gl::Buffer_storage_mask m_storage_mask       {0};

    // Last MapBuffer
    gsl::span<std::byte>        m_map;
    size_t                      m_map_byte_offset       {0};
    gl::Map_buffer_access_mask  m_map_buffer_access_mask{0};
};

template <typename T>
class Scoped_buffer_mapping
{
public:
    Scoped_buffer_mapping(Buffer&                    buffer,
                          size_t                     element_offset,
                          size_t                     element_count,
                          gl::Map_buffer_access_mask access_mask)
        : m_buffer{buffer}
        , m_span  {buffer.map_elements<T>(element_offset, element_count, access_mask)}
    {
    }

    ~Scoped_buffer_mapping()
    {
        m_buffer.unmap();
    }

    Scoped_buffer_mapping(const Scoped_buffer_mapping&) = delete;

    auto operator=(const Scoped_buffer_mapping&)
    -> Scoped_buffer_mapping& = delete;

    auto span() const
    -> const gsl::span<T>&
    {
        return m_span;
    }

private:
    Buffer&      m_buffer;
    gsl::span<T> m_span;
};

struct Buffer_hash
{
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

#endif
