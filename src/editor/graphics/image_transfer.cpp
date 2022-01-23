#include "graphics/image_transfer.hpp"
#include "graphics/gl_context_provider.hpp"

#include "erhe/gl/gl.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor
{

Image_transfer::Image_transfer()
    : erhe::components::Component{c_name}
{
}

Image_transfer::~Image_transfer() = default;

void Image_transfer::connect()
{
    require<Gl_context_provider>();
}

void Image_transfer::initialize_component()
{
    const Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    m_slots = std::make_unique<std::array<Slot, 4>>();
}

auto Image_transfer::get_slot() -> Slot&
{
    m_index = (m_index + 1) % m_slots->size();
    return m_slots->at(m_index);
}

Image_transfer::Slot::Slot()
{
    Expects(pbo.gl_name() != 0);

    capacity = 8 * 1024 * 1024;

    gl::named_buffer_storage(
        pbo.gl_name(),
        capacity,
        nullptr,
        gl::Buffer_storage_mask::map_write_bit |
        gl::Buffer_storage_mask::map_persistent_bit
    );

    auto* map_pointer = gl::map_named_buffer_range(
        pbo.gl_name(),
        0,
        capacity,
        gl::Map_buffer_access_mask::map_invalidate_buffer_bit |
        gl::Map_buffer_access_mask::map_flush_explicit_bit    |
        gl::Map_buffer_access_mask::map_persistent_bit        |
        gl::Map_buffer_access_mask::map_write_bit
    );

    ERHE_VERIFY(map_pointer != nullptr);

    span = gsl::span(
        reinterpret_cast<std::byte*>(map_pointer),
        capacity
    );
}

auto Image_transfer::Slot::span_for(
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
    Expects(byte_count <= capacity);
    return span.subspan(0, byte_count);
}

} // namespace erhe::graphics
