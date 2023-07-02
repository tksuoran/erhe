#pragma once

#include "erhe/graphics/shader_resource.hpp"
#include "erhe/renderer/enums.hpp"
#include "erhe/renderer/multi_buffer.hpp"

#include <vector>

namespace erhe::scene
{
    class Skin;
}

namespace erhe::scene_renderer
{

class Joint_struct
{
public:
    std::size_t world_from_bind;            // mat4 16 * 4 bytes
    std::size_t world_from_bind_cofactor;   // mat4 16 * 4 bytes
};

class Joint_interface
{
public:
    explicit Joint_interface(
        erhe::graphics::Instance& graphics_instance
    );

    erhe::graphics::Shader_resource joint_block;
    erhe::graphics::Shader_resource joint_struct;
    Joint_struct                    offsets;
    std::size_t                     max_joint_count{200};
};

class Joint_buffer
    : public erhe::renderer::Multi_buffer
{
public:
    Joint_buffer(
        erhe::graphics::Instance& graphics_instance,
        Joint_interface&          joint_interface
    );

    auto update(
        const gsl::span<const std::shared_ptr<erhe::scene::Skin>>& skins
    ) -> erhe::renderer::Buffer_range;

private:
    erhe::graphics::Instance& m_graphics_instance;
    Joint_interface&          m_joint_interface;
};

} // namespace erhe::scene_renderer
