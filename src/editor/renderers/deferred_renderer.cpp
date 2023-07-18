#include "renderers/deferred_renderer.hpp"
#include "renderers/light_mesh.hpp"
#include "renderstack_geometry/shapes/cone.hpp"
#include "renderstack_geometry/shapes/triangle.hpp" // quad is currently here...
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
#include "renderstack_toolkit/platform.hpp"
#include "renderstack_toolkit/strong_gl_enums.hpp"
#include <cmath>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

using namespace renderstack::toolkit;
using namespace renderstack::graphics;
using namespace renderstack::mesh;
using namespace renderstack::scene;
using namespace renderstack;
using namespace gl;
using namespace glm;
using namespace std;

Deferred_renderer::Deferred_renderer()
    : service      {"Deferred_renderer"}
    , m_gbuffer_fbo{0}
    , m_linear_fbo {0}
    , m_stencil_rbo{0}
{
}

void Deferred_renderer::connect(
    shared_ptr<Renderer>      renderer,
    shared_ptr<Programs>      programs,
    shared_ptr<Quad_renderer> quad_renderer,
    shared_ptr<Light_mesh>    light_mesh
)
{
    base_connect(renderer, programs, light_mesh);
    m_quad_renderer = quad_renderer;

    depends_on(renderer);
    depends_on(programs);
}

void Deferred_renderer::initialize_service()
{
    base_initialize_service();

    m_gbuffer_render_states.depth.enabled     = true;
    m_gbuffer_render_states.face_cull.enabled = true;

    // Nothing to change in, use default render states:
    // m_show_rt_render_states

    m_light_stencil_render_states.depth.enabled    = true;
    m_light_stencil_render_states.depth.depth_mask = false;

    m_light_stencil_render_states.color_mask.red   = false;
    m_light_stencil_render_states.color_mask.green = false;
    m_light_stencil_render_states.color_mask.blue  = false;
    m_light_stencil_render_states.color_mask.alpha = false;

    m_light_stencil_render_states.face_cull.enabled = false;

    m_light_stencil_render_states.stencil.enabled              = true;
    m_light_stencil_render_states.stencil.back.z_fail_op       = gl::stencil_op_enum::keep;
    m_light_stencil_render_states.stencil.back.z_pass_op       = gl::stencil_op_enum::incr_wrap;
    m_light_stencil_render_states.stencil.back.stencil_fail_op = gl::stencil_op_enum::keep;
    m_light_stencil_render_states.stencil.back.function        = gl::stencil_function_enum::always;

    m_light_stencil_render_states.stencil.front.z_fail_op       = gl::stencil_op_enum::keep;
    m_light_stencil_render_states.stencil.front.z_pass_op       = gl::stencil_op_enum::decr_wrap;
    m_light_stencil_render_states.stencil.front.stencil_fail_op = gl::stencil_op_enum::keep;
    m_light_stencil_render_states.stencil.front.function        = gl::stencil_function_enum::always;

    m_light_stencil_render_states.stencil.separate = true;

    m_light_with_stencil_test_render_states.depth.enabled                = false;
    m_light_with_stencil_test_render_states.depth.depth_mask             = false;
    m_light_with_stencil_test_render_states.face_cull.enabled            = true;
    m_light_with_stencil_test_render_states.face_cull.cull_face_mode     = gl::cull_face_mode::front;
    m_light_with_stencil_test_render_states.stencil.enabled              = true;
    m_light_with_stencil_test_render_states.stencil.back.function        = gl::stencil_function_enum::not_equal;
    m_light_with_stencil_test_render_states.stencil.back.reference       = 0;
    m_light_with_stencil_test_render_states.stencil.front.function       = gl::stencil_function_enum::not_equal;
    m_light_with_stencil_test_render_states.stencil.front.reference      = 0;
    m_light_with_stencil_test_render_states.blend.enabled                = true;
    m_light_with_stencil_test_render_states.blend.rgb.equation_mode      = gl::blend_equation_mode::func_add;
    m_light_with_stencil_test_render_states.blend.rgb.source_factor      = gl::blending_factor_src::one;
    m_light_with_stencil_test_render_states.blend.rgb.destination_factor = gl::blending_factor_dest::one;
    m_light_with_stencil_test_render_states.stencil.back.z_fail_op       = gl::stencil_op_enum::replace;
    m_light_with_stencil_test_render_states.stencil.back.z_pass_op       = gl::stencil_op_enum::replace;
    m_light_with_stencil_test_render_states.stencil.front.z_fail_op      = gl::stencil_op_enum::replace;
    m_light_with_stencil_test_render_states.stencil.front.z_pass_op      = gl::stencil_op_enum::replace;

    m_light_render_states.depth.enabled                = false;
    m_light_render_states.depth.depth_mask             = false;
    m_light_render_states.face_cull.enabled            = true;
    m_light_render_states.face_cull.cull_face_mode     = gl::cull_face_mode::front;
    m_light_render_states.blend.enabled                = true;
    m_light_render_states.blend.rgb.equation_mode      = gl::blend_equation_mode::func_add;
    m_light_render_states.blend.rgb.source_factor      = gl::blending_factor_src::one;
    m_light_render_states.blend.rgb.destination_factor = gl::blending_factor_dest::one;
}

