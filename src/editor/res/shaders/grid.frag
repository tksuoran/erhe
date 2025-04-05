
layout(location = 0) in vec4 v_position;

// Adapted from https://bgolus.medium.com/the-best-darn-grid-shader-yet-727f9278b9d8
float PristineGrid(vec2 uv, vec2 lineWidth)
{
    vec2 uvDDX = dFdx(uv);
    vec2 uvDDY = dFdy(uv);
    float epsilon = 0.00001;
    vec2 uvDeriv = vec2(length(vec2(uvDDX.x, uvDDY.x)), length(vec2(uvDDX.y, uvDDY.y)));
    if ((abs(uvDeriv.x) < epsilon) || (abs(uvDeriv.y) < epsilon)) {
        return 0.0;
    }
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

void main()
{
    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
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

    float epsilon = 0.0001;

    const float grid_l0_cell_size  = camera.cameras[0].grid_size      [0];
    const float grid_l0_line_width = camera.cameras[0].grid_line_width[0];
    const float grid_l1_cell_size  = camera.cameras[0].grid_size      [1];
    const float grid_l1_line_width = camera.cameras[0].grid_line_width[1];
    const float grid_l2_cell_size  = camera.cameras[0].grid_size      [2];
    const float grid_l2_line_width = camera.cameras[0].grid_line_width[2];
    const float grid_l3_cell_size  = camera.cameras[0].grid_size      [3];
    const float grid_l3_line_width = camera.cameras[0].grid_line_width[3];
    float grid_l0 = PristineGrid(uv / grid_l0_cell_size, vec2(grid_l0_line_width));
    float grid_l1 = PristineGrid(uv / grid_l1_cell_size, vec2(grid_l1_line_width));
    float grid_l2 = PristineGrid(uv / grid_l2_cell_size, vec2(grid_l2_line_width));
    float grid_l3 = PristineGrid(uv / grid_l3_cell_size, vec2(grid_l3_line_width));
    if (grid_l0 > epsilon) {
        out_color = vec4(0.0, 0.0, 0.01 * grid_l0, grid_l0);
        return;
    }
    if (grid_l1 > epsilon) {
        out_color = vec4(0.0, 0.0, 0.0, grid_l1);
        return;
    }
    if (grid_l2 > epsilon) {
        out_color = vec4(0.01 * grid_l2, 0.0, 0.0, grid_l2);
        return;
    }
    if (grid_l3 > epsilon) {
        out_color = vec4(0.0, 0.01 * grid_l3, 0.0, grid_l3);
        return;
    }

}
