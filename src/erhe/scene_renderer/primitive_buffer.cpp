// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/scene_renderer/primitive_buffer.hpp"

#include "erhe/configuration/configuration.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/skin.hpp"
#include "erhe/scene_renderer/scene_renderer_log.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::scene_renderer
{

Primitive_interface::Primitive_interface(
    erhe::graphics::Instance& graphics_instance
)
    : primitive_block {graphics_instance, "primitive", 3, erhe::graphics::Shader_resource::Type::shader_storage_block}
    , primitive_struct{graphics_instance, "Primitive"}
    , offsets{
        .world_from_node          = primitive_struct.add_mat4 ("world_from_node"         )->offset_in_parent(),
        .world_from_node_cofactor = primitive_struct.add_mat4 ("world_from_node_cofactor")->offset_in_parent(),
        .color                    = primitive_struct.add_vec4 ("color"                   )->offset_in_parent(),
        .material_index           = primitive_struct.add_uint ("material_index"          )->offset_in_parent(),
        .size                     = primitive_struct.add_float("size"                    )->offset_in_parent(),
        .skinning_factor          = primitive_struct.add_float("skinning_factor"         )->offset_in_parent(),
        .base_joint_index         = primitive_struct.add_uint ("base_joint_index"        )->offset_in_parent()
    }
{
    auto ini = erhe::configuration::get_ini("erhe.ini", "renderer");
    ini->get("max_primitive_count", max_primitive_count);

    primitive_block.add_struct("primitives", &primitive_struct, erhe::graphics::Shader_resource::unsized_array);
}

Primitive_buffer::Primitive_buffer(
    erhe::graphics::Instance& graphics_instance,
    Primitive_interface&      primitive_interface
)
    : Multi_buffer         {graphics_instance, "primitive"}
    , m_primitive_interface{primitive_interface}
{
    Multi_buffer::allocate(
        gl::Buffer_target::shader_storage_buffer,
        m_primitive_interface.primitive_block.binding_point(),
        m_primitive_interface.primitive_struct.size_bytes() * m_primitive_interface.max_primitive_count
    );
}

void Primitive_buffer::reset_id_ranges()
{
    m_id_offset = 0;
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

auto Primitive_buffer::update(
    const gsl::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes,
    const erhe::scene::Item_filter&                            filter,
    const Primitive_interface_settings&                        settings,
    bool                                                       use_id_ranges
) -> erhe::renderer::Buffer_range
{
    ERHE_PROFILE_FUNCTION();

    SPDLOG_LOGGER_TRACE(
        log_render,
        "meshes.size() = {}, m_writer.write_offset = {}",
        meshes.size(),
        m_writer.write_offset
    );

    std::size_t primitive_count = 0;
    std::size_t mesh_index = 0;
    for (const auto& mesh : meshes) {
        ERHE_VERIFY(mesh);
        ++mesh_index;
        if (!filter(mesh->get_flag_bits())) {
            continue;
        }

        const auto* node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }

        const auto& mesh_data = mesh->mesh_data;
        primitive_count += mesh_data.primitives.size();
    }

    auto&             buffer             = current_buffer();
    const auto        entry_size         = m_primitive_interface.primitive_struct.size_bytes();
    const auto&       offsets            = m_primitive_interface.offsets;
    const std::size_t max_byte_count     = primitive_count * entry_size;
    const auto        primitive_gpu_data = m_writer.begin(&buffer, max_byte_count);
    mesh_index = 0;
    for (const auto& mesh : meshes) {
        ++mesh_index;

        ERHE_VERIFY(mesh);
        if (!filter(mesh->get_flag_bits())) {
            continue;
        }

        const auto* node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }

        if ((m_writer.write_offset + entry_size) > m_writer.write_end) {
            log_render->critical("primitive buffer capacity {} exceeded", buffer.capacity_byte_count());
            ERHE_FATAL("primitive buffer capacity exceeded");
            break;
        }

        const auto&     mesh_data       = mesh->mesh_data;
        const glm::mat4 world_from_node = node->world_from_node();

        // TODO Use compute shader
        const glm::mat4 world_from_node_cofactor = erhe::toolkit::compute_cofactor(world_from_node);

        std::size_t mesh_primitive_index{0};
        for (const auto& primitive : mesh_data.primitives) {
            if ((m_writer.write_offset + entry_size) > m_writer.write_end) {
                log_render->critical("primitive buffer capacity {} exceeded", buffer.capacity_byte_count());
                ERHE_FATAL("primitive buffer capacity exceeded");
                break;
            }

            //// log_render->info(
            ////     "writing mesh {} node {} primitive {} offset = {}",
            ////     mesh_index - 1,
            ////     node->describe(),
            ////     mesh_primitive_index,
            ////     m_writer.write_offset
            //// );

            const auto&    primitive_geometry = primitive.gl_primitive_geometry;
            const uint32_t count              = static_cast<uint32_t>(primitive_geometry.triangle_fill_indices.index_count);
            const uint32_t power_of_two       = erhe::toolkit::next_power_of_two(count);
            const uint32_t mask               = power_of_two - 1;
            const uint32_t current_bits       = m_id_offset & mask;
            if (current_bits != 0) {
                const auto add = power_of_two - current_bits;
                m_id_offset += add;
            }

            const glm::vec4 wireframe_color  = mesh->get_wireframe_color();
            const glm::vec3 id_offset_vec3   = erhe::toolkit::vec3_from_uint(m_id_offset);
            const glm::vec4 id_offset_vec4   = glm::vec4{id_offset_vec3, 0.0f};
            const uint32_t  material_index   = (primitive.material != nullptr) ? primitive.material->material_buffer_index : 0u;
            const auto      skin             = mesh_data.skin;
            const float     skinning_factor  = skin ? 1.0f : 0.0f;
            const uint32_t  base_joint_index = skin ? skin->skin_data.joint_buffer_index : 0;

            using erhe::graphics::as_span;
            const auto color_span =
                (settings.color_source == Primitive_color_source::id_offset           ) ? as_span(id_offset_vec4         ) :
                (settings.color_source == Primitive_color_source::mesh_wireframe_color) ? as_span(wireframe_color        ) :
                                                                                          as_span(settings.constant_color);
            const auto size_span =
                (settings.size_source == Primitive_size_source::mesh_point_size) ? as_span(mesh_data.point_size   ) :
                (settings.size_source == Primitive_size_source::mesh_line_width) ? as_span(mesh_data.line_width   ) :
                                                                                   as_span(settings.constant_size);
            //memset(reinterpret_cast<uint8_t*>(model_gpu_data.data()) + offset, 0, entry_size);
            {
                //ZoneScopedN("write");
                using erhe::graphics::write;
                write(primitive_gpu_data, m_writer.write_offset + offsets.world_from_node,          as_span(world_from_node         ));
                write(primitive_gpu_data, m_writer.write_offset + offsets.world_from_node_cofactor, as_span(world_from_node_cofactor));
                write(primitive_gpu_data, m_writer.write_offset + offsets.color,                    color_span                       );
                write(primitive_gpu_data, m_writer.write_offset + offsets.material_index,           as_span(material_index          ));
                write(primitive_gpu_data, m_writer.write_offset + offsets.size,                     size_span                        );
                write(primitive_gpu_data, m_writer.write_offset + offsets.skinning_factor,          as_span(skinning_factor         ));
                write(primitive_gpu_data, m_writer.write_offset + offsets.base_joint_index,         as_span(base_joint_index        ));

            }
            m_writer.write_offset += entry_size;
            ERHE_VERIFY(m_writer.write_offset <= m_writer.write_end);

            if (use_id_ranges) {
                m_id_ranges.push_back(
                    Id_range{
                        .offset          = m_id_offset,
                        .length          = count,
                        .mesh            = mesh,
                        .primitive_index = mesh_primitive_index++
                    }
                );

                m_id_offset += count;
            }
        }
    }

    m_writer.end();

    SPDLOG_LOGGER_TRACE(log_draw, "wrote {} entries to primitive buffer", primitive_index);

    return m_writer.range;
}

} // namespace erhe::scene_renderer
