#include "erhe_ui/color_picker.hpp"
#include "erhe_geometry/shapes/disc.hpp"
#include "erhe_geometry/shapes/triangle.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_primitive/geometry_mesh.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_ui/gui_renderer.hpp"
#include "erhe_ui/ninepatch_style.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace erhe::ui
{

using glm::vec2;
using glm::vec4;
using glm::mat4;

#if 0
Color_picker::Color_picker(Gui_renderer& gui_renderer, Style& style)
    : Area  {gui_renderer, style}
    , m_size{251.0f}
{
    if (style.ninepatch_style != nullptr)
    {
        m_ninepatch = std::make_unique<Ninepatch>(gui_renderer, *style.ninepatch_style);
    }

    float inner_radius   = 0.70f;
    m_disc_handle_radius = 0.90f;
    m_quad_edge_length   = std::sqrt(2.0f) * inner_radius;

    auto& gr = gui_renderer;
    auto& r  = gr.renderer();

    m_color_mesh     = std::make_unique<Geometry_mesh>(r, erhe::geometry::shapes::make_quad(std::sqrt(2.0)),                             Mesh::Normal_style::polygon_normals);
    m_handle_mesh    = std::make_unique<Geometry_mesh>(r, erhe::geometry::shapes::make_disc(4.0, 1.00f, 12, 2),                          Mesh::Normal_style::polygon_normals);
    m_handle2_mesh   = std::make_unique<Geometry_mesh>(r, erhe::geometry::shapes::make_disc(3.0, 2.00f, 12, 2),                          Mesh::Normal_style::polygon_normals);
    m_hsv_disc_mesh  = std::make_unique<Geometry_mesh>(r, erhe::geometry::shapes::make_disc(1.0, m_disc_handle_radius, 256, 2),          Mesh::Normal_style::polygon_normals);
    m_hsv_disc2_mesh = std::make_unique<Geometry_mesh>(r, erhe::geometry::shapes::make_disc(m_disc_handle_radius, inner_radius, 256, 2), Mesh::Normal_style::polygon_normals);
    m_hsv_quad_mesh  = std::make_unique<Geometry_mesh>(r, erhe::geometry::shapes::make_quad(inner_radius),                               Mesh::Normal_style::polygon_normals);

    fill_base_pixels = vec2(m_size, m_size);
}

void Color_picker::begin_place(Rectangle reference, vec2 grow_direction)
{
    Area::begin_place(reference, grow_direction);
    auto translation = create_translation(rect.min());
    m_background_frame = translation * renderer.ortho();
}

void Color_picker::draw_self(ui_context& context)
{
    auto& gr = renderer;
    auto& r  = gr.renderer();

    //r->push();
    gr.set_program(style.ninepatch_style->program);
    gr.set_texture(style.ninepatch_style->texture_unit, style.ninepatch_style->texture.get());
    gr.set_transform(m_background_frame);
    gr.set_color_scale(vec4(1.0f, 1.0f, 1.0f, 1.0f));
    gr.set_color_add(vec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (m_ninepatch)
    {
        m_ninepatch->render(gr);
    }

    {
        animate();
        mat4 d;
        d[0][0] = 1.0f;
        d[1][0] = 0.0f;
        d[2][0] = 0.0f;
        d[3][0] = 0.0f; // h
        d[0][1] = 0.0f;
        d[1][1] = 0.0f;
        d[2][1] = 0.0f;
        d[3][1] = 1.0f; // s
        d[0][2] = 0.0f;
        d[1][2] = 0.0f;
        d[2][2] = 0.0f;
        d[3][2] = 1.0f; // v
        d[0][3] = 0.0f;
        d[1][3] = 0.0f;
        d[2][3] = 0.0f;
        d[3][3] = 0.0f;
        mat4 d2;
        d2[0][0] = 1.0f;
        d2[1][0] = 0.0f;
        d2[2][0] = 0.0f;
        d2[3][0] = 0.0f; // h
        d2[0][1] = 0.0f;
        d2[1][1] = 0.0f;
        d2[2][1] = 0.0f;
        d2[3][1] = m_s; // s
        d2[0][2] = 0.0f;
        d2[1][2] = 0.0f;
        d2[2][2] = 0.0f;
        d2[3][2] = m_v; // v
        d2[0][3] = 0.0f;
        d2[1][3] = 0.0f;
        d2[2][3] = 0.0f;
        d2[3][3] = 0.0f;
        mat4 t;
        t[0][0] = 0.0f;
        t[1][0] = 0.0f;
        t[2][0] = 0.0f;
        t[3][0] = m_h; // h
        t[0][1] = 1.0f;
        t[1][1] = 0.0f;
        t[2][1] = 0.0f;
        t[3][1] = 0.0f; // s
        t[0][2] = 0.0f;
        t[1][2] = 1.0f;
        t[2][2] = 0.0f;
        t[3][2] = 0.0f; // v
        t[0][3] = 0.0f;
        t[1][3] = 0.0f;
        t[2][3] = 0.0f;
        t[3][3] = 0.0f;
        mat4 c;
        c[0][0] = 0.0f;
        c[1][0] = 0.0f;
        c[2][0] = 0.0f;
        c[3][0] = m_h; // h
        c[0][1] = 0.0f;
        c[1][1] = 0.0f;
        c[2][1] = 0.0f;
        c[3][1] = m_s; // s
        c[0][2] = 0.0f;
        c[1][2] = 0.0f;
        c[2][2] = 0.0f;
        c[3][2] = m_v; // v
        c[0][3] = 0.0f;
        c[1][3] = 0.0f;
        c[2][3] = 0.0f;
        c[3][3] = 0.0f;

        mat4 h;
        h[0][0] = 0.0f;
        h[1][0] = 0.0f;
        h[2][0] = 0.0f;
        h[3][0] = 0.0; // h
        h[0][1] = 0.0f;
        h[1][1] = 0.0f;
        h[2][1] = 0.0f;
        h[3][1] = 0.0; // s
        h[0][2] = 0.0f;
        h[1][2] = 0.0f;
        h[2][2] = 0.0f;
        h[3][2] = 1.0; // v
        h[0][3] = 0.0f;
        h[1][3] = 0.0f;
        h[2][3] = 0.0f;
        h[3][3] = 0.0f;

        mat4 h0;
        h0[0][0] = 0.0f;
        h0[1][0] = 0.0f;
        h0[2][0] = 0.0f;
        h0[3][0] = 0.0; // h
        h0[0][1] = 0.0f;
        h0[1][1] = 0.0f;
        h0[2][1] = 0.0f;
        h0[3][1] = 0.0; // s
        h0[0][2] = 0.0f;
        h0[1][2] = 0.0f;
        h0[2][2] = 0.0f;
        h0[3][2] = 0.0; // v
        h0[0][3] = 0.0f;
        h0[1][3] = 0.0f;
        h0[2][3] = 0.0f;
        h0[3][3] = 0.0f;

        gr.set_program(style.program);

        gr.blend_disable();

        auto vbo     = m_color_mesh->get_mesh()->vertex_buffer().get();

        //auto old_vbo =
        static_cast<void>(r.set_buffer(Buffer::Target::array_buffer, vbo));

        // TODO(tksuoran@gmail.com): must access VAO instead!
        assert(false);
        auto ibo = m_color_mesh->get_mesh()->index_buffer().get();

        //auto old_ibo =
        static_cast<void>(r.set_buffer(Buffer::Target::element_array_buffer, ibo));

        gr.set_transform(m_hsv_transform);
        gr.set_hsv_matrix(c);
        gr.set_color_scale(vec4(1.0f, 1.0f, 1.0f, 1.0f));
        gr.set_color_add(vec4(0.0f, 0.0f, 0.0f, 1.0f));
        //m_color_mesh->render(m_color_mesh->fill_indices());

        gr.set_hsv_matrix(d);
        //m_hsv_disc_mesh->render(gl::begin_mode::triangles, m_hsv_disc_mesh->fill_indices());

        gr.set_hsv_matrix(d2);
        //m_hsv_disc2_mesh->render(gl::begin_mode::triangles, m_hsv_disc2_mesh->fill_indices());

        gr.set_hsv_matrix(t);
        gr.set_color_add(vec4(0.0f, 0.0f, 0.0f, 0.5f));
        //m_hsv_quad_mesh->render(gl::begin_mode::triangles, m_hsv_quad_mesh->fill_indices());

        if (rect.hit(context.mouse))
        {
            if (context.mouse_buttons[0 /* TODO(tksuoran@gmail.com): glfw_mouse_button_left*/])
            {
                double x = context.mouse.x;
                double y = context.mouse.y;
                std::array<unsigned char, 4> data{0, 0, 0, 0};

                gl::read_pixels(std::round(x), std::round(y), 1, 1, gl::pixel_format::rgba, gl::pixel_type::unsigned_byte, &data[0]);

                float r2 = static_cast<float>(data[0]) / 255.0f;
                float g2 = static_cast<float>(data[1]) / 255.0f;
                float b2 = static_cast<float>(data[2]) / 255.0f;
                float h2{0.0f};
                float s2{0.0f};
                float v2{0.0f};

                erhe::math::rgb_to_hsv(r2, g2, b2, h2, s2, v2);
                if (data[3] == 255)
                {
                    m_h = h2 / 360.0f;
                }
                else if (data[3] == 128)
                {
                    m_s = s2;
                    m_v = v2;
                }
            }
        }

#if 0
      // draw handles
      {
         gr->begin_edit();
         gr->set_transform(m_disc_handle_transform);
         gr->set_hsv_matrix(h0);
         gr->set_color_add(vec4(0.0f, 0.0f, 0.0f, 0.0f));
         gr->end_edit();
         m_handle_mesh->render(gl::begin_mode::triangles, m_handle_mesh->fill_indices());

         gr->begin_edit();
         gr->set_hsv_matrix(h);
         gr->end_edit();
         m_handle2_mesh->render(gl::begin_mode::triangles, m_handle_mesh->fill_indices());

         gr->begin_edit();
         gr->set_transform(m_quad_handle_transform);
         gr->set_hsv_matrix(h0);
         gr->end_edit();
         m_handle_mesh->render(gl::begin_mode::triangles, m_handle_mesh->fill_indices());

         gr->begin_edit();
         gr->set_hsv_matrix(h);
         gr->end_edit();
         m_handle2_mesh->render(gl::begin_mode::triangles, m_handle_mesh->fill_indices());
      }
#endif
    }

    gr.prepare();
}

void Color_picker::animate()
{
    float scale = (m_size - 10.0f) / 2.0f;

    mat4 o = renderer.ortho();

    auto t = create_translation(rect.min() + rect.half_size());
    //m_background_frame = o * t;

    m_disc_handle_transform = o * t;

    auto qh = create_translation(rect.min() +
                                     rect.half_size() - (0.5f * scale * vec2(m_quad_edge_length)) +
                                     (scale * vec2(m_s * m_quad_edge_length, m_v * m_quad_edge_length)));
    m_quad_handle_transform = o * qh;

    float sin = m_disc_handle_radius * std::sin(m_h * 2.0f * glm::pi<float>());
    float cos = m_disc_handle_radius * std::cos(m_h * 2.0f * glm::pi<float>());
    auto dh = create_translation(rect.min() + rect.half_size() + scale * vec2(cos, sin));
    m_disc_handle_transform = o * dh;

    auto s = create_scale(scale);

    //auto r = create_rotation(m_h * 2.0f * pi<float>(), vec3(0.0f, 0.0f, 1.0f));

    m_hsv_transform = o * t * s;
    //m_quad_model_to_clip = m_disc_model_to_clip * r;
}
#endif

} // namespace erhe::ui
