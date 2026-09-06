// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_scene_renderer/draw_list.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"
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

#include <cstring>

namespace erhe::scene_renderer {

auto get_position_quantization(const erhe::math::Aabb& bounding_box) -> Position_quantization
{
    if (!bounding_box.is_valid()) {
        return Position_quantization{};
    }
    // Small enough that it never perturbs a real extent, large enough to keep
    // the encoder's division finite on a degenerate (zero thickness) axis.
    constexpr float epsilon = 1e-6f;
    const glm::vec3 center      = bounding_box.center();
    const glm::vec3 half_extent = 0.5f * bounding_box.diagonal();
    const glm::vec3 scale       = glm::max(half_extent, glm::vec3{epsilon});
    return Position_quantization{
        .scale  = glm::vec4{scale,  0.0f},
        .offset = glm::vec4{center, 0.0f}
    };
}

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
{
    primitive_block.add_struct("primitives", &primitive_struct, erhe::graphics::Shader_resource::unsized_array);
}

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

void Primitive_buffer::reset_id_ranges()
{
    // Start at 1, not 0: the Id_renderer clears its color attachment to 0,
    // so id 0 is the "background / no hit" sentinel. If the first range
    // began at offset 0 every background pixel would falsely match it.
    // Range offsets only ever increase from here (the per-mesh alignment
    // below only adds), so no range can include 0.
    m_id_offset = 1;
    m_id_ranges.clear();
}

auto Primitive_buffer::id_offset() const -> uint32_t
{
    return m_id_offset;
}

auto Primitive_buffer::id_ranges() const -> const std::vector<Id_range>&
{
    return m_id_ranges;
}

void Primitive_buffer::write_primitive(
    erhe::scene::Mesh&                    mesh_ref,
    const Material_set*                   material_source,
    const uint16_t                        mesh_primitive_index,
    const erhe::primitive::Buffer_mesh&   buffer_mesh_ref,
    const erhe::primitive::Primitive_mode primitive_mode,
    const Primitive_interface_settings&   settings,
    const bool                            use_id_ranges,
    const std::span<std::byte>            primitive_gpu_data,
    std::size_t&                          write_offset
)
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

    const bool      use_primary_color    = mesh->is_selected() || !mesh->is_hovered();
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
    const uint32_t current_bits = m_id_offset & mask;
    if (current_bits != 0) {
        const uint32_t add = power_of_two - current_bits;
        m_id_offset += add;
    }

    const glm::vec4 wireframe_color  = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
    const glm::vec3 id_offset_vec3   = erhe::math::vec3_from_uint(m_id_offset);
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
    const Position_quantization quantization = get_position_quantization(buffer_mesh->bounding_box);
    const erhe::primitive::Texcoord_quantization texcoord_quantization = erhe::primitive::get_texcoord_quantization(*buffer_mesh);

    using erhe::graphics::as_span;
    const auto color_span =
        (settings.color_source == Primitive_color_source::id_offset           ) ? as_span(id_offset_vec4 ) :
        (settings.color_source == Primitive_color_source::mesh_wireframe_color) ? as_span(wireframe_color) :
        use_primary_color                                                       ? as_span(settings.constant_color0) :
                                                                                  as_span(settings.constant_color1);
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

    if (use_id_ranges) {
        m_id_ranges.push_back(
            Id_range{
                .offset                          = m_id_offset,
                .length                          = count,
                .mesh                            = mesh,
                .index_of_gltf_primitive_in_mesh = mesh_primitive_index
            }
        );
        m_id_offset += count;
    }
}

auto Primitive_buffer::update(
    const Render_bucket&                bucket,
    const Material_set*                 material_source,
    erhe::primitive::Primitive_mode     primitive_mode,
    const Primitive_interface_settings& settings,
    bool                                use_id_ranges 
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

    for (const Mesh_primitive_entry& entry : bucket.entries) {
        ERHE_VERIFY(entry.mesh != nullptr);
        ERHE_VERIFY(entry.buffer_mesh != nullptr);
        write_primitive(*entry.mesh, material_source, entry.mesh_primitive_index, *entry.buffer_mesh, primitive_mode, settings, use_id_ranges, primitive_gpu_data, write_offset);
    }

    buffer_range.bytes_written(write_offset);
    buffer_range.close();
    return buffer_range;
}

