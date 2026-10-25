// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_scene_renderer/draw_list.hpp"
#include "erhe_scene_renderer/material_set.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_graphics/span.hpp"

#include "erhe_math/math_util.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/align.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtx/matrix_operation.hpp>

#include <algorithm>
#include <cstring>

namespace erhe::scene_renderer {

Primitive_interface::Primitive_interface(erhe::graphics::Device& graphics_device, const int max_primitive_count)
    : primitive_block{
        graphics_device,
        {
            .name          = "primitive",
            .binding_point = primitive_buffer_binding_point,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , primitive_struct{graphics_device, "Primitive"}
    , offsets{
        .world_from_node  = primitive_struct.add_mat4 ("world_from_node"       )->get_offset_in_parent(),
        .normal_transform = primitive_struct.add_mat4 ("world_from_node_normal")->get_offset_in_parent(),
        .color            = primitive_struct.add_vec4 ("color"                 )->get_offset_in_parent(),
        .lightmap_scale_offset = primitive_struct.add_vec4("lightmap_scale_offset")->get_offset_in_parent(),
        .material_index   = primitive_struct.add_uint ("material_index"        )->get_offset_in_parent(),
        .size             = primitive_struct.add_float("size"                  )->get_offset_in_parent(),
        .skinning_factor  = primitive_struct.add_float("skinning_factor"       )->get_offset_in_parent(),
        .base_joint_index = primitive_struct.add_uint ("base_joint_index"      )->get_offset_in_parent(),
        .base_vertex      = primitive_struct.add_uint ("base_vertex"           )->get_offset_in_parent(),
        .position_scale   = primitive_struct.add_vec4 ("position_scale"        )->get_offset_in_parent(),
        .position_offset  = primitive_struct.add_vec4 ("position_offset"       )->get_offset_in_parent(),
        .texcoord_scale   = primitive_struct.add_vec4 ("texcoord_scale"        )->get_offset_in_parent(),
        .texcoord_offset  = primitive_struct.add_vec4 ("texcoord_offset"       )->get_offset_in_parent()
    }
    , max_primitive_count{static_cast<std::size_t>(max_primitive_count)}
    , record_indexing    {graphics_device.get_info().record_indexing}
{
    primitive_block.add_struct("primitives", &primitive_struct, erhe::graphics::Shader_resource::unsized_array);
}

namespace {

// The record color of an object with these item flag bits in a pass whose
// color source is not id_offset, the one rule of every record writer: white
// for mesh_wireframe_color; else the selected color (the active item's own,
// doc/editor/active_item.md D5) for a selected or not hovered object, and
// constant_color1 for a hovered one that is not selected.
[[nodiscard]] auto get_pass_color(const uint64_t flag_bits, const Primitive_interface_settings& settings) -> glm::vec4
{
    if (settings.color_source == Primitive_color_source::mesh_wireframe_color) {
        return glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
    }
    const bool selected = (flag_bits & erhe::Item_flags::selected) != 0u;
    const bool hovered  = (flag_bits & settings.hovered_flag_bits) != 0u;
    return (selected || !hovered)
        ? settings.get_selected_color(flag_bits)
        : settings.constant_color1;
}

// The pass-dependent fields of a draw-list record: color (get_pass_color()
// of the entry's mirrored flag bits) and the constant size.
void patch_pass_fields(
    std::byte* const                    record,
    const Draw_list_entry&              entry,
    const Primitive_interface_settings& settings,
    const Primitive_struct&             offsets
)
{
    const glm::vec4 color = get_pass_color(entry.flag_bits, settings);
    const float     size  = settings.constant_size;
    std::memcpy(record + offsets.color, &color, sizeof(glm::vec4));
    std::memcpy(record + offsets.size,  &size,  sizeof(float));
}

// Settings that need per-mesh evaluation (id offsets, mesh point size /
// line width) cannot be served from the cached records: the passes that use
// them (the id pass, the non-fill primitive modes) take the bucket path, and
// Draw_list_renderer::render() verifies its settings.
[[nodiscard]] auto records_suffice(const Primitive_interface_settings& settings) -> bool
{
    return
        (settings.color_source != Primitive_color_source::id_offset) &&
        (settings.size_source == Primitive_size_source::constant_size);
}

} // anonymous namespace

Primitive_buffer::Primitive_buffer(erhe::graphics::Device& graphics_device, Primitive_interface& primitive_interface)
    : Ring_buffer_client{
        graphics_device,
        primitive_interface.primitive_block.get_binding_target(),
        "Primitive_buffer",
        primitive_interface.primitive_block.get_binding_point()
    }
    , m_primitive_interface{primitive_interface}
{
}

auto Primitive_buffer::Id_range::lock_mesh() const -> std::shared_ptr<erhe::scene::Mesh>
{
    // Only write_primitive() fills the table, always with a Mesh.
    return std::static_pointer_cast<erhe::scene::Mesh>(mesh.lock());
}

void Primitive_buffer::write_primitive(
    erhe::scene::Mesh&                    mesh_ref,
    const Material_set*                   material_source,
    const uint16_t                        mesh_primitive_index,
    const erhe::primitive::Buffer_mesh&   buffer_mesh_ref,
    const erhe::primitive::Primitive_mode primitive_mode,
    const Primitive_interface_settings&   settings,
    uint32_t&                             io_id_offset,
    std::vector<Id_range>* const          id_ranges,
    const std::span<std::byte>            primitive_gpu_data,
    std::size_t&                          write_offset
) const
{
    ERHE_PROFILE_FUNCTION();

    erhe::scene::Mesh* mesh       = &mesh_ref;
    const std::size_t  entry_size = m_primitive_interface.primitive_struct.get_size_bytes();
    const auto&        offsets    = m_primitive_interface.offsets;
    const erhe::scene::Node* node = mesh;
    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
    ERHE_VERIFY(mesh_primitive_index < mesh_primitives.size());
    const erhe::scene::Mesh_primitive& mesh_primitive = mesh_primitives[mesh_primitive_index];
    const erhe::primitive::Primitive*  primitive_ptr  = mesh_primitive.primitive.get();
    ERHE_VERIFY(primitive_ptr != nullptr);
    const erhe::primitive::Buffer_mesh* buffer_mesh = &buffer_mesh_ref;
    const erhe::primitive::Index_range index_range = buffer_mesh->index_range(primitive_mode);
    const uint32_t count = static_cast<uint32_t>(index_range.index_count);
    ERHE_VERIFY(count > 0);

    const glm::mat4 world_from_node      = node->world_from_node();
    const bool      negative_determinant = (node->get_flag_bits() & erhe::Item_flags::negative_determinant) == erhe::Item_flags::negative_determinant;
    constexpr glm::mat4 invert_normal{
        -1.0f,  0.0f,  0.0f, 0.0f,
         0.0f, -1.0f,  0.0f, 0.0f,
         0.0f,  0.0f, -1.0f, 0.0f,
         0.0f,  0.0f,  0.0f, 1.0f
    };
    const glm::mat4 normal_transform_ = glm::transpose(glm::adjugate(world_from_node));
    const glm::mat4 normal_transform  = negative_determinant
        ? invert_normal * normal_transform_
        : normal_transform_;

    const erhe::primitive::Material* material = mesh_primitive.material.get();

    const uint32_t power_of_two = erhe::utility::next_power_of_two(count);
    const uint32_t mask         = power_of_two - 1;
    const uint32_t current_bits = io_id_offset & mask;
    if (current_bits != 0) {
        const uint32_t add = power_of_two - current_bits;
        io_id_offset += add;
    }

    const glm::vec3 id_offset_vec3   = erhe::math::vec3_from_uint(io_id_offset);
    const glm::vec4 id_offset_vec4   = glm::vec4{id_offset_vec3, 0.0f};
    // Slot 0 on a miss, as this path effectively did before slots left the
    // Material object. It is the one writer that can legitimately be handed a
    // material its set has yet to see: the forward set's object references are
    // ENQUEUED from Scene_root::register_mesh (R13) while Scene::register_mesh
    // files the mesh into its layer immediately, so a mesh created after this
    // frame's flush is visible to a composition pass one frame before its
    // materials are referenced. That window is narrow and pre-existing - such
    // a mesh used to render with a stale material_buffer_index, i.e. an
    // arbitrary material - and slot 0 is no worse. The draw-list record
    // writer, whose objects are registered by construction, verifies instead.
    const uint32_t  material_index   = (material_source != nullptr)
        ? material_source->get_slot(material).value_or(0u)
        : 0u;
    const auto&     skin             = mesh->skin;
    const float     skinning_factor  = skin ? 1.0f : 0.0f;
    const uint32_t  base_joint_index = skin ? skin->skin_data.joint_buffer_index : 0;
    const uint32_t  base_vertex      = buffer_mesh->base_vertex();
    const Position_quantization&                 quantization          = buffer_mesh->position_quantization;
    const erhe::primitive::Texcoord_quantization& texcoord_quantization = buffer_mesh->texcoord_quantization;

    using erhe::graphics::as_span;
    const glm::vec4 color = (settings.color_source == Primitive_color_source::id_offset)
        ? id_offset_vec4
        : get_pass_color(mesh->get_flag_bits(), settings);
    const auto color_span = as_span(color);
    const auto size_span =
        (settings.size_source == Primitive_size_source::mesh_point_size) ? as_span(mesh->point_size      ) :
        (settings.size_source == Primitive_size_source::mesh_line_width) ? as_span(mesh->line_width      ) :
                                                                           as_span(settings.constant_size);

    using erhe::graphics::write;
    write(primitive_gpu_data, write_offset + offsets.world_from_node,       as_span(world_from_node ));
    write(primitive_gpu_data, write_offset + offsets.normal_transform,      as_span(normal_transform));
    write(primitive_gpu_data, write_offset + offsets.color,                 color_span               );
    write(primitive_gpu_data, write_offset + offsets.lightmap_scale_offset, as_span(mesh_primitive.lightmap_uv_scale_offset));
    write(primitive_gpu_data, write_offset + offsets.material_index,        as_span(material_index  ));
    write(primitive_gpu_data, write_offset + offsets.size,                  size_span                );
    write(primitive_gpu_data, write_offset + offsets.skinning_factor,       as_span(skinning_factor ));
    write(primitive_gpu_data, write_offset + offsets.base_joint_index,      as_span(base_joint_index));
    write(primitive_gpu_data, write_offset + offsets.base_vertex,           as_span(base_vertex     ));
    write(primitive_gpu_data, write_offset + offsets.position_scale,        as_span(quantization.scale ));
    write(primitive_gpu_data, write_offset + offsets.position_offset,       as_span(quantization.offset));
    write(primitive_gpu_data, write_offset + offsets.texcoord_scale,        as_span(texcoord_quantization.scale ));
    write(primitive_gpu_data, write_offset + offsets.texcoord_offset,       as_span(texcoord_quantization.offset));
    write_offset += entry_size;

    if (id_ranges != nullptr) {
        id_ranges->push_back(
            Id_range{
                .offset                          = io_id_offset,
                .length                          = count,
                .mesh                            = mesh->weak_from_this(),
                .index_of_gltf_primitive_in_mesh = mesh_primitive_index
            }
        );
        io_id_offset += count;
    }
}

auto Primitive_buffer::update(
    const Render_bucket&                bucket,
    const Material_set*                 material_source,
    erhe::primitive::Primitive_mode     primitive_mode,
    const Primitive_interface_settings& settings
) -> erhe::graphics::Ring_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    const std::size_t primitive_count = bucket.entries.size();
    const auto        entry_size      = m_primitive_interface.primitive_struct.get_size_bytes();
    const std::size_t max_byte_count  = primitive_count * entry_size;

    // See note in joint_buffer.cpp: clamp to block size so MoltenVK's Metal
    // argument validation has enough trailing space past the binding offset.
    const std::size_t acquire_byte_count = std::max(max_byte_count, m_primitive_interface.primitive_block.get_size_bytes());

    erhe::graphics::Ring_buffer_range buffer_range       = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, acquire_byte_count);
    std::span<std::byte>              primitive_gpu_data = buffer_range.get_span();
    std::size_t                       write_offset       = 0;
    uint32_t                          id_offset_unused   = 0;

    for (const Mesh_primitive_entry& entry : bucket.entries) {
        ERHE_VERIFY(entry.mesh != nullptr);
        ERHE_VERIFY(entry.buffer_mesh != nullptr);
        write_primitive(
            *entry.mesh,
            material_source,
            entry.mesh_primitive_index,
            *entry.buffer_mesh,
            primitive_mode,
            settings,
            id_offset_unused,
            nullptr,
            primitive_gpu_data,
            write_offset
        );
    }

    buffer_range.bytes_written(write_offset);
    buffer_range.close();
    return buffer_range;
}

void Primitive_buffer::append_records(
    const Render_bucket&                  bucket,
    const Material_set*                   material_source,
    const erhe::primitive::Primitive_mode primitive_mode,
    const Primitive_interface_settings&   settings,
    std::vector<std::byte>&               out
) const
{
    ERHE_PROFILE_FUNCTION();

    const std::size_t entry_size = m_primitive_interface.primitive_struct.get_size_bytes();
    const std::size_t begin      = out.size();
    out.resize(begin + bucket.entries.size() * entry_size);
    const std::span<std::byte> records{out.data() + begin, bucket.entries.size() * entry_size};
    std::size_t write_offset     = 0;
    uint32_t    id_offset_unused = 0;
    for (const Mesh_primitive_entry& entry : bucket.entries) {
        ERHE_VERIFY(entry.mesh != nullptr);
        ERHE_VERIFY(entry.buffer_mesh != nullptr);
        write_primitive(*entry.mesh, material_source, entry.mesh_primitive_index, *entry.buffer_mesh, primitive_mode, settings, id_offset_unused, nullptr, records, write_offset);
    }
    ERHE_VERIFY(write_offset == records.size());
}

void Primitive_buffer::append_records(
    const Render_bucket&                  bucket,
    const Material_set*                   material_source,
    const erhe::primitive::Primitive_mode primitive_mode,
    const Primitive_interface_settings&   settings,
    uint32_t&                             io_id_offset,
    std::vector<Id_range>&                id_ranges,
    std::vector<std::byte>&               out
) const
{
    ERHE_PROFILE_FUNCTION();

    const std::size_t entry_size = m_primitive_interface.primitive_struct.get_size_bytes();
    const std::size_t begin      = out.size();
    out.resize(begin + bucket.entries.size() * entry_size);
    const std::span<std::byte> records{out.data() + begin, bucket.entries.size() * entry_size};
    std::size_t write_offset = 0;
    for (const Mesh_primitive_entry& entry : bucket.entries) {
        ERHE_VERIFY(entry.mesh != nullptr);
        ERHE_VERIFY(entry.buffer_mesh != nullptr);
        write_primitive(*entry.mesh, material_source, entry.mesh_primitive_index, *entry.buffer_mesh, primitive_mode, settings, io_id_offset, &id_ranges, records, write_offset);
    }
    ERHE_VERIFY(write_offset == records.size());
}

auto Primitive_buffer::update(const std::span<const std::byte> records) -> erhe::graphics::Ring_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    // Same acquire size as the Render_bucket overload (see the note in
    // joint_buffer.cpp: at least one block for MoltenVK's argument validation).
    const std::size_t acquire_byte_count = std::max(records.size(), m_primitive_interface.primitive_block.get_size_bytes());
    erhe::graphics::Ring_buffer_range buffer_range = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, acquire_byte_count);
    const std::span<std::byte> gpu_data = buffer_range.get_span();
    ERHE_VERIFY(gpu_data.size() >= records.size());
    if (!records.empty()) {
        std::memcpy(gpu_data.data(), records.data(), records.size());
    }
    buffer_range.bytes_written(records.size());
    buffer_range.close();
    return buffer_range;
}

