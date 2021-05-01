#include "erhe/graphics/multi_buffer.hpp"
#include "erhe/graphics/buffer.hpp"

namespace erhe::graphics
{

Multi_buffer::Multi_buffer(gl::Buffer_target target,
                           size_t            element_count,
                           size_t            element_size,
                           bool              cpu_copy)
{
    for (auto& storage : m_storage)
    {
        storage.buffer = std::make_shared<erhe::graphics::Buffer>(target,
                                                                  element_count,
                                                                  element_size,
                                                                  gl::Buffer_storage_mask::map_coherent_bit   |
                                                                  gl::Buffer_storage_mask::map_persistent_bit |
                                                                  gl::Buffer_storage_mask::map_write_bit);
        // TODO Coherent mapping vs. explicit flushing?
        storage.map_data = storage.buffer->map_bytes(0,
                                                     element_count * element_size,
                                                     gl::Map_buffer_access_mask::map_coherent_bit   |
                                                     gl::Map_buffer_access_mask::map_persistent_bit |
                                                     gl::Map_buffer_access_mask::map_write_bit);
    }

    if (cpu_copy)
    {
        m_cpu_copy.resize(element_count * element_size);
    }
}

auto Multi_buffer::current()
-> std::shared_ptr<erhe::graphics::Buffer>
{
    Expects(m_current_entry < m_storage.size());

    return m_storage[m_current_entry].buffer;
}

auto Multi_buffer::current_map() const
-> gsl::span<std::byte>
{
    Expects(m_current_entry < m_storage.size());

    return m_storage[m_current_entry].map_data;
}

void Multi_buffer::advance()
{
    m_current_entry = (m_current_entry + 1) % m_storage.size();

    Ensures(m_current_entry < m_storage.size());
}

void Multi_buffer::touch(size_t update_serial)
{
    m_storage[m_current_entry].last_update_serial = update_serial;
}

auto Multi_buffer::get_last_update_serial() const
-> size_t
{
    return m_storage[m_current_entry].last_update_serial;
}

} // namespace erhe::graphics
