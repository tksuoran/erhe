#include "erhe/graphics/shader_stages.hpp"
#include "erhe/gl/wrapper_functions.hpp"

#include <fmt/format.h>
#include <sstream>

namespace erhe::graphics
{

using std::string;

auto Shader_stages::gl_name() const -> unsigned int
{
    return m_handle.gl_name();
}

auto Shader_stages::format(const string& source) -> string
{
    int         line{1};
    const char* head = source.c_str();

    std::stringstream sb;
    sb << fmt::format("{:>3}: ", line);

    for (;;)
    {
        char c = *head;
        ++head;
        if (c == '\r')
        {
            continue;
        }
        if (c == 0)
        {
            break;
        }

        if (c == '\n')
        {
            ++line;
            sb << fmt::format("\n{:>3}: ", line);
            continue;
        }
        sb << c;
    }
    return sb.str();
}

Shader_stages::Shader_stages(Prototype&& prototype)
{
    Expects(prototype.is_valid());
    Expects(prototype.m_handle.gl_name() != 0);
    Expects(!prototype.m_attached_shaders.empty());

    m_name             = std::move(prototype.m_name);
    m_handle           = std::move(prototype.m_handle);
    m_attached_shaders = std::move(prototype.m_attached_shaders);

    gl::object_label(
        gl::Object_identifier::program,
        gl_name(),
        static_cast<GLsizei>(m_name.length()),
        m_name.c_str()
    );
}

void Shader_stages::reload(Prototype&& prototype)
{
    if (
        !prototype.is_valid()               ||
        (prototype.m_handle.gl_name() == 0) ||
        prototype.m_attached_shaders.empty()
    )
    {
        return;
    }

    m_name             = std::move(prototype.m_name);
    m_handle           = std::move(prototype.m_handle);
    m_attached_shaders = std::move(prototype.m_attached_shaders);

    gl::object_label(
        gl::Object_identifier::program,
        gl_name(),
        static_cast<GLsizei>(m_name.length()),
        m_name.c_str()
    );
}

auto operator==(const Shader_stages& lhs, const Shader_stages& rhs) noexcept -> bool
{
    return lhs.gl_name() == rhs.gl_name();
}

auto operator!=(const Shader_stages& lhs, const Shader_stages& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

void Shader_stages_tracker::reset()
{
    gl::use_program(0);
    m_last = 0;
}

void Shader_stages_tracker::execute(const Shader_stages* state)
{
    unsigned int name = (state != nullptr)
        ? state->gl_name()
        : 0;
    if (m_last == name)
    {
        return;
    }
    gl::use_program(name);
    m_last = name;
}

} // namespace erhe::graphics