auto Primitive_buffer::update(
    const Draw_list&                    draw_list,
    const std::size_t                   begin,
    const std::size_t                   end,
    const std::span<const std::uint8_t> entry_passes,
    const Primitive_interface_settings& settings,
    std::size_t&                        out_primitive_count
) -> erhe::graphics::Ring_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(begin <= end);
    ERHE_VERIFY(end <= draw_list.entries.size());
    ERHE_VERIFY(entry_passes.size() == draw_list.entries.size());
    const std::size_t max_primitive_count = end - begin;
    const std::size_t entry_size          = m_primitive_interface.primitive_struct.get_size_bytes();
    const std::size_t max_byte_count      = max_primitive_count * entry_size;
    const std::size_t acquire_byte_count  = std::max(max_byte_count, m_primitive_interface.primitive_block.get_size_bytes());

    erhe::graphics::Ring_buffer_range buffer_range       = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, acquire_byte_count);
    std::span<std::byte>              primitive_gpu_data = buffer_range.get_span();
    std::size_t                       write_offset       = 0;
    std::size_t                       primitive_count    = 0;

    // The draw list owns a complete GPU-layout record per entry
    // (doc/erhe/draw_list_performance_improvements.md); copy it and patch
    // only the pass-dependent color / size. The render phase reads no Mesh
    // here.
    ERHE_VERIFY(records_suffice(settings));
    ERHE_VERIFY(draw_list.primitive_records.size() == draw_list.entries.size() * entry_size);
    const std::byte* records = draw_list.primitive_records.data();
    std::byte*       dst     = primitive_gpu_data.data();
    for (std::size_t i = begin; i < end; ++i) {
        if (entry_passes[i] == 0) {
            continue;
        }
        std::memcpy(dst + write_offset, records + i * entry_size, entry_size);
        patch_pass_fields(dst + write_offset, draw_list.entries[i], settings, m_primitive_interface.offsets);
        write_offset += entry_size;
        ++primitive_count;
    }
    buffer_range.bytes_written(write_offset);
    buffer_range.close();
    out_primitive_count = primitive_count;
    return buffer_range;
}

