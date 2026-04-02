#include "erhe_graphics/gl/gl_blit_command_encoder.hpp"
#include "erhe_graphics/gl/gl_buffer.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_graphics/gl/gl_render_pass.hpp"
#include "erhe_graphics/gl/gl_texture.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_verify/verify.hpp"

#include <optional>
#include <vector>

namespace erhe::graphics {

Blit_command_encoder_impl::Blit_command_encoder_impl(Device& device)
    : Command_encoder_impl{device}
{
}

Blit_command_encoder_impl::~Blit_command_encoder_impl() noexcept = default;

// This is the only way to copy to/from the default framebuffer. Only color buffer is supported
void Blit_command_encoder_impl::blit_framebuffer(
    const Render_pass& source_renderpass,
    glm::ivec2         source_origin,
    glm::ivec2         source_size,
    const Render_pass& destination_renderpass,
    glm::ivec2         destination_origin
)
{
    const GLuint           src_name   = source_renderpass.get_impl().gl_name();
    const GLuint           dst_name   = destination_renderpass.get_impl().gl_name();
    const gl::Color_buffer src_buffer = src_name != 0 ? gl::Color_buffer::color_attachment0 : gl::Color_buffer::back;
    const gl::Color_buffer dst_buffer = dst_name != 0 ? gl::Color_buffer::color_attachment0 : gl::Color_buffer::back;

    if (m_device.get_info().use_direct_state_access) {
        gl::named_framebuffer_read_buffer (src_name, src_buffer);
        gl::named_framebuffer_draw_buffers(dst_name, 1, &dst_buffer);
        gl::blit_named_framebuffer(
            src_name,                                // read framebuffer
            dst_name,                                // draw framebuffer
            source_origin.x,                         // src X0
            source_origin.y,                         // src Y0
            source_size.x,                           // src X1
            source_size.y,                           // src Y1
            destination_origin.x,                    // dst X0
            destination_origin.y,                    // dst Y0
            source_size.x,                           // dst X1
            source_size.y,                           // dst Y1
            gl::Clear_buffer_mask::color_buffer_bit, // mask
            gl::Blit_framebuffer_filter::nearest     // filter
        );
    } else {
        gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, src_name);
        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, dst_name);
        gl::read_buffer(static_cast<gl::Read_buffer_mode>(src_buffer));
        const gl::Draw_buffer_mode draw_buf = static_cast<gl::Draw_buffer_mode>(dst_buffer);
        gl::draw_buffers(1, &draw_buf);
        gl::blit_framebuffer(
            source_origin.x,                         // src X0
            source_origin.y,                         // src Y0
            source_size.x,                           // src X1
            source_size.y,                           // src Y1
            destination_origin.x,                    // dst X0
            destination_origin.y,                    // dst Y0
            source_size.x,                           // dst X1
            source_size.y,                           // dst Y1
            gl::Clear_buffer_mask::color_buffer_bit, // mask
            gl::Blit_framebuffer_filter::nearest     // filter
        );
    }
}

// Texture to texture copy, single level, single array slice
void Blit_command_encoder_impl::copy_from_texture(
    const Texture* const source_texture,
    const std::uintptr_t source_slice,
    const std::uintptr_t source_level,
    const glm::ivec3     source_origin,
    const glm::ivec3     source_size,
    const Texture* const destination_texture,
    const std::uintptr_t destination_slice,
    const std::uintptr_t destination_level,
    const glm::ivec3     destination_origin
)
{
    const gl::Texture_target gl_source_texture_target = convert_to_gl_texture_target(
        source_texture->get_texture_type(),
        source_texture->get_sample_count() != 0,
        source_texture->get_array_layer_count() != 0
    );
    const gl::Texture_target gl_destination_texture_target = convert_to_gl_texture_target(
        destination_texture->get_texture_type(),
        destination_texture->get_sample_count() != 0,
        destination_texture->get_array_layer_count() != 0
    );
    int gl_width  = source_size.x;
    int gl_height = source_size.y;
    int gl_depth  = source_size.z;
    convert_texture_dimensions_to_gl(gl_source_texture_target, gl_width, gl_height, gl_depth, 1);
    int gl_source_x = source_origin.x;
    int gl_source_y = source_origin.y;
    int gl_source_z = source_origin.z;
    convert_texture_offset_to_gl(gl_source_texture_target, gl_source_x, gl_source_y, gl_source_z, static_cast<int>(source_slice));
    int gl_destination_x = destination_origin.x;
    int gl_destination_y = destination_origin.y;
    int gl_destination_z = destination_origin.z;
    convert_texture_offset_to_gl(gl_destination_texture_target, gl_destination_x, gl_destination_y, gl_destination_z, static_cast<int>(destination_slice));
    gl::copy_image_sub_data(
        gl_name(*source_texture),
        static_cast<gl::Copy_image_sub_data_target>(gl_source_texture_target),
        static_cast<int>(source_level),
        gl_source_x,
        gl_source_y,
        gl_source_z,
        gl_name(*destination_texture),
        static_cast<gl::Copy_image_sub_data_target>(gl_destination_texture_target),
        static_cast<int>(destination_level),
        gl_destination_x,
        gl_destination_y,
        gl_destination_z,
        gl_width,
        gl_height,
        gl_depth
    );
}

