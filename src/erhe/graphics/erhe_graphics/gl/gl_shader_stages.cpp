#include "erhe_graphics/gl/gl_shader_stages.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::graphics {

using std::string;

Shader_stages_impl::Shader_stages_impl(Device& device, const std::string& failed_name)
    : m_device{device}
    , m_name  {failed_name}
{
    const std::string label = fmt::format("(P:{}) {} - compilation failed", gl_name(), failed_name);
    ERHE_VERIFY(!failed_name.empty());
    gl::object_label(gl::Object_identifier::program, gl_name(), -1, label.c_str());
}

Shader_stages_impl::Shader_stages_impl(Shader_stages_impl&& from) noexcept
    : m_device          {from.m_device                     }
    , m_handle          {std::move(from.m_handle)          }
    , m_name            {std::move(from.m_name)            }
    , m_is_valid        {from.m_is_valid                   }
    , m_attached_shaders{std::move(from.m_attached_shaders)}
{
}

Shader_stages_impl& Shader_stages_impl::operator=(Shader_stages_impl&& from) noexcept
{
    if (*this != from) {
        m_handle           = std::move(from.m_handle)          ;
        m_name             = std::move(from.m_name)            ;
        m_is_valid         = from.m_is_valid                   ;
        m_attached_shaders = std::move(from.m_attached_shaders);
    }
    return *this;
}

Shader_stages_impl::Shader_stages_impl(Device& device, Shader_stages_prototype&& prototype)
    : m_device{device}
{
    reload(std::move(prototype));
}

auto Shader_stages_impl::is_valid() const -> bool
{
    return m_is_valid;
}

void Shader_stages_impl::invalidate()
{
    m_is_valid = false;
}

auto Shader_stages_impl::name() const -> const std::string&
{
    return m_name;
}

auto Shader_stages_impl::gl_name() const -> unsigned int
{
    return m_handle.gl_name();
}

void Shader_stages_impl::reload(Shader_stages_prototype&& prototype)
{
    ERHE_PROFILE_FUNCTION();

    if (!prototype.is_valid() || (prototype.get_impl().m_handle.gl_name() == 0)) {
        invalidate();
        return;
    }

    ERHE_VERIFY(!prototype.name().empty());
    m_name     = prototype.name();
    m_handle   = std::move(prototype.get_impl().m_handle);
    m_is_valid = true;

    const std::string label = fmt::format("(P:{}) {}{}", gl_name(), m_name, prototype.is_valid() ? "" : " (Failed)");
    gl::object_label(gl::Object_identifier::program, gl_name(), -1, label.c_str());
}

auto operator==(const Shader_stages_impl& lhs, const Shader_stages_impl& rhs) noexcept -> bool
{
    return lhs.gl_name() == rhs.gl_name();
}

auto operator!=(const Shader_stages_impl& lhs, const Shader_stages_impl& rhs) noexcept -> bool
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
    unsigned int name = (state != nullptr) ? state->get_impl().gl_name() : 0;
    if (m_last == name) {
        return;
    }
    gl::use_program(name);
    m_last = name;
}

auto Shader_stages_tracker::get_draw_id_uniform_location() const -> int
{
    return m_last != 0 ? gl::get_uniform_location(m_last, "ERHE_DRAW_ID") : -1;
}

} // namespace erhe::graphics
