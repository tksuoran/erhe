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
#include "erhe_graphics/span.hpp"
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
    : m_graphics_instance  {graphics_instance}
    , m_camera_buffer      {graphics_instance, program_interface.camera_interface}
    , m_program_interface  {program_interface}
    , m_light_buffer       {graphics_instance, program_interface.light_interface}
    , m_primitive_buffer   {graphics_instance, program_interface.primitive_interface}
    , m_cube_control_buffer{graphics_instance, program_interface.cube_interface}
{
}

auto Cube_renderer::make_buffer(const std::vector<uint32_t>& cubes) -> std::shared_ptr<Cube_instance_buffer>
{
    return std::make_shared<Cube_instance_buffer>(
        m_graphics_instance,
        m_program_interface.cube_interface,
        cubes
    );
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

    erhe::renderer::Buffer_range primitive_range = m_primitive_buffer.update(
        erhe::graphics::as_span(parameters.node),
        parameters.primitive_settings
    );
    primitive_range.bind();

    erhe::renderer::Buffer_range cube_control_range = m_cube_control_buffer.update(
        parameters.cube_size,
        parameters.color_bias,
        parameters.color_scale,
        parameters.color_start,
        parameters.color_end
    );
    cube_control_range.bind();

    const erhe::graphics::Pipeline& pipeline = parameters.pipeline;
    m_graphics_instance.opengl_state_tracker.execute(pipeline, false);
    const std::size_t cube_count   = parameters.cube_instance_buffer.bind();
    const GLsizei     vertex_count = static_cast<GLsizei>(cube_count * 6 * 6);
    gl::draw_arrays(pipeline.data.input_assembly.primitive_topology, 0, vertex_count);

    camera_buffer_range.value().submit();
    primitive_range.submit();
    cube_control_range.submit();
}

} // namespace erhe::scene_renderer
