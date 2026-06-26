#include "erhe_camera_view.glsl"
#include "erhe_skinning.glsl"
#include "erhe_standard_variant.glsl"

#define a_valency_edge_count a_custom_2

#if defined(ERHE_EDGE_LINES_CORNER_CAP)
// Screen-space ribbon width (px) at a clip-space point, replicating the wide-line
// expansion (compute_before_content_line.comp get_line_width) so the corner-cap
// disk exactly covers the ribbon thickness at the corner. thickness < 0 == fixed
// screen-space pixel width (no distance scaling).
float cap_line_width(vec4 position, float thickness, vec4 fov, vec4 viewport)
{
    if (thickness < 0.0) {
        return -thickness;
    }
    float fov_width        = fov[1] - fov[0];
    float viewport_width   = viewport[2];
    float max_size         = 4.0 * thickness;
    float scaled_thickness = max(max_size / position.w, 1.0);
    return (1.0 / 1024.0) * viewport_width * scaled_thickness / fov_width;
}

// Project one real triangle corner (object space) to a viewport-relative screen
// position, returning vec4(screen.xy, ribbon_half_width_px, used). vp_y_sign < 0
// (top-left framebuffer origin) flips screen Y to match gl_FragCoord -- mirrors the
// wide-line vp_y_sign path, since clip_from_world is y-up NDC on the main view.
vec4 project_corner_cap(
    vec3  corner_obj,
    bool  used,
    mat4  clip_from_world,
    mat4  world_from_node,
    vec4  vp,
    vec4  fov,
    float vp_y_sign,
    float line_width
)
{
    vec4 clip_c = clip_from_world * (world_from_node * vec4(corner_obj, 1.0));
    // A corner at or behind the eye plane (w <= 0) has no valid screen position.
    if (clip_c.w <= 0.0) {
        return vec4(0.0);
    }
    vec2  ndc_c  = clip_c.xy / clip_c.w;
    vec2  scr    = (ndc_c * 0.5 + 0.5) * vp.zw;
    scr.y        = mix(scr.y, vp.w - scr.y, step(vp_y_sign, 0.0));
    float half_w = 0.5 * cap_line_width(clip_c, line_width, fov, vp);
    return vec4(scr, half_w, used ? 1.0 : 0.0);
}
#endif

#if defined(ERHE_VARIANT_ID_RENDER)
// Locations 0 and 1 are reused under the ID-render variant for the two
// flat outputs the fragment packs into an RGB id. The lit varyings below
// are skipped via ERHE_VARIANT_POSITION_PASS, so there is no link
// conflict.
layout(location = 0) flat out int v_draw_id;
layout(location = 1) flat out int v_primitive_id;
#endif

#if defined(ERHE_VARIANT_POINTS)
// The points variant (corner points / polygon centroids) draws each vertex
// as a GL point. Redeclaring gl_PerVertex is required to write gl_PointSize;
// each variant compiles separately and multiview does not redeclare the
// block, so there is no conflict. Location 0 carries the flat point color --
// free here because v_position is gated out under ERHE_VARIANT_POSITION_PASS
// and the ID-render variant (the only other user of location 0) is mutually
// exclusive with points.
out gl_PerVertex {
    vec4  gl_Position;
    float gl_PointSize;
};
layout(location = 0) out vec4 v_point_color;
#endif

#if defined(ERHE_USE_VARYING_POSITION)
layout(location = 0) out vec4      v_position;
#endif

// TODO In the future we might have alpha test which would need texcoord
//      to be passed to fragment shader
#if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD0) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 1) out vec2      v_texcoord_0;
#endif

#if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD1) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 12) out vec2     v_texcoord_1;
#endif

#if defined(ERHE_USE_VERTEX_VARYING_COLOR) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 2) out vec4      v_color;
#endif

#if defined(ERHE_USE_VERTEX_VARYING_ANISO_CONTROL) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 3) out vec2      v_aniso_control;
#endif

#if defined(ERHE_USE_VERTEX_VARYING_TANGENT) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 4) out vec3      v_T;
#endif

#if defined(ERHE_USE_VERTEX_VARYING_BITANGENT) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 5) out vec3      v_B;
#endif

#if defined(ERHE_USE_VERTEX_VARYING_NORMAL) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 6) out vec3      v_N;
#endif

// TODO In the future we might have alpha test which would need material_index
//      to be passed to fragment shader
#if !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 7) flat out uint v_material_index;
#endif

