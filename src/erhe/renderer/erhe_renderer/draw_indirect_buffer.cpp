// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_renderer/draw_indirect_buffer.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_profile/profile.hpp"

namespace erhe::renderer {

auto Draw_indirect_buffer::get_max_draw_count() -> int
{
    int max_draw_count = 0;
    const auto& ini = erhe::configuration::get_ini_file_section(c_erhe_config_file_path, "renderer");
    ini.get("max_draw_count", max_draw_count);
    return max_draw_count;
}

Draw_indirect_buffer::Draw_indirect_buffer(erhe::graphics::Device& graphics_device)
    : Ring_buffer_client{
        graphics_device,
        erhe::graphics::Buffer_target::draw_indirect,
        "Draw_indirect_buffer"
    }
{
}

auto Draw_indirect_buffer::update(
    const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes,
    erhe::primitive::Primitive_mode                            primitive_mode,
    const erhe::Item_filter&                                   filter
) -> Draw_indirect_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    SPDLOG_LOGGER_TRACE(
        log_render,
        "meshes.size() = {}, m_draw_indirect_writer.write_offset = {}",
        meshes.size(),
        m_writer.write_offset
    );

    // Conservative upper limit
    std::size_t primitive_count = 0;
    for (const auto& mesh : meshes) {
        if (!filter(mesh->get_flag_bits())) {
            continue;
        }
        primitive_count += mesh->get_primitives().size();
    }
    if (primitive_count == 0)
    {
        return {};
    }

    const std::size_t                 entry_size     = sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command);
    const std::size_t                 max_byte_count = primitive_count * entry_size;
    erhe::graphics::Ring_buffer_range buffer_range   = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, max_byte_count);
    const auto                        gpu_data       = buffer_range.get_span();
    size_t                            write_offset   = 0;
    uint32_t                          instance_count     {1};
    uint32_t                          base_instance      {0};
    std::size_t                       draw_indirect_count{0};
    
    for (const auto& mesh : meshes) {
        const auto* node = mesh->get_node();

        if (node == nullptr) {
            continue;
        }

        if (!filter(mesh->get_flag_bits())) {
            continue;
        }

        for (auto& mesh_primitive : mesh->get_primitives()) {
            const erhe::primitive::Primitive&   primitive   = *mesh_primitive.primitive.get();
            const erhe::primitive::Buffer_mesh& buffer_mesh = primitive.render_shape->get_renderable_mesh();
            const erhe::primitive::Index_range  index_range = buffer_mesh.index_range(primitive_mode);
            if (index_range.index_count == 0) {
                continue;
            }

            uint32_t index_count = static_cast<uint32_t>(index_range.index_count);
            if (m_max_index_count_enable) {
                index_count = std::min(index_count, static_cast<uint32_t>(m_max_index_count));
            }

            const uint32_t base_index  = buffer_mesh.base_index();
            const uint32_t first_index = static_cast<uint32_t>(index_range.first_index + base_index);
            const uint32_t base_vertex = buffer_mesh.base_vertex();

            const erhe::graphics::Draw_indexed_primitives_indirect_command draw_command{
                index_count,
                instance_count,
                first_index,
                base_vertex,
                base_instance
            };

            erhe::graphics::write(gpu_data, write_offset, erhe::graphics::as_span(draw_command));

            write_offset += entry_size;
            ++draw_indirect_count;
        }
    }

    buffer_range.bytes_written(write_offset);
    buffer_range.close();

    SPDLOG_LOGGER_TRACE(log_draw, "wrote {} entries to draw indirect buffer", draw_indirect_count);
    return Draw_indirect_buffer_range{
        std::move(buffer_range),
        draw_indirect_count
    };
}

} // namespace erhe::renderer
