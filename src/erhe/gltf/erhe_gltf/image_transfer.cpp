#include "image_transfer.hpp"

#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::gltf {

Image_transfer::Image_transfer(erhe::graphics::Instance& graphics_instance)
    : m_slots{
        Slot{graphics_instance},
        Slot{graphics_instance},
        Slot{graphics_instance},
        Slot{graphics_instance}
    }
{
}

auto Image_transfer::get_slot() -> Slot&
{
    m_index = (m_index + 1) % m_slots.size();
    return m_slots.at(m_index);
}

Image_transfer::Slot::Slot(erhe::graphics::Instance& graphics_instance)
    : m_graphics_instance{graphics_instance}
{
    ERHE_VERIFY(m_pbo.gl_name() != 0);

    m_capacity = 64 * 1024 * 1024;
    m_storage_mask = gl::Buffer_storage_mask::map_write_bit;
    m_access_mask = 
        gl::Map_buffer_access_mask::map_invalidate_buffer_bit |
        gl::Map_buffer_access_mask::map_flush_explicit_bit    |
        gl::Map_buffer_access_mask::map_write_bit;

    gl::named_buffer_storage(
        m_pbo.gl_name(),
        m_capacity,
        nullptr,
        gl::Buffer_storage_mask::map_write_bit
    );
}

void Image_transfer::Slot::map()
{
    auto* map_pointer = gl::map_named_buffer_range(
        m_pbo.gl_name(),
        0,
        m_capacity,
        m_access_mask
    );
    ERHE_VERIFY(map_pointer != nullptr);

    m_span = std::span(
        static_cast<std::byte*>(map_pointer),
        m_capacity
    );
}

void Image_transfer::Slot::unmap()
{
    gl::unmap_named_buffer(m_pbo.gl_name());
    m_span = std::span<std::byte>{};
}

void Image_transfer::Slot::end(bool flush)
{
    if (flush) {
        gl::flush_mapped_named_buffer_range(gl_name(), 0, m_span.size_bytes());
    }
    unmap();
}

auto Image_transfer::Slot::begin_span_for(const int span_width, const int span_height, const gl::Internal_format internal_format) -> std::span<std::byte>
{
    ERHE_VERIFY(span_width >= 1);
    ERHE_VERIFY(span_height >= 1);

    auto row_stride = span_width * erhe::graphics::get_upload_pixel_byte_count(internal_format);
    auto byte_count = row_stride * span_height;
    ERHE_VERIFY(byte_count >= 1);
    ERHE_VERIFY(byte_count <= m_capacity);
    map();

    return m_span.subspan(0, byte_count);
}

} // namespace erhe::gltf
