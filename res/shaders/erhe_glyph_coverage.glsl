// GPU curve-based glyph coverage evaluation.
//
// Adapted from https://github.com/GreenLightning/gpu-font-rendering
// Copyright (c) 2022 Green Lightning, MIT License.
// Based on: http://wdobbie.com/post/gpu-text-rendering-with-vector-textures/
//
// Reads the "glyph" interface block (see erhe::scene_renderer::Glyph_interface):
//   glyph.glyphs[slot] - Glyph_meta { int start; int count; vec2 bearing; vec2 size; float advance; }
//   glyph.curves[]     - flat vec2 array, 3 consecutive entries per quadratic bezier curve
//
// Coordinates are in em units relative to the glyph pen origin (baseline at
// v = 0). Callers must compute inverse_diameter (1.0 / anti-aliasing window
// size in em units) from derivatives taken in uniform control flow; this
// file contains no derivative operations.

#if defined(ERHE_GRID_LABELS)

// Fractional coverage contribution of one quadratic bezier curve for a ray
// along +x from the origin. Curve control points p0, p1, p2 are relative to
// the sample position.
float erhe_glyph_compute_coverage(float inverse_diameter, vec2 p0, vec2 p1, vec2 p2)
{
    if ((p0.y > 0.0) && (p1.y > 0.0) && (p2.y > 0.0)) {
        return 0.0;
    }
    if ((p0.y < 0.0) && (p1.y < 0.0) && (p2.y < 0.0)) {
        return 0.0;
    }

    // Note: Simplified from abc formula by extracting a factor of (-2) from b.
    vec2 a = p0 - 2.0 * p1 + p2;
    vec2 b = p0 - p1;
    vec2 c = p0;

    float t0;
    float t1;
    if (abs(a.y) >= 1e-5) {
        // Quadratic segment, solve abc formula to find roots.
        float radicand = (b.y * b.y) - (a.y * c.y);
        if (radicand <= 0.0) {
            return 0.0;
        }
        float s = sqrt(radicand);
        t0 = (b.y - s) / a.y;
        t1 = (b.y + s) / a.y;
    } else {
        // Linear segment, avoid division by a.y, which is near zero.
        // There is only one root, so we have to decide which variable to
        // assign it to based on the direction of the segment, to ensure
        // that the ray always exits the shape at t0 and enters at t1.
        float t = p0.y / (p0.y - p2.y);
        if (p0.y < p2.y) {
            t0 = -1.0;
            t1 = t;
        } else {
            t0 = t;
            t1 = -1.0;
        }
    }

    float alpha = 0.0;

    if ((t0 >= 0.0) && (t0 < 1.0)) {
        float x = ((a.x * t0) - (2.0 * b.x)) * t0 + c.x;
        alpha += clamp((x * inverse_diameter) + 0.5, 0.0, 1.0);
    }

    if ((t1 >= 0.0) && (t1 < 1.0)) {
        float x = ((a.x * t1) - (2.0 * b.x)) * t1 + c.x;
        alpha -= clamp((x * inverse_diameter) + 0.5, 0.0, 1.0);
    }

    return alpha;
}

vec2 erhe_glyph_rotate(vec2 v)
{
    return vec2(v.y, -v.x);
}

// Coverage of glyph `slot` at `glyph_uv` (em units, relative to the pen
// origin). `inverse_diameter` is the reciprocal of the anti-aliasing window
// size in em units along the two ray directions; `supersample` adds a second
// rotated ray for 2-dimensional anti-aliasing.
float erhe_glyph_coverage(int slot, vec2 glyph_uv, vec2 inverse_diameter, bool supersample)
{
    float alpha = 0.0;
    int   start = glyph.glyphs[slot].start;
    int   count = glyph.glyphs[slot].count;
    for (int i = 0; i < count; ++i) {
        int  base = 3 * (start + i);
        vec2 p0   = glyph.curves[base + 0] - glyph_uv;
        vec2 p1   = glyph.curves[base + 1] - glyph_uv;
        vec2 p2   = glyph.curves[base + 2] - glyph_uv;
        alpha += erhe_glyph_compute_coverage(inverse_diameter.x, p0, p1, p2);
        if (supersample) {
            alpha += erhe_glyph_compute_coverage(
                inverse_diameter.y,
                erhe_glyph_rotate(p0),
                erhe_glyph_rotate(p1),
                erhe_glyph_rotate(p2)
            );
        }
    }
    if (supersample) {
        alpha *= 0.5;
    }
    return clamp(alpha, 0.0, 1.0);
}

#endif // ERHE_GRID_LABELS