// Buffer to texture copy
void Blit_command_encoder_impl::copy_from_buffer(
    const Buffer* const  source_buffer,
    const std::uintptr_t source_offset,
    const std::uintptr_t source_bytes_per_row,
    const std::uintptr_t source_bytes_per_image,
    const glm::ivec3     source_size,
    const Texture* const destination_texture,
    const std::uintptr_t destination_slice,
    const std::uintptr_t destination_level,
    const glm::ivec3     destination_origin
)
{
    ERHE_VERIFY(source_size.x <= destination_texture->get_width ());
    ERHE_VERIFY(source_size.y <= destination_texture->get_height());
    ERHE_VERIFY(source_size.z <= destination_texture->get_depth ());
    ERHE_VERIFY(destination_origin.x + source_size.x <= destination_texture->get_width ());
    ERHE_VERIFY(destination_origin.y + source_size.y <= destination_texture->get_height());
    ERHE_VERIFY(destination_origin.z + source_size.z <= destination_texture->get_depth ());

    const gl::Texture_target gl_destination_texture_target = convert_to_gl_texture_target(
        destination_texture->get_texture_type(),
        destination_texture->get_sample_count() != 0,
        destination_texture->get_array_layer_count() != 0
    );
    int gl_width  = source_size.x;
    int gl_height = source_size.y;
    int gl_depth  = source_size.z;
    convert_texture_dimensions_to_gl(gl_destination_texture_target, gl_width, gl_height, gl_depth, 0);
    int gl_destination_x = destination_origin.x;
    int gl_destination_y = destination_origin.y;
    int gl_destination_z = destination_origin.z;
    convert_texture_offset_to_gl(gl_destination_texture_target, gl_destination_x, gl_destination_y, gl_destination_z, static_cast<int>(destination_slice));

    const std::size_t gl_bytes_per_pixel = get_gl_pixel_byte_count(destination_texture->get_pixelformat());

    // Convert bytes to pixels
    const std::size_t gl_unpack_row_length = source_bytes_per_row / gl_bytes_per_pixel;
    ERHE_VERIFY(source_bytes_per_row % gl_bytes_per_pixel == 0);

    // Convert bytes to rows
    const std::size_t gl_unpack_image_height = source_bytes_per_image / source_bytes_per_row;
    ERHE_VERIFY(source_bytes_per_image % source_bytes_per_row == 0);

    const int alignment = (source_bytes_per_row % 8 == 0) ? 8 :
                          (source_bytes_per_row % 4 == 0) ? 4 :
                          (source_bytes_per_row % 2 == 0) ? 2 : 1;

    gl::Pixel_format gl_format{0};
    gl::Pixel_type   gl_type  {0};
    const bool format_type_ok = get_format_and_type(destination_texture->get_pixelformat(), gl_format, gl_type);
    ERHE_VERIFY(format_type_ok);
    if (!format_type_ok) {
        return;
    }

#if defined(__APPLE__)
    // macOS OpenGL driver does not reliably support PBO-based texture uploads.
    // Read the data from the GPU buffer back to CPU, then upload directly.
    const std::size_t read_byte_count = source_bytes_per_image;
    std::vector<std::byte> cpu_buffer(read_byte_count);
    {
        auto guard = m_device.get_impl().get_binding_state().push_buffer(
            gl::Buffer_target::copy_read_buffer, source_buffer->get_impl().gl_name()
        );
        gl::get_buffer_sub_data(
            gl::Buffer_target::copy_read_buffer,
            static_cast<GLintptr>(source_offset),
            static_cast<GLsizeiptr>(read_byte_count),
            cpu_buffer.data()
        );
    }
    const void* cpu_data = cpu_buffer.data();

    gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, 0);
    gl::pixel_store_i(gl::Pixel_store_parameter::unpack_alignment,    alignment);
    gl::pixel_store_i(gl::Pixel_store_parameter::unpack_image_height, static_cast<int>(gl_unpack_image_height));
    gl::pixel_store_i(gl::Pixel_store_parameter::unpack_row_length,   static_cast<int>(gl_unpack_row_length));

    constexpr GLuint scratch_unit = Gl_binding_state::s_max_texture_units - 1;
    auto texture_guard = m_device.get_impl().get_binding_state().push_texture(
        scratch_unit, gl_destination_texture_target, destination_texture->get_impl().gl_name()
    );

    switch (destination_texture->get_impl().get_storage_dimensions(gl_destination_texture_target)) {
        case 1: {
            gl::tex_sub_image_1d(
                gl_destination_texture_target,
                static_cast<GLint>(destination_level),
                gl_destination_x, gl_width, gl_format, gl_type, cpu_data
            );
            break;
        }

        case 2: {
            gl::tex_sub_image_2d(
                gl_destination_texture_target,
                static_cast<GLint>(destination_level),
                gl_destination_x, gl_destination_y,
                gl_width, gl_height, gl_format, gl_type, cpu_data
            );
            break;
        }

        case 3: {
            gl::tex_sub_image_3d(
                gl_destination_texture_target,
                static_cast<GLint>(destination_level),
                gl_destination_x, gl_destination_y, gl_destination_z,
                gl_width, gl_height, gl_depth, gl_format, gl_type, cpu_data
            );
            break;
        }

        default: {
            ERHE_FATAL("Bad texture target");
        }
    }
