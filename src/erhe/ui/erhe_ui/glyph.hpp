#pragma once

#if defined(ERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE)
#include "erhe_ui/rectangle.hpp"
#include "erhe_ui/bitmap.hpp"
#include "erhe_ui/glyph.hpp"
#include "erhe_verify/verify.hpp"

#include <freetype/freetype.h>
#include <freetype/ftstroke.h>
#include <freetype/ftglyph.h>
#include <freetype/ftbitmap.h>

#include <Rect.h>

#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace erhe::ui {

class Glyph
{
public:
    Glyph(
        FT_Library    library,
        FT_Face       font_face,
        unsigned char c,
        float         bolding,
        float         outline_thickness,
        int           hint_mode
    );

    [[nodiscard]] auto buffer() const -> const std::vector<unsigned char>&
    {
        return m_buffer;
    }

    void dump() const;

    rbp::Rect    atlas_rect       {0, 0, 0, 0};     // atlas space
    Rectangle    font_rect;      // font metric space
    FT_ULong     char_code        {0};
    unsigned int glyph_index      {0};
    float        outline_thickness{0.0f};

    class Metrics
    {
    public:
        float bearing_x         {0.0f};
        float width             {0.0f};
        float bearing_y         {0.0f};
        float height            {0.0f};
        float horizontal_advance{0.0f};

        int x0        {0};
        int y0        {0};
        int x_advance {0};
    };

    Metrics metrics;

    class BitmapLayout
    {
    public:
        int left  {0};
        int top   {0};
        int bottom{0};
        int right {0};
        int width {0};
        int height{0};
        int pitch {0};
    };

    BitmapLayout bitmap;

private:
    static void validate(const FT_Error error)
    {
        if (error != FT_Err_Ok) {
            ERHE_FATAL("freetype error");
        }
    }

    std::vector<unsigned char> m_buffer;
};

} // namespace erhe::ui

#endif // defined(ERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE)

