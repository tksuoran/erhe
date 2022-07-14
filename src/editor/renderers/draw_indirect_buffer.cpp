// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "renderers/draw_indirect_buffer.hpp"
#include "renderers/programs.hpp"
#include "renderers/program_interface.hpp"
#include "log.hpp"

#include "erhe/graphics/texture.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/projection.hpp"
#include "erhe/scene/transform.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/toolkit/profile.hpp"

#include <imgui.h>

namespace editor
{

Draw_indirect_buffer::Draw_indirect_buffer(std::size_t max_draw_count)
    : Multi_buffer{"draw indirect"}
{
    Multi_buffer::allocate(
        gl::Buffer_target::draw_indirect_buffer,
        0,
        sizeof(gl::Draw_elements_indirect_command) * max_draw_count,
        m_name
    );
}

auto Draw_indirect_buffer::update(
    const gsl::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes,
    erhe::primitive::Primitive_mode                            primitive_mode,
    const erhe::scene::Visibility_filter&                      visibility_filter
) -> Draw_indirect_buffer_range
{
    ERHE_PROFILE_FUNCTION

    SPDLOG_LOGGER_TRACE(
        log_render,
        "meshes.size() = {}, m_draw_indirect_writer.write_offset = {}",
        meshes.size(),
        m_writer.write_offset
    );

    auto&             buffer     = current_buffer();
    const std::size_t entry_size = sizeof(gl::Draw_elements_indirect_command);
    const auto        gpu_data   = buffer.map();
    uint32_t          instance_count     {1};
    uint32_t          base_instance      {0};
    std::size_t       draw_indirect_count{0};
    m_writer.begin(buffer.target());
    for (const auto& mesh : meshes)
    {
        if ((m_writer.write_offset + entry_size) > buffer.capacity_byte_count())
        {
            log_render->critical("draw indirect buffer capacity {} exceeded", buffer.capacity_byte_count());
            ERHE_FATAL("draw indirect buffer capacity exceeded");
            break;
        }

        if (!visibility_filter(mesh->get_visibility_mask()))
        {
            continue;
        }
        for (auto& primitive : mesh->mesh_data.primitives)
        {
            if ((m_writer.write_offset + entry_size) > buffer.capacity_byte_count())
            {
                log_render->critical("draw indirect buffer capacity {} exceeded", buffer.capacity_byte_count());
                ERHE_FATAL("draw indirect buffer capacity exceeded");
                break;
            }
            const auto& primitive_geometry = primitive.gl_primitive_geometry;
            const auto  index_range        = primitive_geometry.index_range(primitive_mode);
            if (index_range.index_count == 0)
            {
                continue;
            }

            uint32_t index_count = static_cast<uint32_t>(index_range.index_count);
            if (m_max_index_count_enable)
            {
                index_count = std::min(index_count, static_cast<uint32_t>(m_max_index_count));
            }

            const uint32_t base_index  = primitive_geometry.base_index();
            const uint32_t first_index = static_cast<uint32_t>(index_range.first_index + base_index);
            const uint32_t base_vertex = primitive_geometry.base_vertex();

            const gl::Draw_elements_indirect_command draw_command{
                index_count,
                instance_count,
                first_index,
                base_vertex,
                base_instance
            };

            erhe::graphics::write(
                gpu_data,
                m_writer.write_offset,
                erhe::graphics::as_span(draw_command)
            );

            m_writer.write_offset += entry_size;
            ERHE_VERIFY(m_writer.write_offset <= buffer.capacity_byte_count());
            ++draw_indirect_count;
        }
    }

    m_writer.end();

    SPDLOG_LOGGER_TRACE(log_draw, "wrote {} entries to draw indirect buffer", draw_indirect_count);
    return { m_writer.range, draw_indirect_count };
}

void Draw_indirect_buffer::debug_properties_window()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::Begin    ("Base Renderer Debug Properties");
    ImGui::Checkbox ("Enable Max Index Count", &m_max_index_count_enable);
    ImGui::SliderInt("Max Index Count", &m_max_index_count, 0, 256);
    ImGui::End      ();
#endif
}


}