#else
    gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, source_buffer->get_impl().gl_name());

    gl::pixel_store_i(gl::Pixel_store_parameter::unpack_alignment,    alignment);
    gl::pixel_store_i(gl::Pixel_store_parameter::unpack_image_height, static_cast<int>(gl_unpack_image_height));
    gl::pixel_store_i(gl::Pixel_store_parameter::unpack_row_length,   static_cast<int>(gl_unpack_row_length));

    const char* data_pointer = reinterpret_cast<const char*>(source_offset);

    const bool use_dsa = m_device.get_info().use_direct_state_access;
    std::optional<Texture_binding_guard> texture_guard;
    if (!use_dsa) {
        constexpr GLuint scratch_unit = Gl_binding_state::s_max_texture_units - 1;
        texture_guard.emplace(
            m_device.get_impl().get_binding_state().push_texture(
                scratch_unit, gl_destination_texture_target, destination_texture->get_impl().gl_name()
            )
        );
    }

    switch (destination_texture->get_impl().get_storage_dimensions(gl_destination_texture_target)) {
        case 1: {
            if (use_dsa) {
                gl::texture_sub_image_1d(
                    destination_texture->get_impl().gl_name(),
                    static_cast<GLint>(destination_level),
                    gl_destination_x, gl_width, gl_format, gl_type, data_pointer
                );
            } else {
                gl::tex_sub_image_1d(
                    gl_destination_texture_target,
                    static_cast<GLint>(destination_level),
                    gl_destination_x, gl_width, gl_format, gl_type, data_pointer
                );
            }
            break;
        }

        case 2: {
            if (use_dsa) {
                gl::texture_sub_image_2d(
                    destination_texture->get_impl().gl_name(),
                    static_cast<GLint>(destination_level),
                    gl_destination_x, gl_destination_y,
                    gl_width, gl_height, gl_format, gl_type, data_pointer
                );
            } else {
                gl::tex_sub_image_2d(
                    gl_destination_texture_target,
                    static_cast<GLint>(destination_level),
                    gl_destination_x, gl_destination_y,
                    gl_width, gl_height, gl_format, gl_type, data_pointer
                );
            }
            break;
        }

        case 3: {
            if (use_dsa) {
                gl::texture_sub_image_3d(
                    destination_texture->get_impl().gl_name(),
                    static_cast<GLint>(destination_level),
                    gl_destination_x, gl_destination_y, gl_destination_z,
                    gl_width, gl_height, gl_depth, gl_format, gl_type, data_pointer
                );
            } else {
                gl::tex_sub_image_3d(
                    gl_destination_texture_target,
                    static_cast<GLint>(destination_level),
                    gl_destination_x, gl_destination_y, gl_destination_z,
                    gl_width, gl_height, gl_depth, gl_format, gl_type, data_pointer
                );
            }
            break;
        }

        default: {
            ERHE_FATAL("Bad texture target");
        }
    }
