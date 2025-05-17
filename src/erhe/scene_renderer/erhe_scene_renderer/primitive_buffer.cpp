// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_renderer/renderer_config.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtx/matrix_operation.hpp>

namespace erhe::scene_renderer {

Primitive_interface::Primitive_interface(erhe::graphics::Instance& graphics_instance)
    : primitive_block {graphics_instance, "primitive", 3, erhe::graphics::Shader_resource::Type::shader_storage_block}
    , primitive_struct{graphics_instance, "Primitive"}
    , offsets{
        .world_from_node  = primitive_struct.add_mat4 ("world_from_node"       )->offset_in_parent(),
        .normal_transform = primitive_struct.add_mat4 ("world_from_node_normal")->offset_in_parent(),
        .color            = primitive_struct.add_vec4 ("color"                 )->offset_in_parent(),
        .material_index   = primitive_struct.add_uint ("material_index"        )->offset_in_parent(),
        .size             = primitive_struct.add_float("size"                  )->offset_in_parent(),
        .skinning_factor  = primitive_struct.add_float("skinning_factor"       )->offset_in_parent(),
        .base_joint_index = primitive_struct.add_uint ("base_joint_index"      )->offset_in_parent()
    }
{
    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "renderer");
    ini.get("max_primitive_count", max_primitive_count);

    primitive_block.add_struct("primitives", &primitive_struct, erhe::graphics::Shader_resource::unsized_array);
    primitive_block.set_readonly(true);
}

Primitive_buffer::Primitive_buffer(erhe::graphics::Instance& graphics_instance, Primitive_interface& primitive_interface)
    : GPU_ring_buffer{
        graphics_instance,
        erhe::renderer::GPU_ring_buffer_create_info{
            .target        = gl::Buffer_target::shader_storage_buffer,
            .binding_point = primitive_interface.primitive_block.binding_point(),
            .size          = primitive_interface.primitive_struct.size_bytes() * primitive_interface.max_primitive_count,
            .debug_label   = "primitive"
        }
    }
    , m_primitive_interface{primitive_interface}
{
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
    const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes,
    erhe::primitive::Primitive_mode                            primitive_mode,
    const erhe::Item_filter&                                   filter,
    const Primitive_interface_settings&                        settings,
    std::size_t&                                               out_primitive_count,
    bool                                                       use_id_ranges
) -> erhe::renderer::Buffer_range
{
    ERHE_PROFILE_FUNCTION();

    // SPDLOG_LOGGER_TRACE(
    //     log_primitive_buffer,
    //     "meshes.size() = {}, write_offset = {}",
    //     meshes.size(),
    //     m_writer.write_offset
    // );

    out_primitive_count = 0;

    std::size_t primitive_count = 0;
    std::size_t mesh_index = 0;
    for (const auto& mesh : meshes) {
        ERHE_VERIFY(mesh);
        ++mesh_index;
        const auto* node = mesh->get_node();

        // TODO Re-enable this after fixing example to use nodes
        //ERHE_VERIFY(node != nullptr); 

        if (node == nullptr) {
            SPDLOG_LOGGER_TRACE(log_primitive_buffer, "{} does not have node", mesh->get_name());
            continue;
        }

        if (!filter(mesh->get_flag_bits())) {
            // SPDLOG_LOGGER_TRACE(log_primitive_buffer, "node = {}, mesh = {} does not pass filter", node->get_name(), mesh->get_name());
            continue;
        }

        primitive_count += mesh->get_primitives().size();
    }

    const auto        entry_size     = m_primitive_interface.primitive_struct.size_bytes();
    const auto&       offsets        = m_primitive_interface.offsets;
    const std::size_t max_byte_count = primitive_count * entry_size;

    erhe::renderer::Buffer_range buffer_range       = open(erhe::renderer::Ring_buffer_usage::CPU_write, max_byte_count);
    std::span<std::byte>         primitive_gpu_data = buffer_range.get_span();
    std::size_t                  write_offset       = 0;

    std::size_t primitive_index = 0;
    mesh_index = 0;
    for (const auto& mesh : meshes) {
        ERHE_VERIFY(mesh);
        ++mesh_index;

        const auto* node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }

        if (!filter(mesh->get_flag_bits())) {
            continue;
        }

        const glm::mat4 world_from_node = node->world_from_node();

        // TODO Use compute shader ?
        const glm::mat4 normal_transform = glm::transpose(glm::adjugate(world_from_node));

        std::size_t mesh_primitive_index{0};
        for (const auto& primitive : mesh->get_primitives()) {
            const erhe::primitive::Buffer_mesh* buffer_mesh = primitive.get_renderable_mesh();
            ERHE_VERIFY(buffer_mesh != nullptr);
            const erhe::primitive::Index_range  index_range = buffer_mesh->index_range(primitive_mode);
            const uint32_t count = static_cast<uint32_t>(index_range.index_count);
            if (count == 0) {
                continue;
            }
            const uint32_t power_of_two = erhe::math::next_power_of_two(count);
            const uint32_t mask         = power_of_two - 1;
            const uint32_t current_bits = m_id_offset & mask;
            if (current_bits != 0) {
                const auto add = power_of_two - current_bits;
                m_id_offset += add;
            }

            erhe::primitive::Material* material = primitive.material.get();
            const glm::vec4 wireframe_color  = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}; //// mesh->get_wireframe_color();
            const glm::vec3 id_offset_vec3   = erhe::math::vec3_from_uint(m_id_offset);
            const glm::vec4 id_offset_vec4   = glm::vec4{id_offset_vec3, 0.0f};
            const uint32_t  material_index   = (material != nullptr) ? material->material_buffer_index : 0u;
            const auto&     skin             = mesh->skin;
            const float     skinning_factor  = skin ? 1.0f : 0.0f;
            const uint32_t  base_joint_index = skin ? skin->skin_data.joint_buffer_index : 0;

            SPDLOG_LOGGER_TRACE(
                log_primitive_buffer, 
                "[{}] node {}, mesh {}, material {}, mat. idx = {}, offset = {}",
                primitive_index,
                node->describe(),
                mesh_index - 1,
                (material != nullptr) ? material->get_name() : std::string{},
                material_index,
                writer.write_offset
            );

            using erhe::graphics::as_span;
            const auto color_span =
                (settings.color_source == Primitive_color_source::id_offset           ) ? as_span(id_offset_vec4         ) :
                (settings.color_source == Primitive_color_source::mesh_wireframe_color) ? as_span(wireframe_color        ) :
                                                                                          as_span(settings.constant_color);
            const auto size_span =
                (settings.size_source == Primitive_size_source::mesh_point_size) ? as_span(mesh->point_size      ) :
                (settings.size_source == Primitive_size_source::mesh_line_width) ? as_span(mesh->line_width      ) :
                                                                                   as_span(settings.constant_size);
            //memset(reinterpret_cast<uint8_t*>(model_gpu_data.data()) + offset, 0, entry_size);
            {
                //ZoneScopedN("write");
                using erhe::graphics::write;
                write(primitive_gpu_data, write_offset + offsets.world_from_node,  as_span(world_from_node ));
                write(primitive_gpu_data, write_offset + offsets.normal_transform, as_span(normal_transform));
                write(primitive_gpu_data, write_offset + offsets.color,            color_span               );
                write(primitive_gpu_data, write_offset + offsets.material_index,   as_span(material_index  ));
                write(primitive_gpu_data, write_offset + offsets.size,             size_span                );
                write(primitive_gpu_data, write_offset + offsets.skinning_factor,  as_span(skinning_factor ));
                write(primitive_gpu_data, write_offset + offsets.base_joint_index, as_span(base_joint_index));

            }
            write_offset += entry_size;

            if (use_id_ranges) {
                m_id_ranges.push_back(
                    Id_range{
                        .offset          = m_id_offset,
                        .length          = count,
                        .mesh            = mesh.get(),
                        .primitive_index = mesh_primitive_index++
                    }
                );

                m_id_offset += count;
            }
            ++primitive_index;
            ++out_primitive_count;
        }
    }

    buffer_range.close(write_offset);

    // SPDLOG_LOGGER_TRACE(log_primitive_buffer, "wrote {} entries to primitive buffer", primitive_index);
    return buffer_range;
}

