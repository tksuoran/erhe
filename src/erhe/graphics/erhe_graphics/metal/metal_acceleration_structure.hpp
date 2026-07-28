#pragma once

#include "erhe_graphics/acceleration_structure.hpp"

namespace erhe::graphics {

class Command_buffer;
class Device;

// Metal backend: GPU ray tracing not implemented yet. The public API is
// designed to map onto MTLPrimitiveAccelerationStructureDescriptor /
// MTLInstanceAccelerationStructureDescriptor + MSL intersector; until that
// exists Device_info::use_ray_query stays false and these stubs keep shared
// code compiling.
class Acceleration_structure_impl final
{
public:
    Acceleration_structure_impl(Device& device, const Acceleration_structure_create_info& create_info);
    ~Acceleration_structure_impl() noexcept;
    Acceleration_structure_impl(const Acceleration_structure_impl&) = delete;
    Acceleration_structure_impl& operator=(const Acceleration_structure_impl&) = delete;
    Acceleration_structure_impl(Acceleration_structure_impl&&) = delete;
    Acceleration_structure_impl& operator=(Acceleration_structure_impl&&) = delete;

    void build(Command_buffer& command_buffer);
    void build(Command_buffer& command_buffer, std::span<const Acceleration_structure_instance> instances);

    [[nodiscard]] auto get_type       () const -> Acceleration_structure_type;
    [[nodiscard]] auto get_debug_label() const -> erhe::utility::Debug_label;

private:
    Acceleration_structure_type m_type;
    erhe::utility::Debug_label  m_debug_label;
};

} // namespace erhe::graphics
