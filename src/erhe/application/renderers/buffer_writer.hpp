#pragma once

#include "erhe/gl/wrapper_enums.hpp"

#include <cstddef>

namespace erhe::application
{

class Buffer_range
{
public:
    std::size_t first_byte_offset{0};
    std::size_t byte_count       {0};
};

class Buffer_writer
{
public:
    Buffer_range range;
    std::size_t  write_offset{0};

    void shader_storage_align();
    void uniform_align       ();
    void begin               (const gl::Buffer_target buffer_target);
    void end                 ();
    void reset               ();
};

} // namespace erhe::application
