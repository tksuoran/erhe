#if defined(ERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE)
#include <fmt/printf.h>

#include <freetype/freetype.h>
#include <freetype/ftbitmap.h>
#include <freetype/ftglyph.h>
#include <freetype/ftstroke.h>

#include "erhe/ui/bitmap.hpp"
#include "erhe/ui/glyph.hpp"
#include "erhe/ui/log.hpp"

#include <cstdio>
#include <map>
#include <stdexcept>

namespace erhe::ui
{


Glyph::Glyph(
    FT_Library          library,
    FT_Face             font_face,
    const unsigned char c,
    const float         bolding,
    const float         outline_thickness,
    const int           hint_mode
)
    : char_code        {c}
    , outline_thickness{outline_thickness}
{
    glyph_index = FT_Get_Char_Index(font_face, char_code);
    if (glyph_index == 0)
    {
        return;
    }

    const bool outline = outline_thickness > 0.0f;
    const FT_Int32 load_flags = outline ? 0 : FT_LOAD_RENDER;

    validate(FT_Load_Glyph(font_face, glyph_index, load_flags | hint_mode));

    const bool has_width  = (font_face->glyph->metrics.width != 0);
    const bool has_height = (font_face->glyph->metrics.height != 0);
    const bool render     = has_width && has_height;

    metrics.bearing_x          = static_cast<float>(font_face->glyph->metrics.horiBearingX / 64.0f);
    metrics.width              = static_cast<float>(font_face->glyph->metrics.width        / 64.0f);
    metrics.bearing_y          = static_cast<float>(font_face->glyph->metrics.horiBearingY / 64.0f);
    metrics.height             = static_cast<float>(font_face->glyph->metrics.height       / 64.0f);
    metrics.horizontal_advance = static_cast<float>(font_face->glyph->metrics.horiAdvance  / 64.0f);

    metrics.x0        = static_cast<int>(std::floor(metrics.bearing_x + 0.5f));
    metrics.y0        = static_cast<int>(std::floor(metrics.bearing_y - metrics.height + 0.5f));
    metrics.x_advance = static_cast<int>(std::floor(metrics.horizontal_advance));

    if (!render)
    {
        return;
    }

    FT_Bitmap ft_bitmap;
    FT_Bitmap_New(&ft_bitmap);

    if (outline)
    {
        FT_Stroker stroker;

        validate(FT_Stroker_New(library, &stroker));

        FT_Stroker_Set(
            stroker,
            static_cast<int>(outline_thickness * 64.0f),
            FT_STROKER_LINECAP_ROUND,
            FT_STROKER_LINEJOIN_ROUND,
            0
        );

        FT_GlyphRec_* glyph{nullptr};
        validate(FT_Get_Glyph(font_face->glyph, &glyph));

        auto* stroked_glyph{glyph};
        validate(FT_Glyph_Stroke(&stroked_glyph, stroker, 0));
        //validate(FT_Glyph_StrokeBorder(&stroked_glyph, stroker, 0, 0));

        auto* bitmap_glyph{stroked_glyph};
        validate(FT_Glyph_To_Bitmap(&bitmap_glyph, FT_RENDER_MODE_NORMAL, nullptr, 1));

        auto* ft_bitmap_glyph = reinterpret_cast<FT_BitmapGlyphRec_ *>(bitmap_glyph);
        validate(FT_Bitmap_Copy(library, &ft_bitmap_glyph->bitmap, &ft_bitmap));

        bitmap.left = ft_bitmap_glyph->left;
        bitmap.top = ft_bitmap_glyph->top;

        FT_Done_Glyph(bitmap_glyph);
        FT_Done_Glyph(glyph);
        FT_Stroker_Done(stroker);
    }
    else
    {
        bitmap.left = font_face->glyph->bitmap_left;
        bitmap.top  = font_face->glyph->bitmap_top;
        FT_Bitmap_Copy(library, &font_face->glyph->bitmap, &ft_bitmap);
    }

    if (bolding > 0.0f)
    {
        const int i_bolding = static_cast<int>(bolding * 64.0f);
        validate(FT_Bitmap_Embolden(library, &ft_bitmap, i_bolding, 0));
    }

    bitmap.width  = static_cast<int>(ft_bitmap.width);
    bitmap.height = static_cast<int>(ft_bitmap.rows);
    bitmap.pitch  = ft_bitmap.pitch;
    bitmap.bottom = bitmap.top - bitmap.height;
    bitmap.right  = bitmap.left + bitmap.width;

    m_buffer.resize(bitmap.height * bitmap.pitch);

    for (int iy = 0; iy < bitmap.height; ++iy)
    {
        for (int ix = 0; ix < bitmap.width; ++ix)
        {
            const int           src_x   = ix;
            const int           src_y   = bitmap.height - 1 - iy;
            const std::size_t   address = src_x + (src_y * bitmap.pitch);
            const unsigned char value   = ft_bitmap.buffer[address];
            m_buffer[address] = value;
        }
    }
    FT_Bitmap_Done(library, &ft_bitmap);

    //dump();
}

void Glyph::dump() const
{
    const char* shades = " .:#";
    fmt::print("\nglyph index = {} code = '{}': width = {} height = {} left = {} top = {} outline = {}\n",
               glyph_index, static_cast<char>(char_code), bitmap.width, bitmap.height, bitmap.left, bitmap.top, outline_thickness);
    for (int iy = 0; iy < bitmap.height; ++iy)
    {
        fputc('|', stdout);
        for (int ix = 0; ix < bitmap.width; ++ix)
        {
            const auto offset = ix + (iy * bitmap.pitch);
            const auto value  = m_buffer[offset];
            fputc(shades[value / 64], stdout);
        }
        fputc('|', stdout);
        fputc('\n', stdout);
    }
    fmt::print("______________________________\n");
}

} // namespace erhe::ui

#endif // defined(ERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE)
