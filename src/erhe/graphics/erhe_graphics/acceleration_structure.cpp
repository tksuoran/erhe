#include "erhe_graphics/acceleration_structure.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_graphics/gl/gl_acceleration_structure.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_VULKAN)
# include "erhe_graphics/vulkan/vulkan_acceleration_structure.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_METAL)
# include "erhe_graphics/metal/metal_acceleration_structure.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_NONE)
# include "erhe_graphics/null/null_acceleration_structure.hpp"
#endif

namespace erhe::graphics {

Acceleration_structure::Acceleration_structure(Device& device, const Acceleration_structure_create_info& create_info)
    : m_impl{std::make_unique<Acceleration_structure_impl>(device, create_info)}
{
}

Acceleration_structure::~Acceleration_structure() noexcept = default;

Acceleration_structure::Acceleration_structure(Acceleration_structure&& other) noexcept = default;

auto Acceleration_structure::operator=(Acceleration_structure&& other) noexcept -> Acceleration_structure& = default;

void Acceleration_structure::build(Command_buffer& command_buffer)
{
    m_impl->build(command_buffer);
}

void Acceleration_structure::build(Command_buffer& command_buffer, std::span<const Acceleration_structure_instance> instances)
{
    m_impl->build(command_buffer, instances);
}

auto Acceleration_structure::get_type() const -> Acceleration_structure_type
{
    return m_impl->get_type();
}

auto Acceleration_structure::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_impl->get_debug_label();
}

auto Acceleration_structure::get_impl() -> Acceleration_structure_impl&
{
    return *m_impl;
}

auto Acceleration_structure::get_impl() const -> const Acceleration_structure_impl&
{
    return *m_impl;
}

} // namespace erhe::graphics
