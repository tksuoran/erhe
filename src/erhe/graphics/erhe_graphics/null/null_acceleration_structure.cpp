#include "erhe_graphics/null/null_acceleration_structure.hpp"

namespace erhe::graphics {

Acceleration_structure_impl::Acceleration_structure_impl(Device& device, const Acceleration_structure_create_info& create_info)
    : m_type       {create_info.type}
    , m_debug_label{create_info.debug_label}
{
    static_cast<void>(device);
}

Acceleration_structure_impl::~Acceleration_structure_impl() noexcept = default;

void Acceleration_structure_impl::build(Command_buffer& command_buffer)
{
    static_cast<void>(command_buffer);
}

void Acceleration_structure_impl::build(Command_buffer& command_buffer, std::span<const Acceleration_structure_instance> instances)
{
    static_cast<void>(command_buffer);
    static_cast<void>(instances);
}

auto Acceleration_structure_impl::get_type() const -> Acceleration_structure_type
{
    return m_type;
}

auto Acceleration_structure_impl::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_debug_label;
}

} // namespace erhe::graphics