#endif
}

// Copy from texture to buffer
void Blit_command_encoder_impl::copy_from_texture(
    const Texture* const source_texture,
    const std::uintptr_t source_slice,
    const std::uintptr_t source_level,
    const glm::ivec3     source_origin,
    const glm::ivec3     source_size,
    const Buffer* const  destination_buffer,
    const std::uintptr_t destination_offset,
    const std::uintptr_t destination_bytes_per_row,
    const std::uintptr_t destination_bytes_per_image
)
{
    const gl::Texture_target gl_source_texture_target = convert_to_gl_texture_target(
        source_texture->get_texture_type(),
        source_texture->get_sample_count() != 0,
        source_texture->get_array_layer_count() != 0
    );
    int gl_width  = source_size.x;
    int gl_height = source_size.y;
    int gl_depth  = source_size.z;

    ERHE_VERIFY(source_size.x <= source_texture->get_width ());
    ERHE_VERIFY(source_size.y <= source_texture->get_height());
    ERHE_VERIFY(source_size.z <= source_texture->get_depth ());
    ERHE_VERIFY((source_origin.x + source_size.x) <= source_texture->get_width ());
    ERHE_VERIFY((source_origin.y + source_size.y) <= source_texture->get_height());
    ERHE_VERIFY((source_origin.z + source_size.z) <= source_texture->get_depth ());

    convert_texture_dimensions_to_gl(gl_source_texture_target, gl_width, gl_height, gl_depth, static_cast<int>(source_slice));
    int gl_source_x = source_origin.x;
    int gl_source_y = source_origin.y;
    int gl_source_z = source_origin.z;
    convert_texture_offset_to_gl(gl_source_texture_target, gl_source_x, gl_source_y, gl_source_z, static_cast<int>(source_slice));

    std::size_t gl_bytes_per_pixel = get_gl_pixel_byte_count(source_texture->get_pixelformat());
    ERHE_VERIFY(destination_bytes_per_row >= source_size.x * gl_bytes_per_pixel);

    // Convert bytes to pixels
    std::size_t gl_pack_row_length = destination_bytes_per_row / gl_bytes_per_pixel;
    ERHE_VERIFY(destination_bytes_per_row % gl_bytes_per_pixel == 0);

    // Convert bytes to rows
    std::size_t gl_pack_image_height = destination_bytes_per_image / destination_bytes_per_row;
    ERHE_VERIFY(destination_bytes_per_image % destination_bytes_per_row == 0);

    // TODO Verify this is correct
    std::size_t destination_size = source_size.z == 1
        ? destination_bytes_per_row * source_size.y
        : destination_bytes_per_image * source_size.z;

    const int alignment = (destination_bytes_per_row % 8 == 0) ? 8 :
                          (destination_bytes_per_row % 4 == 0) ? 4 :
                          (destination_bytes_per_row % 2 == 0) ? 2 : 1;

    gl::bind_buffer(gl::Buffer_target::pixel_pack_buffer, destination_buffer->get_impl().gl_name());

    gl::pixel_store_i(gl::Pixel_store_parameter::pack_alignment,    alignment);
    gl::pixel_store_i(gl::Pixel_store_parameter::pack_image_height, static_cast<int>(gl_pack_image_height));
    gl::pixel_store_i(gl::Pixel_store_parameter::pack_row_length,   static_cast<int>(gl_pack_row_length));

    // p indicates the location in memory of the first element of the first row
    // the first element of the Nth row is indicated by
    //
    //      p + N * k
    //
    // where N is the row number (counting from zero) and k is defined as
    //
    // k = (s >= a) ? n * l
    //              : (a / s) * ceil(s * n * l / a)
    //
    // n is the number of elements in a group                ~ number of components in pixel
    // l is the number of groups in the row                  ~ number of pixels per row
    // a is the value of UNPACK_ALIGNMENT                    alignment
    // s is the size, in units of GL ubytes, of an element   ~ bytes per component
    //
    // If the number of bits per element is not 1, 2, 4, or 8 times the number of bits in a GL ubyte, then k = nl for all values of a.

    gl::Pixel_format gl_format{0};
    gl::Pixel_type   gl_type  {0};
    const bool format_type_ok = get_format_and_type(source_texture->get_pixelformat(), gl_format, gl_type);
    ERHE_VERIFY(format_type_ok);
    if (!format_type_ok) {
        return;
    }
    if (m_device.get_info().use_direct_state_access) {
        gl::get_texture_sub_image(
            source_texture->get_impl().gl_name(),
            static_cast<GLint>(source_level),
            gl_source_x, gl_source_y, gl_source_z,
            gl_width, gl_height, gl_depth,
            gl_format, gl_type,
            static_cast<GLsizei>(destination_size),
            reinterpret_cast<void *>(destination_offset)
        );
    } else {
        // Pre-DSA: bind and use glGetTexImage (reads entire mip level)
        constexpr GLuint scratch_unit = Gl_binding_state::s_max_texture_units - 1;
        auto guard = m_device.get_impl().get_binding_state().push_texture(
            scratch_unit, gl_source_texture_target, source_texture->get_impl().gl_name()
        );
        gl::get_tex_image(
            gl_source_texture_target,
            static_cast<GLint>(source_level),
            gl_format, gl_type,
            reinterpret_cast<void *>(destination_offset)
        );
    }
}

