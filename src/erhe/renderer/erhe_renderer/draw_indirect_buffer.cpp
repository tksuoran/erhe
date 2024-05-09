// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_renderer/draw_indirect_buffer.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_renderer/renderer_log.hpp"

#include "erhe_gl/draw_indirect.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

////#if defined(ERHE_GUI_LIBRARY_IMGUI)
////#   include <imgui/imgui.h>
////#endif

namespace erhe::renderer
{

Draw_indirect_buffer::Draw_indirect_buffer(
    erhe::graphics::Instance& graphics_instance
)
    : Multi_buffer{graphics_instance, "draw indirect"}
{
    auto ini = erhe::configuration::get_ini("erhe.ini", "renderer");
    ini->get("max_draw_count", m_max_draw_count);

    Multi_buffer::allocate(
        gl::Buffer_target::draw_indirect_buffer,
        sizeof(gl::Draw_elements_indirect_command) * m_max_draw_count
    );
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

    auto&             buffer         = current_buffer();
    const std::size_t entry_size     = sizeof(gl::Draw_elements_indirect_command);
    const std::size_t max_byte_count = primitive_count * entry_size;
    const auto        gpu_data       = m_writer.begin(&buffer, max_byte_count);
    uint32_t          instance_count     {1};
    uint32_t          base_instance      {0};
    std::size_t       draw_indirect_count{0};
    
    for (const auto& mesh : meshes) {
        if (!filter(mesh->get_flag_bits())) {
            continue;
        }

        if ((m_writer.write_offset + entry_size) > m_writer.write_end) {
            log_render->critical("draw indirect buffer capacity {} exceeded", buffer.capacity_byte_count());
            ERHE_FATAL("draw indirect buffer capacity exceeded");
            break;
        }

        for (auto& primitive : mesh->get_primitives()) {
            const auto& geometry_mesh = primitive.geometry_primitive->gl_geometry_mesh;
            const auto  index_range   = geometry_mesh.index_range(primitive_mode);
            if (index_range.index_count == 0) {
                continue;
            }

            if ((m_writer.write_offset + entry_size) > m_writer.write_end) {
                log_render->critical("draw indirect buffer capacity {} exceeded", buffer.capacity_byte_count());
                ERHE_FATAL("draw indirect buffer capacity exceeded");
                break;
            }

            uint32_t index_count = static_cast<uint32_t>(index_range.index_count);
            if (m_max_index_count_enable) {
                index_count = std::min(index_count, static_cast<uint32_t>(m_max_index_count));
            }

            const uint32_t base_index  = geometry_mesh.base_index();
            const uint32_t first_index = static_cast<uint32_t>(index_range.first_index + base_index);
            const uint32_t base_vertex = geometry_mesh.base_vertex();

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
            ERHE_VERIFY(m_writer.write_offset <= m_writer.write_end);
            ++draw_indirect_count;
        }
    }

    m_writer.end();

    SPDLOG_LOGGER_TRACE(log_draw, "wrote {} entries to draw indirect buffer", draw_indirect_count);
    return { m_writer.range, draw_indirect_count };
}

//// void Draw_indirect_buffer::debug_properties_window()
//// {
//// #if defined(ERHE_GUI_LIBRARY_IMGUI)
////     ImGui::Begin    ("Base Renderer Debug Properties");
////     ImGui::Checkbox ("Enable Max Index Count", &m_max_index_count_enable);
////     ImGui::SliderInt("Max Index Count", &m_max_index_count, 0, 256);
////     ImGui::End      ();
//// #endif
//// }
//// 

}
