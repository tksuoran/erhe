#pragma once

namespace erhe::graphics {

enum class Primitive_type : unsigned int
{
    point          = 0,
    line           = 1,
    line_strip     = 2,
    triangle       = 3,
    triangle_strip = 4
};

enum class Buffer_target : unsigned int
{
    index         = 0,
    vertex        = 1,
    uniform       = 2,
    storage       = 3,
    draw_indirect = 4
};

[[nodiscard]] auto c_str     (const Primitive_type primitive_type) -> const char*;
[[nodiscard]] auto c_str     (const Buffer_target  buffer_target) -> const char*;
[[nodiscard]] auto is_indexed(const Buffer_target  buffer_target) -> bool;

} // namespace erhe::graphics