// Debug-visualization varyings, only emitted when the viewport has
// selected a Shader_debug != none. Each is further gated on
// attribute presence so meshes lacking the underlying attribute
// still link; the fragment shader treats absent attributes as
// neutral defaults.
#if (ERHE_SHADER_DEBUG != 0) && !defined(ERHE_VARIANT_POSITION_PASS)
#  ifdef ERHE_ATTRIBUTE_a_tangent
layout(location =  8) out float      v_tangent_scale;
#  endif
layout(location =  9) out float      v_line_width;
#  ifdef ERHE_USE_SKINNING
layout(location = 10) out vec4       v_bone_color;
#  endif
#  ifdef ERHE_ATTRIBUTE_a_custom_2
layout(location = 11) flat out uvec2 v_valency_edge_count;
#  endif
#endif

// Solid-wireframe varyings: a per-vertex barycentric basis (reconstructed from
// the packed corner index in a_custom_4) plus the per-triangle real-edge mask
// and the wireframe color / width. Only emitted for the expanded fill mesh
// (which carries a_custom_4) under the solid-wireframe variant.
#if defined(ERHE_SOLID_WIREFRAME) && defined(ERHE_ATTRIBUTE_a_custom_4) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 13)      out vec3  v_bary;
layout(location = 14) flat out uint  v_edge_mask;
layout(location = 15) flat out vec4  v_wire_color;
layout(location = 16) flat out float v_wire_width;
#endif

// ID-buffer edge-line method: this fragment's own face id (per-primitive base
// from primitive.color.x + this triangle's facet id from a_custom_0). The
// EDGE_LINES_FROM_ID fill compares it against the face-ID buffer it samples and
// paints an edge line on a match; the FACE_ID_SEED seed pass writes it out as the
// frontmost-visible-face id. Flat: every vertex of a triangle shares one facet id.
#if defined(ERHE_EDGE_LINES_FROM_ID) || defined(ERHE_VARIANT_FACE_ID_SEED)
layout(location = 17) flat out uint v_edge_face_id;
#endif

// ID-buffer edge-line method, corner caps: this triangle's 3 corners projected to
// viewport-relative screen space (xy), each with its ribbon half-width (z, px) and
// a used flag (w; 1 = real shared vertex that should be capped, 0 = internal
// fan-triangulation corner that must not be). The fragment paints the edge color
// within half-width of any used corner, filling the gaps the per-pixel id match
// leaves at shared vertices.
#if defined(ERHE_EDGE_LINES_CORNER_CAP)
layout(location = 18) flat out vec4 v_cap0;
layout(location = 19) flat out vec4 v_cap1;
layout(location = 20) flat out vec4 v_cap2;
#endif

