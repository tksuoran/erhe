#include "erhe_graphics/shader_stages.hpp"

#include "erhe_graphics/instance.hpp"
#include "erhe_gl/wrapper_functions.hpp"

#include <fmt/format.h>

namespace erhe::graphics
{

using std::string;

auto Reloadable_shader_stages::make_prototype(
    Instance& graphics_instance
) -> Shader_stages_prototype
{
    erhe::graphics::Shader_stages_prototype prototype{graphics_instance, create_info};
    return prototype;
}

Reloadable_shader_stages::Reloadable_shader_stages(
    const std::string& non_functional_name
)
    : create_info  {}
    , shader_stages{non_functional_name}
{
}

Reloadable_shader_stages::Reloadable_shader_stages(
    Instance&                        graphics_instance,
    const Shader_stages_create_info& create_info
)
    : create_info  {create_info}
    , shader_stages{make_prototype(graphics_instance)}
{
}

Reloadable_shader_stages::Reloadable_shader_stages(
    Shader_stages_prototype&& prototype
)
    : create_info  {prototype.create_info()}
    , shader_stages{std::move(prototype)}
{
}

Reloadable_shader_stages::Reloadable_shader_stages(Reloadable_shader_stages&& other)
    : create_info  {std::move(other.create_info)}
    , shader_stages{std::move(other.shader_stages)}
{
}

Reloadable_shader_stages& Reloadable_shader_stages::operator=(Reloadable_shader_stages&& other)
{
    create_info   = std::move(other.create_info);
    shader_stages = std::move(other.shader_stages);
    return *this;
}

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
    ERHE_VERIFY(prototype.m_handle.gl_name() != 0);

    m_name     = prototype.name();
    m_handle   = std::move(prototype.m_handle);
    m_is_valid = true;

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

auto Shader_stages::is_valid() const -> bool
{
    return m_is_valid;
}

void Shader_stages::invalidate()
{
    m_is_valid = false;
}

void Shader_stages::reload(Shader_stages_prototype&& prototype)
{
    if (
        !prototype.is_valid() ||
        (prototype.m_handle.gl_name() == 0)
    ) {
        invalidate();
        return;
    }

    m_handle   = std::move(prototype.m_handle);
    m_is_valid = true;

    std::string label = fmt::format("(P:{}) {}", gl_name(), m_name);
    gl::object_label(
        gl::Object_identifier::program,
        gl_name(),
        static_cast<GLsizei>(label.length()),
        label.c_str()
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