void Deferred_renderer::resize(int width, int height)
{
    base_resize(width, height);
    {
        if (m_gbuffer_fbo == 0)
        {
            gl::gen_framebuffers(1, &m_gbuffer_fbo);
        }

        gl::bind_framebuffer(GL_DRAW_FRAMEBUFFER, m_gbuffer_fbo);
        GLenum formats[] = {
            GL_RGBA32F, // 0 normal
            GL_RGBA32F, // 1 tangent
            GL_RGBA8,   // 2 albedo
            GL_RGBA32F  // 3 Material (TODO GL_RGBA8)
        };

        for (int i = 0; i < 4; ++i)
        {
            m_gbuffer_rt[i].reset();

            m_gbuffer_rt[i] = make_shared<Texture>(
                Texture::Target::texture_2d,
                formats[i],
                false,
                Base_renderer::width(),
                Base_renderer::height(),
                0
            );
            m_gbuffer_rt[i]->allocate_storage(renderer());
            m_gbuffer_rt[i]->set_mag_filter(gl::texture_mag_filter::nearest);
            m_gbuffer_rt[i]->set_min_filter(gl::texture_min_filter::nearest);
            m_gbuffer_rt[i]->set_wrap(0, gl::texture_wrap_mode::clamp_to_edge);
            m_gbuffer_rt[i]->set_wrap(1, gl::texture_wrap_mode::clamp_to_edge);

            gl::framebuffer_texture_2d(
                GL_DRAW_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0 + i,
                GL_TEXTURE_2D,
                m_gbuffer_rt[i]->gl_name(),
                0
            );
        }

        GLenum depth_format = use_stencil()
            ? GL_DEPTH32F_STENCIL8
            : GL_DEPTH_COMPONENT32F;
        GLenum attachment_point = use_stencil()
            ? GL_DEPTH_STENCIL_ATTACHMENT
            : GL_DEPTH_ATTACHMENT;

        m_depth.reset();
        m_depth = make_shared<Texture>(
            Texture::Target::texture_2d,
            depth_format,
            false,
            Base_renderer::width(),
            Base_renderer::height(),
            0
        );
        m_depth->set_mag_filter(gl::texture_mag_filter::nearest);
        m_depth->set_min_filter(gl::texture_min_filter::nearest);
        m_depth->set_wrap(0, gl::texture_wrap_mode::clamp_to_edge);
        m_depth->set_wrap(1, gl::texture_wrap_mode::clamp_to_edge);
        m_depth->allocate_storage(renderer());

        gl::framebuffer_texture_2d(
            GL_DRAW_FRAMEBUFFER,
            attachment_point,
            GL_TEXTURE_2D,
            m_depth->gl_name(),
            0
        );

        GLenum a = gl::check_framebuffer_status(GL_FRAMEBUFFER);
        if (a != GL_FRAMEBUFFER_COMPLETE)
        {
            const char *status = gl::enum_string(a);
            throw runtime_error(status);
        }
    }

    {
        if (m_linear_fbo == 0)
        {
            gl::gen_framebuffers(1, &m_linear_fbo);
        }

        gl::bind_framebuffer(GL_DRAW_FRAMEBUFFER, m_linear_fbo);
        GLenum formats[] = {GL_RGBA32F};

        for (int i = 0; i < 1; ++i)
        {
            m_linear_rt[i].reset();

            m_linear_rt[i] = make_shared<Texture>(
                Texture::Target::texture_2d,
                formats[i],
                false,
                Base_renderer::width(),
                Base_renderer::height(),
                0
            );
            m_linear_rt[i]->allocate_storage(renderer());
            m_linear_rt[i]->set_mag_filter(gl::texture_mag_filter::nearest);
            m_linear_rt[i]->set_min_filter(gl::texture_min_filter::nearest);
            m_linear_rt[i]->set_wrap(0, gl::texture_wrap_mode::clamp_to_edge);
            m_linear_rt[i]->set_wrap(1, gl::texture_wrap_mode::clamp_to_edge);

            gl::framebuffer_texture_2d(
                GL_DRAW_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0 + i,
                GL_TEXTURE_2D,
                m_linear_rt[i]->gl_name(),
                0
            );
        }

        if (use_stencil())
        {
            if (m_stencil_rbo == 0)
            {
                gl::gen_renderbuffers(1, &m_stencil_rbo);
            }

            gl::bind_renderbuffer(GL_RENDERBUFFER, m_stencil_rbo);

            gl::renderbuffer_storage(
                GL_RENDERBUFFER,
                GL_DEPTH32F_STENCIL8,
                Base_renderer::width(),
                Base_renderer::height()
            );
            gl::framebuffer_renderbuffer(
                GL_FRAMEBUFFER,
                GL_DEPTH_STENCIL_ATTACHMENT,
                GL_RENDERBUFFER,
                m_stencil_rbo
            );
        }

        GLenum a = gl::check_framebuffer_status(GL_FRAMEBUFFER);
        if (a != GL_FRAMEBUFFER_COMPLETE)
        {
            const char *status = gl::enum_string(a);
            throw runtime_error(status);
        }
    }

    gl::bind_framebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

void Deferred_renderer::bind_gbuffer_fbo()
{
    gl::bind_framebuffer(GL_DRAW_FRAMEBUFFER, m_gbuffer_fbo);

    GLenum draw_buffers[] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3 };
    gl::draw_buffers(4, draw_buffers);
}

