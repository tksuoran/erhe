#include "erhe/graphics/shader_stages.hpp"
#include <sstream>

namespace erhe::graphics
{

using std::string;

auto Shader_stages::gl_name() const
-> unsigned int
{
    return m_handle.gl_name();
}

auto Shader_stages::format(const string& source)
-> string
{
    int         line{1};
    const char* head = source.c_str();

    std::stringstream sb;
    sb << std::setw(3) << std::setfill(' ') << line << ": ";

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
            sb << '\n'
               << std::setw(3) << std::setfill(' ') << line << ": ";
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
    if (!prototype.is_valid()               ||
        (prototype.m_handle.gl_name() == 0) ||
        prototype.m_attached_shaders.empty())
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

} // namespace erhe::graphics
