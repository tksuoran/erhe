// Glyph outline extraction for GPU curve-based text rendering.
//
// The contour-to-quadratic-bezier conversion is adapted from
// https://github.com/GreenLightning/gpu-font-rendering
// Copyright (c) 2022 Green Lightning, MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "erhe_ui/glyph_outlines.hpp"
#include "erhe_ui/ui_log.hpp"

#if defined(ERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE)
#   include "erhe_file/file.hpp"
#   include <freetype/freetype.h>
#   include <freetype/ftoutln.h>
#endif

#include <optional>
#include <string>

namespace erhe::ui {

#if defined(ERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE)

namespace {

// This function takes a single contour (defined by first_index and
// last_index, both inclusive) from outline and converts it into individual
// quadratic bezier curves, which are added to the curves vector.
//
// See https://freetype.org/freetype2/docs/glyphs/glyphs-6.html for a
// detailed description of the outline format. In short, a contour is a
// list of points describing line segments and quadratic or cubic bezier
// curves that form a closed shape. Each point has a tag: FT_CURVE_TAG_ON
// points sit exactly on the outline; FT_CURVE_TAG_CONIC and
// FT_CURVE_TAG_CUBIC are off-curve control points. Two consecutive points
// of type ON or CONIC imply a virtual point of the opposite type at their
// exact midpoint; this rule is used to represent line segments as
// degenerate quadratic bezier curves so the shader only needs to handle
// quadratics.
//
// Cubic bezier curves (CFF / OpenType outlines) are approximated by two
// quadratic curves each, preserving C1 continuity, following:
//   Quadratic Approximation of Cubic Curves
//   Nghia Truong, Cem Yuksel, Larry Seiler
//   https://doi.org/10.1145/3406178
void convert_contour(std::vector<Glyph_curve>& curves, const FT_Outline* outline, short first_index, short last_index, const float em_size)
{
    if (first_index == last_index) {
        return;
    }

    short d_index = 1;
    if ((outline->flags & FT_OUTLINE_REVERSE_FILL) != 0) {
        const short tmp_index = last_index;
        last_index = first_index;
        first_index = tmp_index;
        d_index = -1;
    }

    auto convert = [em_size](const FT_Vector& v) -> glm::vec2 {
        return glm::vec2(static_cast<float>(v.x) / em_size, static_cast<float>(v.y) / em_size);
    };

    auto make_midpoint = [](const glm::vec2& a, const glm::vec2& b) -> glm::vec2 {
        return 0.5f * (a + b);
    };

    auto make_curve = [](const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2) -> Glyph_curve {
        Glyph_curve result;
        result.p0 = p0;
        result.p1 = p1;
        result.p2 = p2;
        return result;
    };

    // Find a point that is on the curve and remove it from the list.
    glm::vec2 first{0.0f, 0.0f};
    const bool first_on_curve = (outline->tags[first_index] & FT_CURVE_TAG_ON) != 0;
    if (first_on_curve) {
        first = convert(outline->points[first_index]);
        first_index += d_index;
    } else {
        const bool last_on_curve = (outline->tags[last_index] & FT_CURVE_TAG_ON) != 0;
        if (last_on_curve) {
            first = convert(outline->points[last_index]);
            last_index -= d_index;
        } else {
            first = make_midpoint(convert(outline->points[first_index]), convert(outline->points[last_index]));
            // This is a virtual point, so we don't have to remove it.
        }
    }

    glm::vec2 start        = first;
    glm::vec2 control      = first;
    glm::vec2 previous     = first;
    char      previous_tag = FT_CURVE_TAG_ON;
    for (short index = first_index; index != static_cast<short>(last_index + d_index); index += d_index) {
        const glm::vec2 current     = convert(outline->points[index]);
        const char      current_tag = FT_CURVE_TAG(outline->tags[index]);
        if (current_tag == FT_CURVE_TAG_CUBIC) {
            // No-op, wait for more points.
            control = previous;
        } else if (current_tag == FT_CURVE_TAG_ON) {
            if (previous_tag == FT_CURVE_TAG_CUBIC) {
                const glm::vec2& b0 = start;
                const glm::vec2& b1 = control;
                const glm::vec2& b2 = previous;
                const glm::vec2& b3 = current;

                const glm::vec2 c0 = b0 + 0.75f * (b1 - b0);
                const glm::vec2 c1 = b3 + 0.75f * (b2 - b3);

                const glm::vec2 d = make_midpoint(c0, c1);

                curves.push_back(make_curve(b0, c0, d));
                curves.push_back(make_curve(d, c1, b3));
            } else if (previous_tag == FT_CURVE_TAG_ON) {
                // Linear segment.
                curves.push_back(make_curve(previous, make_midpoint(previous, current), current));
            } else {
                // Regular bezier curve.
                curves.push_back(make_curve(start, previous, current));
            }
            start = current;
            control = current;
        } else { // current_tag == FT_CURVE_TAG_CONIC
            if (previous_tag == FT_CURVE_TAG_ON) {
                // No-op, wait for third point.
            } else {
                // Create virtual on point.
                const glm::vec2 mid = make_midpoint(previous, current);
                curves.push_back(make_curve(start, previous, mid));
                start = mid;
                control = mid;
            }
        }
        previous = current;
        previous_tag = current_tag;
    }

    // Close the contour.
    if (previous_tag == FT_CURVE_TAG_CUBIC) {
        const glm::vec2& b0 = start;
        const glm::vec2& b1 = control;
        const glm::vec2& b2 = previous;
        const glm::vec2& b3 = first;

        const glm::vec2 c0 = b0 + 0.75f * (b1 - b0);
        const glm::vec2 c1 = b3 + 0.75f * (b2 - b3);

        const glm::vec2 d = make_midpoint(c0, c1);

        curves.push_back(make_curve(b0, c0, d));
        curves.push_back(make_curve(d, c1, b3));
    } else if (previous_tag == FT_CURVE_TAG_ON) {
        // Linear segment.
        curves.push_back(make_curve(previous, make_midpoint(previous, first), first));
    } else {
        curves.push_back(make_curve(start, previous, first));
    }
}

} // anonymous namespace

auto extract_glyph_outlines(const std::filesystem::path& font_path, std::span<const char32_t> codepoints) -> Glyph_outline_set
{
    Glyph_outline_set result;

    FT_Library library{nullptr};
    FT_Error ft_error = FT_Init_FreeType(&library);
    if (ft_error != FT_Err_Ok) {
        log_font->error("extract_glyph_outlines: FT_Init_FreeType() returned error code {}", ft_error);
        return result;
    }

    // Read the font into memory via erhe::file::read so the same code path
    // works for filesystem assets on desktop and APK assets on Android.
    std::optional<std::string> font_data = erhe::file::read("extract_glyph_outlines", font_path);
    if (!font_data.has_value()) {
        log_font->error("extract_glyph_outlines: erhe::file::read('{}') failed - cannot load font", font_path.string());
        FT_Done_FreeType(library);
        return result;
    }

    FT_Face face{nullptr};
    ft_error = FT_New_Memory_Face(
        library,
        reinterpret_cast<const FT_Byte*>(font_data.value().data()),
        static_cast<FT_Long>(font_data.value().size()),
        0,
        &face
    );
    if (ft_error != FT_Err_Ok) {
        log_font->error("extract_glyph_outlines: FT_New_Memory_Face('{}') returned error code {}", font_path.string(), ft_error);
        FT_Done_FreeType(library);
        return result;
    }

    ft_error = FT_Select_Charmap(face, FT_ENCODING_UNICODE);
    if (ft_error != FT_Err_Ok) {
        log_font->error("extract_glyph_outlines: FT_Select_Charmap() returned error code {}", ft_error);
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return result;
    }

    if ((face->face_flags & FT_FACE_FLAG_SCALABLE) == 0) {
        log_font->error("extract_glyph_outlines: '{}' is not a scalable font", font_path.string());
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return result;
    }

    // Load unscaled, unhinted outlines in font units; normalize to em units.
    const FT_Int32 load_flags = FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP;
    const float    em_size    = static_cast<float>(face->units_per_EM);

    result.glyphs.reserve(codepoints.size());
    for (const char32_t codepoint : codepoints) {
        Glyph_outline glyph_outline;
        glyph_outline.curve_start = static_cast<int>(result.curves.size());

        const FT_UInt glyph_index = FT_Get_Char_Index(face, static_cast<FT_ULong>(codepoint));
        if (glyph_index == 0) {
            log_font->warn("extract_glyph_outlines: '{}' has no glyph for codepoint {}", font_path.string(), static_cast<uint32_t>(codepoint));
            result.glyphs.push_back(glyph_outline); // curve_count == 0, slot order preserved
            continue;
        }

        ft_error = FT_Load_Glyph(face, glyph_index, load_flags);
        if (ft_error != FT_Err_Ok) {
            log_font->warn(
                "extract_glyph_outlines: FT_Load_Glyph() for codepoint {} returned error code {}",
                static_cast<uint32_t>(codepoint),
                ft_error
            );
            result.glyphs.push_back(glyph_outline); // curve_count == 0, slot order preserved
            continue;
        }

        const FT_Outline& outline = face->glyph->outline;
        short start = 0;
        for (short contour = 0; contour < outline.n_contours; ++contour) {
            // Note: The end indices in outline.contours are inclusive.
            convert_contour(result.curves, &outline, start, outline.contours[contour], em_size);
            start = static_cast<short>(outline.contours[contour] + 1);
        }

        glyph_outline.curve_count = static_cast<int>(result.curves.size()) - glyph_outline.curve_start;
        glyph_outline.bearing = glm::vec2(
            static_cast<float>(face->glyph->metrics.horiBearingX) / em_size,
            static_cast<float>(face->glyph->metrics.horiBearingY) / em_size
        );
        glyph_outline.size = glm::vec2(
            static_cast<float>(face->glyph->metrics.width) / em_size,
            static_cast<float>(face->glyph->metrics.height) / em_size
        );
        glyph_outline.advance = static_cast<float>(face->glyph->metrics.horiAdvance) / em_size;
        result.glyphs.push_back(glyph_outline);
    }

    FT_Done_Face(face);
    FT_Done_FreeType(library);

    result.valid = true;
    return result;
}

#else // !defined(ERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE)

auto extract_glyph_outlines(const std::filesystem::path& font_path, std::span<const char32_t> codepoints) -> Glyph_outline_set
{
    static_cast<void>(codepoints);
    log_font->warn("extract_glyph_outlines('{}'): font rasterization library is not available in this build", font_path.string());
    return Glyph_outline_set{};
}

#endif

} // namespace erhe::ui
