#include "erhe/graphics_experimental/storage_buffer_range.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/shader_resource.hpp"

namespace erhe::graphics
{

Storage_buffer_range::Storage_buffer_range(gsl::not_null<Shader_resource*> block,
                                           gsl::not_null<Buffer*>          buffer,
                                           size_t                          block_count)
    : m_buffer{buffer}
{
    Expects(block->size_bytes() > 0);
    Expects(block_count > 0);

    m_capacity_byte_count = block->size_bytes() * block_count;
    m_byte_offset = m_buffer->allocate_bytes(m_capacity_byte_count);

    Ensures(m_capacity_byte_count > 0);
}

auto Storage_buffer_range::begin_edit()
-> gsl::span<std::byte>
{
    Expects(m_buffer != nullptr);
    Expects(!m_in_edit);
    Expects(m_edit.empty());

    m_edit = m_buffer->map_bytes(m_byte_offset,
                                 m_capacity_byte_count,
                                 gl::Map_buffer_access_mask::map_write_bit);
    m_in_edit = true;
    Ensures(m_in_edit);
    return m_edit;
}

void Storage_buffer_range::end_edit()
{
    Expects(m_buffer != nullptr);
    Expects(m_in_edit);

    VERIFY(m_in_edit);

    m_buffer->unmap();
    m_edit = gsl::span<std::byte>();
    m_in_edit = false;

    Ensures(!m_in_edit);
    Ensures(m_edit.empty());
}

void Storage_buffer_range::flush()
{
    Expects(m_buffer != nullptr);

    m_buffer->flush_bytes(byte_offset(), byte_count());
}

void Storage_buffer_range::flush(size_t byte_count)
{
    Expects(m_buffer != nullptr);

    m_buffer->flush_bytes(byte_offset(), byte_count);
}

} // namespace erhe::graphics