auto Primitive_buffer::update(
    const Draw_list&                    draw_list,
    const std::size_t                   begin,
    const std::size_t                   end,
    const Draw_list_scene&              draw_list_scene,
    const erhe::Item_filter&            filter,
    const Primitive_interface_settings& settings,
    std::size_t&                        out_primitive_count
) -> erhe::graphics::Ring_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(begin <= end);
    ERHE_VERIFY(end <= draw_list.entries.size());
    const std::size_t max_primitive_count = end - begin;
    const std::size_t entry_size          = m_primitive_interface.primitive_struct.get_size_bytes();
    const std::size_t max_byte_count      = max_primitive_count * entry_size;
    const std::size_t acquire_byte_count  = std::max(max_byte_count, m_primitive_interface.primitive_block.get_size_bytes());

    erhe::graphics::Ring_buffer_range buffer_range       = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, acquire_byte_count);
    std::span<std::byte>              primitive_gpu_data = buffer_range.get_span();
    std::size_t                       write_offset       = 0;
    std::size_t                       primitive_count    = 0;

    // Fast path (doc/draw_list_performance_improvements.md): the draw list
    // owns a complete GPU-layout record per entry; copy it and patch only the
    // pass-dependent color / size. Settings that need per-mesh evaluation
    // (id offsets, mesh point size / line width) take the
    // generic per-entry writer below; no draw-list-routed pass uses them.
    const bool fast_path =
        (settings.color_source != Primitive_color_source::id_offset) &&
        (settings.size_source == Primitive_size_source::constant_size);
    if (fast_path) {
        ERHE_VERIFY(draw_list.primitive_records.size() == draw_list.entries.size() * entry_size);
        const std::byte* records = draw_list.primitive_records.data();
        std::byte*       dst     = primitive_gpu_data.data();
        const auto&      offsets = m_primitive_interface.offsets;
        constexpr glm::vec4 wireframe_color{1.0f, 1.0f, 1.0f, 1.0f};
        const bool  wireframe = (settings.color_source == Primitive_color_source::mesh_wireframe_color);
        const float size      = settings.constant_size;
        for (std::size_t i = begin; i < end; ++i) {
            const Draw_list_entry& entry = draw_list.entries[i];
            if (!filter(entry.flag_bits)) {
                continue;
            }
            std::memcpy(dst + write_offset, records + i * entry_size, entry_size);
            // Same selection as write_primitive(): Item_base::is_selected() /
            // is_hovered() on the mirrored flag word.
            const bool selected = (entry.flag_bits & erhe::Item_flags::selected) != 0u;
            const bool hovered  = (entry.flag_bits & (erhe::Item_flags::hovered_in_viewport | erhe::Item_flags::hovered_in_item_tree)) != 0u;
            const glm::vec4& color = wireframe
                ? wireframe_color
                : (selected || !hovered)
                    ? settings.constant_color0
                    : settings.constant_color1;
            std::memcpy(dst + write_offset + offsets.color, &color, sizeof(glm::vec4));
            std::memcpy(dst + write_offset + offsets.size,  &size,  sizeof(float));
            write_offset += entry_size;
            ++primitive_count;
        }
    } else {
        const erhe::primitive::Primitive_mode primitive_mode = draw_list.key.primitive_mode;
        for (std::size_t i = begin; i < end; ++i) {
            const Draw_list_entry& entry = draw_list.entries[i];
            if (!filter(entry.flag_bits)) {
                continue;
            }
            erhe::scene::Mesh* mesh = draw_list_scene.get_object_mesh(entry.object_index);
            ERHE_VERIFY(mesh != nullptr);
            const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
            ERHE_VERIFY(entry.mesh_primitive_index < mesh_primitives.size());
            const erhe::primitive::Primitive* primitive = mesh_primitives[entry.mesh_primitive_index].primitive.get();
            ERHE_VERIFY(primitive != nullptr);
            // The variant the entry's draw parameters were baked from at
            // registration, not a fresh choice.
            const erhe::primitive::Buffer_mesh* buffer_mesh = primitive->get_renderable_mesh(entry.variant);
            ERHE_VERIFY(buffer_mesh != nullptr);
            // The DRAW-LIST set, sourced from the scene this pass draws:
            // these records are consumed by the same pass that binds it (D5).
            write_primitive(*mesh, &draw_list_scene.get_material_set(), entry.mesh_primitive_index, *buffer_mesh, primitive_mode, settings, false, primitive_gpu_data, write_offset);
            ++primitive_count;
        }
    }

    buffer_range.bytes_written(write_offset);
    buffer_range.close();
    out_primitive_count = primitive_count;
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
