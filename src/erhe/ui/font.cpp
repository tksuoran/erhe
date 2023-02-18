#include "erhe/ui/font.hpp"
#include "erhe/ui/glyph.hpp"
#include "erhe/ui/ui_log.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/toolkit/profile.hpp"

#include <fmt/printf.h>

#if defined(ERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE)
#   include <freetype/freetype.h>
#   include <freetype/ftbitmap.h>
#   include <freetype/ftglyph.h>
#   include <freetype/ftstroke.h>
#endif

#if defined(ERHE_TEXT_LAYOUT_LIBRARY_HARFBUZZ)
#   include <hb.h>
#   include <hb-ft.h>
#endif

#include <SkylineBinPack.h> // RectangleBinPack

#include <stdexcept>
#include <string_view>

namespace erhe::ui
{

using erhe::graphics::Texture;
using std::map;
using std::shared_ptr;
using std::unique_ptr;
using std::make_shared;
using std::make_unique;

Font::~Font() noexcept
{
    ERHE_PROFILE_FUNCTION

#if defined(ERHE_TEXT_LAYOUT_LIBRARY_HARFBUZZ)
    hb_buffer_destroy(m_harfbuzz_buffer);
    hb_font_destroy(m_harfbuzz_font);
#endif

#if defined(ERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE)
    FT_Done_Face(m_freetype_face);
    FT_Done_FreeType(m_freetype_library);
#endif
}

#if defined(ERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE) && defined(ERHE_TEXT_LAYOUT_LIBRARY_HARFBUZZ)
Font::Font(
    const std::filesystem::path& path,
    const unsigned int           size,
    const float                  outline_thickness
)
    : m_path             {path}
    , m_bolding          {(size > 10) ? 0.5f : 0.0f}
    , m_outline_thickness{outline_thickness}
{
    ERHE_PROFILE_FUNCTION

    const auto current_path = std::filesystem::current_path();
    log_font->info("current path = {}", current_path.string());

    log_font->info(
        "Font::Font(path = {}, size = {}, outline_thickness = {})",
        path.string(),
        size,
        outline_thickness
    );

    if (m_hinting) {
        //m_hint_mode = FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_LIGHT;
        //m_hint_mode = FT_LOAD_FORCE_AUTOHINT;
        m_hint_mode = 0;
    } else {
        m_hint_mode = FT_LOAD_NO_HINTING; // NOLINT(hicpp-signed-bitwise)
    }

    m_chars =
        " \"\\@#$%&/" // latin
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "<>*+-.,:;=!?'`^~_|()[]{}";

    m_pixel_size = size;

    const bool render_ok = render();
    if (!render_ok) {
         log_font->error("Font loading failed. Check working directory '{}'", current_path.string());
    }
}

auto Font::render() -> bool
{
    ERHE_PROFILE_FUNCTION

    FT_Error res;
    res = FT_Init_FreeType(&m_freetype_library);
    if (res != FT_Err_Ok) {
        log_font->error("FT_Init_FreeType() returned error code {}", res);
        return false;
    }

    {
        FT_Int major{0};
        FT_Int minor{0};
        FT_Int patch{0};
        FT_Library_Version(m_freetype_library, &major, &minor, &patch);
        log_font->trace("Freetype version {}.{}.{}", major, minor, patch);
    }

    res = FT_New_Face(m_freetype_library, m_path.string().c_str(), 0, &m_freetype_face);
    if (res != FT_Err_Ok) {
        log_font->error(
            "FT_New_Face(pathname = '{}') returned error code {}",
            m_path.string(),
            res
        );
        return false;
    }
    res = FT_Select_Charmap(m_freetype_face, FT_ENCODING_UNICODE);
    if (res != FT_Err_Ok) {
        log_font->error("FT_Select_Charmap() returned error code {}", res);
        return false;
    }
    FT_Face face = m_freetype_face;

    auto xsize = m_pixel_size << 6u;
    auto ysize = m_pixel_size << 6u;

    res = FT_Set_Char_Size(m_freetype_face, xsize, ysize, m_dpi, m_dpi);
    if (res != FT_Err_Ok) {
        log_font->error("FT_Set_Char_Size() returned error code {}", res);
        return false;
    }

    m_harfbuzz_font = hb_ft_font_create(m_freetype_face, nullptr);
    m_harfbuzz_buffer = hb_buffer_create();

    trace_info();

    m_line_height = std::ceil(static_cast<float>(face->size->metrics.height) / 64.0f);

    using GlyphSet = map<char, unique_ptr<Glyph>>;

    GlyphSet glyphs;

    std::vector<float> outline_sizes;
    for (float outline_thickness = m_outline_thickness;
         outline_thickness > 0.0f;
         outline_thickness -= 10.0f
    ) {
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

        if (m_outline_thickness == 0.0f) {
            continue;
        }

        auto& box = glyph_box[c];
        for (size_t i = 0; i < outline_glyphs_vector.size(); ++i) {
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
    for (;;) {
        // Reserve 1 pixel border
        packer.Init(m_texture_width - 2, m_texture_height - 2, false);

        bool pack_failed = false;
        for (auto c : m_chars) {
            const auto& box = glyph_box[c];
            const int width  = box.right - box.left;
            const int height = box.top   - box.bottom;
            if (width == 0 || height == 0) {
                glyphs[c]->atlas_rect = {};
                continue;
            }

            glyphs[c]->atlas_rect = packer.Insert(width + 1, height + 1, rbp::SkylineBinPack::LevelBottomLeft);
            if (
                (glyphs[c]->atlas_rect.width  == 0) ||
                (glyphs[c]->atlas_rect.height == 0)
            ) {
                pack_failed = true;
                break;
            }
        }

        if (!pack_failed) {
            break;
        }

        if (m_texture_width <= m_texture_height) {
            m_texture_width *= 2;
        } else {
            m_texture_height *= 2;
        }

        ERHE_VERIFY(m_texture_width <= 16384);
    }

    log_font->trace("packing glyps to {} x {} succeeded", m_texture_width, m_texture_height);

    // Third pass: render glyphs
    m_bitmap = make_unique<Bitmap>(m_texture_width, m_texture_height, 2);
    m_bitmap->fill(0);
    for (auto c : m_chars) {
        auto uc = static_cast<unsigned char>(c);
        if (!glyphs[c]) {
            continue;
        }
        auto  g          = glyphs[c].get();
        auto& box        = glyph_box[c];
        int   box_width  = box.right - box.left;
        int   box_height = box.top   - box.bottom;
        bool  render     = (box_width != 0) && (box_height != 0);

        const auto& r = g->atlas_rect;
        log_font->trace("char '{}' blit size {} * {}", c, face->glyph->bitmap.width, face->glyph->bitmap.rows);

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

        if (!rotated) {
            d.u[0] =  fx       * x_scale;
            d.v[0] =  fy       * y_scale;
            d.u[1] = (fx + fw) * x_scale;
            d.v[1] =  fy       * y_scale;
            d.u[2] = (fx + fw) * x_scale;
            d.v[2] = (fy + fh) * y_scale;
            d.u[3] =  fx       * x_scale;
            d.v[3] = (fy + fh) * y_scale;
        } else {
            d.u[0] = (fx + fh) * x_scale;
            d.v[0] =  fy       * y_scale;
            d.u[1] = (fx + fh) * x_scale;
            d.v[1] = (fy + fw) * y_scale;
            d.u[2] =  fx       * x_scale;
            d.v[2] = (fy + fw) * y_scale;
            d.u[3] =  fx       * x_scale;
            d.v[3] =  fy       * y_scale;
        }
        if (render) {
            m_bitmap->blit<false>(
                g->bitmap.width,
                g->bitmap.height,
                r.x + 1 + std::max(0, rotated ? (g->bitmap.bottom - box.bottom) : (g->bitmap.left   - box.left  )),
                r.y + 1 + std::max(0, rotated ? (g->bitmap.left   - box.left  ) : (g->bitmap.bottom - box.bottom)),
                g->buffer(),
                g->bitmap.pitch,
                g->bitmap.width,
                1,
                0,
                rotated
            );
            if (m_outline_thickness > 0.0f) {
                for (auto& outline_glyphs : outline_glyphs_vector) {
                    const auto og = outline_glyphs[c].get();
                    m_bitmap->blit<true>(
                        og->bitmap.width,
                        og->bitmap.height,
                        r.x + 1 + std::max(0, rotated ? (og->bitmap.bottom - box.bottom) : (og->bitmap.left   - box.left  )),
                        r.y + 1 + std::max(0, rotated ? (og->bitmap.left   - box.left  ) : (og->bitmap.bottom - box.bottom)),
                        og->buffer(),
                        og->bitmap.pitch,
                        og->bitmap.width,
                        1,
                        1,
                        rotated
                    );
                }
            }
        }
        m_chars_256[uc] = d;
    }

    post_process();

    return true;
    //m_bitmap->dump();
}

namespace {

auto c_str_charmap(FT_Encoding encoding) -> const char*
{
    switch (encoding) {
        case FT_ENCODING_NONE:           return "none";
        case FT_ENCODING_MS_SYMBOL:      return "MS Symbol";
        case FT_ENCODING_UNICODE:        return "Unicode";
        case FT_ENCODING_SJIS:           return "SJIS";
        case FT_ENCODING_GB2312:         return "GB2312 Simplified Chinese";
        case FT_ENCODING_BIG5:           return "BIG5 Traditional Chinese";
        case FT_ENCODING_WANSUNG:        return "WANSUNG";                  // Korean
        case FT_ENCODING_JOHAB:          return "JOHAB KS C 5601-1992";     // Korean, MS Windows code page 1361
        case FT_ENCODING_ADOBE_STANDARD: return "Adobe Standard";           // Type 1, CFF, and OpenType/CFF (256)
        case FT_ENCODING_ADOBE_EXPERT:   return "Adobe Expert";             // Type 1, CFF, and OpenType/CFF (256)
        case FT_ENCODING_ADOBE_CUSTOM:   return "Adobe Custom";             // Type 1, CFF, and OpenType/CFF (256)
        case FT_ENCODING_ADOBE_LATIN_1:  return "Adobe Latin 1";            // Type 1 PostScript (256)
        case FT_ENCODING_OLD_LATIN_2:    return "Old Latin 2";
        case FT_ENCODING_APPLE_ROMAN:    return "Apple Roman";
        default: return "unknown";
    }
}

}

void Font::trace_info() const
{
    FT_Face face = m_freetype_face;
    log_font->trace("num faces     {}", face->num_faces);
    log_font->trace("face index    {}", face->face_index);

    {
        std::stringstream ss;
        ss << "face flags    ";
        if (face->face_flags & FT_FACE_FLAG_SCALABLE) {
            ss << "scalable ";
        }
        if (face->face_flags & FT_FACE_FLAG_FIXED_WIDTH) {
            ss << "fixed_width ";
        }
        if (face->face_flags & FT_FACE_FLAG_SFNT) {
            ss << "sfnt ";
        }
        if (face->face_flags & FT_FACE_FLAG_HORIZONTAL) {
            ss << "horizontal ";
        }
        if (face->face_flags & FT_FACE_FLAG_VERTICAL) {
            ss << "vertical ";
        }
        if (face->face_flags & FT_FACE_FLAG_KERNING) {
            ss << "kerning ";
        }
        if (face->face_flags & FT_FACE_FLAG_FAST_GLYPHS) {
            ss << "fast_glyphs ";
        }
        if (face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) {
            ss << "multiple_masters ";
        }
        if (face->face_flags & FT_FACE_FLAG_GLYPH_NAMES) {
            ss << "glyph_names ";
        }
        if (face->face_flags & FT_FACE_FLAG_EXTERNAL_STREAM) {
            ss << "external_stream ";
        }
        if (face->face_flags & FT_FACE_FLAG_HINTER) {
            ss << "hinter ";
        }
        if (face->face_flags & FT_FACE_FLAG_CID_KEYED) {
            ss << "cid_keyed ";
        }
        if (face->face_flags & FT_FACE_FLAG_TRICKY) {
            ss << "tricky ";
        }
        log_font->trace("{}", ss.str());
    }

    {
        std::stringstream ss;
        ss << "style flags   ";
        if (face->style_flags & FT_STYLE_FLAG_ITALIC) {
            ss << "italic ";
        }
        if (face->style_flags & FT_STYLE_FLAG_BOLD) {
            ss << "bold ";
        }
        log_font->trace("{}", ss.str());
    }
    log_font->trace("num glyphs    {}", face->num_glyphs);
    log_font->trace("family name   {}", face->family_name);
    log_font->trace("style name    {}", face->style_name);

    log_font->trace("fixed sizes   {}", face->num_fixed_sizes);
    for (int i = 0; i < face->num_fixed_sizes; ++i) {
        log_font->trace("\t{} * {}", face->available_sizes[i].width, face->available_sizes[i].height);
    }

    log_font->trace("num charmaps  {}", face->num_charmaps);
    log_font->trace("units per em  {}", face->units_per_EM);
    log_font->trace("ascender      {}", face->ascender);
    log_font->trace("descender     {}", face->descender);
    log_font->trace("height        {}", face->height);
    log_font->trace("max a. width  {}", face->max_advance_width);
    log_font->trace("max a. height {}", face->max_advance_height);
    log_font->trace("underline pos {}", face->underline_position);
    log_font->trace("underline thk {}", face->underline_thickness);
    log_font->trace("charmap       {}", c_str_charmap(face->charmap->encoding));
    log_font->trace("patents       {}", (FT_Face_CheckTrueTypePatents(face) == 1) ? "yes" : "no");
}
#else
Font::Font(const std::filesystem::path&, const unsigned int, const float) {}
void Font::render(){}
void Font::trace_info() const {}
#endif

void Font::post_process()
{
    ERHE_PROFILE_FUNCTION

    Bitmap bm{
        m_bitmap->width(),
        m_bitmap->height(),
        m_bitmap->components()
    };
    m_bitmap->post_process(bm, m_gamma);

    auto internal_format = gl::Internal_format::rg8;

    const Texture::Create_info create_info{
        .target          = gl::Texture_target::texture_2d,
        .internal_format = internal_format,
        .use_mipmaps     = false,
        .width           = m_texture_width,
        .height          = m_texture_height
    };

    m_texture = std::make_unique<Texture>(create_info);
    m_texture->upload(create_info.internal_format, bm.as_span(), create_info.width, create_info.height);
    m_texture->set_debug_label(m_path.filename().generic_string());
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

#if defined(ERHE_TEXT_LAYOUT_LIBRARY_HARFBUZZ)
auto Font::print(
    gsl::span<float>    float_data,
    gsl::span<uint32_t> uint_data,
    std::string_view    text,
    glm::vec3           text_position,
    const uint32_t      text_color,
    Rectangle&          out_bounds
) const -> size_t
{
    ERHE_PROFILE_FUNCTION

    SPDLOG_LOGGER_TRACE(
        log_font,
        "Font::print(text = {}, x = {}, y = {}, z = {})",
        text,
        text_position.x,
        text_position.y,
        text_position.z
    );

    if (text.empty()) {
        return 0;
    }

    //hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_clear_contents(m_harfbuzz_buffer);
    hb_buffer_set_direction(m_harfbuzz_buffer, HB_DIRECTION_LTR);
    hb_buffer_set_script   (m_harfbuzz_buffer, HB_SCRIPT_LATIN);
    hb_buffer_set_language (m_harfbuzz_buffer, hb_language_from_string("en", -1));
    hb_buffer_add_utf8(m_harfbuzz_buffer, text.data(), -1, 0, -1);

    hb_feature_t userfeatures[1]; // clig, dlig
    userfeatures[0].tag   = HB_TAG('l','i','g','a');
    userfeatures[0].value = 0;
    userfeatures[0].start = HB_FEATURE_GLOBAL_START;
    userfeatures[0].end   = HB_FEATURE_GLOBAL_END;
    hb_shape               (m_harfbuzz_font, m_harfbuzz_buffer, &userfeatures[0], 1);
    unsigned int glyph_count{0};
    hb_glyph_info_t*     glyph_info = hb_buffer_get_glyph_infos    (m_harfbuzz_buffer, &glyph_count);
    hb_glyph_position_t* glyph_pos  = hb_buffer_get_glyph_positions(m_harfbuzz_buffer, &glyph_count);

    std::size_t chars_printed{0};
    std::size_t word_offset{0};
    for (unsigned int i = 0; i < glyph_count; ++i) {
        const auto  glyph_id  = glyph_info[i].codepoint;
        const float x_offset  = static_cast<float>(glyph_pos[i].x_offset ) / 64.0f;
        const float y_offset  = static_cast<float>(glyph_pos[i].y_offset ) / 64.0f;
        const float x_advance = static_cast<float>(glyph_pos[i].x_advance) / 64.0f;
        const float y_advance = static_cast<float>(glyph_pos[i].y_advance) / 64.0f;
        auto j = m_glyph_to_char.find(glyph_id);
        if (j != m_glyph_to_char.end()) {
            auto c = j->second;
            auto uc = static_cast<unsigned char>(c);
            const ft_char& font_char = m_chars_256[uc];
            if (font_char.width != 0) {
                const float b  = static_cast<float>(font_char.g_bottom - font_char.b_bottom);
                const float t  = static_cast<float>(font_char.g_top    - font_char.b_top);
                const float w  = static_cast<float>(font_char.width);
                const float h  = static_cast<float>(font_char.height);
                const float ox = static_cast<float>(font_char.b_left);
                const float oy = static_cast<float>(font_char.b_bottom + t + b);
                const float x0 = text_position.x + x_offset + ox;
                const float y0 = text_position.y + y_offset + oy;
                const float x1 = x0 + w;
                const float y1 = y0 + h;

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

    return chars_printed;
}

auto Font::measure(const std::string_view text) const -> Rectangle
{
    ERHE_PROFILE_FUNCTION

    if (text.empty()) {
        return Rectangle{0.0f, 0.0f, 0.0f, 0.0f};
    }

    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_add_utf8     (buf, text.data(), static_cast<int>(text.size()), 0, -1);
    hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
    hb_buffer_set_script   (buf, HB_SCRIPT_LATIN);
    hb_buffer_set_language (buf, hb_language_from_string("en", -1));
    hb_shape               (m_harfbuzz_font, buf, nullptr, 0);
    unsigned int glyph_count{0};
    hb_glyph_info_t*     glyph_info = hb_buffer_get_glyph_infos    (buf, &glyph_count);
    hb_glyph_position_t* glyph_pos  = hb_buffer_get_glyph_positions(buf, &glyph_count);

    float x{0.0f};
    float y{0.0f};
    Rectangle bounds{};
    bounds.reset_for_grow();
    for (unsigned int i = 0; i < glyph_count; ++i) {
        const auto  glyph_id  = glyph_info[i].codepoint;
        const float x_offset  = static_cast<float>(glyph_pos[i].x_offset ) / 64.0f;
        const float y_offset  = static_cast<float>(glyph_pos[i].y_offset ) / 64.0f;
        const float x_advance = static_cast<float>(glyph_pos[i].x_advance) / 64.0f;
        const float y_advance = static_cast<float>(glyph_pos[i].y_advance) / 64.0f;
        const auto  j = m_glyph_to_char.find(glyph_id);
        if (j != m_glyph_to_char.end()) {
            const auto c = j->second;
            const auto uc = static_cast<unsigned char>(c);
            const ft_char& font_char = m_chars_256[uc];
            if (font_char.width != 0) {
                const float b  = static_cast<float>(font_char.g_bottom - font_char.b_bottom);
                const float t  = static_cast<float>(font_char.g_top    - font_char.b_top);
                const float w  = static_cast<float>(font_char.width);
                const float h  = static_cast<float>(font_char.height);
                const float ox = static_cast<float>(font_char.b_left);
                const float oy = static_cast<float>(font_char.b_bottom + t + b);
                const float x0 = x + x_offset + ox;
                const float y0 = y + y_offset + oy;
                const float x1 = x0 + w;
                const float y1 = y0 + h;
                bounds.extend_by(x0, y0);
                bounds.extend_by(x1, y1);
            }
        }
        x += x_advance;
        y += y_advance;
    }
    hb_buffer_destroy(buf);
    return bounds;
}
#else
auto Font::print(
    gsl::span<float>    ,
    gsl::span<uint32_t> ,
    std::string_view    ,
    glm::vec3           ,
    const uint32_t      ,
    Rectangle&
) const -> size_t
{
    return 0;
}
auto Font::measure(const std::string_view text) const -> Rectangle
{
    static_cast<void>(text);
    return {};
}
#endif

} // namespace erhe::ui

#undef LOG
