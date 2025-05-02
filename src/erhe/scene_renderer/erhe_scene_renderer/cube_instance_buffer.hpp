#pragma once

#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/buffer.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::graphics {
    class Instance;
}

namespace erhe::scene_renderer {

class Cube_instance_struct
{
public:
    std::size_t position; // vec4 4 * 4 bytes
    std::size_t color;    // vec4 4 * 4 bytes
};

class Cube_instance
{
public:
    glm::vec4 position;
    glm::vec4 color;
};


class Cube_interface
{
public:
    explicit Cube_interface(erhe::graphics::Instance& graphics_instance);

    erhe::graphics::Shader_resource cube_instance_block;
    erhe::graphics::Shader_resource cube_instance_struct;
    Cube_instance_struct            offsets;
    std::size_t                     max_cube_instance_count{65536 * 32};
};

class Cube_frame_info
{
public:
    std::size_t byte_offset;
    std::size_t cube_count;
};

class Cube_instance_buffer
{
public:
    Cube_instance_buffer(erhe::graphics::Instance& graphics_instance, Cube_interface& cube_interface);

    auto append_frame(const std::vector<Cube_instance>& cubes) -> std::size_t;
    auto bind        (std::size_t frame_index) -> std::size_t;

private:
    Cube_interface&              m_cube_interface;
    erhe::graphics::Buffer       m_buffer;
    std::vector<Cube_frame_info> m_frames;

};

} // namespace erhe::scene_renderer