void main()
{
    mat4 world_from_node;
    mat4 world_from_node_normal;

#ifdef ERHE_USE_SKINNING
    if (primitive.primitives[ERHE_DRAW_ID].skinning_factor < 0.5) {
        world_from_node        = primitive.primitives[ERHE_DRAW_ID].world_from_node;
        world_from_node_normal = primitive.primitives[ERHE_DRAW_ID].world_from_node_normal;
    } else {
        erhe_skin_matrices(
            primitive.primitives[ERHE_DRAW_ID].base_joint_index,
            a_joint_indices_0,
            a_joint_weights_0,
            world_from_node,
            world_from_node_normal
        );
    }
#else
    world_from_node        = primitive.primitives[ERHE_DRAW_ID].world_from_node;
    world_from_node_normal = primitive.primitives[ERHE_DRAW_ID].world_from_node_normal;
#endif

    mat4 clip_from_world = camera.cameras[c_view_index].clip_from_world;

    vec4 position        = world_from_node * vec4(a_position, 1.0);
    gl_Position          = clip_from_world * position;

#if defined(ERHE_VARIANT_SHADOW_CUBE)
    // Point-light shadow cube caster: the fragment shader needs the world
    // position to compute radial distance to the light. This is a position pass,
    // so the lit-varying block below is skipped; assign v_position here. The
    // per-face clip-space y-flip that makes the stored face match the
    // samplerCubeArray (s,t) convention is NOT done here: it is applied on the
    // C++ side via the cube caster's coordinate conventions (clip_space_y_flip
    // derived from framebuffer_origin), baked into clip_from_world. See the
    // Shadow_renderer point-cube pass.
    v_position = position;
#endif

#if defined(ERHE_VARIANT_ID_RENDER)
    v_draw_id      = ERHE_DRAW_ID;
    // Reading gl_PrimitiveID in a fragment shader pulls in the SPIR-V
    // Geometry capability (and on Metal is not supported at all). The
    // ID pass always draws triangle lists, so the vertex shader writes
    // a per-vertex triangle index instead.
    v_primitive_id = gl_VertexID / 3;
#endif

#if defined(ERHE_EDGE_LINES_FROM_ID) || defined(ERHE_VARIANT_FACE_ID_SEED)
    // ID-buffer edge-line method face id. Recover the per-primitive face-id base
    // (raw float in primitive.color.x, stamped by Primitive_buffer via the shared
    // Face_id_base_provider) and add this triangle's facet id (a_custom_0 =
    // vec4_from_uint(facet)). The result matches what the edge-id pre-pass encoded
    // for the same face and what the seed pass writes out for the same face.
    // Computed here -- outside the !POSITION_PASS lit block -- so the FACE_ID_SEED
    // position-pass variant (which skips that block) still gets it.
    {
        uint face_id_base = uint(primitive.primitives[ERHE_DRAW_ID].color.x + 0.5);
#       if defined(ERHE_ATTRIBUTE_a_custom_0)
        // a_custom_0 is build_polygon_id()'s vec4_from_uint(facet): r = (facet>>24),
        // g = (facet>>16), b = (facet>>8), a = (facet>>0). Small facet indices live
        // entirely in .a, so all four components must be recombined (decoding only
        // r,g,b yields 0 for every facet < 256 -> only facet 0 ever matches).
        uint facet_id = (uint(a_custom_0.r * 255.0 + 0.5) << 24) |
                        (uint(a_custom_0.g * 255.0 + 0.5) << 16) |
                        (uint(a_custom_0.b * 255.0 + 0.5) <<  8) |
                         uint(a_custom_0.a * 255.0 + 0.5);
#       else
        uint facet_id = 0u;
#       endif
        // base 0 = this mesh has no face-id assignment (not registered for the
        // edge method, e.g. controller / rendertarget meshes in a shared fill
        // pass). 0 is the buffer's "no edge" id, so it can never match -> these
        // meshes simply render the normal fill / seed to 0 (occluder, no edge).
        v_edge_face_id = (face_id_base != 0u) ? (face_id_base + facet_id) : 0u;
    }
#endif

#if defined(ERHE_VARIANT_POINTS)
    // Point size and color are per-draw-primitive values from the primitive
    // buffer. gl_PointSize uses 1/distance attenuation (floored at 2 px). The
    // small depth bias pulls the point toward the camera so it passes the
    // surface depth test (Compare_operation::less) instead of z-fighting the
    // mesh corner / facet centroid it sits on.
    vec3  view_position_in_world = camera.cameras[c_view_index].world_from_node[3].xyz;
    float point_distance         = distance(view_position_in_world, position.xyz);
    float clip_depth_direction   = camera.cameras[c_view_index].clip_depth_direction;
#   ifdef ERHE_ATTRIBUTE_a_normal
    vec3  point_normal           = normalize(vec3(world_from_node_normal * vec4(a_normal, 0.0)));
    vec3  point_view_vector      = normalize(view_position_in_world - position.xyz);
    float point_NdotV            = dot(point_normal, point_view_vector);
    gl_Position.z               -= clip_depth_direction * 0.0005 * abs(point_NdotV);
#   else
    gl_Position.z               -= clip_depth_direction * 0.0005;
#   endif
    gl_PointSize  = max(primitive.primitives[ERHE_DRAW_ID].size / point_distance, 2.0);
    v_point_color = primitive.primitives[ERHE_DRAW_ID].color;
#endif

#if !defined(ERHE_VARIANT_POSITION_PASS)

#   if defined(ERHE_USE_VERTEX_VARYING_NORMAL)
    vec3 normal          = normalize(vec3(world_from_node_normal * vec4(a_normal, 0.0)));
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_TANGENT)
    vec3 tangent         = vec3(world_from_node * vec4(a_tangent.xyz, 0.0));
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_BITANGENT) && defined(ERHE_USE_VERTEX_VARYING_TANGENT) && defined(ERHE_USE_VERTEX_VARYING_NORMAL)
    vec3 bitangent       = cross(normal, tangent) * a_tangent.w;
#   endif