void Deferred_renderer::fbo_clear()
{
    GLfloat albedo_clear[]   = {0.5f, 0.5f, 0.5f, 0.0f};
    GLfloat normal_clear[]   = {0.0f, 0.0f, -1.0f, 0.0f};
    GLfloat tangent_clear[]  = {0.0f, 1.0f, 0.0f, 0.0f};
    GLfloat material_clear[] = {0.5f, 0.5f, 0.5f, 0.5f};
    GLfloat one              = 1.0f;

    auto &r = renderer();
    r.reset_texture(0, Texture::Target::texture_2d, nullptr);
    r.reset_texture(1, Texture::Target::texture_2d, nullptr);
    r.reset_texture(2, Texture::Target::texture_2d, nullptr);
    r.reset_texture(3, Texture::Target::texture_2d, nullptr);
    r.reset_texture(4, Texture::Target::texture_2d, nullptr);

    GLenum a = gl::check_framebuffer_status(GL_FRAMEBUFFER);
    if (a != GL_FRAMEBUFFER_COMPLETE)
    {
        throw runtime_error("FBO is not complete");
    }

    gl::clear_buffer_fv(GL_COLOR, 0, &normal_clear[0]);
    gl::clear_buffer_fv(GL_COLOR, 1, &tangent_clear[0]);
    gl::clear_buffer_fv(GL_COLOR, 2, &albedo_clear[0]);
    gl::clear_buffer_fv(GL_COLOR, 3, &material_clear[0]);
    gl::clear_buffer_fv(GL_DEPTH, 0, &one);

    // GLfloat normal_tangent_clear  [4];
    // vec3 N = vec3(0.0f, 0.0f, -1.0f);
    // vec3 T = vec3(0.0f, 1.0f,  0.0f);
    //
    // cartesian_to_spherical(N, normal_tangent_clear[0], normal_tangent_clear[1]);
    // cartesian_to_spherical(T, normal_tangent_clear[2], normal_tangent_clear[3]);
}

void Deferred_renderer::geometry_pass(
    const Material_collection& materials,
    const Model_collection&    models,
    const Camera&              camera
)
{
    bind_gbuffer_fbo();

    gl::viewport(0, 0, width(), height());

    fbo_clear();

    if (models.size() == 0)
    {
        return;
    }

    update_camera(camera);
    update_models(models, camera);
    update_materials(materials);

    auto &r = renderer();
    auto &t = r.track();
    t.reset();
    t.execute(&m_gbuffer_render_states);

    auto p = programs()->gbuffer.get();
    assert(p != nullptr);
    r.set_program(p);

    bind_camera();

    std::size_t model_index = 0;
    for (auto model : models)
    {
        auto        geometry_mesh  = model->geometry_mesh;
        auto        Vertex_stream  = geometry_mesh->vertex_stream();
        auto        mesh           = geometry_mesh->get_mesh();
        auto        material       = model->material;
        std::size_t material_index = material->index;

        gl::begin_mode::value         begin_mode    = gl::begin_mode::triangles;
        Index_range                   index_range   = geometry_mesh->fill_indices();
        GLsizei                       count         = static_cast<GLsizei>(index_range.index_count);
        gl::draw_elements_type::value index_type    = gl::draw_elements_type::unsigned_int;
        GLvoid *                      index_pointer = static_cast<GLvoid*>((index_range.first_index + mesh->first_index()) * mesh->index_buffer()->stride());
        GLint                         base_vertex   = configuration::can_use.draw_elements_base_vertex
            ? static_cast<GLint>(mesh->first_vertex())
            : 0;

        bind_model(model_index);
        bind_material(material_index);

        auto vertex_stream = model->geometry_mesh->vertex_stream().get();
        assert(vertex_stream != nullptr);

        r.draw_elements_base_vertex(
            *vertex_stream,
            begin_mode,
            count,
            index_type,
            index_pointer,
            base_vertex
        );
        ++model_index;
    }
}

