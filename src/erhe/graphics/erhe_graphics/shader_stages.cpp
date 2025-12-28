#include "erhe_graphics/shader_stages.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_shader_stages.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_shader_stages.hpp"
#endif

#include "erhe_graphics/device.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <sstream>

namespace erhe::graphics {

using std::string;

auto Reloadable_shader_stages::make_prototype(Device& device) -> Shader_stages_prototype
{
    ERHE_PROFILE_FUNCTION();
    Shader_stages_prototype prototype{device, create_info};
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

//
//
//

Shader_stage::Shader_stage(Shader_type type, std::string_view source)
    : type  {type}
    , source{source}
{
}

Shader_stage::Shader_stage(Shader_type type, const std::filesystem::path& path)
    : type {type}
    , paths{path}
{
}

auto Shader_stage::get_description() const -> std::string
{
    std::stringstream ss;
    ss << c_str(type);
    for (const std::filesystem::path& path : paths) {
        ss << " " << path.string();
    }
    return ss.str();
}

//

Shader_stages::Shader_stages(Device& device, const std::string& failed_name)
    : m_impl{std::make_unique<Shader_stages_impl>(device, failed_name)}
{
}

Shader_stages::~Shader_stages() noexcept = default;
Shader_stages::Shader_stages(Shader_stages&&) = default;
Shader_stages& Shader_stages::operator=(Shader_stages&&) = default;
//Shader_stages& Shader_stages::operator=(Shader_stages&& from)
//{
//    if (*this != from) {
//        m_impl = std::move(from.m_impl);
//    }
//    return *this;
//}
Shader_stages::Shader_stages(Device& device, Shader_stages_prototype&& prototype)
    : m_impl{std::make_unique<Shader_stages_impl>(device, std::move(prototype))}
{
}
auto Shader_stages::name() const -> const std::string&
{
    return m_impl->name();
}
auto Shader_stages::is_valid() const -> bool
{
    return m_impl->is_valid();
}
void Shader_stages::invalidate()
{
    m_impl->invalidate();
}
auto Shader_stages::get_impl() -> Shader_stages_impl&
{
    return *m_impl.get();
}
auto Shader_stages::get_impl() const -> const Shader_stages_impl&
{
    return *m_impl.get();
}
void Shader_stages::reload(Shader_stages_prototype&& prototype)
{
    m_impl->reload(std::move(prototype));
}


/// auto operator==(const Shader_stages& lhs, const Shader_stages& rhs) noexcept -> bool
/// {
///     return lhs.m_impl == rhs.m_impl;
/// }
/// 
/// auto operator!=(const Shader_stages& lhs, const Shader_stages& rhs) noexcept -> bool
/// {
///     return !(lhs == rhs);
/// }


Shader_stages_prototype::Shader_stages_prototype(Device& device, Shader_stages_create_info&& create_info)
    : m_impl{std::make_unique<Shader_stages_prototype_impl>(device, std::move(create_info))}
{
}
Shader_stages_prototype::Shader_stages_prototype(Device& device, const Shader_stages_create_info& create_info)
    : m_impl{std::make_unique<Shader_stages_prototype_impl>(device, create_info)}
{
}
Shader_stages_prototype::~Shader_stages_prototype() noexcept = default;
Shader_stages_prototype::Shader_stages_prototype(Shader_stages_prototype&&) = default;

void Shader_stages_prototype::compile_shaders()
{
    m_impl->compile_shaders();
}
auto Shader_stages_prototype::link_program() -> bool
{
    return m_impl->link_program();
}
auto Shader_stages_prototype::name() const -> const std::string&
{
    return m_impl->name();
}
auto Shader_stages_prototype::create_info() const -> const Shader_stages_create_info&
{
    return m_impl->create_info();
}
auto Shader_stages_prototype::is_valid() -> bool
{
    return m_impl->is_valid();
}
auto Shader_stages_prototype::get_final_source(const Shader_stage& shader, std::optional<unsigned int> gl_name) -> std::string
{
    return m_impl->get_final_source(shader, gl_name);
}
auto Shader_stages_prototype::get_impl() -> Shader_stages_prototype_impl&
{
    return *m_impl.get();
}
auto Shader_stages_prototype::get_impl() const -> const Shader_stages_prototype_impl&
{
    return *m_impl.get();
}

} // namespace erhe::graphics
