#include "erhe/graphics_experimental/debug.hpp"

#include "erhe/gl/gl.hpp"

#include "erhe/graphics/log.hpp"
#include "erhe/graphics/vertex_format.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace erhe::graphics
{

using erhe::log::Log;

void debug_program()
{
    GLint current_program;
    GLint active_attributes;
    GLint link_status;

    gl::get_integer_v(gl::Get_p_name::current_program, &current_program);

    GLint p = current_program;

    if (p == 0)
    {
        return;
    }

    gl::get_program_iv(p, gl::Program_property::active_attributes, &active_attributes);
    gl::get_program_iv(p, gl::Program_property::link_status,       &link_status);

    log_program.trace("Shader_stages:     {}\n", p);
    log_program.trace("Link status:       {}\n", link_status);
    log_program.trace("Active attributes: {}\n", active_attributes);
    for (int i = 0; i < active_attributes; ++i)
    {
        constexpr int      buf_size{1024};
        int                length{0};
        int                size{0};
        gl::Attribute_type type;
        std::string buffer;
        buffer.resize(buf_size);

        gl::get_active_attrib(p, i, buf_size, &length, &size, &type, &buffer[0]);
        GLint location = gl::get_attrib_location(p, &buffer[0]);

        log_program.trace("Shader_stages attribute {}: {} size = {}, type {}, location = {}\n",
                          i, buffer, size, gl::c_str(type), location);
    }
}

void debug_buffer(gl::Buffer_target target, GLint buffer, GLenum index_type)
{
    GLint64 size{0ULL};
    GLint   usage;
    GLint   access_flags;
    GLint   mapped;
    void*   map_pointer{nullptr};
    GLint   map_offset;
    GLint   map_length;

    if (buffer <= 0)
    {
        return;
    }

    gl::get_named_buffer_parameter_i_64v(buffer, gl::Buffer_p_name::buffer_size,              &size);
    gl::get_named_buffer_parameter_iv   (buffer, gl::Buffer_p_name::buffer_usage,             &usage);
    gl::get_named_buffer_parameter_iv   (buffer, gl::Buffer_p_name::buffer_access_flags,      &access_flags);
    gl::get_named_buffer_parameter_iv   (buffer, gl::Buffer_p_name::buffer_mapped,            &mapped);
    gl::get_named_buffer_pointer_v      (buffer, gl::Buffer_pointer_name::buffer_map_pointer, &map_pointer);
    gl::get_named_buffer_parameter_iv   (buffer, gl::Buffer_p_name::buffer_map_offset,        &map_offset);
    gl::get_named_buffer_parameter_iv   (buffer, gl::Buffer_p_name::buffer_map_length,        &map_length);

    log_buffer.trace("Buffer {}\n", buffer);
    log_buffer.trace("Size:           {}\n", static_cast<unsigned int>(size));
    log_buffer.trace("Usage:          {}\n", usage);
    log_buffer.trace("Access flags:   {}\n", access_flags);
    log_buffer.trace("Mapped:         {}\n", mapped);
    log_buffer.trace("Map pointer:    {}\n", reinterpret_cast<intptr_t>(map_pointer));
    log_buffer.trace("Map offset:     {}\n", map_offset);
    log_buffer.trace("Map length:     {}\n", map_length);

    if (mapped == GL_FALSE)
    {
        switch (index_type)
        {
            case GL_NONE:
            {
                auto count = static_cast<size_t>(size) / sizeof(float);
                std::vector<float> data(count);

                gl::get_named_buffer_sub_data(buffer, 0, count * 4, &data[0]);

                for (size_t i = 0; (i < count) && (i < 16); ++i)
                {
                    log_buffer.log_ni(Log::Level::LEVEL_TRACE, "{} ", data[i]);
                }

                log_buffer.log_ni(Log::Level::LEVEL_TRACE, "\n");
            }
            break;

            case GL_UNSIGNED_BYTE:
            {
                auto count = static_cast<size_t>(size) / sizeof(uint8_t);
                std::vector<uint8_t> data(count);

                gl::get_buffer_sub_data(target, 0, count * sizeof(uint8_t), &data[0]);
                for (size_t i = 0; i < count && i < 16; ++i)
                {
                    log_buffer.log_ni(Log::Level::LEVEL_TRACE, "{} ", data[i]);
                }

                log_buffer.log_ni(Log::Level::LEVEL_TRACE, "\n");
            }
            break;

            case GL_UNSIGNED_SHORT:
            {
                auto count = static_cast<size_t>(size) / sizeof(uint16_t);
                std::vector<uint16_t> data(count);

                gl::get_buffer_sub_data(target, 0, count * sizeof(uint16_t), &data[0]);
                for (size_t i = 0; (i < count) && (i < 16); ++i)
                {
                    log_buffer.log_ni(Log::Level::LEVEL_TRACE, "{} ", data[i]);
                }

                log_buffer.log_ni(Log::Level::LEVEL_TRACE, "\n");
            }
            break;

            case GL_UNSIGNED_INT:
            {
                auto count = static_cast<size_t>(size) / sizeof(uint32_t);
                std::vector<uint32_t> data(count);

                gl::get_buffer_sub_data(target, 0, count * sizeof(uint32_t), &data[0]);
                for (size_t i = 0; (i < count) && (i < 16); ++i)
                {
                    log_buffer.log_ni(Log::Level::LEVEL_TRACE, "{} ", data[i]);
                }

                log_buffer.log_ni(Log::Level::LEVEL_TRACE, "\n");
            }
            break;

            default:
            {
                FATAL("Bad index type");
            }
        }
    }
}

void debug_vao()
{
    GLint max_vertex_attribs;
    GLint vertex_array_binding;
    GLint element_array_buffer_binding;

    gl::get_integer_v(gl::Get_p_name::max_vertex_attribs,           &max_vertex_attribs);
    gl::get_integer_v(gl::Get_p_name::vertex_array_binding,         &vertex_array_binding);
    gl::get_integer_v(gl::Get_p_name::element_array_buffer_binding, &element_array_buffer_binding);

    log_vertex_stream.trace("Vertex array binding:          {}\n", vertex_array_binding);
    log_vertex_stream.trace("Element array buffer binding:  {}\n", element_array_buffer_binding);
    debug_buffer(gl::Buffer_target::element_array_buffer, element_array_buffer_binding, GL_UNSIGNED_SHORT);

    for (int i = 0; i < max_vertex_attribs; ++i)
    {
        GLint   buffer_binding{0};
        GLint   enabled{0};
        GLint   size{0};
        GLint   stride{0};
        GLint   type{0};
        GLint   normalized{0};
        GLint   integer{0};
        GLint   divisor{0};
        GLvoid* pointer{nullptr};

        gl::get_vertex_attrib_iv(i, gl::Vertex_attrib_property::vertex_attrib_array_buffer_binding, &buffer_binding);
        gl::get_vertex_attrib_iv(i, gl::Vertex_attrib_property::vertex_attrib_array_enabled,        &enabled);
        gl::get_vertex_attrib_iv(i, gl::Vertex_attrib_property::vertex_attrib_array_size,           &size);
        gl::get_vertex_attrib_iv(i, gl::Vertex_attrib_property::vertex_attrib_array_stride,         &stride);
        gl::get_vertex_attrib_iv(i, gl::Vertex_attrib_property::vertex_attrib_array_type,           &type);
        gl::get_vertex_attrib_iv(i, gl::Vertex_attrib_property::vertex_attrib_array_normalized,     &normalized);
        gl::get_vertex_attrib_iv(i, gl::Vertex_attrib_property::vertex_attrib_array_integer,        &integer);
        gl::get_vertex_attrib_iv(i, gl::Vertex_attrib_property::vertex_attrib_array_divisor,        &divisor);

        gl::get_vertex_attrib_pointer_v(i, gl::Vertex_attrib_pointer_property::vertex_attrib_array_pointer, &pointer);

        if ((buffer_binding != 0) || (enabled == GL_TRUE))
        {
            log_vertex_stream.trace(
                "Vertex attrib {}: buffer = {}, enabled = {}, size = {}, stride = {}, type = {}, normalized = {}, integer = {}, divisor = {}, pointer = {}\n",
                i,
                buffer_binding,
                (enabled == GL_TRUE) ? 1 : 0,
                size,
                stride,
                type,
                (normalized == GL_TRUE) ? 1 : 0,
                (integer == GL_TRUE) ? 1 : 0,
                divisor,
                fmt::ptr(pointer));

            debug_buffer(gl::Buffer_target::array_buffer, buffer_binding, GL_NONE);
        }
    }
}

void debug_gl_state()
{
    debug_program();
    debug_vao();
}

} // namespace erhe::graphics
