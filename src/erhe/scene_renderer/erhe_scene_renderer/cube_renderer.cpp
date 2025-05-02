#include "erhe_scene_renderer/cube_renderer.hpp"
#include "erhe_scene_renderer/cube_instance_buffer.hpp"

#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/opengl_state_tracker.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/scoped_buffer_mapping.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_profile/profile.hpp"

namespace erhe::scene_renderer {

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;

// https://x.com/SebAaltonen/status/1315982782439591938
//
// Let's talk about rendering a massive set of cubes efficiently...
//
// Geometry shaders and instanced draw are unoptimal choices.
// Geometry shader outputs strips (unoptimal topology) and it
// needs GPU storage and load balancing.
//
// Instanced draw is suboptimal on many older GPUs for small 8 vertex / 12 triangle instances.
//
// The most efficient way to render a big amount of procedural cubes
// on most GPUs is the following: At startup you fill an index buffer
// with max amount of cubes. 3*2*6 = 36 indices each (index data = 0..7 + i*8).
// Never modify this index buffer at runtime.
//
//   6-------7   +X: 1,7,3, 7,1,5   0 = (0, 0, 0)
//  /|      /|   -X: 0,6,4, 6,0,2   1 = (1, 0, 0)
// 2-------3 |   +Y: 2,7,6, 7,2,3   2 = (0, 1, 0)
// | |     | |   -Y: 0,5,1, 5,0,4   3 = (1, 1, 0)
// | |     | |   +Z: 4,7,5, 7,4,6   4 = (0, 0, 1)
// | 4-----|-5   -Z: 0,3,2, 3,0,1   5 = (1, 0, 1)
// |/      |/                       6 = (0, 1, 1)
// 0-------1                        7 = (1, 1, 1)
//
// - No vertex buffer.
// - You use SV_VertexId in the shader.
// - Divide it by 8 (bit shift) to get cube index (to fetch cube position from an array).
// - The low 3 bits are XYZ bits (see OP image).
// - LocalVertexPos = float3(X*2-1, Y*2-1, Z*2-1). This is just a few ALU in the vertex shader.
//
// Use indirect draw call to control the drawn cube count from GPU side.
// Write the (visible) cube data (position or transform matrix, or whatever you need) to an array (UAV).
// This is indexed in the vertex shader (SRV).
//
// There's an additional optimization:
// - Only 3 faces of a cube can be visible at once.
// - Instead generate only 3*2*3=18 indices per cube (positive corner).
// - Calculate vec from cube center to camera.
// - Extract XYZ signs.
// - Flip XYZ of the output vertices accordingly...
//
// If you flip odd number of vertex coordinates, the triangle winding will get flipped.
// Thus you need to fix it (read triangle lookup using "2-i") if you have 1 or 3 flips.
// This is just a few extra ALU too. Result = you are rendering 6 faces per cube,
// thus saving 50% triangle count.
//
// The index buffer is still fully static even with this optimization.
// It's best to keep the index numbering 0..7 (8) per cube even with this optimization
// to be able to use bit shift instead of integer divide (which is slow).
// GPU's do index dedup. Extra "slot" doesn't cost anything, 

Cube_renderer::Cube_renderer(erhe::graphics::Instance& graphics_instance, Program_interface& program_interface)
    : m_graphics_instance   {graphics_instance}
    , m_index_buffer{
        graphics_instance,
        erhe::graphics::Buffer_create_info{
            .target              = gl::Buffer_target::element_array_buffer,
            .capacity_byte_count = sizeof(uint32_t) * program_interface.cube_interface.max_cube_instance_count * 36,
            .storage_mask        = gl::Buffer_storage_mask::map_write_bit,
            .debug_label         = "Cube renderer indices"
        }
    }
    , m_camera_buffer       {graphics_instance, program_interface.camera_interface}
    , m_cube_instance_buffer{graphics_instance, program_interface.cube_interface}
{
    std::array<uint32_t, 6 * 6> cube_indices{
        1,7,3, 7,1,5,
        0,6,4, 6,0,2,
        2,7,6, 7,2,3,
        0,5,1, 5,0,4,
        4,7,5, 7,4,6,
        0,3,2, 3,0,1
    };

    erhe::graphics::Scoped_buffer_mapping<uint32_t> index_buffer_map{m_index_buffer, 0, program_interface.cube_interface.max_cube_instance_count * 36, gl::Map_buffer_access_mask::map_write_bit};
    const std::span<uint32_t>& gpu_index_data = index_buffer_map.span();
    for (size_t i = 0, end = gpu_index_data.size(); i < end; ++i) {
        gpu_index_data[i] = static_cast<uint32_t>(cube_indices[i % 36] + (i / 36) * 8);
    }
}

auto Cube_renderer::get_buffer() -> Cube_instance_buffer&
{
    return m_cube_instance_buffer;
}

void Cube_renderer::render(const Render_parameters& parameters)
{
    ERHE_PROFILE_FUNCTION();

    const auto& viewport = parameters.viewport;
    const auto* camera   = parameters.camera;

    erhe::graphics::Scoped_debug_group pass_scope{"render cubes"};

    gl::viewport(viewport.x, viewport.y, viewport.width, viewport.height);

    using Buffer_range = erhe::renderer::Buffer_range;
    std::optional<Buffer_range> camera_buffer_range{};
    ERHE_VERIFY(camera != nullptr);
    camera_buffer_range = m_camera_buffer.update(
        *camera->projection(),
        *camera->get_node(),
        viewport,
        camera->get_exposure(),
        glm::vec4{0.0f},
        glm::vec4{0.0f}
    );
    camera_buffer_range.value().bind();

    const erhe::graphics::Pipeline& pipeline = parameters.pipeline;
    m_graphics_instance.opengl_state_tracker.execute(pipeline, false);
    m_graphics_instance.opengl_state_tracker.vertex_input.set_index_buffer(&m_index_buffer);

    const std::size_t cube_count   = m_cube_instance_buffer.bind(parameters.frame);
    const GLsizei     vertex_count = static_cast<GLsizei>(cube_count * 6 * 6);

    gl::draw_elements(
        pipeline.data.input_assembly.primitive_topology,
        vertex_count,
        erhe::graphics::to_gl_index_type(erhe::dataformat::Format::format_32_scalar_uint),
        0
    );

    camera_buffer_range.value().submit();
}

} // namespace erhe::scene_renderer
