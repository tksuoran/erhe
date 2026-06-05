#include "erhe_camera_view.glsl"
#include "erhe_glyph_coverage.glsl"

layout(location = 0) in vec4 v_position;

// Adapted from https://bgolus.medium.com/the-best-darn-grid-shader-yet-727f9278b9d8
float PristineGrid(vec2 uv, vec2 lineWidth)
{
    vec2 uvDDX = dFdx(uv);
    vec2 uvDDY = dFdy(uv);
    float epsilon = 0.00001;
    // Clamp the derivative away from zero instead of early-outing: a
    // tiny derivative is normal when the camera is close to the plane
    // and a coarse level's uv changes slowly per pixel (an early-out
    // here made the coarsest level's lines vanish on close-ups). The
    // clamp keeps drawWidth > 0 so targetWidth / drawWidth below stays
    // finite even for a zero derivative.
    vec2 uvDeriv = max(
        vec2(length(vec2(uvDDX.x, uvDDY.x)), length(vec2(uvDDX.y, uvDDY.y))),
        vec2(epsilon)
    );
    bvec2 invertLine = bvec2(lineWidth.x > 0.5, lineWidth.y > 0.5);
    vec2 targetWidth = vec2(
        invertLine.x ? 1.0 - lineWidth.x : lineWidth.x,
        invertLine.y ? 1.0 - lineWidth.y : lineWidth.y
    );
    vec2 drawWidth = clamp(targetWidth, uvDeriv, vec2(0.5));
    vec2 lineAA = max(uvDeriv, epsilon) * 1.5;
    vec2 gridUV = abs(fract(uv) * 2.0 - 1.0);
    gridUV.x = invertLine.x ? gridUV.x : 1.0 - gridUV.x;
    gridUV.y = invertLine.y ? gridUV.y : 1.0 - gridUV.y;
    vec2 grid2 = smoothstep(drawWidth + lineAA, drawWidth - lineAA, gridUV);

    grid2 *= clamp(targetWidth / drawWidth, 0.0, 1.0);
    grid2 = mix(grid2, targetWidth, clamp(uvDeriv * 2.0 - 1.0, 0.0, 1.0));
    grid2.x = invertLine.x ? 1.0 - grid2.x : grid2.x;
    grid2.y = invertLine.y ? 1.0 - grid2.y : grid2.y;
    return mix(grid2.x, 1.0, grid2.y);
}

bool intersect_plane(vec3 plane_normal, vec3 point_on_plane, vec3 ray_origin, vec3 ray_direction, out float t)
{
    const float epsilon = 0.000001;
    float denominator = dot(plane_normal, ray_direction);
    if (abs(denominator) < epsilon) {
        return false;
    }
    t = dot(point_on_plane - ray_origin, plane_normal) / denominator;
    return true;
}

#if defined(ERHE_GRID_LABELS)

// Number of character cells in the decimal representation of value
// (digits plus an optional leading minus sign).
int grid_label_char_count(int value)
{
    int count = (value < 0) ? 2 : 1;
    int a = abs(value);
    if (a >= 10)     { count += 1; }
    if (a >= 100)    { count += 1; }
    if (a >= 1000)   { count += 1; }
    if (a >= 10000)  { count += 1; }
    if (a >= 100000) { count += 1; }
    return count;
}

// Glyph slot for character cell `cell` of the decimal representation of
// value: slots 0..9 are digits '0'..'9', slot 10 is '-'.
int grid_label_slot(int value, int char_count, int cell)
{
    bool negative = (value < 0);
    if (negative && (cell == 0)) {
        return 10; // '-'
    }
    int a           = abs(value);
    int digit_count = char_count - (negative ? 1 : 0);
    int digit_index = cell       - (negative ? 1 : 0);
    int divisor     = 1;
    for (int i = 0; i < (digit_count - 1 - digit_index); ++i) {
        divisor *= 10;
    }
    return (a / divisor) % 10;
}

