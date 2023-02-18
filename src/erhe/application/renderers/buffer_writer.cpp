#include "erhe/application//renderers/buffer_writer.hpp"
#include "erhe/graphics/instance.hpp"

namespace erhe::application
{

void Buffer_writer::shader_storage_align()
{
    while (write_offset % erhe::graphics::Instance::implementation_defined.shader_storage_buffer_offset_alignment) {
        write_offset++;
    }
}

void Buffer_writer::uniform_align()
{
    while (write_offset % erhe::graphics::Instance::implementation_defined.uniform_buffer_offset_alignment) {
        write_offset++;
    }
}

void Buffer_writer::begin(const gl::Buffer_target buffer_target)
{
    switch (buffer_target) {
        //using enum gl::Buffer_target;
        case gl::Buffer_target::shader_storage_buffer: {
            shader_storage_align();
            break;
        }

        case gl::Buffer_target::uniform_buffer: {
            uniform_align();
            break;
        }
        default: {
            // TODO
            break;
        }
    }
    range.first_byte_offset = write_offset;
}

void Buffer_writer::end()
{
    range.byte_count = write_offset - range.first_byte_offset;
}

void Buffer_writer::reset()
{
    range.first_byte_offset = 0;
    range.byte_count        = 0;
    write_offset            = 0;
}

} // namespace erhe::application