void Blit_command_encoder_impl::generate_mipmaps(const Texture* texture)
{
    if (m_device.get_info().use_direct_state_access) {
        gl::generate_texture_mipmap(texture->get_impl().gl_name());
    } else {
        const gl::Texture_target target = texture->get_impl().get_gl_texture_target();
        constexpr GLuint scratch_unit = Gl_binding_state::s_max_texture_units - 1;
        auto guard = m_device.get_impl().get_binding_state().push_texture(
            scratch_unit, target, texture->get_impl().gl_name()
        );
        gl::generate_mipmap(target);
    }
}

void Blit_command_encoder_impl::fill_buffer(
    const Buffer* const  buffer,
    const std::uintptr_t offset,
    const std::uintptr_t length,
    const uint8_t        value
)
{
    if (m_device.get_info().use_direct_state_access) {
        gl::clear_named_buffer_sub_data(
            buffer->get_impl().gl_name(),
            gl::Internal_format::r8ui,
            offset,
            length,
            gl::Pixel_format::red_integer,
            gl::Pixel_type::unsigned_byte,
            &value
        );
    } else {
        auto guard = m_device.get_impl().get_binding_state().push_buffer(gl::Buffer_target::copy_write_buffer, buffer->get_impl().gl_name());
        gl::clear_buffer_sub_data(
            gl::Buffer_target::copy_write_buffer,
            gl::Internal_format::r8ui,
            offset,
            length,
            gl::Pixel_format::red_integer,
            gl::Pixel_type::unsigned_byte,
            &value
        );
    }
}

