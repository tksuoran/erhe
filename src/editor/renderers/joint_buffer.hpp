#pragma once

#include "renderers/enums.hpp"

#include "erhe/application/renderers/multi_buffer.hpp"

#include "erhe/graphics/shader_resource.hpp"

#include <vector>

namespace erhe::scene
{
    class Skin;
}

namespace editor
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
    explicit Joint_interface(std::size_t max_joint_count);

    erhe::graphics::Shader_resource joint_block;
    erhe::graphics::Shader_resource joint_struct;
    Joint_struct                    offsets;
    std::size_t                     max_joint_count;
};

class Joint_buffer
    : public erhe::application::Multi_buffer
{
public:
    explicit Joint_buffer(Joint_interface* joint_interface);

    auto update(
        const gsl::span<const std::shared_ptr<erhe::scene::Skin>>& skins
    ) -> erhe::application::Buffer_range;

private:
    Joint_interface* m_joint_interface{nullptr};
};

} // namespace editor