void Deferred_renderer::bind_linear_fbo()
{
    GLenum draw_buffers[] = {GL_COLOR_ATTACHMENT0};
    gl::draw_buffers(1, draw_buffers);

    gl::bind_framebuffer(GL_DRAW_FRAMEBUFFER, m_linear_fbo);
    if (use_stencil())
    {
        gl::bind_framebuffer(GL_READ_FRAMEBUFFER, m_gbuffer_fbo);
        gl::blit_framebuffer(
            0, 0, width(), height(),
            0, 0, width(), height(),
            GL_DEPTH_BUFFER_BIT,
            GL_NEAREST
        );
    }

    GLenum a = gl::check_framebuffer_status(GL_FRAMEBUFFER);
    if (a != GL_FRAMEBUFFER_COMPLETE)
    {
        throw runtime_error("FBO is not complete");
    }
}

void Deferred_renderer::light_pass(const Light_collection &lights, const Camera &camera)
{
    bind_linear_fbo();

    gl::viewport(0, 0, width(), height());
    gl::clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    gl::clear_stencil(0);
    if (use_stencil())
    {
        gl::clear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }
    else
    {
        gl::clear(GL_COLOR_BUFFER_BIT);
    }

    update_lights_models(lights, camera);
    update_lights(lights, camera);

    mat4 world_from_view        = camera.frame.world_from_local.matrix();
    vec3 view_position_in_world = vec3(world_from_view * vec4(0.0f, 0.0f, 0.0f, 1.0f));

    auto &r = renderer();
    auto &t = r.track();

    // Don't bind emission texture for now
    r.set_texture(0, m_gbuffer_rt[0].get()); // normal
    r.set_texture(1, m_gbuffer_rt[1].get()); // tangent
    r.set_texture(2, m_gbuffer_rt[2].get()); // albedo
    r.set_texture(3, m_gbuffer_rt[3].get()); // material
    //r.reset_texture(3, Texture::Target::texture_2d, nullptr);
    r.set_texture(4, m_depth.get());
    r.set_program(programs()->light_spot.get());

    bind_camera();

    std::size_t light_index = 0;
    if (!use_stencil())
    {
        t.execute(&m_light_render_states);
    }

    auto stencil_program = programs()->stencil.get();
    auto spot_program = programs()->light_spot.get();
    //auto point_program = programs()->light_point.get();
    auto directional_program = programs()->light_directional.get();
    assert(stencil_program != nullptr);
    assert(spot_program != nullptr);
    //assert(point_program != nullptr);
    assert(directional_program != nullptr);

    for (auto l : lights)
    {
        if (light_index == max_lights())
        {
            break;
        }

        auto geometry_mesh = light_mesh()->get_light_mesh(l);
        auto vertex_stream = geometry_mesh->vertex_stream().get();
        auto mesh          = geometry_mesh->get_mesh();
        assert(vertex_stream != nullptr);

        bind_light_model(light_index);
        bind_light(light_index);

        gl::begin_mode::value         begin_mode    = gl::begin_mode::triangles;
        const Index_range             &index_range   = geometry_mesh->fill_indices();
        GLsizei                       count         = static_cast<GLsizei>(index_range.index_count);
        gl::draw_elements_type::value index_type    = gl::draw_elements_type::unsigned_int;
        GLvoid *                      index_pointer = static_cast<GLvoid*>((index_range.first_index + mesh->first_index()) * mesh->index_buffer()->stride());
        GLint                         base_vertex   = configuration::can_use.draw_elements_base_vertex
            ? static_cast<GLint>(mesh->first_vertex())
            : 0;

        assert(index_range.index_count > 0);

        bool used_stencil = false;
        if (use_stencil() && (l->type == Light::Type::spot))
        {
            bool view_in_light = light_mesh()->point_in_light(view_position_in_world, l);
            if (!view_in_light)
            {
                t.execute(&m_light_stencil_render_states);
                r.set_program(stencil_program);

                r.draw_elements_base_vertex(*vertex_stream, begin_mode, count, index_type, index_pointer, base_vertex);
                used_stencil = true;
            }
        }

        switch (l->type)
        {
            //using enum Light_type;
            case Light_type::spot:
            {
                if (use_stencil())
                {
                    if (used_stencil)
                    {
                        t.execute(&m_light_with_stencil_test_render_states);
                    }
                    else
                    {
                        t.execute(&m_light_render_states);
                    }
                }
                r.set_program(spot_program);
                break;
            }

            case Light_type::directional:
            {
                if (use_stencil())
                {
                    t.execute(&m_light_render_states);
                }
                r.set_program(directional_program);
                break;
            }

            default:
            {
                assert(0);
            }
        }

        r.draw_elements_base_vertex(*vertex_stream, begin_mode, count, index_type, index_pointer, base_vertex);

        ++light_index;
    }

    bind_default_framebuffer();

    t.execute(&m_camera_render_states);
    r.set_texture(0, m_linear_rt[0].get());
    r.reset_texture(1, Texture::Target::texture_2d, nullptr);
    r.reset_texture(2, Texture::Target::texture_2d, nullptr);
    r.reset_texture(3, Texture::Target::texture_2d, nullptr);
    r.reset_texture(4, Texture::Target::texture_2d, nullptr);
    r.reset_texture(5, Texture::Target::texture_2d, nullptr);

    auto camera_program = programs()->camera.get();
    assert(camera_program != nullptr);
    r.set_program(camera_program);

    gl::viewport(0, 0, width_full(), height_full());
    //gl::enable(GL_FRAMEBUFFER_SRGB);
    m_quad_renderer->render_minus_one_to_one();
    //gl::disable(GL_FRAMEBUFFER_SRGB);
}