auto Primitive_buffer::update(
    const std::span<const std::shared_ptr<erhe::scene::Node>>& nodes,
    const Primitive_interface_settings&                        primitive_settings
) -> erhe::renderer::Buffer_range
{
    const std::size_t primitive_count = nodes.size();
    const auto        entry_size      = m_primitive_interface.primitive_struct.size_bytes();
    const auto&       offsets         = m_primitive_interface.offsets;
    const std::size_t max_byte_count  = primitive_count * entry_size;

    erhe::renderer::Buffer_range buffer_range       = open(erhe::renderer::Ring_buffer_usage::CPU_write, max_byte_count);
    std::span<std::byte>         primitive_gpu_data = buffer_range.get_span();
    std::size_t                  write_offset       = 0;

    for (const auto& node : nodes) {
        const glm::mat4 world_from_node  = node->world_from_node();
        const glm::mat4 normal_transform = glm::transpose(glm::adjugate(world_from_node));
        const glm::vec4 wireframe_color  = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
        const uint32_t  material_index   = 0;
        const float     skinning_factor  = 0.0f;
        const uint32_t  base_joint_index = 0;
        using erhe::graphics::as_span;
        const auto color_span = as_span(primitive_settings.constant_color);
        const auto size_span  = as_span(primitive_settings.constant_size);
        using erhe::graphics::write;
        write(primitive_gpu_data, write_offset + offsets.world_from_node,  as_span(world_from_node ));
        write(primitive_gpu_data, write_offset + offsets.normal_transform, as_span(normal_transform));
        write(primitive_gpu_data, write_offset + offsets.color,            color_span               );
        write(primitive_gpu_data, write_offset + offsets.material_index,   as_span(material_index  ));
        write(primitive_gpu_data, write_offset + offsets.size,             size_span                );
        write(primitive_gpu_data, write_offset + offsets.skinning_factor,  as_span(skinning_factor ));
        write(primitive_gpu_data, write_offset + offsets.base_joint_index, as_span(base_joint_index));
        write_offset += entry_size;
    }
    buffer_range.close(write_offset);
    return buffer_range;
}

} // namespace erhe::scene_renderer
