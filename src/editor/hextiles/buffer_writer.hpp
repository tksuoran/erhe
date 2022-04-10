#pragma once

#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/graphics/configuration.hpp"

namespace erhe::primitive
{
    class Primitive;
    class Material;
}

namespace erhe::scene
{
    class ICamera;
    class Camera;
    class Light;
    class Light_layer;
    class Mesh_layer;
    class Node;
    class Viewport;
    class Visibility_filter;
}

namespace power_battle
{

class Buffer_range
{
public:
    size_t first_byte_offset{0};
    size_t byte_count       {0};
};

class Buffer_writer
{
public:
    Buffer_range range;
    size_t       write_offset{0};

    void shader_storage_align()
    {
        while (write_offset % erhe::graphics::Instance::implementation_defined.shader_storage_buffer_offset_alignment)
        {
            write_offset++;
        }
    }

    void uniform_align()
    {
        while (write_offset % erhe::graphics::Instance::implementation_defined.uniform_buffer_offset_alignment)
        {
            write_offset++;
        }
    }

    void begin(const gl::Buffer_target buffer_target)
    {
        switch (buffer_target)
        {
            //using enum gl::Buffer_target;
            case gl::Buffer_target::shader_storage_buffer:
            {
                shader_storage_align();
                break;
            }
            case gl::Buffer_target::uniform_buffer:
            {
                uniform_align();
                break;
            }
            default:
            {
                // TODO
                break;
            }
        }
        range.first_byte_offset = write_offset;
    }

    void end()
    {
        range.byte_count = write_offset - range.first_byte_offset;
    }

    void reset()
    {
        range.first_byte_offset = 0;
        range.byte_count        = 0;
        write_offset            = 0;
    }
};

} // namespace editor
