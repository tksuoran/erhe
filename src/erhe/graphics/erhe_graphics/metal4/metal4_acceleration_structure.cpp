#include "erhe_graphics/metal4/metal4_acceleration_structure.hpp"
#include "erhe_graphics/metal4/metal4_buffer.hpp"
#include "erhe_graphics/metal4/metal4_command_buffer.hpp"
#include "erhe_graphics/metal4/metal4_device.hpp"
#include "erhe_graphics/metal4/metal4_submission.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>
#include <Metal/MTL4AccelerationStructure.hpp> // not in the metal-cpp umbrella header

#include <algorithm>
#include <cstring>

namespace erhe::graphics {

namespace {

// The formats a Metal acceleration structure build accepts for positions.
// Short3Normalized is accepted unconditionally, unlike Vulkan, where the
// 3-component snorm format has to be queried.
[[nodiscard]] auto to_mtl_acceleration_structure_vertex_format(const erhe::dataformat::Format format) -> MTL::AttributeFormat
{
    switch (format) {
        case erhe::dataformat::Format::format_32_vec3_float: return MTL::AttributeFormatFloat3;
        case erhe::dataformat::Format::format_16_vec3_snorm: return MTL::AttributeFormatShort3Normalized;
        // Read in place over a padded stream-0 stride; the build uses xyz only.
        case erhe::dataformat::Format::format_16_vec4_snorm: return MTL::AttributeFormatShort4Normalized;
        default: {
            // Not fatal; see the Vulkan mapping.
            return MTL::AttributeFormatFloat3;
        }
    }
}

// MTLPackedFloat4x3 is three columns of 4 packed floats... i.e. 4 columns of 3:
// columns[c][r]. glm::mat4 is column-major too, so this is a plain truncation
// of the fourth row.
[[nodiscard]] auto to_mtl_packed_float4x3(const glm::mat4& m) -> MTL::PackedFloat4x3
{
    MTL::PackedFloat4x3 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 3; ++row) {
            result.columns[column][row] = m[column][row];
        }
    }
    return result;
}

} // anonymous namespace

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

        // One packed 4x3 per geometry, allocated once for the whole structure and
        // only when something actually needs a transform. The build reads it, so it
        // must outlive this constructor.
        const bool any_transform = std::any_of(
            create_info.triangle_geometries.begin(),
            create_info.triangle_geometries.end(),
            [](const Acceleration_structure_triangles& t) { return t.transform != glm::mat4{1.0f}; }
        );
        if (any_transform) {
            m_transform_buffer = mtl_device->newBuffer(
                create_info.triangle_geometries.size() * sizeof(MTL::PackedFloat4x3),
                MTL::ResourceStorageModeShared
            );
            ERHE_VERIFY(m_transform_buffer != nullptr);
            device_impl.get_residency().add(m_transform_buffer);
        }
        std::size_t geometry_index = 0;
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

            MTL4::AccelerationStructureTriangleGeometryDescriptor* geometry =
                MTL4::AccelerationStructureTriangleGeometryDescriptor::alloc()->init();
            geometry->setVertexBuffer(
                MTL4::BufferRange::Make(
                    vertex_buffer->gpuAddress() + triangles.vertex_byte_offset,
                    vertex_buffer->length() - triangles.vertex_byte_offset
                )
            );
            geometry->setVertexFormat      (to_mtl_acceleration_structure_vertex_format(triangles.vertex_format));
            geometry->setVertexStride      (triangles.vertex_byte_stride);
            geometry->setIndexBuffer(
                MTL4::BufferRange::Make(
                    index_buffer->gpuAddress() + triangles.index_byte_offset,
                    index_buffer->length() - triangles.index_byte_offset
                )
            );
            geometry->setIndexType         (MTL::IndexTypeUInt32);
            geometry->setTriangleCount     (triangles.index_count / 3);
            geometry->setOpaque            (triangles.opaque);
            if (m_transform_buffer != nullptr) {
                const std::size_t transform_byte_offset = geometry_index * sizeof(MTL::PackedFloat4x3);
                const MTL::PackedFloat4x3 mtl_transform = to_mtl_packed_float4x3(triangles.transform);
                std::memcpy(
                    static_cast<std::byte*>(m_transform_buffer->contents()) + transform_byte_offset,
                    &mtl_transform,
                    sizeof(MTL::PackedFloat4x3)
                );
                geometry->setTransformationMatrixBuffer(
                    MTL4::BufferRange::Make(m_transform_buffer->gpuAddress() + transform_byte_offset, sizeof(MTL::PackedFloat4x3))
                );
            }
            ++geometry_index;
            geometry_descriptors.push_back(geometry);
        }

        NS::Array* geometry_array = NS::Array::alloc()->init(
            geometry_descriptors.data(),
            static_cast<NS::UInteger>(geometry_descriptors.size())
        );
        for (NS::Object* geometry : geometry_descriptors) {
            geometry->release(); // retained by the array
        }

        m_primitive_descriptor = MTL4::PrimitiveAccelerationStructureDescriptor::alloc()->init();
        m_primitive_descriptor->setGeometryDescriptors(geometry_array);
        geometry_array->release(); // retained by the descriptor

        sizes = mtl_device->accelerationStructureSizes(m_primitive_descriptor);
    } else {
        ERHE_VERIFY(m_max_instance_count > 0);
        ERHE_VERIFY(create_info.triangle_geometries.empty());

        m_instance_buffer = mtl_device->newBuffer(
            m_max_instance_count * sizeof(MTL::IndirectAccelerationStructureInstanceDescriptor),
            MTL::ResourceStorageModeShared
        );
        ERHE_VERIFY(m_instance_buffer != nullptr);
        device_impl.get_residency().add(m_instance_buffer);

        // Indirect instance descriptors name their bottom level structure by
        // resource id (MTL4 descriptors have no instancedAccelerationStructures
        // array).
        m_instance_descriptor = MTL4::InstanceAccelerationStructureDescriptor::alloc()->init();
        m_instance_descriptor->setInstanceDescriptorType  (MTL::AccelerationStructureInstanceDescriptorTypeIndirect);
        m_instance_descriptor->setInstanceDescriptorBuffer(
            MTL4::BufferRange::Make(m_instance_buffer->gpuAddress(), m_instance_buffer->length())
        );
        m_instance_descriptor->setInstanceDescriptorStride(sizeof(MTL::IndirectAccelerationStructureInstanceDescriptor));
        // Size at full instance capacity; the instance structure's size
        // depends on the instance count, not on which bottom level
        // structures the instances end up referencing.
        m_instance_descriptor->setInstanceCount(m_max_instance_count);

        sizes = mtl_device->accelerationStructureSizes(m_instance_descriptor);
    }

    m_acceleration_structure = mtl_device->newAccelerationStructure(sizes.accelerationStructureSize);
    ERHE_VERIFY(m_acceleration_structure != nullptr);
    m_scratch_buffer = mtl_device->newBuffer(std::max<NS::UInteger>(sizes.buildScratchBufferSize, 16), MTL::ResourceStorageModePrivate);
    ERHE_VERIFY(m_scratch_buffer != nullptr);
    device_impl.get_residency().add(m_acceleration_structure);
    device_impl.get_residency().add(m_scratch_buffer);

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
    MTL::Buffer*                                    transform_buffer       = m_transform_buffer;
    MTL4::PrimitiveAccelerationStructureDescriptor* primitive_descriptor   = m_primitive_descriptor;
    MTL4::InstanceAccelerationStructureDescriptor*  instance_descriptor    = m_instance_descriptor;
    if ((acceleration_structure != nullptr) || (scratch_buffer != nullptr) || (instance_buffer != nullptr) ||
        (transform_buffer != nullptr) ||
        (primitive_descriptor != nullptr) || (instance_descriptor != nullptr)) {
        m_device.get_impl().add_completion_handler(
            [acceleration_structure, scratch_buffer, instance_buffer, transform_buffer, primitive_descriptor, instance_descriptor](Device_impl& device_impl) {
                Residency& residency = device_impl.get_residency();
                if (acceleration_structure != nullptr) { residency.remove(acceleration_structure); acceleration_structure->release(); }
                if (scratch_buffer         != nullptr) { residency.remove(scratch_buffer);         scratch_buffer->release(); }
                if (instance_buffer        != nullptr) { residency.remove(instance_buffer);        instance_buffer->release(); }
                if (transform_buffer       != nullptr) { residency.remove(transform_buffer);       transform_buffer->release(); }
                if (primitive_descriptor   != nullptr) { primitive_descriptor->release(); }
                if (instance_descriptor    != nullptr) { instance_descriptor->release(); }
            }
        );
    }
}

