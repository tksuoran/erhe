#include "erhe/ui/font.hpp"

#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/texture.hpp"

#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/ui/bitmap.hpp"
#include "erhe/ui/glyph.hpp"
#include "erhe/ui/log.hpp"

#include "Tracy.hpp"

#include "SkylineBinPack.h" // RectangleBinPack

#include <fmt/printf.h>

#include <freetype/freetype.h>
#include <freetype/ftbitmap.h>
#include <freetype/ftglyph.h>
#include <freetype/ftstroke.h>

#include <hb.h>
#include <hb-ft.h>

#include <map>
#include <memory>
#include <stdexcept>


namespace erhe::ui
{

using erhe::graphics::Texture;
using std::map;
using std::shared_ptr;
using std::unique_ptr;
using std::make_shared;
using std::make_unique;

Font::~Font()
{
    ZoneScoped;

    hb_font_destroy(m_harfbuzz_font);
    validate(FT_Done_Face(m_freetype_face));
    validate(FT_Done_FreeType(m_freetype_library));
}

Font::Font(const std::filesystem::path& path, unsigned int size, float outline_thickness)
    : m_path             {path}
    , m_bolding          {(size > 10) ? 0.5f : 0.0f}
    , m_outline_thickness{outline_thickness}
{
    ZoneScoped;

    log_font.trace("Font::Font(path = {}, size = {}, outline_thickness = {})\n", path, size, outline_thickness);

    if (m_hinting)
    {
        //m_hint_mode = FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_LIGHT;
        //m_hint_mode = FT_LOAD_FORCE_AUTOHINT;
        m_hint_mode = 0;
    }
    else
    {
        m_hint_mode = FT_LOAD_NO_HINTING; // NOLINT(hicpp-signed-bitwise)
    }

    m_chars = " \"\\@#$%&/" // latin
              "0123456789"
              "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
              "abcdefghijklmnopqrstuvwxyz"
              "<>*+-.,:;=!?'`^~_|()[]{}";

    m_pixel_size = size;

    render();
}

void Font::validate(int /*FT_Error*/ error)
{
    VERIFY(error == FT_Err_Ok);
}

void Font::render()
{
    ZoneScoped;

    log_font.trace("Font::render()\n");

    validate(FT_Init_FreeType(&m_freetype_library));

    {
        FT_Int major{0};
        FT_Int minor{0};
        FT_Int patch{0};
        FT_Library_Version(m_freetype_library, &major, &minor, &patch);
        log_font.trace("Freetype version {}.{}.{}\n", major, minor, patch);
    }

    validate(FT_New_Face(m_freetype_library, m_path.string().c_str(), 0, &m_freetype_face));
    validate(FT_Select_Charmap(m_freetype_face, FT_ENCODING_UNICODE));
    FT_Face face = m_freetype_face;

    auto xsize = m_pixel_size << 6u;
    auto ysize = m_pixel_size << 6u;

    validate(FT_Set_Char_Size(m_freetype_face, xsize, ysize, m_dpi, m_dpi));

    m_harfbuzz_font = hb_ft_font_create(m_freetype_face, nullptr);

    trace_info();

    m_line_height = std::ceil(static_cast<float>(face->size->metrics.height) / 64.0f);

    using GlyphSet = map<char, unique_ptr<Glyph>>;

    GlyphSet glyphs;

    std::vector<float> outline_sizes;
    for (float outline_thickness = m_outline_thickness;
         outline_thickness > 0.0f;
         outline_thickness -= 10.0f
    )
    {
        outline_sizes.emplace_back(outline_thickness);
    }

    std::vector<GlyphSet> outline_glyphs_vector(outline_sizes.size());

    map<char, Glyph::BitmapLayout> glyph_box; // all glyphs fit inside

    // First pass: Render glyphs to bitmaps, keep track of glyph box
    for (auto c : m_chars)
    {
        const auto uc = static_cast<unsigned char>(c);
        glyphs[c] = make_unique<Glyph>(m_freetype_library, face, uc, m_bolding, 0.0f, m_hint_mode);
        const auto glyph = glyphs[c].get();
        glyph_box[c] = glyphs[c]->bitmap;
        m_glyph_to_char[glyph->glyph_index] = c;

        if (m_outline_thickness == 0.0f)
        {
            continue;
        }

        auto& box = glyph_box[c];
        for (size_t i = 0; i < outline_glyphs_vector.size(); ++i)
        {
            auto& outline_glyphs = outline_glyphs_vector[i];
            auto  og             = make_unique<Glyph>(m_freetype_library, face, uc, m_bolding, outline_sizes[i], m_hint_mode);
            box.left   = std::min(box.left,   og->bitmap.left);
            box.right  = std::max(box.right,  og->bitmap.right);
            box.top    = std::max(box.top,    og->bitmap.top);
            box.bottom = std::min(box.bottom, og->bitmap.bottom);
            outline_glyphs[c] = std::move(og);
        }
    }

    // Second pass: Search pack resolution
    rbp::SkylineBinPack packer;
    m_texture_width  = 2;
    m_texture_height = 2;
    for (;;)
    {
        // Reserve 1 pixel border
        packer.Init(m_texture_width - 2, m_texture_height - 2, false);

        bool pack_failed = false;
        for (auto c : m_chars)
        {
            const auto& box = glyph_box[c];
            const int width  = box.right - box.left;
            const int height = box.top   - box.bottom;
            if (width == 0 || height == 0)
            {
                glyphs[c]->atlas_rect = {};
                continue;
            }

            glyphs[c]->atlas_rect = packer.Insert(width + 1, height + 1, rbp::SkylineBinPack::LevelBottomLeft);
            if ((glyphs[c]->atlas_rect.width  == 0) ||
                (glyphs[c]->atlas_rect.height == 0))
            {
                pack_failed = true;
                break;
            }
        }

        if (!pack_failed)
        {
            break;
        }

        if (m_texture_width <= m_texture_height)
        {
            m_texture_width *= 2;
        }
        else
        {
            m_texture_height *= 2;
        }

        VERIFY(m_texture_width <= 16384);
    }

    log_font.trace("packing glyps to {} x {} succeeded\n", m_texture_width, m_texture_height);

    // Third pass: render glyphs
    m_bitmap = make_unique<Bitmap>(m_texture_width, m_texture_height, 2);
    m_bitmap->fill(0);
    for (auto c : m_chars)
    {
        auto uc = static_cast<unsigned char>(c);
        if (!glyphs[c])
        {
            continue;
        }
        auto  g          = glyphs[c].get();
        auto& box        = glyph_box[c];
        int   box_width  = box.right - box.left;
        int   box_height = box.top   - box.bottom;
        bool  render     = (box_width != 0) && (box_height != 0);

        const auto& r = g->atlas_rect;
        log_font.trace("char '{}' blit size {} * {}\n", c, face->glyph->bitmap.width, face->glyph->bitmap.rows);

        bool  rotated = (r.width != r.height) && ((box_width + 1) == r.height) && ((box_height + 1) == r.width);
        float x_scale = 1.0f / static_cast<float>(m_texture_width);
        float y_scale = 1.0f / static_cast<float>(m_texture_height);
        auto  fx      = static_cast<float>(r.x) + 1.0f;
        auto  fy      = static_cast<float>(r.y) + 1.0f;
        auto  fw      = static_cast<float>(box_width);
        auto  fh      = static_cast<float>(box_height);

        ft_char d;
        d.width    = box_width;
        d.height   = box_height;
        d.g_left   = g->bitmap.left;
        d.g_bottom = g->bitmap.bottom;
        d.g_top    = g->bitmap.top;
        d.g_height = g->bitmap.height;
        d.b_left   = box.left;  // g->bitmap.left    - x_extra;
        d.b_bottom = box.bottom;// g->bitmap.bottom  - y_extra;
        d.b_top    = box.top;
        d.rotated  = rotated;

        if (!rotated)
        {
            d.u[0] =  fx       * x_scale;
            d.v[0] =  fy       * y_scale;
            d.u[1] = (fx + fw) * x_scale;
            d.v[1] =  fy       * y_scale;
            d.u[2] = (fx + fw) * x_scale;
            d.v[2] = (fy + fh) * y_scale;
            d.u[3] =  fx       * x_scale;
            d.v[3] = (fy + fh) * y_scale;
        }
        else
        {
            d.u[0] = (fx + fh) * x_scale;
            d.v[0] =  fy       * y_scale;
            d.u[1] = (fx + fh) * x_scale;
            d.v[1] = (fy + fw) * y_scale;
            d.u[2] =  fx       * x_scale;
            d.v[2] = (fy + fw) * y_scale;
            d.u[3] =  fx       * x_scale;
            d.v[3] =  fy       * y_scale;
        }
        if (render)
        {
            m_bitmap->blit<false>(g->bitmap.width,
                                  g->bitmap.height,
                                  r.x + 1 + std::max(0, rotated ? (g->bitmap.bottom - box.bottom) : (g->bitmap.left   - box.left  )),
                                  r.y + 1 + std::max(0, rotated ? (g->bitmap.left   - box.left  ) : (g->bitmap.bottom - box.bottom)),
                                  g->buffer(),
                                  g->bitmap.pitch,
                                  g->bitmap.width,
                                  1,
                                  0,
                                  rotated);
            if (m_outline_thickness > 0.0f)
            {
                for (auto& outline_glyphs : outline_glyphs_vector)
                {
                    const auto og = outline_glyphs[c].get();
                    m_bitmap->blit<true>(og->bitmap.width,
                                         og->bitmap.height,
                                         r.x + 1 + std::max(0, rotated ? (og->bitmap.bottom - box.bottom) : (og->bitmap.left   - box.left  )),
                                         r.y + 1 + std::max(0, rotated ? (og->bitmap.left   - box.left  ) : (og->bitmap.bottom - box.bottom)),
                                         og->buffer(),
                                         og->bitmap.pitch,
                                         og->bitmap.width,
                                         1,
                                         1,
                                         rotated);
                }
            }
        }
        m_chars_256[uc] = d;
    }

    post_process();

    //m_bitmap->dump();
}

void Font::trace_info() const
{
    FT_Face face = m_freetype_face;
    log_font.trace("num faces     {}\n", face->num_faces);
    log_font.trace("face index    {}\n", face->face_index);
    log_font.trace("face flags    ");
    if (face->face_flags & FT_FACE_FLAG_SCALABLE)
    {
        log_font.trace("scalable ");
    }
    if (face->face_flags & FT_FACE_FLAG_FIXED_WIDTH)
    {
        log_font.trace("fixed_width ");
    }
    if (face->face_flags & FT_FACE_FLAG_SFNT)
    {
        log_font.trace("sfnt ");
    }
    if (face->face_flags & FT_FACE_FLAG_HORIZONTAL)
    {
        log_font.trace("horizontal ");
    }
    if (face->face_flags & FT_FACE_FLAG_VERTICAL)
    {
        log_font.trace("vertical ");
    }
    if (face->face_flags & FT_FACE_FLAG_KERNING)
    {
        log_font.trace("kerning ");
    }
    if (face->face_flags & FT_FACE_FLAG_FAST_GLYPHS)
    {
        log_font.trace("fast_glyphs ");
    }
    if (face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS)
    {
        log_font.trace("multiple_masters ");
    }
    if (face->face_flags & FT_FACE_FLAG_GLYPH_NAMES)
    {
        log_font.trace("glyph_names ");
    }
    if (face->face_flags & FT_FACE_FLAG_EXTERNAL_STREAM)
    {
        log_font.trace("external_stream ");
    }
    if (face->face_flags & FT_FACE_FLAG_HINTER)
    {
        log_font.trace("hinter ");
    }
    if (face->face_flags & FT_FACE_FLAG_CID_KEYED)
    {
        log_font.trace("cid_keyed ");
    }
    if (face->face_flags & FT_FACE_FLAG_TRICKY)
    {
        log_font.trace("tricky ");
    }
    log_font.trace("\n");
    log_font.trace("style flags   ");
    if (face->style_flags & FT_STYLE_FLAG_ITALIC)
    {
        log_font.trace("italic ");
    }
    if (face->style_flags & FT_STYLE_FLAG_BOLD)
    {
        log_font.trace("bold ");
    }
    log_font.trace("\n");
    log_font.trace("num glyphs    {}\n", face->num_glyphs);
    log_font.trace("family name   {}\n", face->family_name);
    log_font.trace("style name    {}\n", face->style_name);
    log_font.trace("fixed sizes   {}\n", face->num_fixed_sizes);
    for (int i = 0; i < face->num_fixed_sizes; ++i)
    {
        log_font.trace("\t{} * {}, ", face->available_sizes[i].width, face->available_sizes[i].height);
    }

    log_font.trace("\n");
    log_font.trace("num charmaps  {}\n", face->num_charmaps);
    log_font.trace("units per em  {}\n", face->units_per_EM);
    log_font.trace("ascender      {}\n", face->ascender);
    log_font.trace("descender     {}\n", face->descender);
    log_font.trace("height        {}\n", face->height);
    log_font.trace("max a. width  {}\n", face->max_advance_width);
    log_font.trace("max a. height {}\n", face->max_advance_height);
    log_font.trace("underline pos {}\n", face->underline_position);
    log_font.trace("underline thk {}\n", face->underline_thickness);
    log_font.trace("charmap       ");
    switch (face->charmap->encoding)
    {
        case FT_ENCODING_NONE:           log_font.trace("none\n"); break;
        case FT_ENCODING_MS_SYMBOL:      log_font.trace("MS Symbol\n"); break;
        case FT_ENCODING_UNICODE:        log_font.trace("Unicode\n"); break;
        case FT_ENCODING_SJIS:           log_font.trace("SJIS\n"); break;
        case FT_ENCODING_GB2312:         log_font.trace("GB2312 Simplified Chinese\n"); break;
        case FT_ENCODING_BIG5:           log_font.trace("BIG5 Traditional Chinese\n"); break;
        case FT_ENCODING_WANSUNG:        log_font.trace("WANSUNG\n"); break;               // Korean
        case FT_ENCODING_JOHAB:          log_font.trace("JOHAB KS C 5601-1992\n"); break;    // Korean, MS Windows code page 1361
        case FT_ENCODING_ADOBE_STANDARD: log_font.trace("Adobe Standard\n"); break; // Type 1, CFF, and OpenType/CFF (256)
        case FT_ENCODING_ADOBE_EXPERT:   log_font.trace("Adobe Expert\n"); break;     // Type 1, CFF, and OpenType/CFF (256)
        case FT_ENCODING_ADOBE_CUSTOM:   log_font.trace("Adobe Custom\n"); break;     // Type 1, CFF, and OpenType/CFF (256)
        case FT_ENCODING_ADOBE_LATIN_1:  log_font.trace("Adobe Latin 1\n"); break;   // Type 1 PostScript (256)
        case FT_ENCODING_OLD_LATIN_2:    log_font.trace("Old Latin 2\n"); break;
        case FT_ENCODING_APPLE_ROMAN:    log_font.trace("Apple Roman\n"); break;
        default: log_font.trace("unknown\n"); break;
    }

    log_font.trace("patents       ");
    FT_Bool res = FT_Face_CheckTrueTypePatents(face);
    log_font.trace((res == 1) ? "yes\n" : "no\n");
}

void Font::post_process()
{
    ZoneScoped;

    Bitmap bm(m_bitmap->width(), m_bitmap->height(), m_bitmap->components());
    m_bitmap->post_process(bm, m_gamma);

    auto internal_format = gl::Internal_format::rg8;

    Texture::Create_info create_info(gl::Texture_target::texture_2d,
                                     internal_format,
                                     false,
                                     m_texture_width,
                                     m_texture_height);

    m_texture = std::make_unique<Texture>(create_info);

    m_texture->upload(create_info.internal_format, bm.as_span(), create_info.width, create_info.height);

    m_texture->set_debug_label("font");
}

// https://en.wikipedia.org/wiki/List_of_typographic_features

// Default
// abvm Above-base Mark Positioning     Positions a mark glyph above a base glyph.
// blwm Below-base Mark Positioning     Positions a mark glyph below a base glyph
// ccmp Glyph Composition/Decomposition Either calls a ligature replacement on a sequence of characters or replaces a character with a sequence of glyphs. Provides logic that can for example effectively alter the order of input characters.
// locl Localized Forms                 Substitutes character with the preferred form based on script language
// mark Mark Positioning                Fine positioning of a mark glyph to a base character
// mkmk Mark-to-mark Positioning        Fine positioning of a mark glyph to another mark character
// rlig Required Ligatures              Ligatures required for correct text display (any script, but in cursive)

// Horizontal default
// calt Contextual Alternates           Applies a second substitution feature based on a match of a character pattern within a context of surrounding patterns
// clig Contextual Ligatures            Applies a second ligature feature based on a match of a character pattern within a context of surrounding patterns
// curs Cursive Positioning             Precise positioning of a letter's connection to an adjacent one
// dist Distance                        Adjusts horizontal positioning between glyphs. (Always enabled, as opposed to 'kern'.)
// kern Kerning                         Fine horizontal positioning of one glyph to the next, based on the shapes of the glyphs
// liga Standard Ligatures              Replaces (by default) sequence of characters with a single ligature glyph
// rclt Required Contextual Alternates  Contextual alternates required for correct text display which differs from the default join for other letters, required especially important by Arabic
// frac Fractions                       Converts figures separated by slash with diagonal fraction

// Vertical default
// vert Vertical Alternates             A subset of vrt2: prefer the latter feature

auto Font::print(gsl::span<float>    float_data,
                 gsl::span<uint32_t> uint_data,
                 const std::string&  text,
                 glm::vec3           text_position,
                 uint32_t            text_color,
                 Rectangle&          out_bounds) const
-> size_t
{
    ZoneScoped;

    log_font.trace("Font::print(text = {}, x = {}, y = {}, z = {})\n", text, text_position.x, text_position.y, text_position.z);

    if (text.empty())
    {
        return 0;
    }

    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_add_utf8     (buf, text.c_str(), -1, 0, -1);
    hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
    hb_buffer_set_script   (buf, HB_SCRIPT_LATIN);
    hb_buffer_set_language (buf, hb_language_from_string("en", -1));

    hb_feature_t userfeatures[1]; // clig, dlig
    userfeatures[0].tag   = HB_TAG('l','i','g','a');
    userfeatures[0].value = 0;
    userfeatures[0].start = HB_FEATURE_GLOBAL_START;
    userfeatures[0].end   = HB_FEATURE_GLOBAL_END;
    hb_shape               (m_harfbuzz_font, buf, &userfeatures[0], 1);
    unsigned int glyph_count{0};
    hb_glyph_info_t*     glyph_info = hb_buffer_get_glyph_infos    (buf, &glyph_count);
    hb_glyph_position_t* glyph_pos  = hb_buffer_get_glyph_positions(buf, &glyph_count);

    size_t chars_printed{0};
    size_t word_offset{0};
    for (unsigned int i = 0; i < glyph_count; ++i)
    {
        auto glyph_id = glyph_info[i].codepoint;
        float x_offset  = static_cast<float>(glyph_pos[i].x_offset ) / 64.0f;
        float y_offset  = static_cast<float>(glyph_pos[i].y_offset ) / 64.0f;
        float x_advance = static_cast<float>(glyph_pos[i].x_advance) / 64.0f;
        float y_advance = static_cast<float>(glyph_pos[i].y_advance) / 64.0f;
        auto j = m_glyph_to_char.find(glyph_id);
        if (j != m_glyph_to_char.end())
        {
            auto c = j->second;
            auto uc = static_cast<unsigned char>(c);
            const ft_char& font_char = m_chars_256[uc];
            if (font_char.width != 0)
            {
                float b  = static_cast<float>(font_char.g_bottom - font_char.b_bottom);
                float t  = static_cast<float>(font_char.g_top    - font_char.b_top);
                float w  = static_cast<float>(font_char.width);
                float h  = static_cast<float>(font_char.height);
                float ox = static_cast<float>(font_char.b_left);
                float oy = static_cast<float>(font_char.b_bottom + t + b);
                float x0 = text_position.x + x_offset + ox;
                float y0 = text_position.y + y_offset + oy;
                float x1 = x0 + w;
                float y1 = y0 + h;

                float_data[word_offset++] = x0;
                float_data[word_offset++] = y0;
                float_data[word_offset++] = text_position.z;
                uint_data [word_offset++] = text_color;
                float_data[word_offset++] = font_char.u[0];
                float_data[word_offset++] = font_char.v[0];
                float_data[word_offset++] = x1;
                float_data[word_offset++] = y0;
                float_data[word_offset++] = text_position.z;
                uint_data [word_offset++] = text_color;
                float_data[word_offset++] = font_char.u[1];
                float_data[word_offset++] = font_char.v[1];
                float_data[word_offset++] = x1;
                float_data[word_offset++] = y1;
                float_data[word_offset++] = text_position.z;
                uint_data [word_offset++] = text_color;
                float_data[word_offset++] = font_char.u[2];
                float_data[word_offset++] = font_char.v[2];
                float_data[word_offset++] = x0;
                float_data[word_offset++] = y1;
                float_data[word_offset++] = text_position.z;
                uint_data [word_offset++] = text_color;
                float_data[word_offset++] = font_char.u[3];
                float_data[word_offset++] = font_char.v[3];

                out_bounds.extend_by(x0, y0);
                out_bounds.extend_by(x1, y1);
                ++chars_printed;
            }
        }
        text_position.x += x_advance;
        text_position.y += y_advance;
    }
    hb_buffer_destroy(buf);

    return chars_printed;
}

void Font::measure(const std::string& text, Rectangle& bounds) const
{
    ZoneScoped;

    log_font.trace("Font::measure(text = {}\n", text);

    if (text.empty())
    {
        return;
    }

    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_add_utf8     (buf, text.c_str(), -1, 0, -1);
    hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
    hb_buffer_set_script   (buf, HB_SCRIPT_LATIN);
    hb_buffer_set_language (buf, hb_language_from_string("en", -1));
    hb_shape               (m_harfbuzz_font, buf, nullptr, 0);
    unsigned int glyph_count{0};
    hb_glyph_info_t*     glyph_info = hb_buffer_get_glyph_infos    (buf, &glyph_count);
    hb_glyph_position_t* glyph_pos  = hb_buffer_get_glyph_positions(buf, &glyph_count);

    float x{0.0f};
    float y{0.0f};
    for (unsigned int i = 0; i < glyph_count; ++i)
    {
        auto glyph_id = glyph_info[i].codepoint;
        float x_offset  = static_cast<float>(glyph_pos[i].x_offset ) / 64.0f;
        float y_offset  = static_cast<float>(glyph_pos[i].y_offset ) / 64.0f;
        float x_advance = static_cast<float>(glyph_pos[i].x_advance) / 64.0f;
        float y_advance = static_cast<float>(glyph_pos[i].y_advance) / 64.0f;
        auto j = m_glyph_to_char.find(glyph_id);
        if (j != m_glyph_to_char.end())
        {
            auto c = j->second;
            auto uc = static_cast<unsigned char>(c);
            const ft_char& font_char = m_chars_256[uc];
            if (font_char.width != 0)
            {
                float b  = static_cast<float>(font_char.g_bottom - font_char.b_bottom);
                float t  = static_cast<float>(font_char.g_top    - font_char.b_top);
                float w  = static_cast<float>(font_char.width);
                float h  = static_cast<float>(font_char.height);
                float ox = static_cast<float>(font_char.b_left);
                float oy = static_cast<float>(font_char.b_bottom + t + b);
                float x0 = x + x_offset + ox;
                float y0 = y + y_offset + oy;
                float x1 = x0 + w;
                float y1 = y0 + h;
                bounds.extend_by(x0, y0);
                bounds.extend_by(x1, y1);
            }
        }
        x += x_advance;
        y += y_advance;
    }
    hb_buffer_destroy(buf);
}

} // namespace erhe::ui

#undef LOG
