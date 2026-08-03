#include "erhe_graphics/metal/metal_acceleration_structure.hpp"
#include "erhe_graphics/metal/metal_buffer.hpp"
#include "erhe_graphics/metal/metal_command_buffer.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

#include <unordered_map>

namespace erhe::graphics {

Acceleration_structure_impl::Acceleration_structure_impl(Device& device, const Acceleration_structure_create_info& create_info)
    : m_device            {device}
    , m_type              {create_info.type}
    , m_debug_label       {create_info.debug_label}
    , m_max_instance_count{create_info.max_instance_count}
{
    ERHE_VERIFY(device.get_info().use_ray_query);

    Device_impl& device_impl = device.get_impl();
    MTL::Device* mtl_device  = device_impl.get_mtl_device();
    ERHE_VERIFY(mtl_device != nullptr);

    MTL::AccelerationStructureSizes sizes{};

    if (m_type == Acceleration_structure_type::bottom_level) {
        ERHE_VERIFY(!create_info.triangle_geometries.empty());

        std::vector<NS::Object*> geometry_descriptors;
        geometry_descriptors.reserve(create_info.triangle_geometries.size());
        for (const Acceleration_structure_triangles& triangles : create_info.triangle_geometries) {
            ERHE_VERIFY(triangles.vertex_buffer != nullptr);
            ERHE_VERIFY(triangles.index_buffer != nullptr);
            ERHE_VERIFY(triangles.vertex_byte_stride > 0);
            ERHE_VERIFY(triangles.vertex_count > 0);
            ERHE_VERIFY(triangles.index_count > 0);
            ERHE_VERIFY((triangles.index_count % 3) == 0);
            MTL::Buffer* vertex_buffer = triangles.vertex_buffer->get_impl().get_mtl_buffer();
            MTL::Buffer* index_buffer  = triangles.index_buffer->get_impl().get_mtl_buffer();
            ERHE_VERIFY(vertex_buffer != nullptr);
            ERHE_VERIFY(index_buffer != nullptr);

            MTL::AccelerationStructureTriangleGeometryDescriptor* geometry =
                MTL::AccelerationStructureTriangleGeometryDescriptor::alloc()->init();
            geometry->setVertexBuffer      (vertex_buffer);
            geometry->setVertexBufferOffset(triangles.vertex_byte_offset);
            geometry->setVertexFormat      (MTL::AttributeFormatFloat3);
            geometry->setVertexStride      (triangles.vertex_byte_stride);
            geometry->setIndexBuffer       (index_buffer);
            geometry->setIndexBufferOffset (triangles.index_byte_offset);
            geometry->setIndexType         (MTL::IndexTypeUInt32);
            geometry->setTriangleCount     (triangles.index_count / 3);
            geometry->setOpaque            (triangles.opaque);
            geometry_descriptors.push_back(geometry);
        }

        NS::Array* geometry_array = NS::Array::alloc()->init(
            geometry_descriptors.data(),
            static_cast<NS::UInteger>(geometry_descriptors.size())
        );
        for (NS::Object* geometry : geometry_descriptors) {
            geometry->release(); // retained by the array
        }

        m_primitive_descriptor = MTL::PrimitiveAccelerationStructureDescriptor::alloc()->init();
        m_primitive_descriptor->setGeometryDescriptors(geometry_array);
        geometry_array->release(); // retained by the descriptor

        sizes = mtl_device->accelerationStructureSizes(m_primitive_descriptor);
    } else {
        ERHE_VERIFY(m_max_instance_count > 0);
        ERHE_VERIFY(create_info.triangle_geometries.empty());

        m_instance_buffer = mtl_device->newBuffer(
            m_max_instance_count * sizeof(MTL::AccelerationStructureUserIDInstanceDescriptor),
            MTL::ResourceStorageModeShared
        );
        ERHE_VERIFY(m_instance_buffer != nullptr);

        m_instance_descriptor = MTL::InstanceAccelerationStructureDescriptor::alloc()->init();
        m_instance_descriptor->setInstanceDescriptorType  (MTL::AccelerationStructureInstanceDescriptorTypeUserID);
        m_instance_descriptor->setInstanceDescriptorBuffer(m_instance_buffer);
        m_instance_descriptor->setInstanceDescriptorStride(sizeof(MTL::AccelerationStructureUserIDInstanceDescriptor));
        // Size at full instance capacity; the instance structure's size
        // depends on the instance count, not on which bottom level
        // structures the instances end up referencing, so the (per-build)
        // instancedAccelerationStructures array can stay unset here.
        m_instance_descriptor->setInstanceCount(m_max_instance_count);

        sizes = mtl_device->accelerationStructureSizes(m_instance_descriptor);
    }

    m_acceleration_structure = mtl_device->newAccelerationStructure(sizes.accelerationStructureSize);
    ERHE_VERIFY(m_acceleration_structure != nullptr);
    m_scratch_buffer = mtl_device->newBuffer(sizes.buildScratchBufferSize, MTL::ResourceStorageModePrivate);
    ERHE_VERIFY(m_scratch_buffer != nullptr);

    if (!create_info.debug_label.empty()) {
        NS::String* label = NS::String::alloc()->init(create_info.debug_label.data(), NS::UTF8StringEncoding);
        m_acceleration_structure->setLabel(label);
        m_scratch_buffer->setLabel(label);
        label->release();
    }
}

Acceleration_structure_impl::~Acceleration_structure_impl() noexcept
{
    // Defer releases to frame completion; in-flight command buffers may
    // still reference the structure (matches the Vulkan impl and the
    // Metal pipeline-state deferred-release pattern).
    MTL::AccelerationStructure*                    acceleration_structure = m_acceleration_structure;
    MTL::Buffer*                                   scratch_buffer         = m_scratch_buffer;
    MTL::Buffer*                                   instance_buffer        = m_instance_buffer;
    MTL::PrimitiveAccelerationStructureDescriptor* primitive_descriptor   = m_primitive_descriptor;
    MTL::InstanceAccelerationStructureDescriptor*  instance_descriptor    = m_instance_descriptor;
    if ((acceleration_structure != nullptr) || (scratch_buffer != nullptr) || (instance_buffer != nullptr) ||
        (primitive_descriptor != nullptr) || (instance_descriptor != nullptr)) {
        m_device.get_impl().add_completion_handler(
            [acceleration_structure, scratch_buffer, instance_buffer, primitive_descriptor, instance_descriptor](Device_impl&) {
                if (acceleration_structure != nullptr) { acceleration_structure->release(); }
                if (scratch_buffer         != nullptr) { scratch_buffer->release(); }
                if (instance_buffer        != nullptr) { instance_buffer->release(); }
                if (primitive_descriptor   != nullptr) { primitive_descriptor->release(); }
                if (instance_descriptor    != nullptr) { instance_descriptor->release(); }
            }
        );
    }
}

void Acceleration_structure_impl::record_build(Command_buffer& command_buffer)
{
    Command_buffer_impl& cb_impl = command_buffer.get_impl();
    MTL::CommandBuffer*  mtl_cb  = cb_impl.get_mtl_command_buffer();
    ERHE_VERIFY(mtl_cb != nullptr);

    MTL::AccelerationStructureDescriptor* descriptor = (m_type == Acceleration_structure_type::bottom_level)
        ? static_cast<MTL::AccelerationStructureDescriptor*>(m_primitive_descriptor)
        : static_cast<MTL::AccelerationStructureDescriptor*>(m_instance_descriptor);

    // The inter-encoder fence orders this build after prior encoders on
    // the cb (vertex/index uploads, earlier bottom level builds feeding a
    // top level build) and before subsequent ones (the ray query compute
    // dispatch) - the Metal equivalent of the Vulkan
    // ACCELERATION_STRUCTURE_BUILD -> BUILD | COMPUTE barrier record_build
    // emits there.
    //
    // Note: under Xcode's GPU frame-capture layer ANY acceleration
    // structure encoder crashes in endEncoding (GPUToolsCapture encodes a
    // signal event that throws NSInvalidArgumentException inside the AGX
    // driver; reproduced with a minimal standalone build, fence or no
    // fence). Device_impl therefore forces use_ray_query off when the
    // capture layer is loaded, so this code never runs under it.
    MTL::Fence* fence = cb_impl.get_inter_encoder_fence();
    MTL::AccelerationStructureCommandEncoder* encoder = mtl_cb->accelerationStructureCommandEncoder();
    ERHE_VERIFY(encoder != nullptr);
    if (fence != nullptr) {
        encoder->waitForFence(fence);
    }
    encoder->buildAccelerationStructure(m_acceleration_structure, descriptor, m_scratch_buffer, 0);
    if (fence != nullptr) {
        encoder->updateFence(fence);
    }
    encoder->endEncoding();
}

void Acceleration_structure_impl::build(Command_buffer& command_buffer)
{
    ERHE_VERIFY(m_type == Acceleration_structure_type::bottom_level);
    record_build(command_buffer);
}

void Acceleration_structure_impl::build(Command_buffer& command_buffer, std::span<const Acceleration_structure_instance> instances)
{
    ERHE_VERIFY(m_type == Acceleration_structure_type::top_level);
    ERHE_VERIFY(instances.size() <= m_max_instance_count);

    // Instance descriptors reference bottom level structures by index into
    // the descriptor's instancedAccelerationStructures array; deduplicate
    // (instances routinely share a bottom level) and keep the array
    // contents for useResource() residency at bind time.
    m_referenced_bottom_level.clear();
    std::unordered_map<MTL::AccelerationStructure*, uint32_t> bottom_level_indices;

    MTL::AccelerationStructureUserIDInstanceDescriptor* mtl_instances =
        static_cast<MTL::AccelerationStructureUserIDInstanceDescriptor*>(m_instance_buffer->contents());
    ERHE_VERIFY(instances.empty() || (mtl_instances != nullptr));
    for (std::size_t i = 0, end = instances.size(); i < end; ++i) {
        const Acceleration_structure_instance& instance = instances[i];
        ERHE_VERIFY(instance.bottom_level != nullptr);
        ERHE_VERIFY(instance.bottom_level->get_type() == Acceleration_structure_type::bottom_level);
        MTL::AccelerationStructure* bottom_level = instance.bottom_level->get_impl().get_mtl_acceleration_structure();
        ERHE_VERIFY(bottom_level != nullptr);
        uint32_t bottom_level_index = 0;
        const auto existing = bottom_level_indices.find(bottom_level);
        if (existing != bottom_level_indices.end()) {
            bottom_level_index = existing->second;
        } else {
            bottom_level_index = static_cast<uint32_t>(m_referenced_bottom_level.size());
            bottom_level_indices.emplace(bottom_level, bottom_level_index);
            m_referenced_bottom_level.push_back(bottom_level);
        }

        MTL::AccelerationStructureUserIDInstanceDescriptor& mtl_instance = mtl_instances[i];
        // MTL::PackedFloat4x3 is column major (4 columns of packed float3),
        // the same logical 3x4 object-to-world matrix Vulkan stores row
        // major - copy glm's columns straight in.
        const glm::mat4& transform = instance.transform;
        for (int column = 0; column < 4; ++column) {
            mtl_instance.transformationMatrix.columns[column] = MTL::PackedFloat3(
                transform[column][0],
                transform[column][1],
                transform[column][2]
            );
        }
        // Vulkan (and the GLSL the shaders are written against) defines
        // front facing as counter-clockwise; Metal defaults to clockwise,
        // so set the winding on every instance to keep the
        // gl_RayFlagsCull*FacingTrianglesEXT semantics identical.
        MTL::AccelerationStructureInstanceOptions options =
            MTL::AccelerationStructureInstanceOptionTriangleFrontFacingWindingCounterClockwise;
        if (instance.disable_facing_cull) {
            options |= MTL::AccelerationStructureInstanceOptionDisableTriangleCulling;
        }
        mtl_instance.options                         = options;
        mtl_instance.mask                            = instance.mask & 0xffu;
        mtl_instance.intersectionFunctionTableOffset = 0;
        mtl_instance.accelerationStructureIndex      = bottom_level_index;
        mtl_instance.userID                          = instance.instance_custom_index & 0x00ffffffu;
    }

    NS::Array* bottom_level_array = NS::Array::alloc()->init(
        reinterpret_cast<NS::Object* const*>(m_referenced_bottom_level.data()),
        static_cast<NS::UInteger>(m_referenced_bottom_level.size())
    );
    m_instance_descriptor->setInstancedAccelerationStructures(bottom_level_array);
    bottom_level_array->release(); // retained by the descriptor
    m_instance_descriptor->setInstanceCount(instances.size());

    record_build(command_buffer);
}

auto Acceleration_structure_impl::get_type() const -> Acceleration_structure_type
{
    return m_type;
}

auto Acceleration_structure_impl::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_debug_label;
}

auto Acceleration_structure_impl::get_mtl_acceleration_structure() const -> MTL::AccelerationStructure*
{
    return m_acceleration_structure;
}

auto Acceleration_structure_impl::get_referenced_bottom_level() const -> const std::vector<MTL::AccelerationStructure*>&
{
    return m_referenced_bottom_level;
}

} // namespace erhe::graphics
