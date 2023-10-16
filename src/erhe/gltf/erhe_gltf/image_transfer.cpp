#include "image_transfer.hpp"

#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::gltf
{

Image_transfer::Image_transfer(
    erhe::graphics::Instance& graphics_instance
)
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
    Expects(m_pbo.gl_name() != 0);

    m_capacity = 64 * 1024 * 1024;
    m_storage_mask = gl::Buffer_storage_mask::map_write_bit;
    m_access_mask = 
        gl::Map_buffer_access_mask::map_invalidate_buffer_bit |
        gl::Map_buffer_access_mask::map_flush_explicit_bit    |
        gl::Map_buffer_access_mask::map_write_bit;

    if (graphics_instance.info.use_persistent_buffers) {
        m_storage_mask = m_storage_mask | gl::Buffer_storage_mask::map_persistent_bit;
        m_access_mask  = m_access_mask  | gl::Map_buffer_access_mask::map_persistent_bit;
    }

    gl::named_buffer_storage(
        m_pbo.gl_name(),
        m_capacity,
        nullptr,
        gl::Buffer_storage_mask::map_write_bit |
        gl::Buffer_storage_mask::map_persistent_bit
    );

    if (graphics_instance.info.use_persistent_buffers) {
        map();
    }
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

    m_span = gsl::span(
        static_cast<std::byte*>(map_pointer),
        m_capacity
    );
}

void Image_transfer::Slot::unmap()
{
    gl::unmap_named_buffer(m_pbo.gl_name());
    m_span = gsl::span<std::byte>{};
}

void Image_transfer::Slot::end()
{
    if (!m_graphics_instance.info.use_persistent_buffers) {
        unmap();
    }
}

auto Image_transfer::Slot::begin_span_for(
    const int                 span_width,
    const int                 span_height,
    const gl::Internal_format internal_format
) -> gsl::span<std::byte>
{
    Expects(span_width >= 1);
    Expects(span_height >= 1);

    auto row_stride = span_width * erhe::graphics::get_upload_pixel_byte_count(internal_format);
    auto byte_count = row_stride * span_height;
    Expects(byte_count >= 1);
    Expects(byte_count <= m_capacity);
    if (!m_graphics_instance.info.use_persistent_buffers) {
        map();
    }

    return m_span.subspan(0, byte_count);
}

} // namespace erhe::gltf
