#include "erhe_graphics/null/null_shader_stages.hpp"
#include "erhe_graphics/device.hpp"

namespace erhe::graphics {

Shader_stages_impl::Shader_stages_impl(Device& device, const std::string& non_functional_name)
    : m_device  {device}
    , m_name    {non_functional_name}
    , m_is_valid{false}
{
}

Shader_stages_impl::Shader_stages_impl(Device& device, Shader_stages_prototype&& prototype)
    : m_device{device}
{
    reload(std::move(prototype));
}

Shader_stages_impl::Shader_stages_impl(Shader_stages_impl&& from) noexcept
    : m_device  {from.m_device}
    , m_name    {std::move(from.m_name)}
    , m_is_valid{from.m_is_valid}
{
}

Shader_stages_impl& Shader_stages_impl::operator=(Shader_stages_impl&& from) noexcept
{
    if (this != &from) {
        m_name     = std::move(from.m_name);
        m_is_valid = from.m_is_valid;
    }
    return *this;
}

void Shader_stages_impl::reload(Shader_stages_prototype&& prototype)
{
    if (!prototype.is_valid()) {
        invalidate();
        return;
    }
    m_name     = prototype.name();
    m_is_valid = true;
}

void Shader_stages_impl::invalidate()
{
    m_is_valid = false;
}

auto Shader_stages_impl::get_bind_group_layout() const -> const Bind_group_layout*
{
    return nullptr;
}

auto Shader_stages_impl::name() const -> const std::string&
{
    return m_name;
}

auto Shader_stages_impl::gl_name() const -> unsigned int
{
    return 0;
}

auto Shader_stages_impl::is_valid() const -> bool
{
    return m_is_valid;
}

auto operator==(const Shader_stages_impl& lhs, const Shader_stages_impl& rhs) noexcept -> bool
{
    return (&lhs == &rhs);
}

auto operator!=(const Shader_stages_impl& lhs, const Shader_stages_impl& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

void Shader_stages_tracker::reset()
{
    m_last = 0;
}

void Shader_stages_tracker::execute(const Shader_stages* state)
{
    static_cast<void>(state);
    m_last = 0;
}

auto Shader_stages_tracker::get_draw_id_uniform_location() const -> int
{
    return -1;
}

} // namespace erhe::graphics