#endif // ERHE_GRID_LABELS

void main()
{
    vec3 view_position_in_world = vec3(
        camera.cameras[c_view_index].world_from_node[3][0],
        camera.cameras[c_view_index].world_from_node[3][1],
        camera.cameras[c_view_index].world_from_node[3][2]
    );
    vec3  fragment_position = v_position.xyz / v_position.w;
    vec3  ro                = view_position_in_world;
    vec3  rd                = normalize(fragment_position - view_position_in_world);
    vec3  grid_plane_normal = vec3(0.0, 1.0, 0.0);
    vec3  grid_plane_point  = vec3(0.0, 0.0, 0.0);
    float t;
    bool  intersects_plane  = intersect_plane(grid_plane_normal, grid_plane_point, ro, rd, t);
    vec3  pos               = ro + t * rd;
    vec2  uv                = pos.xz;

    // Derivatives must be computed in uniform control flow, before any
    // divergent branching below.
    vec2 uv_fw = fwidth(uv);

    float epsilon = 0.0001;

    float grid_l0_cell_size  = camera.cameras[c_view_index].grid_size      [0];
    float grid_l0_line_width = camera.cameras[c_view_index].grid_line_width[0];
    float grid_l1_cell_size  = camera.cameras[c_view_index].grid_size      [1];
    float grid_l1_line_width = camera.cameras[c_view_index].grid_line_width[1];
    float grid_l2_cell_size  = camera.cameras[c_view_index].grid_size      [2];
    float grid_l2_line_width = camera.cameras[c_view_index].grid_line_width[2];
    float grid_l3_cell_size  = camera.cameras[c_view_index].grid_size      [3];
    float grid_l3_line_width = camera.cameras[c_view_index].grid_line_width[3];
    float grid_l0 = PristineGrid(uv / grid_l0_cell_size, vec2(grid_l0_line_width));
    float grid_l1 = PristineGrid(uv / grid_l1_cell_size, vec2(grid_l1_line_width));
    float grid_l2 = PristineGrid(uv / grid_l2_cell_size, vec2(grid_l2_line_width));
    float grid_l3 = PristineGrid(uv / grid_l3_cell_size, vec2(grid_l3_line_width));

    // Composite all levels with the premultiplied "over" operator
    // (finer levels under coarser ones) so that crossing lines from
    // different levels blend their coverage instead of one level
    // replacing the other. Per-level colors come from the camera UBO
    // (rgb = line color, a = line opacity multiplied into coverage).
    vec4 grid_l0_tint  = camera.cameras[c_view_index].grid_color[0];
    vec4 grid_l1_tint  = camera.cameras[c_view_index].grid_color[1];
    vec4 grid_l2_tint  = camera.cameras[c_view_index].grid_color[2];
    vec4 grid_l3_tint  = camera.cameras[c_view_index].grid_color[3];
    vec4 grid_l0_color = vec4(grid_l0_tint.rgb, 1.0) * (grid_l0 * grid_l0_tint.a);
    vec4 grid_l1_color = vec4(grid_l1_tint.rgb, 1.0) * (grid_l1 * grid_l1_tint.a);
    vec4 grid_l2_color = vec4(grid_l2_tint.rgb, 1.0) * (grid_l2 * grid_l2_tint.a);
    vec4 grid_l3_color = vec4(grid_l3_tint.rgb, 1.0) * (grid_l3 * grid_l3_tint.a);
    vec4 grid_color = grid_l3_color;
    grid_color = grid_l2_color + (grid_color * (1.0 - grid_l2_color.a));
    grid_color = grid_l1_color + (grid_color * (1.0 - grid_l1_color.a));
    grid_color = grid_l0_color + (grid_color * (1.0 - grid_l0_color.a));

    float label_alpha = 0.0;
    vec3  label_rgb   = vec3(0.0);

#if defined(ERHE_GRID_LABELS)
    // Axis coordinate labels: grid_label.x = enable, .y = text height as a
    // fraction of label spacing, .z = label spacing in world units (integer
    // values >= 1 expected), .w = fade threshold: glyph size in pixels per
    // em at which labels are fully visible (fade starts at half of it).
    vec4 grid_label = camera.cameras[c_view_index].grid_label;

    // Fade labels in once a glyph covers enough pixels to be legible.
    float pixel_size     = max(max(uv_fw.x, uv_fw.y), 0.0000001);
    float spacing        = max(grid_label.z, 0.000001);
    float text_h         = grid_label.y * spacing; // em size in world units
    float pixels_per_em  = text_h / pixel_size;
    float fade_full      = max(grid_label.w, 0.0001);
    float label_fade     = smoothstep(0.5 * fade_full, fade_full, pixels_per_em);

    if ((grid_label.x > 0.5) && (label_fade > 0.0)) {
        float cell_w  = glyph.glyphs[0].advance * text_h; // monospace: every slot has the same advance
        float margin  = 0.25 * text_h;

        int  slot = -1;
        vec2 glyph_uv = vec2(0.0);

        // X axis: labels below the axis line (positive z side), centered
        // on each tick. Glyph u maps to +x, glyph v maps to -z so text
        // reads left to right when viewed from +Y.
        {
            float k    = round(uv.x / spacing);
            float tick = k * spacing;
            if (abs(tick) < 100000.0) {
                int   value      = int(round(tick));
                int   char_count = grid_label_char_count(value);
                float width      = float(char_count) * cell_w;
                float x_start    = tick - (0.5 * width);
                float z_base     = margin + (0.75 * text_h);
                int   cell       = int(floor((uv.x - x_start) / cell_w));
                bool  in_band    = (uv.y > 0.0) && (uv.y < (z_base + (0.3 * text_h)));
                if ((cell >= 0) && (cell < char_count) && in_band) {
                    slot     = grid_label_slot(value, char_count, cell);
                    glyph_uv = vec2(uv.x - (x_start + (float(cell) * cell_w)), z_base - uv.y) / text_h;
                }
            }
        }

        // Z axis: labels to the right of the axis line (positive x side),
        // vertically centered on each tick. Skip the origin; the X axis
        // already labels it.
        if (slot < 0) {
            float k    = round(uv.y / spacing);
            float tick = k * spacing;
            if ((k != 0.0) && (abs(tick) < 100000.0)) {
                int   value      = int(round(tick));
                int   char_count = grid_label_char_count(value);
                float x_start    = margin;
                float z_base     = tick + (0.35 * text_h);
                int   cell       = int(floor((uv.x - x_start) / cell_w));
                bool  in_band    = (uv.y > (z_base - text_h)) && (uv.y < (z_base + (0.3 * text_h)));
                if ((cell >= 0) && (cell < char_count) && in_band) {
                    slot     = grid_label_slot(value, char_count, cell);
                    glyph_uv = vec2(uv.x - (x_start + (float(cell) * cell_w)), z_base - uv.y) / text_h;
                }
            }
        }

        if (slot >= 0) {
            vec2 inverse_diameter = text_h / max(uv_fw, vec2(0.0000001));
            vec4 label_color      = camera.cameras[c_view_index].grid_label_color;
            label_alpha = erhe_glyph_coverage(slot, glyph_uv, inverse_diameter, true) * label_fade * label_color.a;
            label_rgb   = label_color.rgb;
        }
    }
#endif // ERHE_GRID_LABELS

    // Composite labels over grid lines (premultiplied output).
    out_color = vec4(
        (label_rgb * label_alpha) + (grid_color.rgb * (1.0 - label_alpha)),
        (grid_color.a * (1.0 - label_alpha)) + label_alpha
    );
    if (out_color.a < epsilon) {
        discard;
    }
}