// Texture to texture copy, multiple array slices and/or mipmap levels
void Blit_command_encoder_impl::copy_from_texture(
    const Texture* const source_texture,
    const std::uintptr_t source_slice,
    const std::uintptr_t source_level,
    const Texture* const destination_texture,
    const std::uintptr_t destination_slice,
    const std::uintptr_t destination_level,
    const std::uintptr_t slice_count,
    const std::uintptr_t level_count
)
{
    const gl::Texture_target gl_source_texture_target = convert_to_gl_texture_target(
        source_texture->get_texture_type(),
        source_texture->get_sample_count() != 0,
        source_texture->get_array_layer_count() != 0
    );
    const gl::Texture_target gl_destination_texture_target = convert_to_gl_texture_target(
        destination_texture->get_texture_type(),
        destination_texture->get_sample_count() != 0,
        destination_texture->get_array_layer_count() != 0
    );
    int gl_width  = std::min(source_texture->get_width(),  destination_texture->get_width ());
    int gl_height = std::min(source_texture->get_height(), destination_texture->get_height());
    int gl_depth  = std::min(source_texture->get_depth(),  destination_texture->get_depth ());
    convert_texture_dimensions_to_gl(gl_source_texture_target, gl_width, gl_height, gl_depth, static_cast<int>(slice_count));
    int gl_source_x = 0;
    int gl_source_y = 0;
    int gl_source_z = 0;
    convert_texture_offset_to_gl(gl_source_texture_target, gl_source_x, gl_source_y, gl_source_z, static_cast<int>(source_slice));
    int gl_destination_x = 0;
    int gl_destination_y = 0;
    int gl_destination_z = 0;
    convert_texture_offset_to_gl(gl_destination_texture_target, gl_destination_x, gl_destination_y, gl_destination_z, static_cast<int>(destination_slice));
    for (int level = 0; level < level_count; ++level) {
        gl::copy_image_sub_data(
            source_texture->get_impl().gl_name(),
            static_cast<gl::Copy_image_sub_data_target>(gl_source_texture_target),
            static_cast<int>(source_level + level),
            gl_source_x,
            gl_source_y,
            gl_source_z,
            destination_texture->get_impl().gl_name(),
            static_cast<gl::Copy_image_sub_data_target>(gl_destination_texture_target),
            static_cast<int>(destination_level + level),
            gl_destination_x,
            gl_destination_y,
            gl_destination_z,
            gl_width,
            gl_height,
            gl_depth
        );
    }
}

void Blit_command_encoder_impl::copy_from_texture(
    const Texture* const source_texture,
    const Texture* const destination_texture
)
{
    // Texture to texture copy, all layer slices and mipmap levels

    ERHE_VERIFY(source_texture->get_array_layer_count() == destination_texture->get_array_layer_count());
    ERHE_VERIFY(source_texture->get_level_count()       == destination_texture->get_level_count());

    const int slice_count = std::min(source_texture->get_array_layer_count(), destination_texture->get_array_layer_count());
    const int level_count = std::min(source_texture->get_level_count(),       destination_texture->get_level_count());
    copy_from_texture(
        source_texture,
        0, // source_slice
        0, // source_level
        destination_texture,
        0, // destination_slice
        0, // destination_level
        slice_count,
        level_count
    );
}

void Blit_command_encoder_impl::copy_from_buffer(
    const Buffer* const  source_buffer,
    const std::uintptr_t source_offset,
    const Buffer* const  destination_buffer,
    const std::uintptr_t destination_offset,
    const std::uintptr_t size
)
{
    // Buffer to buffer copy
    if (m_device.get_info().use_direct_state_access) {
        gl::copy_named_buffer_sub_data(
            source_buffer->get_impl().gl_name(),
            destination_buffer->get_impl().gl_name(),
            source_offset,
            destination_offset,
            size
        );
    } else {
        auto guard_read  = m_device.get_impl().get_binding_state().push_buffer(gl::Buffer_target::copy_read_buffer,  source_buffer->get_impl().gl_name());
        auto guard_write = m_device.get_impl().get_binding_state().push_buffer(gl::Buffer_target::copy_write_buffer, destination_buffer->get_impl().gl_name());
        gl::copy_buffer_sub_data(
            gl::Copy_buffer_sub_data_target::copy_read_buffer,
            gl::Copy_buffer_sub_data_target::copy_write_buffer,
            source_offset,
            destination_offset,
            size
        );
    }
}

} // namespace erhe::graphics