auto Primitive_buffer::update_records(
    const Draw_list&                    draw_list,
    const std::size_t                   begin,
    const std::size_t                   end,
    const Primitive_interface_settings& settings
) -> erhe::graphics::Ring_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(begin < end);
    ERHE_VERIFY(end <= draw_list.entries.size());
    ERHE_VERIFY(records_suffice(settings));
    const std::size_t entry_size = m_primitive_interface.primitive_struct.get_size_bytes();
    ERHE_VERIFY(draw_list.primitive_records.size() == draw_list.entries.size() * entry_size);
    ERHE_VERIFY((end - begin) <= m_primitive_interface.max_primitive_count);
    const std::size_t byte_count         = (end - begin) * entry_size;
    const std::size_t acquire_byte_count = std::max(byte_count, m_primitive_interface.primitive_block.get_size_bytes());

    erhe::graphics::Ring_buffer_range buffer_range = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, acquire_byte_count);
    const std::span<std::byte>        gpu_data     = buffer_range.get_span();
    ERHE_VERIFY(gpu_data.size() >= byte_count);
    std::byte* const dst = gpu_data.data();
    std::memcpy(dst, draw_list.primitive_records.data() + (begin * entry_size), byte_count);
    for (std::size_t i = begin; i < end; ++i) {
        patch_pass_fields(dst + ((i - begin) * entry_size), draw_list.entries[i], settings, m_primitive_interface.offsets);
    }
    buffer_range.bytes_written(byte_count);
    buffer_range.close();
    return buffer_range;
}

