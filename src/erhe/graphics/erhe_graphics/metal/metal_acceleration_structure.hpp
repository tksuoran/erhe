#pragma once

#include "erhe_graphics/acceleration_structure.hpp"

#include <vector>

namespace MTL {
    class AccelerationStructure;
    class Buffer;
    class InstanceAccelerationStructureDescriptor;
    class PrimitiveAccelerationStructureDescriptor;
}

namespace erhe::graphics {

class Command_buffer;
class Device;

// Reserved Metal buffer index for the (single) top level acceleration
// structure a ray query shader binds. UBOs/SSBOs identity-map their GLSL
// binding to the Metal buffer index (see metal_shader_stages_prototype),
// so the TLAS cannot share that namespace; compile_spirv_to_mtl_function
// remaps every SPIR-V acceleration structure resource here and
// Compute_command_encoder_impl::set_acceleration_structure binds the same
// slot. 13 sits next to the reserved argument buffer (14) and push
// constant (15) indices.
constexpr uint32_t c_metal_acceleration_structure_buffer_index = 13;

// Metal backend: bottom level structures build from
// MTL::PrimitiveAccelerationStructureDescriptor (one triangle geometry
// descriptor per Acceleration_structure_triangles), top level structures
// from MTL::InstanceAccelerationStructureDescriptor with userID instance
// descriptors so InstanceCustomIndex surfaces as
// get_committed_user_instance_id() in the SPIRV-Cross generated MSL.
// build() records into an MTL::AccelerationStructureCommandEncoder on the
// caller's command buffer, serialized against the cb's other encoders via
// the inter-encoder fence (this stands in for the Vulkan build barrier the
// public API promises).
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

    [[nodiscard]] auto get_mtl_acceleration_structure() const -> MTL::AccelerationStructure*;
    // Unique bottom level structures referenced by the last top level
    // build. Metal makes a directly-bound instance structure resident but
    // not the primitive structures it references; the compute encoder
    // useResource()s each of these at set_acceleration_structure time.
    [[nodiscard]] auto get_referenced_bottom_level() const -> const std::vector<MTL::AccelerationStructure*>&;

private:
    void record_build(Command_buffer& command_buffer);

    Device&                     m_device;
    Acceleration_structure_type m_type;
    erhe::utility::Debug_label  m_debug_label;
    uint32_t                    m_max_instance_count{0};

    MTL::AccelerationStructure*                    m_acceleration_structure{nullptr};
    MTL::Buffer*                                   m_scratch_buffer        {nullptr};
    MTL::Buffer*                                   m_instance_buffer       {nullptr}; // top level only, host visible
    MTL::PrimitiveAccelerationStructureDescriptor* m_primitive_descriptor  {nullptr}; // bottom level only
    MTL::InstanceAccelerationStructureDescriptor*  m_instance_descriptor   {nullptr}; // top level only
    std::vector<MTL::AccelerationStructure*>       m_referenced_bottom_level{};
};

} // namespace erhe::graphics
