#include "renderers/light_debug_renderer.hpp"
#include "main/log.hpp"
#include "renderers/light_mesh.hpp"
#include "renderstack_geometry/shapes/cone.hpp"
#include "renderstack_graphics/buffer.hpp"
#include "renderstack_graphics/configuration.hpp"
#include "renderstack_graphics/program.hpp"
#include "renderstack_graphics/renderer.hpp"
#include "renderstack_graphics/uniform.hpp"
#include "renderstack_graphics/uniform_block.hpp"
#include "renderstack_graphics/uniform_buffer_range.hpp"
#include "renderstack_graphics/vertex_format.hpp"
#include "renderstack_mesh/geometry_mesh.hpp"
#include "renderstack_mesh/mesh.hpp"
#include "renderstack_scene/camera.hpp"
#include "renderstack_scene/light.hpp"
#include "renderstack_scene/viewport.hpp"
#include "renderstack_toolkit/gl.hpp"
#include "renderstack_toolkit/math_util.hpp"
#include "renderstack_toolkit/strong_gl_enums.hpp"
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

#define LOG_CATEGORY &log_renderer

using namespace renderstack::toolkit;
using namespace renderstack::graphics;
using namespace renderstack::mesh;
using namespace renderstack::scene;
using namespace renderstack;
using namespace gl;
using namespace glm;
using namespace std;

Light_debug_renderer::Light_debug_renderer()
    : service("Light_debug_renderer")
{
}

void Light_debug_renderer::connect(
    const shared_ptr<renderstack::graphics::Renderer>& renderer_,
    const shared_ptr<Programs>&                        programs_,
    const shared_ptr<Light_mesh>&                      light_mesh_
)
{
    base_connect(renderer_, programs_, light_mesh_);

    depends_on(renderer_);
    depends_on(programs_);
}

void Light_debug_renderer::initialize_service()
{
    base_initialize_service();

    m_debug_light_render_states.depth.enabled                  = true;
    m_debug_light_render_states.depth.function                 = gl::depth_function::l_equal;
    m_debug_light_render_states.face_cull.enabled              = true;
    m_debug_light_render_states.blend.enabled                  = true;
    m_debug_light_render_states.blend.rgb.equation_mode        = gl::blend_equation_mode::func_add;
    m_debug_light_render_states.blend.rgb.source_factor        = gl::blending_factor_src::src_alpha;
    m_debug_light_render_states.blend.rgb.destination_factor   = gl::blending_factor_dest::one;
    m_debug_light_render_states.blend.alpha.equation_mode      = gl::blend_equation_mode::func_add;
    m_debug_light_render_states.blend.alpha.source_factor      = gl::blending_factor_src::one;
    m_debug_light_render_states.blend.alpha.destination_factor = gl::blending_factor_dest::one;
}

void Light_debug_renderer::light_pass(const Light_collection& lights, const Camera &camera)
{
    slog_trace("Base_renderer::light_pass(lights.size() = %u)\n", static_cast<unsigned int>(lights.size()));

    bind_default_framebuffer();

    //t.execute(&m_camera_render_states);
    //r.set_texture(0, m_linear_rt[0]);
    //r.reset_texture(1, renderstack::graphics::texture_target::texture_2d, nullptr);
    //r.reset_texture(2, renderstack::graphics::texture_target::texture_2d, nullptr);
    //r.reset_texture(3, renderstack::graphics::texture_target::texture_2d, nullptr);
    //r.reset_texture(4, renderstack::graphics::texture_target::texture_2d, nullptr);
    //r.reset_texture(5, renderstack::graphics::texture_target::texture_2d, nullptr);
    //r.set_program(programs()->camera);

    gl::viewport(0, 0, width_full(), height_full());

    if (false)
    {
        gl::clear_color(0.1f, 0.1f, 0.1f, 1.0f);
    }
    gl::clear_depth(1.0f);
    gl::clear(GL_DEPTH_BUFFER_BIT);

    //gl::viewport(0, 0, width(), height());

    update_lights_models(lights, camera);
    update_lights(lights, camera);
    update_camera(camera);

    auto &r = renderer();
    auto &t = r.track();

    t.execute(&m_debug_light_render_states);
    r.set_program(programs()->debug_light.get());

    bind_camera();

    log_trace("for (auto l : lights)\n");
    std::size_t light_index = 0;
    for (auto l : lights) {
        if (light_index == max_lights()) {
            break;
        }

        auto geometry_mesh = light_mesh()->get_light_mesh(l);
        auto vertex_stream = geometry_mesh->vertex_stream().get();
        auto mesh          = geometry_mesh->get_mesh();
        assert(vertex_stream != nullptr);

        log_trace("light_index = %u\n", static_cast<unsigned int>(light_index));

        bind_light_model(light_index);
        bind_light(light_index);

        //gl::begin_mode::value         begin_mode    = gl::begin_mode::points;
        gl::begin_mode::value         begin_mode    = gl::begin_mode::lines;
        Index_range                   index_range   = geometry_mesh->edge_line_indices();
        GLsizei                       count         = static_cast<GLsizei>(index_range.index_count);
        gl::draw_elements_type::value index_type    = gl::draw_elements_type::unsigned_int;
        GLvoid *                      index_pointer = static_cast<GLvoid*>((index_range.first_index + mesh->first_index()) * mesh->index_buffer()->stride());
        GLint                         base_vertex   = configuration::can_use.draw_elements_base_vertex
            ? static_cast<GLint>(mesh->first_vertex())
            : 0;

        assert(index_range.index_count > 0);

        r.draw_elements_base_vertex(
            *vertex_stream,
            begin_mode,
            count,
            index_type,
            index_pointer,
            base_vertex
        );

        ++light_index;
    }
}
