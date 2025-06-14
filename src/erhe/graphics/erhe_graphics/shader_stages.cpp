#include "erhe_graphics/shader_stages.hpp"

#include "erhe_graphics/instance.hpp"
#include "erhe_gl/command_info.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <sstream>

namespace erhe::graphics {

using std::string;

auto Reloadable_shader_stages::make_prototype(Instance& graphics_instance) -> Shader_stages_prototype
{
    ERHE_PROFILE_FUNCTION();
    erhe::graphics::Shader_stages_prototype prototype{graphics_instance, create_info};
    return prototype;
}

Reloadable_shader_stages::Reloadable_shader_stages(Instance& instance, const std::string& non_functional_name)
    : create_info  {}
    , shader_stages{instance, non_functional_name}
{
}

Reloadable_shader_stages::Reloadable_shader_stages(Instance& graphics_instance, const Shader_stages_create_info& create_info)
    : create_info  {create_info}
    , shader_stages{graphics_instance, make_prototype(graphics_instance)}
{
}

Reloadable_shader_stages::Reloadable_shader_stages(Instance& instance, Shader_stages_prototype&& prototype)
    : create_info  {prototype.create_info()}
    , shader_stages{instance, std::move(prototype)}
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

Shader_stages::Shader_stages(Instance& instance, const std::string& failed_name)
    : m_instance{instance}
    , m_handle  {instance}
    , m_name    {failed_name}
{
    std::string label = fmt::format("(P:{}) {} - compilation failed", gl_name(), failed_name);
    ERHE_VERIFY(!failed_name.empty());
#if defined(ERHE_USE_OPENGL_DIRECT_STATE_ACCESS)
    gl::object_label(gl::Object_identifier::program, gl_name(), static_cast<GLsizei>(label.length()), label.c_str());
#endif
}

Shader_stages::Shader_stages(Shader_stages&& from)
    : m_instance             {from.m_instance                        }
    , m_handle               {std::move(from.m_handle)               }
    , m_name                 {std::move(from.m_name)                 }
    , m_is_valid             {from.m_is_valid                        }
    , m_attached_shaders     {std::move(from.m_attached_shaders)     }
    , m_erhe_draw_id_location{from.m_erhe_draw_id_location           }
    , m_gl_from_erhe_bindings{std::move(from.m_gl_from_erhe_bindings)}
{
}

Shader_stages& Shader_stages::operator=(Shader_stages&& from)
{
    if (*this != from) {
        m_handle                = std::move(from.m_handle)               ;
        m_name                  = std::move(from.m_name)                 ;
        m_is_valid              = from.m_is_valid                        ;
        m_attached_shaders      = std::move(from.m_attached_shaders)     ;
        m_erhe_draw_id_location = from.m_erhe_draw_id_location           ;
        m_gl_from_erhe_bindings = std::move(from.m_gl_from_erhe_bindings);
    }
    return *this;
}

auto Shader_stages::erhe_draw_id_location() const -> int
{
    return m_erhe_draw_id_location;
}

auto Shader_stages::get_gl_from_erhe_bindings() const -> const std::vector<int>*
{
    return m_gl_from_erhe_bindings.empty() ? nullptr : &m_gl_from_erhe_bindings;
}

Shader_stages::Shader_stages(Instance& instance, Shader_stages_prototype&& prototype)
    : m_instance{instance}
    , m_handle  {instance}
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
    m_name                  = prototype.name();
    m_handle                = std::move(prototype.m_handle);
    m_gl_from_erhe_bindings = std::move(prototype.m_gl_from_erhe_bindings);
    m_is_valid              = true;
    if (!prototype.m_graphics_instance.info.use_multi_draw_indirect) {
        m_erhe_draw_id_location = gl::get_uniform_location(gl_name(), "erhe_draw_id");
    }

    std::string label = fmt::format("(P:{}) {}{}", gl_name(), m_name, prototype.is_valid() ? "" : " (Failed)");

#if defined(ERHE_USE_OPENGL_DIRECT_STATE_ACCESS)
    gl::object_label(gl::Object_identifier::program, gl_name(), static_cast<GLsizei>(label.length()), label.c_str());
#endif

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

void Shader_stages_tracker::set_erhe_draw_id(int draw_id)
{
    // TODO move this to instance ERHE_VERIFY(graphics_instance.info.gl_version < 460);
    // ERHE_VERIFY(!gl::is_extension_supported(gl::Extension::Extension_GL_ARB_shader_draw_parameters));

    // Not all programs use draw, so it is quite possible location is -1
    if (m_erhe_draw_id_location != -1) {
        gl::uniform_1i(m_erhe_draw_id_location, draw_id);
    }
}

auto Shader_stages_tracker::gl_binding_point(unsigned int erhe_binding_point) -> unsigned int
{
    if (m_gl_from_erhe_bindings == nullptr) {
        ERHE_FATAL("binding point");
    }
    if (m_gl_from_erhe_bindings->size() <= erhe_binding_point) {
        ERHE_FATAL("binding point");
    }
    return m_gl_from_erhe_bindings->at(erhe_binding_point);
}

void Shader_stages_tracker::execute(const Shader_stages* state)
{
    unsigned int name = (state != nullptr) ? state->gl_name() : 0;
    if (m_last == name) {
        return;
    }
    gl::use_program(name);
    m_last = name;
    m_erhe_draw_id_location = (state != nullptr) ? state->erhe_draw_id_location() : -1;
    m_gl_from_erhe_bindings = (state != nullptr) ? state->get_gl_from_erhe_bindings() : nullptr;

    // NOTE: We would have to keep track of buffer bindings and
    //       apply remapped buffer bindings every time program is switched..
}

} // namespace erhe::graphics
