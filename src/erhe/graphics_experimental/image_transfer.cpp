#include "erhe/graphics_experimental/image_transfer.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::graphics
{

Image_transfer::Slot::Slot()
{
    Expects(pbo.gl_name() != 0);

    capacity = 8 * 1024 * 1024;

    gl::named_buffer_storage(pbo.gl_name(),
                             capacity,
                             nullptr,
                             gl::Buffer_storage_mask::map_write_bit |
                             gl::Buffer_storage_mask::map_persistent_bit);

    auto* map_pointer = gl::map_named_buffer_range(pbo.gl_name(),
                                                   0,
                                                   capacity,
                                                   gl::Map_buffer_access_mask::map_invalidate_buffer_bit |
                                                   gl::Map_buffer_access_mask::map_flush_explicit_bit    |
                                                   gl::Map_buffer_access_mask::map_persistent_bit        |
                                                   gl::Map_buffer_access_mask::map_write_bit);

    VERIFY(map_pointer != nullptr);

    span = gsl::span(reinterpret_cast<std::byte*>(map_pointer),
                     capacity);
}

auto Image_transfer::Slot::span_for(int width, int height, gl::Internal_format internal_format)
-> gsl::span<std::byte>
{
    Expects(width >= 1);
    Expects(height >= 1);

    row_stride = width * get_upload_pixel_byte_count(internal_format);
    auto byte_count = row_stride * height;
    Expects(byte_count >= 1);
    Expects(byte_count <= capacity);
    return span.subspan(0, byte_count);
}

} // namespace erhe::graphics
