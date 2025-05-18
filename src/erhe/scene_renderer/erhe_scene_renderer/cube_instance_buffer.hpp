#pragma once

#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_renderer/gpu_ring_buffer.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::graphics {
    class Instance;
}

namespace erhe::scene_renderer {

class Cube_instance_struct
{
public:
    std::size_t packed_position; // uint 1 * 4 bytes
};

class Cube_control_struct
{
public:
    std::size_t cube_size;   // vec4 4 * 4 bytes
    std::size_t color_bias;  // vec4 4 * 4 bytes
    std::size_t color_scale; // vec4 4 * 4 bytes
    std::size_t color_start; // vec4 4 * 4 bytes
    std::size_t color_end;   // vec4 4 * 4 bytes
};

[[nodiscard]] inline auto pack_x11y11z10(glm::uvec3 xyz) -> uint32_t
{
    const uint32_t x = std::min(xyz.x, 0x7FFu); // 11 bits
    const uint32_t y = std::min(xyz.y, 0x7FFu); // 11 bits
    const uint32_t z = std::min(xyz.z, 0x3FFu); // 10 bits

    return (x & 0x7FFu) | ((y & 0x7FFu) << 11) | ((z & 0x3FFu) << 22);
}

[[nodiscard]] inline auto pack_x11y11z10(int x, int y, int z) -> uint32_t
{
    assert(x >= 0);
    assert(y >= 0);
    assert(z >= 0);
    assert(x <= 0x7FFu);
    assert(y <= 0x7FFu);
    assert(z <= 0x3FFu);

    return 
        ((static_cast<uint32_t>(x) & 0x7FFu)      ) |
        ((static_cast<uint32_t>(y) & 0x7FFu) << 11) |
        ((static_cast<uint32_t>(z) & 0x3FFu) << 22);
}

[[nodiscard]] inline auto unpack_x11y11z10(uint32_t packed_xyz) -> glm::uvec3
{
    const uint32_t x =  packed_xyz        & 0x7FFu;
    const uint32_t y = (packed_xyz >> 11) & 0x7FFu;
    const uint32_t z = (packed_xyz >> 22) & 0x3FFu;
    return glm::uvec3{x, y, z};
}

class Cube_interface
{
public:
    explicit Cube_interface(erhe::graphics::Instance& graphics_instance);

    erhe::graphics::Shader_resource cube_instance_block;
    erhe::graphics::Shader_resource cube_instance_struct;
    Cube_instance_struct            cube_instance_offsets;

    erhe::graphics::Shader_resource cube_control_block;
    erhe::graphics::Shader_resource cube_control_struct;
    Cube_control_struct             cube_control_offsets;
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
    Cube_instance_buffer(
        erhe::graphics::Instance&    graphics_instance,
        Cube_interface&              cube_interface,
        const std::vector<uint32_t>& cubes
    );

    auto bind() -> std::size_t;

private:
    Cube_interface&        m_cube_interface;
    erhe::graphics::Buffer m_buffer;
    std::size_t            m_cube_count;
};

class Cube_control_buffer : public erhe::renderer::GPU_ring_buffer
{
public:
    Cube_control_buffer(erhe::graphics::Instance& graphics_instance, Cube_interface& cube_interface);

    auto update(
        const glm::vec4& cube_size,
        const glm::vec4& color_bias,
        const glm::vec4& color_scale,
        const glm::vec4& color_start,
        const glm::vec4& color_end
    ) -> erhe::renderer::Buffer_range;

private:
    Cube_interface& m_cube_interface;
};

} // namespace erhe::scene_renderer
