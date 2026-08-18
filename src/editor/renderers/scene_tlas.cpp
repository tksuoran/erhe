#include "renderers/scene_tlas.hpp"

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>

namespace editor {

Scene_tlas::Scene_tlas(
    erhe::graphics::Device&            graphics_device,
    erhe::scene_renderer::Mesh_memory& mesh_memory,
    const unsigned int                 instance_record_binding_point,
    const std::string&                 debug_label
)
    : m_graphics_device{graphics_device}
    , m_mesh_memory    {mesh_memory}
    , m_debug_label    {debug_label}
    , m_instance_struct{graphics_device, "Instance_record"}
    , m_instance_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "instance",
            .binding_point = static_cast<int>(instance_record_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
{
    // Per-instance record layout (std430). Mirrors Instance_record_data;
    // verified here so the CPU-side memcpy upload matches the generated GPU
    // layout.
    const std::size_t off_index_address         = m_instance_struct.add_uvec2("index_address"        )->get_offset_in_parent();
    const std::size_t off_vertex_address        = m_instance_struct.add_uvec2("vertex_address"       )->get_offset_in_parent();
    const std::size_t off_position_address      = m_instance_struct.add_uvec2("position_address"     )->get_offset_in_parent();
    const std::size_t off_vertex_stride_uints   = m_instance_struct.add_uint ("vertex_stride_uints"  )->get_offset_in_parent();
    const std::size_t off_position_stride_uints = m_instance_struct.add_uint ("position_stride_uints")->get_offset_in_parent();
    const std::size_t off_material_index        = m_instance_struct.add_uint ("material_index"       )->get_offset_in_parent();
    const std::size_t off_flags                 = m_instance_struct.add_uint ("flags"                )->get_offset_in_parent();
    m_instance_struct.add_uint("reserved0");
    m_instance_struct.add_uint("reserved1");
    ERHE_VERIFY(off_index_address         == offsetof(Instance_record_data, index_address));
    ERHE_VERIFY(off_vertex_address        == offsetof(Instance_record_data, vertex_address));
    ERHE_VERIFY(off_position_address      == offsetof(Instance_record_data, position_address));
    ERHE_VERIFY(off_vertex_stride_uints   == offsetof(Instance_record_data, vertex_stride_uints));
    ERHE_VERIFY(off_position_stride_uints == offsetof(Instance_record_data, position_stride_uints));
    ERHE_VERIFY(off_material_index        == offsetof(Instance_record_data, material_index));
    ERHE_VERIFY(off_flags                 == offsetof(Instance_record_data, flags));
    ERHE_VERIFY(m_instance_struct.get_size_bytes() == sizeof(Instance_record_data));
    m_instance_block.add_struct("instances", &m_instance_struct, erhe::graphics::Shader_resource::unsized_array);

    m_instance_record_buffer = std::make_unique<erhe::graphics::Ring_buffer_client>(
        graphics_device,
        erhe::graphics::Buffer_target::storage,
        m_debug_label + "::instance_records",
        instance_record_binding_point
    );
}

Scene_tlas::~Scene_tlas() noexcept = default;

auto Scene_tlas::get_instance_struct() -> erhe::graphics::Shader_resource&
{
    return m_instance_struct;
}

auto Scene_tlas::get_instance_block() -> erhe::graphics::Shader_resource&
{
    return m_instance_block;
}

auto Scene_tlas::get_instance_count() const -> std::size_t
{
    return m_instances.size();
}

auto Scene_tlas::get_or_create_blas(
    erhe::graphics::Command_buffer&                    command_buffer,
    const std::shared_ptr<erhe::primitive::Primitive>& primitive,
    const erhe::primitive::Buffer_mesh&                buffer_mesh
) -> erhe::graphics::Acceleration_structure*
{
    using namespace erhe::graphics;

    const std::unordered_map<const erhe::primitive::Buffer_mesh*, Blas_entry>::iterator existing = m_blas_cache.find(&buffer_mesh);
    if (existing != m_blas_cache.end()) {
        return existing->second.acceleration_structure.get();
    }

    // The acceleration structure reads triangles straight from the mesh
    // memory pools: stream 0 starts with position (3 x float32 at offset 0)
    // for both the skinned and non-skinned vertex formats, and indices are a
    // uint32 triangle list. Indices are relative to the range start (draws
    // use base_vertex), so baking the range byte offsets into the build
    // addresses matches.
    if (buffer_mesh.vertex_buffer_ranges.empty()) {
        return nullptr;
    }
    const erhe::primitive::Buffer_range& vertex_range = buffer_mesh.vertex_buffer_ranges[0];
    const erhe::primitive::Buffer_range& index_range  = buffer_mesh.index_buffer_range;
    const erhe::primitive::Index_range&  triangles    = buffer_mesh.triangle_fill_indices;
    if ((triangles.index_count == 0) || (triangles.primitive_type != erhe::primitive::Primitive_type::triangles)) {
        return nullptr;
    }
    if ((vertex_range.count == 0) || (index_range.element_size != sizeof(uint32_t))) {
        return nullptr;
    }
    erhe::graphics::Buffer* vertex_buffer = m_mesh_memory.get_vertex_buffer(vertex_range);
    erhe::graphics::Buffer* index_buffer  = m_mesh_memory.get_index_buffer(index_range);
    if ((vertex_buffer == nullptr) || (index_buffer == nullptr)) {
        return nullptr;
    }

    Blas_entry& entry = m_blas_cache[&buffer_mesh];
    entry.primitive = primitive;
    entry.acceleration_structure = std::make_unique<Acceleration_structure>(
        m_graphics_device,
        Acceleration_structure_create_info{
            .type                = Acceleration_structure_type::bottom_level,
            .triangle_geometries = {
                Acceleration_structure_triangles{
                    .vertex_buffer      = vertex_buffer,
                    .vertex_byte_offset = vertex_range.byte_offset,
                    .vertex_byte_stride = vertex_range.element_size,
                    .vertex_count       = vertex_range.count,
                    .index_buffer       = index_buffer,
                    .index_byte_offset  = index_range.byte_offset + (triangles.first_index * index_range.element_size),
                    .index_count        = triangles.index_count,
                    .opaque             = true
                }
            },
            .debug_label = erhe::utility::Debug_label{m_debug_label + " BLAS"}
        }
    );
    entry.acceleration_structure->build(command_buffer);
    entry.built = true;
    return entry.acceleration_structure.get();
}

auto Scene_tlas::update(
    erhe::graphics::Command_buffer& command_buffer,
    const erhe::scene::Mesh_layer&  content_layer
) -> Frame
{
    using namespace erhe::graphics;

    // Gather visible, non-skinned content mesh instances; build missing
    // bottom level structures into this command buffer (their builds are
    // ordered before the top level build below, and Acceleration_structure::
    // build() ends with the build->build barrier). Each instance also gets a
    // record (indexed by instance_custom_index = ordinal) carrying its
    // material index and the device addresses for attribute fetch.
    m_instances.clear();
    m_instance_records.clear();
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : content_layer.meshes) {
        if (!mesh || !mesh->is_visible() || mesh->skin) {
            continue;
        }
        const erhe::scene::Node* node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        const glm::mat4 world_from_node = node->world_from_node();
        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
            if (!mesh_primitive.primitive) {
                continue;
            }
            const erhe::primitive::Buffer_mesh* buffer_mesh = mesh_primitive.primitive->get_renderable_mesh();
            if (buffer_mesh == nullptr) {
                continue;
            }
            Acceleration_structure* blas = get_or_create_blas(command_buffer, mesh_primitive.primitive, *buffer_mesh);
            if (blas == nullptr) {
                continue;
            }
            // Attribute fetch needs stream 1 (normal / texcoords / color);
            // the position-fetch fallback additionally needs stream 0 (the
            // same range the BLAS was built from).
            if (buffer_mesh->vertex_buffer_ranges.size() < 2) {
                continue;
            }
            const erhe::primitive::Buffer_range& position_range  = buffer_mesh->vertex_buffer_ranges[0];
            const erhe::primitive::Buffer_range& attribute_range = buffer_mesh->vertex_buffer_ranges[1];
            const erhe::primitive::Buffer_range& index_range     = buffer_mesh->index_buffer_range;
            erhe::graphics::Buffer* position_buffer  = m_mesh_memory.get_vertex_buffer(position_range);
            erhe::graphics::Buffer* attribute_buffer = m_mesh_memory.get_vertex_buffer(attribute_range);
            erhe::graphics::Buffer* index_buffer     = m_mesh_memory.get_index_buffer(index_range);
            if ((position_buffer == nullptr) || (attribute_buffer == nullptr) || (index_buffer == nullptr)) {
                continue;
            }
            const uint64_t position_base_address  = position_buffer->get_device_address();
            const uint64_t attribute_base_address = attribute_buffer->get_device_address();
            const uint64_t index_base_address     = index_buffer->get_device_address();
            if ((position_base_address == 0) || (attribute_base_address == 0) || (index_base_address == 0) ||
                ((attribute_range.element_size % 4) != 0) || ((position_range.element_size % 4) != 0)) {
                continue;
            }

            const erhe::primitive::Material* material       = mesh_primitive.material.get();
            const uint32_t                   material_index = (material != nullptr) ? material->material_buffer_index : 0u;
            const bool                       transmissive   = (material != nullptr) && (material->data.transmission > 0.0f);

            m_instance_records.push_back(
                Instance_record_data{
                    .index_address         = index_base_address + index_range.byte_offset + (buffer_mesh->triangle_fill_indices.first_index * index_range.element_size),
                    .vertex_address        = attribute_base_address + attribute_range.byte_offset,
                    .position_address      = position_base_address + position_range.byte_offset,
                    .vertex_stride_uints   = static_cast<uint32_t>(attribute_range.element_size / 4),
                    .position_stride_uints = static_cast<uint32_t>(position_range.element_size / 4),
                    .material_index        = material_index,
                    .flags                 = transmissive ? 1u : 0u,
                    .reserved0             = 0u,
                    .reserved1             = 0u
                }
            );
            m_instances.push_back(
                Acceleration_structure_instance{
                    .transform             = world_from_node,
                    .instance_custom_index = static_cast<uint32_t>(m_instances.size()),
                    .mask                  = transmissive ? c_instance_mask_all : (c_instance_mask_all | c_instance_mask_opaque),
                    .bottom_level          = blas
                }
            );
        }
    }

