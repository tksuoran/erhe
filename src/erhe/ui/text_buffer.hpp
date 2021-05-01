#ifndef text_buffer_hpp_erhe_ui
#define text_buffer_hpp_erhe_ui


#include "erhe/primitive/mesh.hpp"
#include "erhe/ui/font.hpp"
#include "erhe/ui/rectangle.hpp"
#include <gsl/pointers>
#include <string>

namespace erhe::ui
{

class Font;

class Text_buffer
{
public:
    Text_buffer(const Font& font, unsigned int max_char_count = 3000);

    ~Text_buffer() = default;

    auto bounding_box() const noexcept
    -> Rectangle;

    auto line_height() const noexcept
    -> float;

    auto font() const
    -> gsl::not_null<const Font*>;

    void print(const std::string& text, int x, int y);

    void measure(const std::string& text);

    void print_center(const std::string& text, float x, float y);

private:
    const Font&      m_font;
    unsigned int     m_max_char_count{0};
    Rectangle        m_bounding_box;
    gsl::span<float> m_vertex_data;
    size_t           m_chars_printed{0};
};

} // namespace erhe::ui

#endif
