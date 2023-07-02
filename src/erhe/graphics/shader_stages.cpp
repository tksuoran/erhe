#include "erhe/graphics/shader_stages.hpp"
#include "erhe/gl/wrapper_functions.hpp"

#include <fmt/format.h>
#include <sstream>

namespace erhe::graphics
{

using std::string;

Shader_stage::Shader_stage(gl::Shader_type type, const std::string_view source)
    : type  {type}
    , source{source}
{
}

Shader_stage::Shader_stage(gl::Shader_type type, const std::filesystem::path path)
    : type{type}
    , path{path}
{
}

auto Shader_stages::name() const -> const std::string&
{
    return m_name;
}

auto Shader_stages::gl_name() const -> unsigned int
{
    return m_handle.gl_name();
}

Shader_stages::Shader_stages(const std::string& failed_name)
{
    std::string label = fmt::format("(P:{}) {} - compilation failed", gl_name(), failed_name);
    gl::object_label(
        gl::Object_identifier::program,
        gl_name(),
        static_cast<GLsizei>(label.length()),
        label.c_str()
    );
}

Shader_stages::Shader_stages(Shader_stages_prototype&& prototype)
{
    Expects(prototype.m_handle.gl_name() != 0);
    Expects(!prototype.m_shaders.empty());

    m_name             = prototype.name();
    m_handle           = std::move(prototype.m_handle);
    m_attached_shaders = std::move(prototype.m_shaders);

    std::string label = fmt::format(
        "(P:{}) {}{}",
        gl_name(),
        m_name,
        prototype.is_valid() ? "" : " (Failed)"
    );
    gl::object_label(
        gl::Object_identifier::program,
        gl_name(),
        static_cast<GLsizei>(label.length()),
        label.c_str()
    );
}

void Shader_stages::reload(Shader_stages_prototype&& prototype)
{
    if (
        !prototype.is_valid()               ||
        (prototype.m_handle.gl_name() == 0) ||
        prototype.m_shaders.empty()
    ) {
        return;
    }

    m_handle           = std::move(prototype.m_handle);
    m_attached_shaders = std::move(prototype.m_shaders);

    std::string label = fmt::format("(P:{}) {}", gl_name(), m_name);
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
    if (m_last == name) {
        return;
    }
    gl::use_program(name);
    m_last = name;
}

} // namespace erhe::graphics
