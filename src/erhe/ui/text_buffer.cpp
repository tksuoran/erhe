#include "erhe/ui/text_buffer.hpp"
#include "erhe/graphics_experimental/render_group.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/primitive/mesh.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/ui/gui_renderer.hpp"
#include "erhe/ui/log.hpp"
#include "erhe/ui/style.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <vector>

namespace erhe::ui
{

using erhe::graphics::Scoped_buffer_mapping;
using erhe::graphics::Draw_key;
using erhe::log::Log;

Text_buffer::Text_buffer(gsl::not_null<const Style*> style,
                         const unsigned int          max_chars)
    : m_style    {style}
    , m_max_chars{max_chars}
{
    auto index_map = Scoped_buffer_mapping<uint16_t>(*m_mesh.index_buffer(),
                                                     m_mesh.index_offset(),
                                                     m_mesh.index_count(),
                                                     gl::Map_buffer_access_mask::map_write_bit |
                                                     gl::Map_buffer_access_mask::map_invalidate_range_bit);
    auto index_data = index_map.span();

    size_t offset{0};
    uint16_t vertex_index{0};
    for (unsigned int i = 0; i < m_max_chars; ++i)
    {
        index_data[offset + 0] = vertex_index;
        index_data[offset + 1] = vertex_index + 1;
        index_data[offset + 2] = vertex_index + 2;
        index_data[offset + 3] = vertex_index + 3;
        index_data[offset + 4] = 0xffffu;
        vertex_index += 4;
        offset += 5;
    }
}

Text_buffer::Text_buffer(gsl::not_null<const Style*> style,
                         const unsigned int          max_chars)
    : Text_buffer{renderer.vertex_buffer(), renderer.index_buffer(), style, max_chars}
{
}

Text_buffer::Text_buffer(gsl::not_null<const Style*> style,
                         const unsigned int          max_chars)
    : Text_buffer{{}, {}, style, max_chars}
{
}


auto Text_buffer::font() const
-> gsl::not_null<const Font*>
{
    return m_style->font;
}

auto Text_buffer::draw() const noexcept
-> gl::Draw_elements_indirect_command
{
    auto index_count = static_cast<uint32_t>(m_chars_printed * 5); // quad triangle fan + restart = 4 + 1 = 5 indices
    auto first_index = static_cast<uint32_t>(m_mesh.index_offset());
    auto base_vertex = static_cast<uint32_t>(m_mesh.vertex_offset());
    return gl::Draw_elements_indirect_command{index_count, 1, first_index, base_vertex, 0};
}

void Text_buffer::begin_print()
{
    m_chars_printed = 0;
    m_bounding_box.reset_for_grow();
}

auto Text_buffer::end_print()
-> size_t
{
    m_vertex_data = {};
    m_mesh.vertex_buffer()->flush_and_unmap_elements(4 * m_chars_printed);
    return m_chars_printed;
}

void Text_buffer::print(const std::string& text, const int x, const int y)
{
    Expects(m_chars_printed <= m_max_chars);

    if (m_chars_printed == m_max_chars)
    {
        return;
    }

    size_t char_count            = std::min(text.size(), m_max_chars - m_chars_printed);
    size_t vertex_count_per_char = 4;
    size_t vec4_size             = 4 * sizeof(float);
    size_t byte_count_per_char   = vec4_size * vertex_count_per_char;
    auto   destination           = m_vertex_data.subspan(byte_count_per_char * m_chars_printed,
                                                         byte_count_per_char * char_count);

    m_bounding_box.reset_for_grow();
    m_chars_printed += m_style->font->print(text,
                                            m_bounding_box,
                                            destination,
                                            static_cast<float>(x), static_cast<float>(y),
                                            char_count);
    Ensures(m_chars_printed <= m_max_chars);
}

void Text_buffer::measure(const std::string& text)
{
    m_bounding_box.reset_for_grow();

    if (text.empty())
    {
        return;
    }

    m_style->font->measure(text, m_bounding_box);
}

void Text_buffer::print_center(const std::string& text, const float x, const float y)
{
    measure(text);
    glm::vec2 p(x, y);
    p -= m_bounding_box.half_size();
    print(text,
          static_cast<int>(std::ceil(p.x)),
          static_cast<int>(std::ceil(p.y)));
}

auto Text_buffer::bounding_box() const noexcept
-> Rectangle
{
    return m_bounding_box;
}

auto Text_buffer::line_height() const noexcept
-> float
{
    return m_style->font->line_height();
}

} // namespace erhe::ui