    // Top level structure for this frame-in-flight slot; capacity grows to a
    // high-water mark (steady-state frames re-use the existing structure).
    const std::size_t slot_index = static_cast<std::size_t>(m_graphics_device.get_frame_index() % s_tlas_slot_count);
    Tlas_slot& slot = m_tlas_slots[slot_index];
    const uint32_t required_capacity = static_cast<uint32_t>(m_instances.size());
    if (!slot.acceleration_structure || (slot.capacity < required_capacity)) {
        const uint32_t new_capacity = std::max(64u, std::bit_ceil(required_capacity));
        slot.acceleration_structure = std::make_unique<Acceleration_structure>(
            m_graphics_device,
            Acceleration_structure_create_info{
                .type               = Acceleration_structure_type::top_level,
                .max_instance_count = new_capacity,
                .debug_label        = erhe::utility::Debug_label{m_debug_label + " TLAS"}
            }
        );
        slot.capacity = new_capacity;
    }
    slot.acceleration_structure->build(command_buffer, m_instances);

    // Per-instance records into the ring buffer. Always at least one
    // (zeroed) record so the binding stays valid when the scene is empty.
    const std::size_t record_count      = std::max(m_instance_records.size(), std::size_t{1});
    const std::size_t record_byte_count = record_count * sizeof(Instance_record_data);
    Ring_buffer_range instance_range = m_instance_record_buffer->acquire(Ring_buffer_usage::CPU_write, record_byte_count);
    {
        std::span<std::byte> gpu_data = instance_range.get_span();
        if (m_instance_records.empty()) {
            std::memset(gpu_data.data(), 0, record_byte_count);
        } else {
            std::memcpy(gpu_data.data(), m_instance_records.data(), record_byte_count);
        }
        instance_range.bytes_written(record_byte_count);
        instance_range.close();
    }

    Frame frame{};
    frame.acceleration_structure = slot.acceleration_structure.get();
    frame.instance_records       = std::move(instance_range);
    frame.instance_count         = m_instances.size();
    return frame;
}

void Scene_tlas::bind_instance_records(erhe::graphics::Compute_command_encoder& encoder, const Frame& frame)
{
    m_instance_record_buffer->bind(encoder, frame.instance_records);
}

} // namespace editor
