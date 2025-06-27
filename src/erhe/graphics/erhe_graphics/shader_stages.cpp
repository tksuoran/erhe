#include "erhe_graphics/shader_stages.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_gl/command_info.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <sstream>

namespace erhe::graphics {

using std::string;

auto Reloadable_shader_stages::make_prototype(Device& graphics_device) -> Shader_stages_prototype
{
    ERHE_PROFILE_FUNCTION();
    erhe::graphics::Shader_stages_prototype prototype{graphics_device, create_info};
    return prototype;
}

Reloadable_shader_stages::Reloadable_shader_stages(Device& device, const std::string& non_functional_name)
    : create_info  {}
    , shader_stages{device, non_functional_name}
{
}

Reloadable_shader_stages::Reloadable_shader_stages(Device& graphics_device, const Shader_stages_create_info& create_info)
    : create_info  {create_info}
    , shader_stages{graphics_device, make_prototype(graphics_device)}
{
}

Reloadable_shader_stages::Reloadable_shader_stages(Device& device, Shader_stages_prototype&& prototype)
    : create_info  {prototype.create_info()}
    , shader_stages{device, std::move(prototype)}
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

Shader_stage::Shader_stage(gl::Shader_type type, std::string_view source)
    : type  {type}
    , source{source}
{
}

Shader_stage::Shader_stage(gl::Shader_type type, const std::filesystem::path& path)
    : type {type}
    , paths{path}
{
}

auto Shader_stage::get_description() const -> std::string
{
    std::stringstream ss;
    ss << gl::c_str(type);
    for (const std::filesystem::path& path : paths) {
        ss << " " << path.string();
    }
    return ss.str();
}

auto Shader_stages::name() const -> const std::string&
{
    return m_name;
}

auto Shader_stages::gl_name() const -> unsigned int
{
    return m_handle.gl_name();
}

Shader_stages::Shader_stages(Device& device, const std::string& failed_name)
    : m_device{device}
    , m_handle{device}
    , m_name  {failed_name}
{
    std::string label = fmt::format("(P:{}) {} - compilation failed", gl_name(), failed_name);
    ERHE_VERIFY(!failed_name.empty());
    gl::object_label(
        gl::Object_identifier::program,
        gl_name(),
        -1, //static_cast<GLsizei>(label.length()),
        label.c_str()
    );
}

Shader_stages::Shader_stages(Shader_stages&& from)
    : m_device          {from.m_device                     }
    , m_handle          {std::move(from.m_handle)          }
    , m_name            {std::move(from.m_name)            }
    , m_is_valid        {from.m_is_valid                   }
    , m_attached_shaders{std::move(from.m_attached_shaders)}
{
}

Shader_stages& Shader_stages::operator=(Shader_stages&& from)
{
    if (*this != from) {
        m_handle           = std::move(from.m_handle)          ;
        m_name             = std::move(from.m_name)            ;
        m_is_valid         = from.m_is_valid                   ;
        m_attached_shaders = std::move(from.m_attached_shaders);
    }
    return *this;
}

Shader_stages::Shader_stages(Device& device, Shader_stages_prototype&& prototype)
    : m_device{device}
    , m_handle{device}
{
    reload(std::move(prototype));
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
    ERHE_PROFILE_FUNCTION();

    if (!prototype.is_valid() || (prototype.m_handle.gl_name() == 0)) {
        invalidate();
        return;
    }

    ERHE_VERIFY(!prototype.name().empty());
    m_name     = prototype.name();
    m_handle   = std::move(prototype.m_handle);
    m_is_valid = true;

    const std::string label = fmt::format("(P:{}) {}{}", gl_name(), m_name, prototype.is_valid() ? "" : " (Failed)");
    gl::object_label(
        gl::Object_identifier::program,
        gl_name(),
        -1, //static_cast<GLsizei>(label.length()),
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
    unsigned int name = (state != nullptr) ? state->gl_name() : 0;
    if (m_last == name) {
        return;
    }
    gl::use_program(name);
    m_last = name;
}

} // namespace erhe::graphics
