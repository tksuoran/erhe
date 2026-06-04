#pragma once

#include <glm/glm.hpp>

#include <filesystem>
#include <span>
#include <vector>

namespace erhe::ui {

// Quadratic bezier curve in em units (font units divided by units_per_EM).
class Glyph_curve
{
public:
    glm::vec2 p0{0.0f, 0.0f};
    glm::vec2 p1{0.0f, 0.0f};
    glm::vec2 p2{0.0f, 0.0f};
};

// Outline and metrics for a single glyph. Curves for the glyph are
// curves[curve_start .. curve_start + curve_count) in the owning
// Glyph_outline_set. All values are in em units.
class Glyph_outline
{
public:
    int       curve_start{0};
    int       curve_count{0};
    glm::vec2 bearing    {0.0f, 0.0f}; // horizontal bearing x, y
    glm::vec2 size       {0.0f, 0.0f}; // width, height
    float     advance    {0.0f};       // horizontal advance
};

// Plain CPU-side glyph outline data: no GPU types. Glyphs are stored in
// the same order as the codepoints passed to extract_glyph_outlines(),
// so callers can use a fixed codepoint-to-slot convention.
class Glyph_outline_set
{
public:
    std::vector<Glyph_outline> glyphs;
    std::vector<Glyph_curve>   curves;
    bool                       valid{false};
};

// Extracts glyph outlines as quadratic bezier curves for the given
// codepoints from a font file. Line segments become degenerate quadratics
// (control point at segment midpoint) and cubic beziers (CFF / OpenType
// outlines) are approximated with two quadratics each. Codepoints missing
// from the font produce an entry with curve_count == 0 so slot order is
// preserved. Returns valid == false when font loading fails or when the
// font rasterization library (FreeType) is not available in this build.
auto extract_glyph_outlines(const std::filesystem::path& font_path, std::span<const char32_t> codepoints) -> Glyph_outline_set;

} // namespace erhe::ui
