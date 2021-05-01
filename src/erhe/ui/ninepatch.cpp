#include "erhe/ui/ninepatch.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics_experimental/render_group.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/ui/gui_renderer.hpp"
#include "erhe/ui/log.hpp"
#include "erhe/ui/ninepatch_style.hpp"
#include "erhe/ui/style.hpp"

namespace erhe::ui
{

using erhe::graphics::Draw_key;

void make_quad_indices(gsl::span<uint16_t> span,
                       size_t &            offset,
                       uint16_t            v0,
                       uint16_t            v1,
                       uint16_t            v2,
                       uint16_t            v3)
{
    span[offset    ] = v0;
    span[offset + 1] = v1;
    span[offset + 2] = v2;
    span[offset + 3] = v3;
    offset += 4;
}

void make_quad_vertices(gsl::span<float> span, size_t& offset, float x, float y, float u, float v)
{
    span[offset    ] = x;
    span[offset + 1] = y;
    span[offset + 2] = u;
    span[offset + 3] = v;
    offset += 4;
}

Ninepatch::Ninepatch(Gui_renderer&                         gui_renderer,
                     gsl::not_null<const Ninepatch_style*> style)
    : m_style{style}
{
    // 16 vertices, 9 quads

    // 12 13 14 15
    //
    //  8  9 10 11
    //
    //  4  5  6  7
    //
    //  0  1  2  3
    m_mesh.allocate_vertex_buffer(gui_renderer.vertex_buffer(), 16);
    m_mesh.allocate_index_buffer(gui_renderer.index_buffer(), 9 * 6);

    auto raw_index_data = m_mesh.index_buffer()->map_elements(m_mesh.index_offset(),
                                                              m_mesh.index_count(),
                                                              gl::Map_buffer_access_mask::map_write_bit |
                                                              gl::Map_buffer_access_mask::map_invalidate_range_bit);

    auto index_data = gsl::span<uint16_t>(reinterpret_cast<uint16_t*>(raw_index_data.data()),
                                          raw_index_data.size_bytes() / sizeof(uint16_t));

    size_t offset{0};

    make_quad_indices(index_data, offset,  4,  5,  1,  0);
    make_quad_indices(index_data, offset,  5,  6,  2,  1);
    make_quad_indices(index_data, offset,  6,  7,  3,  2);

    make_quad_indices(index_data, offset,  8,  9,  5,  4);
    make_quad_indices(index_data, offset,  9, 10,  6,  5);
    make_quad_indices(index_data, offset, 10, 11,  7,  6);

    make_quad_indices(index_data, offset, 12, 13,  9,  8);
    make_quad_indices(index_data, offset, 13, 14, 10,  9);
    make_quad_indices(index_data, offset, 14, 15, 11, 10);

    m_mesh.index_buffer()->unmap();

    //Texture_bindings texture_bindings;
    Draw_key key(style->program);
    key.is_blend_enabled = true;
    key.set_texture(style->texture_unit, style->texture.get());

    auto index_count = static_cast<GLsizei>(mesh().index_count());
    //auto base_vertex = static_cast<GLint>(mesh().vertex_offset());
    m_draw = gui_renderer.render_group().make_draw(
        key,
        static_cast<uint32_t>(index_count),
        static_cast<uint32_t>(mesh().index_offset()),
        static_cast<uint32_t>(mesh().vertex_offset()));
}

void Ninepatch::place(float x0, float y0, float width, float height)
{
    log_ninepatch.trace("Ninepatch::place(x0 = {}, y0 = {}, width = {}, height = {})", x0, y0, width, height);

    m_size.x = width;
    m_size.y = height;

    // TODO(tksuoran@gmail.com): Use persistently mapped Multi_buffer?
    auto raw_vertex_data = m_mesh.vertex_buffer()->map_elements(m_mesh.vertex_offset(),
                                                                m_mesh.vertex_count(),
                                                                gl::Map_buffer_access_mask::map_write_bit |
                                                                gl::Map_buffer_access_mask::map_invalidate_range_bit);
    auto vertex_data = gsl::span<float>(reinterpret_cast<float*>(raw_vertex_data.data()),
                                        raw_vertex_data.size_bytes() / sizeof(float));

    std::array u{0.0f,
                 m_style->border_uv.x,
                 1.0f - m_style->border_uv.x,
                 1.0f};

    std::array v{0.0f,
                 m_style->border_uv.y,
                 1.0f - m_style->border_uv.y,
                 1.0f};

    std::array x{x0,
                 x0 + m_style->border_pixels.x,         // m_style->Texture.Size.Width;
                 x0 + width - m_style->border_pixels.x, // * style.Texture.Size.Width;
                 x0 + width};

    std::array y{y0,
                 y0 + m_style->border_pixels.y,          // * style.Texture.Size.Height;
                 y0 + height - m_style->border_pixels.y, // * style.Texture.Size.Height;
                 y0 + height};

    size_t offset(0);
    for (size_t yi = 0; yi < 4; ++yi)
    {
        for (int xi = 0; xi < 4; ++xi)
        {
            make_quad_vertices(vertex_data, offset, x[xi], y[yi], u[xi], v[3 - yi]);
        }
    }

    m_mesh.vertex_buffer()->unmap();
}

void Ninepatch::update_render(Gui_renderer& gui_renderer) const noexcept
{
    gui_renderer.record_uniforms(m_draw);
}

// void Ninepatch::render(Gui_renderer &renderer)
// {
//     log_ninepatch.trace("Ninepatch::render()");
//
//     auto begin_mode    = gl::Primitive_type::triangles;
//     auto count         = static_cast<GLsizei>(mesh().index_count());
//     auto index_type    = gl::Draw_elements_type::unsigned_short;
//     auto index_pointer = reinterpret_cast<GLvoid *>(mesh().index_offset() * mesh().index_buffer()->element_size());
//     auto base_vertex   = static_cast<GLint>(mesh().vertex_offset());
//
//     auto& d = renderer.render_group();
//
//     d.draw_elements(begin_mode, count, index_pointer, base_vertex);
// }

} // namespace erhe::ui

// 9 patch
//
// 4 x 4 = 16 vertices
// 3 x 3 =  9 quads    = 9 x 6 = 54 indices
//
// b = border
//
// m:::n   o::p       0  a (0,     0) x0, y0
// ::::    ::::       1  b (b,     0) x1, y0
// i:::j   k::l       2  c (1-b,   0) x2, y0
//     ::::           3  d (1,     0) x3, y0
//     ::::           4  e (0,     b) x0, y1
// e   f:::g  h       5  f (b,     b) x1, y1
// ::::    ::::       6  g (1-b,   b) x2, y1
// ::::    ::::       7  h (1,     b) x3, y1
// a:::b   c::d       8  i (0,   1-b) x0, y2
//                    9  j (b,   1-b)
// 12 13 14 15       10  k (1-b, 1-b)
//                   11  l (1,   1-b)
//  8  9 10 11       12  m (0,     1)
//                   13  n (b,     1)
//  4  5  6  7       14  o (1-b,   1)
//                   15  p (1,     1)
//  0  1  2  3
