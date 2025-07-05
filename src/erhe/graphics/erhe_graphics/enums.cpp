#include "erhe_graphics/enums.hpp"

namespace erhe::graphics {

auto c_str(const Primitive_type primitive_type) -> const char*
{
    switch (primitive_type) {
        case Primitive_type::point         : return "point";
        case Primitive_type::line          : return "line";
        case Primitive_type::line_strip    : return "line_strip";
        case Primitive_type::triangle      : return "triangle";
        case Primitive_type::triangle_strip: return "triangle_strip";
        default: return "?";
    }
}

auto c_str(const Buffer_target buffer_target) -> const char*
{
    switch (buffer_target) {
        case Buffer_target::vertex       : return "vertex";
        case Buffer_target::uniform      : return "uniform";
        case Buffer_target::storage      : return "storage";
        case Buffer_target::draw_indirect: return "draw_indirect";
        default: return "?";
    }
}

auto is_indexed(const Buffer_target buffer_target) -> bool
{
    switch (buffer_target) {
        case Buffer_target::vertex       : return false;
        case Buffer_target::uniform      : return true;
        case Buffer_target::storage      : return true;
        case Buffer_target::draw_indirect: return false;
        default: return false;
    }
}

} // namespace erhe::graphics
