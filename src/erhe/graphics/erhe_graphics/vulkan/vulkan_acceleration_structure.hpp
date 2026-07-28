#pragma once

#include "erhe_graphics/acceleration_structure.hpp"
#include "erhe_graphics/buffer.hpp"

#include "volk.h"

#include <memory>
#include <vector>

namespace erhe::graphics {

class Command_buffer;
class Device;

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

    [[nodiscard]] auto get_type                     () const -> Acceleration_structure_type;
    [[nodiscard]] auto get_debug_label              () const -> erhe::utility::Debug_label;
    [[nodiscard]] auto get_vk_acceleration_structure() const -> VkAccelerationStructureKHR;

    // Device address of the acceleration structure itself (as consumed by
    // VkAccelerationStructureInstanceKHR::accelerationStructureReference).
    [[nodiscard]] auto get_device_address           () const -> VkDeviceAddress;

private:
    void record_build(Command_buffer& command_buffer, uint32_t instance_count);

    Device&                     m_device;
    Acceleration_structure_type m_type;
    erhe::utility::Debug_label  m_debug_label;
    uint32_t                    m_max_instance_count{0};

    std::unique_ptr<Buffer>     m_buffer;          // acceleration structure storage
    std::unique_ptr<Buffer>     m_scratch_buffer;  // build scratch (device local)
    std::unique_ptr<Buffer>     m_instance_buffer; // top level only, host visible persistent
    VkAccelerationStructureKHR  m_acceleration_structure{VK_NULL_HANDLE};
    VkDeviceAddress             m_device_address {0};
    VkDeviceAddress             m_scratch_address{0}; // aligned to minAccelerationStructureScratchOffsetAlignment

    // Build flags used both for the size query and every recorded build --
    // the two must match per VUID-vkCmdBuildAccelerationStructuresKHR-pInfos-03758.
    VkBuildAccelerationStructureFlagsKHR m_vk_build_flags{0};

    // Persistent build description. Geometry (device addresses, strides,
    // counts) is fixed at creation; only the top level primitiveCount is
    // patched per build.
    std::vector<VkAccelerationStructureGeometryKHR>       m_vk_geometries;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> m_vk_ranges;
};

} // namespace erhe::graphics
