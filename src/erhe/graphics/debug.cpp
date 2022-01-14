#include "erhe/graphics/debug.hpp"
#include "erhe/gl/gl.hpp"

namespace erhe::graphics
{

Scoped_debug_group::Scoped_debug_group(
    const std::string_view debug_label
)
{
    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(debug_label.length()),
        debug_label.data()
    );
}

Scoped_debug_group::~Scoped_debug_group()
{
    gl::pop_debug_group();
}

} // namespace erhe::graphics
