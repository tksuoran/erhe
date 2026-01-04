#include "erhe_graphics/vulkan/vulkan_shader_stages.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <sstream>

namespace erhe::graphics {

using std::string;

Shader_stages_impl::Shader_stages_impl(Device& device, const std::string& failed_name)
    : m_device{device}
{
    m_name = fmt::format("{} - compilation failed", failed_name);
    ERHE_VERIFY(!failed_name.empty());
}

Shader_stages_impl::Shader_stages_impl(Shader_stages_impl&& from)
    : m_device{from.m_device}
{
    ERHE_FATAL("Not implemented");
}

Shader_stages_impl& Shader_stages_impl::operator=(Shader_stages_impl&& from)
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(from);
    return *this;
}

Shader_stages_impl::Shader_stages_impl(Device& device, Shader_stages_prototype&& prototype)
    : m_device{device}
{
    reload(std::move(prototype));
}

auto Shader_stages_impl::is_valid() const -> bool
{
    return false;
}

void Shader_stages_impl::invalidate()
{
}

auto Shader_stages_impl::name() const -> const std::string&
{
    return m_name;
}

void Shader_stages_impl::reload(Shader_stages_prototype&& prototype)
{
    static_cast<void>(prototype);
}

auto operator==(const Shader_stages_impl& lhs, const Shader_stages_impl& rhs) noexcept -> bool
{
    static_cast<void>(lhs);
    static_cast<void>(rhs);
    return false;
}

auto operator!=(const Shader_stages_impl& lhs, const Shader_stages_impl& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

void Shader_stages_tracker::reset()
{
}

void Shader_stages_tracker::execute(const Shader_stages* state)
{
    static_cast<void>(state);
}

} // namespace erhe::graphics
