#include "renderers/scene_tlas.hpp"
#include "renderers/ray_trace_scene.hpp"

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>

namespace editor {

Scene_tlas::Scene_tlas(
    erhe::graphics::Device& graphics_device,
    const unsigned int      instance_record_binding_point,
    const std::string&      debug_label
)
    : m_graphics_device{graphics_device}
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
    const std::size_t off_position_stride_bytes = m_instance_struct.add_uint ("position_stride_bytes")->get_offset_in_parent();
    const std::size_t off_material_index        = m_instance_struct.add_uint ("material_index"       )->get_offset_in_parent();
    const std::size_t off_flags                 = m_instance_struct.add_uint ("flags"                )->get_offset_in_parent();
    const std::size_t off_position_encoding     = m_instance_struct.add_uint ("position_encoding"    )->get_offset_in_parent();
    m_instance_struct.add_uint("reserved0");
    const std::size_t off_position_scale        = m_instance_struct.add_vec4 ("position_scale"       )->get_offset_in_parent();
    const std::size_t off_position_offset       = m_instance_struct.add_vec4 ("position_offset"      )->get_offset_in_parent();
    ERHE_VERIFY(off_index_address         == offsetof(Instance_record_data, index_address));
    ERHE_VERIFY(off_vertex_address        == offsetof(Instance_record_data, vertex_address));
    ERHE_VERIFY(off_position_address      == offsetof(Instance_record_data, position_address));
    ERHE_VERIFY(off_vertex_stride_uints   == offsetof(Instance_record_data, vertex_stride_uints));
    ERHE_VERIFY(off_position_stride_bytes == offsetof(Instance_record_data, position_stride_bytes));
    ERHE_VERIFY(off_material_index        == offsetof(Instance_record_data, material_index));
    ERHE_VERIFY(off_flags                 == offsetof(Instance_record_data, flags));
    ERHE_VERIFY(off_position_encoding     == offsetof(Instance_record_data, position_encoding));
    ERHE_VERIFY(off_position_scale        == offsetof(Instance_record_data, position_scale));
    ERHE_VERIFY(off_position_offset       == offsetof(Instance_record_data, position_offset));
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

auto Scene_tlas::update(erhe::graphics::Command_buffer& command_buffer, const Ray_trace_slot& slot) -> Frame
{
    using namespace erhe::graphics;

    // The slot is the prepare phase's view of the scene for this frame
    // (Ray_trace_scene::get_slot() verifies the frame and the phase).
    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);

    // Bottom level structures: one per geometry, shared by every consumer.
    // The first consumer to trace a new geometry builds it into this command
    // buffer; the builds are ordered before the top level build below
    // (Acceleration_structure::build() ends with the build->build barrier).
    for (const std::shared_ptr<Ray_trace_geometry>& geometry : slot.geometries) {
        if (geometry->blas) {
            continue;
        }
        Acceleration_structure_create_info create_info{
            .type                = Acceleration_structure_type::bottom_level,
            .triangle_geometries = { geometry->triangles },
            .debug_label         = erhe::utility::Debug_label{"Ray_trace_scene BLAS"}
        };
        geometry->blas = std::make_unique<Acceleration_structure>(m_graphics_device, create_info);
        geometry->blas->build(command_buffer);
    }

    // Instance custom index = instance ordinal = record index.
    m_instances.clear();
    for (std::size_t i = 0, end = slot.instances.size(); i < end; ++i) {
        const Ray_trace_slot_instance& instance = slot.instances[i];
        m_instances.push_back(
            Acceleration_structure_instance{
                .transform             = instance.transform,
                .instance_custom_index = static_cast<uint32_t>(i),
                .mask                  = instance.mask,
                .bottom_level          = slot.geometries[instance.geometry_index]->blas.get()
            }
        );
    }

    // Top level structure for this frame-in-flight slot; capacity grows to a
    // high-water mark (steady-state frames re-use the existing structure).
    const std::size_t slot_index = static_cast<std::size_t>(m_graphics_device.get_frame_index() % s_tlas_slot_count);
    Tlas_slot& tlas_slot = m_tlas_slots[slot_index];
    const uint32_t required_capacity = static_cast<uint32_t>(m_instances.size());
    if (!tlas_slot.acceleration_structure || (tlas_slot.capacity < required_capacity)) {
        const uint32_t new_capacity = std::max(64u, std::bit_ceil(required_capacity));
        tlas_slot.acceleration_structure = std::make_unique<Acceleration_structure>(
            m_graphics_device,
            Acceleration_structure_create_info{
                .type               = Acceleration_structure_type::top_level,
                .max_instance_count = new_capacity,
                .debug_label        = erhe::utility::Debug_label{m_debug_label + " TLAS"}
            }
        );
        tlas_slot.capacity = new_capacity;
    }
    tlas_slot.acceleration_structure->build(command_buffer, m_instances);

    // Per-instance records into the ring buffer. Always at least one
    // (zeroed) record so the binding stays valid when the scene is empty.
    const std::size_t record_count      = std::max(slot.records.size(), std::size_t{1});
    const std::size_t record_byte_count = record_count * sizeof(Instance_record_data);
    Ring_buffer_range instance_range = m_instance_record_buffer->acquire(Ring_buffer_usage::CPU_write, record_byte_count);
    {
        std::span<std::byte> gpu_data = instance_range.get_span();
        if (slot.records.empty()) {
            std::memset(gpu_data.data(), 0, record_byte_count);
        } else {
            std::memcpy(gpu_data.data(), slot.records.data(), record_byte_count);
        }
        instance_range.bytes_written(record_byte_count);
        instance_range.close();
    }

    Frame frame{};
    frame.acceleration_structure = tlas_slot.acceleration_structure.get();
    frame.instance_records       = std::move(instance_range);
    frame.instance_count         = m_instances.size();
    return frame;
}

void Scene_tlas::bind_instance_records(erhe::graphics::Compute_command_encoder& encoder, const Frame& frame)
{
    m_instance_record_buffer->bind(encoder, frame.instance_records);
}

} // namespace editor
