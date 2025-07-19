#include "erhe_graphics/command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

Command_encoder::Command_encoder(Device& device)
    : m_device{device}
{
}

Command_encoder::~Command_encoder()
{
}

[[nodiscard]] auto convert_to_gl(const Buffer_target buffer_target) -> gl::Buffer_target
{
    switch (buffer_target) {
        case Buffer_target::vertex       : return gl::Buffer_target::element_array_buffer;
        case Buffer_target::uniform      : return gl::Buffer_target::uniform_buffer;
        case Buffer_target::storage      : return gl::Buffer_target::shader_storage_buffer;
        case Buffer_target::draw_indirect: return gl::Buffer_target::draw_indirect_buffer;
        default: {
            ERHE_FATAL("Bad Buffer_target %u", static_cast<unsigned int>(buffer_target));
            return gl::Buffer_target::copy_read_buffer;
        }
    }
}

void Command_encoder::set_buffer(const Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index)
{
    gl::Buffer_target gl_buffer_target = convert_to_gl(buffer_target);
    gl::bind_buffer_range(
        gl_buffer_target,
        static_cast<GLuint>    (index),
        static_cast<GLuint>    (buffer->gl_name()),
        static_cast<GLintptr>  (offset),
        static_cast<GLsizeiptr>(length)
    );
}

void Command_encoder::set_buffer(Buffer_target buffer_target, const Buffer* buffer)
{
    gl::Buffer_target gl_buffer_target = convert_to_gl(buffer_target);
    gl::bind_buffer(
        gl_buffer_target,
        static_cast<GLuint>(buffer->gl_name())
    );
}

} // namespace erhe::graphics
