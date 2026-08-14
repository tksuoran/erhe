#include "erhe_graphics/vulkan/vulkan_acceleration_structure.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_command_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_verify/verify.hpp"

#include <cstring>

namespace erhe::graphics {

namespace {

[[nodiscard]] auto get_buffer_device_address(VkDevice vulkan_device, VkBuffer vk_buffer) -> VkDeviceAddress
{
    const VkBufferDeviceAddressInfo address_info{
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .pNext  = nullptr,
        .buffer = vk_buffer
    };
    return vkGetBufferDeviceAddress(vulkan_device, &address_info);
}

} // anonymous namespace

Acceleration_structure_impl::Acceleration_structure_impl(Device& device, const Acceleration_structure_create_info& create_info)
    : m_device            {device}
    , m_type              {create_info.type}
    , m_debug_label       {create_info.debug_label}
    , m_max_instance_count{create_info.max_instance_count}
{
    ERHE_VERIFY(device.get_info().use_ray_query);

    Device_impl& device_impl   = device.get_impl();
    VkDevice     vulkan_device = device_impl.get_vulkan_device();

    // Per-geometry maximum primitive counts for the size query. For a bottom
    // level structure the counts are also the exact build counts; for a top
    // level structure the size is queried at full instance capacity and the
    // actual count is patched into the range info at build time.
    std::vector<uint32_t> primitive_counts;

    if (m_type == Acceleration_structure_type::bottom_level) {
        ERHE_VERIFY(!create_info.triangle_geometries.empty());
        m_vk_geometries.reserve(create_info.triangle_geometries.size());
        m_vk_ranges    .reserve(create_info.triangle_geometries.size());
        primitive_counts.reserve(create_info.triangle_geometries.size());
        for (const Acceleration_structure_triangles& triangles : create_info.triangle_geometries) {
            ERHE_VERIFY(triangles.vertex_buffer != nullptr);
            ERHE_VERIFY(triangles.index_buffer != nullptr);
            ERHE_VERIFY(triangles.vertex_byte_stride > 0);
            ERHE_VERIFY(triangles.vertex_count > 0);
            ERHE_VERIFY(triangles.index_count > 0);
            ERHE_VERIFY((triangles.index_count % 3) == 0);
            const VkDeviceAddress vertex_address =
                get_buffer_device_address(vulkan_device, triangles.vertex_buffer->get_impl().get_vk_buffer()) +
                triangles.vertex_byte_offset;
            const VkDeviceAddress index_address =
                get_buffer_device_address(vulkan_device, triangles.index_buffer->get_impl().get_vk_buffer()) +
                triangles.index_byte_offset;
            m_vk_geometries.push_back(
                VkAccelerationStructureGeometryKHR{
                    .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                    .pNext        = nullptr,
                    .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
                    .geometry     = {
                        .triangles = VkAccelerationStructureGeometryTrianglesDataKHR{
                            .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
                            .pNext         = nullptr,
                            .vertexFormat  = VK_FORMAT_R32G32B32_SFLOAT,
                            .vertexData    = { .deviceAddress = vertex_address },
                            .vertexStride  = triangles.vertex_byte_stride,
                            .maxVertex     = static_cast<uint32_t>(triangles.vertex_count - 1),
                            .indexType     = VK_INDEX_TYPE_UINT32,
                            .indexData     = { .deviceAddress = index_address },
                            .transformData = { .deviceAddress = 0 }
                        }
                    },
                    .flags = triangles.opaque ? static_cast<VkGeometryFlagsKHR>(VK_GEOMETRY_OPAQUE_BIT_KHR) : VkGeometryFlagsKHR{0}
                }
            );
            const uint32_t triangle_count = static_cast<uint32_t>(triangles.index_count / 3);
            m_vk_ranges.push_back(
                VkAccelerationStructureBuildRangeInfoKHR{
                    .primitiveCount  = triangle_count,
                    .primitiveOffset = 0,
                    .firstVertex     = 0,
                    .transformOffset = 0
                }
            );
            primitive_counts.push_back(triangle_count);
        }
    } else {
        ERHE_VERIFY(m_max_instance_count > 0);
        ERHE_VERIFY(create_info.triangle_geometries.empty());
        m_instance_buffer = std::make_unique<Buffer>(
            device,
            Buffer_create_info{
                .capacity_byte_count               = m_max_instance_count * sizeof(VkAccelerationStructureInstanceKHR),
                .usage                             = Buffer_usage::acceleration_structure_build_input | Buffer_usage::shader_device_address,
                .required_memory_property_bit_mask =
                    Memory_property_flag_bit_mask::host_write |
                    Memory_property_flag_bit_mask::host_coherent |
                    Memory_property_flag_bit_mask::host_persistent,
                .debug_label                       = create_info.debug_label
            }
        );
        const VkDeviceAddress instance_address =
            get_buffer_device_address(vulkan_device, m_instance_buffer->get_impl().get_vk_buffer());
        m_vk_geometries.push_back(
            VkAccelerationStructureGeometryKHR{
                .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                .pNext        = nullptr,
                .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
                .geometry     = {
                    .instances = VkAccelerationStructureGeometryInstancesDataKHR{
                        .sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
                        .pNext           = nullptr,
                        .arrayOfPointers = VK_FALSE,
                        .data            = { .deviceAddress = instance_address }
                    }
                },
                .flags = 0
            }
        );
        m_vk_ranges.push_back(
            VkAccelerationStructureBuildRangeInfoKHR{
                .primitiveCount  = 0, // patched per build
                .primitiveOffset = 0,
                .firstVertex     = 0,
                .transformOffset = 0
            }
        );
        primitive_counts.push_back(m_max_instance_count);
    }

    const VkAccelerationStructureTypeKHR vk_type = (m_type == Acceleration_structure_type::bottom_level)
        ? VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
        : VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    m_vk_build_flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if ((m_type == Acceleration_structure_type::bottom_level) && device.get_info().use_ray_tracing_position_fetch) {
        // Lets ray query shaders read the committed triangle's vertex
        // positions (GL_EXT_ray_tracing_position_fetch).
        m_vk_build_flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_KHR;
    }

    const VkAccelerationStructureBuildGeometryInfoKHR size_query_info{
        .sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .pNext                    = nullptr,
        .type                     = vk_type,
        .flags                    = m_vk_build_flags,
        .mode                     = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .srcAccelerationStructure = VK_NULL_HANDLE,
        .dstAccelerationStructure = VK_NULL_HANDLE,
        .geometryCount            = static_cast<uint32_t>(m_vk_geometries.size()),
        .pGeometries              = m_vk_geometries.data(),
        .ppGeometries             = nullptr,
        .scratchData              = {}
    };
    VkAccelerationStructureBuildSizesInfoKHR size_info{
        .sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
        .pNext                     = nullptr,
        .accelerationStructureSize = 0,
        .updateScratchSize         = 0,
        .buildScratchSize          = 0
    };
    vkGetAccelerationStructureBuildSizesKHR(
        vulkan_device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &size_query_info,
        primitive_counts.data(),
        &size_info
    );

    m_buffer = std::make_unique<Buffer>(
        device,
        Buffer_create_info{
            .capacity_byte_count               = size_info.accelerationStructureSize,
            // shader_device_address: vkGetAccelerationStructureDeviceAddressKHR
            // requires the backing buffer to have
            // VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
            // (VUID-vkGetAccelerationStructureDeviceAddressKHR-pInfo-09542).
            .usage                             = Buffer_usage::acceleration_structure_storage | Buffer_usage::shader_device_address,
            .required_memory_property_bit_mask = Memory_property_flag_bit_mask::device_local,
            .debug_label                       = create_info.debug_label
        }
    );

    const VkAccelerationStructureCreateInfoKHR as_create_info{
        .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .pNext         = nullptr,
        .createFlags   = 0,
        .buffer        = m_buffer->get_impl().get_vk_buffer(),
        .offset        = 0,
        .size          = size_info.accelerationStructureSize,
        .type          = vk_type,
        .deviceAddress = 0
    };
    const VkResult result = vkCreateAccelerationStructureKHR(vulkan_device, &as_create_info, nullptr, &m_acceleration_structure);
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateAccelerationStructureKHR() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    if (!create_info.debug_label.empty()) {
        device_impl.set_debug_label(
            VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR,
            reinterpret_cast<uint64_t>(m_acceleration_structure),
            create_info.debug_label.data()
        );
    }

    const VkAccelerationStructureDeviceAddressInfoKHR as_address_info{
        .sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .pNext                 = nullptr,
        .accelerationStructure = m_acceleration_structure
    };
    m_device_address = vkGetAccelerationStructureDeviceAddressKHR(vulkan_device, &as_address_info);

    // Scratch buffer, over-allocated so the consumed address can be aligned up
    // to minAccelerationStructureScratchOffsetAlignment (VMA gives no direct
    // control over the buffer's device address alignment).
    const VkDeviceSize scratch_alignment =
        device_impl.get_acceleration_structure_properties().minAccelerationStructureScratchOffsetAlignment;
    m_scratch_buffer = std::make_unique<Buffer>(
        device,
        Buffer_create_info{
            .capacity_byte_count               = size_info.buildScratchSize + scratch_alignment,
            .usage                             = Buffer_usage::storage | Buffer_usage::shader_device_address,
            .required_memory_property_bit_mask = Memory_property_flag_bit_mask::device_local,
            .debug_label                       = create_info.debug_label
        }
    );
    const VkDeviceAddress raw_scratch_address =
        get_buffer_device_address(vulkan_device, m_scratch_buffer->get_impl().get_vk_buffer());
    m_scratch_address = (raw_scratch_address + scratch_alignment - 1) & ~(scratch_alignment - 1);
}

Acceleration_structure_impl::~Acceleration_structure_impl() noexcept
{
    const VkAccelerationStructureKHR acceleration_structure = m_acceleration_structure;
    if (acceleration_structure != VK_NULL_HANDLE) {
        m_device.get_impl().add_completion_handler(
            [acceleration_structure](Device_impl& device_impl) {
                vkDestroyAccelerationStructureKHR(device_impl.get_vulkan_device(), acceleration_structure, nullptr);
            }
        );
    }
}

void Acceleration_structure_impl::record_build(Command_buffer& command_buffer, const uint32_t instance_count)
{
    VkCommandBuffer vk_command_buffer = command_buffer.get_impl().get_vulkan_command_buffer();
    ERHE_VERIFY(vk_command_buffer != VK_NULL_HANDLE);

    if (m_type == Acceleration_structure_type::top_level) {
        m_vk_ranges[0].primitiveCount = instance_count;
    }

    const VkAccelerationStructureBuildGeometryInfoKHR build_info{
        .sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .pNext                    = nullptr,
        .type                     = (m_type == Acceleration_structure_type::bottom_level)
            ? VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
            : VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags                    = m_vk_build_flags,
        .mode                     = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .srcAccelerationStructure = VK_NULL_HANDLE,
        .dstAccelerationStructure = m_acceleration_structure,
        .geometryCount            = static_cast<uint32_t>(m_vk_geometries.size()),
        .pGeometries              = m_vk_geometries.data(),
        .ppGeometries             = nullptr,
        .scratchData              = { .deviceAddress = m_scratch_address }
    };
    const VkAccelerationStructureBuildRangeInfoKHR* p_ranges = m_vk_ranges.data();
    vkCmdBuildAccelerationStructuresKHR(vk_command_buffer, 1, &build_info, &p_ranges);

    // Make the built structure visible to subsequent builds (a top level
    // build reading this bottom level) and to ray queries in compute shaders.
    const VkMemoryBarrier2 barrier{
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .pNext         = nullptr,
        .srcStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        .dstStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
    };
    const VkDependencyInfo dependency_info{
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext                    = nullptr,
        .dependencyFlags          = 0,
        .memoryBarrierCount       = 1,
        .pMemoryBarriers          = &barrier,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers    = nullptr,
        .imageMemoryBarrierCount  = 0,
        .pImageMemoryBarriers     = nullptr
    };
    vkCmdPipelineBarrier2(vk_command_buffer, &dependency_info);
}

void Acceleration_structure_impl::build(Command_buffer& command_buffer)
{
    ERHE_VERIFY(m_type == Acceleration_structure_type::bottom_level);
    record_build(command_buffer, 0);
}

void Acceleration_structure_impl::build(Command_buffer& command_buffer, std::span<const Acceleration_structure_instance> instances)
{
    ERHE_VERIFY(m_type == Acceleration_structure_type::top_level);
    ERHE_VERIFY(instances.size() <= m_max_instance_count);

    if (!instances.empty()) {
        const std::size_t byte_count = instances.size() * sizeof(VkAccelerationStructureInstanceKHR);
        std::span<std::byte> map = m_instance_buffer->begin_write(0, byte_count);
        ERHE_VERIFY(map.size() >= byte_count);
        VkAccelerationStructureInstanceKHR* vk_instances = reinterpret_cast<VkAccelerationStructureInstanceKHR*>(map.data());
        for (std::size_t i = 0, end = instances.size(); i < end; ++i) {
            const Acceleration_structure_instance& instance = instances[i];
            ERHE_VERIFY(instance.bottom_level != nullptr);
            ERHE_VERIFY(instance.bottom_level->get_type() == Acceleration_structure_type::bottom_level);
            VkAccelerationStructureInstanceKHR& vk_instance = vk_instances[i];
            // VkTransformMatrixKHR is row major 3x4; glm::mat4 is column major.
            const glm::mat4& transform = instance.transform;
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 4; ++column) {
                    vk_instance.transform.matrix[row][column] = transform[column][row];
                }
            }
            vk_instance.instanceCustomIndex                    = instance.instance_custom_index & 0x00ffffffu;
            vk_instance.mask                                   = instance.mask & 0xffu;
            vk_instance.instanceShaderBindingTableRecordOffset = 0;
            vk_instance.flags                                  = instance.disable_facing_cull ? VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR : 0u;
            vk_instance.accelerationStructureReference         = instance.bottom_level->get_impl().get_device_address();
        }
        m_instance_buffer->end_write(0, byte_count);
    }

    record_build(command_buffer, static_cast<uint32_t>(instances.size()));
}

auto Acceleration_structure_impl::get_type() const -> Acceleration_structure_type
{
    return m_type;
}

auto Acceleration_structure_impl::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_debug_label;
}

auto Acceleration_structure_impl::get_vk_acceleration_structure() const -> VkAccelerationStructureKHR
{
    return m_acceleration_structure;
}

auto Acceleration_structure_impl::get_device_address() const -> VkDeviceAddress
{
    return m_device_address;
}

} // namespace erhe::graphics