#   if (ERHE_SHADER_DEBUG != 0) && defined(ERHE_USE_SKINNING)
    if (primitive.primitives[ERHE_DRAW_ID].skinning_factor < 0.5) {
        v_bone_color = vec4(0.3, 0.0, 0.3, 1.0);
    } else {
        v_bone_color =
            a_joint_weights_0.x * joint.debug_joint_colors[(int(a_joint_indices_0.x) + primitive.primitives[ERHE_DRAW_ID].base_joint_index) % joint.debug_joint_color_count] +
            a_joint_weights_0.y * joint.debug_joint_colors[(int(a_joint_indices_0.y) + primitive.primitives[ERHE_DRAW_ID].base_joint_index) % joint.debug_joint_color_count] +
            a_joint_weights_0.z * joint.debug_joint_colors[(int(a_joint_indices_0.z) + primitive.primitives[ERHE_DRAW_ID].base_joint_index) % joint.debug_joint_color_count] +
            a_joint_weights_0.w * joint.debug_joint_colors[(int(a_joint_indices_0.w) + primitive.primitives[ERHE_DRAW_ID].base_joint_index) % joint.debug_joint_color_count];
    }
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_TANGENT)
    v_T              = normalize(tangent  );
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_BITANGENT)
    v_B              = normalize(bitangent);
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_NORMAL)
    v_N              = normal;
#   endif
    v_position       = position;

    v_material_index = primitive.primitives[ERHE_DRAW_ID].material_index;

#   if defined(ERHE_EDGE_LINES_CORNER_CAP)
    // Project this triangle's 3 real corners (object positions in a_custom_5/6/7,
    // replicated to all 3 soup vertices by build_expanded_polygon_fill) so the
    // fragment can cap shared vertices. used = the corner is an endpoint of a real
    // polygon edge: edge_mask (a_custom_4 bits 2..4) bit b gates the edge opposite
    // corner b, so corner c is real when either of the two bits != c is set --
    // internal fan-triangulation corners (no real edge) are left uncapped.
    {
        vec4  vp        = camera.cameras[c_view_index].viewport;
        vec4  fov       = camera.cameras[c_view_index].fov;
        float vp_y_sign = camera.cameras[c_view_index].vp_y_sign;
        float lw        = camera.cameras[c_view_index].edge_line_width;
#       if defined(ERHE_ATTRIBUTE_a_custom_4) && defined(ERHE_ATTRIBUTE_a_custom_5) && defined(ERHE_ATTRIBUTE_a_custom_6) && defined(ERHE_ATTRIBUTE_a_custom_7)
        uint edge_mask = (a_custom_4 >> 2u) & 7u;
        v_cap0 = project_corner_cap(a_custom_5, (edge_mask & 0x6u) != 0u, clip_from_world, world_from_node, vp, fov, vp_y_sign, lw);
        v_cap1 = project_corner_cap(a_custom_6, (edge_mask & 0x5u) != 0u, clip_from_world, world_from_node, vp, fov, vp_y_sign, lw);
        v_cap2 = project_corner_cap(a_custom_7, (edge_mask & 0x3u) != 0u, clip_from_world, world_from_node, vp, fov, vp_y_sign, lw);
#       else
        v_cap0 = vec4(0.0);
        v_cap1 = vec4(0.0);
        v_cap2 = vec4(0.0);
#       endif
    }
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD0)
    v_texcoord_0     = a_texcoord_0;
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD1)
    v_texcoord_1     = a_texcoord_1;
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_COLOR)
    v_color          = a_color_0;
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_ANISO_CONTROL)
    v_aniso_control  = a_custom_1;
#   endif

#   if ERHE_SHADER_DEBUG != 0
#       ifdef ERHE_ATTRIBUTE_a_tangent
    v_tangent_scale       = a_tangent.w;
#       endif
    v_line_width          = primitive.primitives[ERHE_DRAW_ID].size;
#       if defined(ERHE_ATTRIBUTE_a_custom_2)
    v_valency_edge_count  = a_valency_edge_count;
#       endif
#   endif

#   if defined(ERHE_SOLID_WIREFRAME) && defined(ERHE_ATTRIBUTE_a_custom_4)
    // a_custom_4: bits 0..1 = triangle corner index (barycentric basis),
    // bits 2..4 = real-edge mask (bit b gates barycentric component b).
    uint wireframe_corner = a_custom_4 & 3u;
    v_bary       = vec3(
        (wireframe_corner == 0u) ? 1.0 : 0.0,
        (wireframe_corner == 1u) ? 1.0 : 0.0,
        (wireframe_corner == 2u) ? 1.0 : 0.0
    );
    v_edge_mask  = (a_custom_4 >> 2u) & 7u;
    v_wire_color = primitive.primitives[ERHE_DRAW_ID].color;
    v_wire_width = primitive.primitives[ERHE_DRAW_ID].size;
#   endif
#endif

}