void Deferred_renderer::show_rt()
{
#if 0
   bind_default_framebuffer();

   auto &r = *m_renderer;
   auto &t = r.track();
   t.reset();
   m_show_rt_render_states.face_cull.set_enabled(false);
   t.execute(&m_show_rt_render_states);
   for (int i = 0; i < 4; ++i)
   {
      shared_ptr<Program> p = (i == 2)
            ? m_programs->show_rt_spherical
            : m_programs->show_rt;
      r.set_program(p);

      r.set_texture(0, m_rt[i]);

      mat4 identity = mat4(1.0f);
      //mat4 const &ortho = m_gui_renderer->ortho();
      mat4 scale;
      mat4 to_bottom_left;
      mat4 offset;
      create_translation(-1.0, -1.0, 0.0f, to_bottom_left);
      create_scale(0.33f, scale);
      create_translation(0.33f * i, 0.0, 0.0f, offset);

      mat4 transform = identity;

      transform = scale * transform;
      transform = to_bottom_left * transform;
      transform = offset * transform;

      if (p->use_uniform_buffers())
      {
         assert(m_programs->model_ubr);
         assert(m_programs->debug_ubr);

         unsigned char *start = m_programs->begin_edit_uniforms();
         ::memcpy(&start[m_programs->model_ubr->first_byte() + m_programs->model_block_access.clip_from_model], value_ptr(transform), 16 * sizeof(float));
         ::memcpy(&start[m_programs->debug_ubr->first_byte() + m_programs->debug_block_access.show_rt_transform], value_ptr(identity), 16 * sizeof(float));
         m_programs->model_ubr->flush(r);
         m_programs->debug_ubr->flush(r);
         m_programs->end_edit_uniforms();
      }
      else
      {
         gl::uniform_matrix_4fv(
            p->uniform_at(m_programs->model_block_access.clip_from_model),
            1, GL_FALSE, value_ptr(transform));
         gl::uniform_matrix_4fv(
            p->uniform_at(m_programs->debug_block_access.show_rt_transform),
            1, GL_FALSE, value_ptr(identity));
      }

      m_quad_renderer->render_zero_to_one();

      //int iw = m_application->width();
      //int ih = m_application->height();

      //gl::bind_framebuffer(gl::framebuffer_target::read_framebuffer, m_fbo);
      //gl::bind_framebuffer(gl::framebuffer_target::draw_framebuffer, 0);

      /*
      gl::blit_framebuffer(
         0, 0, iw, ih,
         0, 0, iw, ih,
         GL_COLOR_BUFFER_BIT,
         GL_NEAREST);*/
   }
#endif
}
