// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "renderers/joint_buffer.hpp"
#include "renderers/program_interface.hpp"
#include "editor_log.hpp"

#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/skin.hpp"
#include "erhe/toolkit/profile.hpp"

namespace editor
{

Joint_interface::Joint_interface(const std::size_t max_joint_count)
    : joint_block {"joint", 4, erhe::graphics::Shader_resource::Type::shader_storage_block}
    , joint_struct{"Joint"}
    , offsets{
        .world_from_bind          = joint_struct.add_mat4("world_from_bind"         )->offset_in_parent(),
        .world_from_bind_cofactor = joint_struct.add_mat4("world_from_bind_cofactor")->offset_in_parent(),
    },
    max_joint_count{max_joint_count}
{
    joint_block.add_struct("joints", &joint_struct, erhe::graphics::Shader_resource::unsized_array);
}

Joint_buffer::Joint_buffer(Joint_interface* joint_interface)
    : Multi_buffer     {"joint"}
    , m_joint_interface{joint_interface}
{
    Multi_buffer::allocate(
        gl::Buffer_target::shader_storage_buffer,
        m_joint_interface->joint_block.binding_point(),
        m_joint_interface->joint_struct.size_bytes() * m_joint_interface->max_joint_count
    );
}

auto Joint_buffer::update(
    const gsl::span<const std::shared_ptr<erhe::scene::Skin>>& skins
) -> erhe::application::Buffer_range
{
    ERHE_PROFILE_FUNCTION();

    SPDLOG_LOGGER_TRACE(
        log_render,
        "skins.size() = {}, m_writer.write_offset = {}",
        skins.size(),
        m_writer.write_offset
    );

    std::size_t joint_count = 0;
    std::size_t skin_index = 0;
    for (const auto& skin : skins) {
        ERHE_VERIFY(skin);
        ++skin_index;

        const auto& skin_data = skin->skin_data;
        joint_count += skin_data.joints.size();
    }

    auto&             buffer             = current_buffer();
    const auto        entry_size         = m_joint_interface->joint_struct.size_bytes();
    const auto&       offsets            = m_joint_interface->offsets;
    const std::size_t max_byte_count     = joint_count * entry_size;
    const auto        primitive_gpu_data = m_writer.begin(&buffer, max_byte_count);
    uint32_t joint_index = 0;
    for (auto& skin : skins) {
        ERHE_VERIFY(skin);

        if ((m_writer.write_offset + entry_size) > m_writer.write_end) {
            log_render->critical("joint buffer capacity {} exceeded", buffer.capacity_byte_count());
            ERHE_FATAL("joint buffer capacity exceeded");
            break;
        }

        //const auto& node_data = node->node_data;
        auto& skin_data = skin->skin_data;
        skin_data.joint_buffer_index = joint_index;
        for (std::size_t i = 0, end_i = skin->skin_data.joints.size(); i < end_i; ++i) {
            const auto&     joint           = skin->skin_data.joints[i];
            //if (!joint) {
            //    continue;
            //}

            const glm::mat4 joint_from_bind = skin->skin_data.inverse_bind_matrices[i];
            if ((m_writer.write_offset + entry_size) > m_writer.write_end) {
                log_render->critical("joint buffer capacity {} exceeded", buffer.capacity_byte_count());
                ERHE_FATAL("joint buffer capacity exceeded");
                break;
            }

            const glm::mat4 world_from_joint = joint->world_from_node();
            const glm::mat4 world_from_bind  = world_from_joint * joint_from_bind;

            // TODO Use compute shader
            const glm::mat4 world_from_bind_cofactor = erhe::toolkit::compute_cofactor(world_from_bind);

            using erhe::graphics::as_span;
            using erhe::graphics::write;
            write(primitive_gpu_data, m_writer.write_offset + offsets.world_from_bind,          as_span(world_from_bind         ));
            write(primitive_gpu_data, m_writer.write_offset + offsets.world_from_bind_cofactor, as_span(world_from_bind_cofactor));
            m_writer.write_offset += entry_size;
            ++joint_index;
            ERHE_VERIFY(m_writer.write_offset <= m_writer.write_end);
        }
    }

    m_writer.end();

    SPDLOG_LOGGER_TRACE(log_draw, "wrote {} entries to joint buffer", joint_index);

    return m_writer.range;
}

}