auto Primitive_buffer::update(
    const std::span<const std::shared_ptr<erhe::scene::Node>>& nodes,
    const Primitive_interface_settings&                        primitive_settings
) -> erhe::graphics::Ring_buffer_range
{
    const std::size_t primitive_count = nodes.size();
    const auto        entry_size      = m_primitive_interface.primitive_struct.get_size_bytes();
    const auto&       offsets         = m_primitive_interface.offsets;
    const std::size_t max_byte_count  = primitive_count * entry_size;

    // See note in joint_buffer.cpp.
    const std::size_t acquire_byte_count = std::max(max_byte_count, m_primitive_interface.primitive_block.get_size_bytes());

    erhe::graphics::Ring_buffer_range buffer_range       = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, acquire_byte_count);
    std::span<std::byte>              primitive_gpu_data = buffer_range.get_span();
    std::size_t                       write_offset       = 0;

    for (const auto& node : nodes) {
        const glm::mat4 world_from_node  = node->world_from_node();
        const glm::mat4 normal_transform = glm::transpose(glm::adjugate(world_from_node));
        const glm::vec4 wireframe_color  = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
        const uint32_t  material_index   = 0;
        const float     skinning_factor  = 0.0f;
        const uint32_t  base_joint_index = 0;
        using erhe::graphics::as_span;
        const auto color_span = as_span(primitive_settings.constant_color0);
        const auto size_span  = as_span(primitive_settings.constant_size);
        const glm::vec4 no_lightmap{0.0f};
        using erhe::graphics::write;
        write(primitive_gpu_data, write_offset + offsets.world_from_node,  as_span(world_from_node ));
        write(primitive_gpu_data, write_offset + offsets.normal_transform, as_span(normal_transform));
        write(primitive_gpu_data, write_offset + offsets.color,            color_span               );
        write(primitive_gpu_data, write_offset + offsets.lightmap_scale_offset, as_span(no_lightmap));
        write(primitive_gpu_data, write_offset + offsets.material_index,   as_span(material_index  ));
        write(primitive_gpu_data, write_offset + offsets.size,             size_span                );
        write(primitive_gpu_data, write_offset + offsets.skinning_factor,  as_span(skinning_factor ));
        write(primitive_gpu_data, write_offset + offsets.base_joint_index, as_span(base_joint_index));
        // No Buffer_mesh here (this overload iterates Nodes, and correspondingly
        // never writes base_vertex either), so these draws can only ever be
        // passthrough. Write the identity affine for hygiene.
        const Position_quantization quantization{};
        const erhe::primitive::Texcoord_quantization texcoord_quantization{};
        write(primitive_gpu_data, write_offset + offsets.position_scale,   as_span(quantization.scale ));
        write(primitive_gpu_data, write_offset + offsets.position_offset,  as_span(quantization.offset));
        write(primitive_gpu_data, write_offset + offsets.texcoord_scale,   as_span(texcoord_quantization.scale ));
        write(primitive_gpu_data, write_offset + offsets.texcoord_offset,  as_span(texcoord_quantization.offset));
        write_offset += entry_size;
    }
    buffer_range.bytes_written(write_offset);
    buffer_range.close();
    return buffer_range;
}

} // namespace erhe::scene_renderer
