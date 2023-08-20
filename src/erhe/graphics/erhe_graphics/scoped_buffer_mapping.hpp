#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/gl_objects.hpp"
#include "erhe_graphics/span.hpp"

#include <gsl/span>

#include <string_view>
#include <mutex>

namespace erhe::graphics
{


template <typename T>
class Scoped_buffer_mapping
{
public:
    Scoped_buffer_mapping(
        Buffer&                          buffer,
        const std::size_t                element_offset,
        const std::size_t                element_count,
        const gl::Map_buffer_access_mask access_mask
    )
        : m_buffer{buffer}
        , m_span  {buffer.map_elements<T>(element_offset, element_count, access_mask)}
    {
    }

    ~Scoped_buffer_mapping() noexcept
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

} // namespace erhe::graphics