void Acceleration_structure_impl::record_build(Command_buffer& command_buffer)
{
    Scoped_transient_object_pool object_pool{}; // drains the encoder and other autoreleased objects created here
    Command_buffer_impl& cb_impl = command_buffer.get_impl();
    MTL4::CommandBuffer* mtl_cb  = cb_impl.get_mtl4_command_buffer();
    ERHE_VERIFY(mtl_cb != nullptr);

    MTL4::AccelerationStructureDescriptor* descriptor = (m_type == Acceleration_structure_type::bottom_level)
        ? static_cast<MTL4::AccelerationStructureDescriptor*>(m_primitive_descriptor)
        : static_cast<MTL4::AccelerationStructureDescriptor*>(m_instance_descriptor);

    // The queue barrier orders this build after earlier work (vertex/index
    // uploads, earlier bottom level builds feeding a top level build), and
    // the next encoder's barrier orders later work (the ray query compute
    // dispatch) after it - the Metal equivalent of the Vulkan
    // ACCELERATION_STRUCTURE_BUILD -> BUILD | COMPUTE barrier record_build
    // emits there.
    //
    // Note: under Xcode's GPU frame-capture layer acceleration structure
    // encoders of the Metal 3 API crash in endEncoding. Device_impl
    // therefore forces use_ray_query off when the capture layer is loaded,
    // so this code never runs under it.
    MTL4::ComputeCommandEncoder* encoder = mtl_cb->computeCommandEncoder();
    ERHE_VERIFY(encoder != nullptr);
    cb_impl.encode_start_barriers(encoder);
    encoder->buildAccelerationStructure(
        m_acceleration_structure,
        descriptor,
        MTL4::BufferRange::Make(m_scratch_buffer->gpuAddress(), m_scratch_buffer->length())
    );
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

    // Indirect instance descriptors name their bottom level structure by
    // resource id; it is resident through the device's residency set.
    MTL::IndirectAccelerationStructureInstanceDescriptor* mtl_instances =
        static_cast<MTL::IndirectAccelerationStructureInstanceDescriptor*>(m_instance_buffer->contents());
    ERHE_VERIFY(instances.empty() || (mtl_instances != nullptr));
    for (std::size_t i = 0, end = instances.size(); i < end; ++i) {
        const Acceleration_structure_instance& instance = instances[i];
        ERHE_VERIFY(instance.bottom_level != nullptr);
        ERHE_VERIFY(instance.bottom_level->get_type() == Acceleration_structure_type::bottom_level);
        MTL::AccelerationStructure* bottom_level = instance.bottom_level->get_impl().get_mtl_acceleration_structure();
        ERHE_VERIFY(bottom_level != nullptr);

        MTL::IndirectAccelerationStructureInstanceDescriptor& mtl_instance = mtl_instances[i];
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
        mtl_instance.userID                          = instance.instance_custom_index & 0x00ffffffu;
        mtl_instance.accelerationStructureID         = bottom_level->gpuResourceID();
    }
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

} // namespace erhe::graphics
