#include "erhe_camera_view.glsl"
#include "erhe_skinning.glsl"
#include "erhe_standard_variant.glsl"
#include "erhe_vertex_joint_weights.glsl"
#include "erhe_position_rounding.glsl"
#include "erhe_vertex_position.glsl"
#include "erhe_vertex_tbn.glsl"
#include "erhe_vertex_texcoord.glsl"

#define a_valency_edge_count a_custom_2

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
// The position relative to the pass's view origin
// (camera.cameras[].view_origin): what gl_Position is computed from, and what
// the shadow casters and receivers work with (doc/erhe/shadows.md
// "View-relative positions"). Its fp32 rounding scales with the distance
// from the view origin; v_position's scales with the distance from the world
// origin. v_position_rounding is the bound on that rounding
// (get_vertex_position_rounding(), erhe_position_rounding.glsl), interpolated
// to the fragment for the shadow receiver's minimum bias.
layout(location = 17) out vec3     v_view_relative_position;
layout(location = 18) out float    v_position_rounding;
#endif

// TODO In the future we might have alpha test which would need texcoord
//      to be passed to fragment shader
#if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD0) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 1) out vec2      v_texcoord_0;
#endif

#if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD1) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 12) out vec2     v_texcoord_1;
#endif

#if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD2) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 21) out vec2      v_texcoord_2;             // lightmap UVs
layout(location = 22) flat out vec4 v_lightmap_scale_offset;  // atlas region; xy 0 = no lightmap
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

// Scale-only tangent frame for ERHE_TEXGEN_MODE_TANGENT (see
// erhe_standard_variant.glsl). Independent of the shading frame v_T / v_B:
// it is built from the normal alone, so it carries no authored tangent
// direction, and the only part of world_from_node that reaches it is the
// per-axis scale.
#if defined(ERHE_USE_TANGENT_TEXGEN) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 25) out vec3     v_T_scale_only;
layout(location = 26) out vec3     v_B_scale_only;
// The node-space position with only the node's scale applied - what the
// tangent texgen mode projects onto that frame, in place of the world
// position.
layout(location = 27) out vec3     v_position_scale_only;
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

// Single-joint weight for the joint_weight_ramp debug mode. Sign-encoded
// Blender-style: >= 0 = the target joint's weight at this vertex, -1 =
// no influence from the target joint with the zero-black option on
// (joint.debug_joint_indices.y != 0), -2 = no target joint selected.
// Only skinned variants emit it; the fragment side falls back to a const
// and leaves non-skinned meshes with their normal shading.
#if (ERHE_SHADER_DEBUG == 34) && defined(ERHE_USE_SKINNING) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 23) out float v_weight;
#endif

// Untransformed node-space position for the node_* texgen modes
// (ERHE_SELECT_TEXCOORD in standard.frag); only emitted when a material
// texture slot or the circular-brushed-metal block sources one.
#if defined(ERHE_USE_VERTEX_VARYING_NODE_POSITION) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 24) out vec3 v_node_position;
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

#if defined(ERHE_USE_TANGENT_TEXGEN) && !defined(ERHE_VARIANT_POSITION_PASS)
// Unit axis along the smallest absolute component of v: the axis least
// aligned with v, so cross(erhe_min_axis(v), v) is the best conditioned
// perpendicular of the three axis choices and never degenerates.
vec3 erhe_min_axis(vec3 v)
{
    vec3 a = abs(v);
    if ((a.x <= a.y) && (a.x <= a.z)) {
        return vec3(1.0, 0.0, 0.0);
    }
    if (a.y <= a.z) {
        return vec3(0.0, 1.0, 0.0);
    }
    return vec3(0.0, 0.0, 1.0);
}
#endif

// `precise` is core GLSL from 4.00.
#if __VERSION__ >= 400
#   define ERHE_VIEW_RELATIVE_PRECISE precise
#else
#   define ERHE_VIEW_RELATIVE_PRECISE
#endif

void main()
{
    mat4 world_from_node;
    mat4 world_from_node_normal;

#ifdef ERHE_USE_SKINNING
    // The optimized variant stores only three weights and recovers the fourth
    // from the fact that they sum to one; see erhe_vertex_joint_weights.glsl.
    vec4 node_joint_weights = erhe_decode_vertex_joint_weights();

    if (primitive.primitives[ERHE_DRAW_ID].skinning_factor < 0.5) {
        world_from_node        = primitive.primitives[ERHE_DRAW_ID].world_from_node;
        world_from_node_normal = primitive.primitives[ERHE_DRAW_ID].world_from_node_normal;
    } else {
        erhe_skin_matrices(
            primitive.primitives[ERHE_DRAW_ID].base_joint_index,
            a_joint_indices_0,
            node_joint_weights,
            world_from_node,
            world_from_node_normal
        );
    }
#else
    world_from_node        = primitive.primitives[ERHE_DRAW_ID].world_from_node;
    world_from_node_normal = primitive.primitives[ERHE_DRAW_ID].world_from_node_normal;
#endif

    // Relative to eye (doc/erhe/shadows.md "View-relative positions"): the
    // node's translation relative to the pass's view origin. Both are exact
    // fp32 values (the CPU writes the node's fp32 world transform as is, and
    // the view origin is the fp32 camera position), so this one subtraction
    // is correctly rounded - the value a double subtraction rounded once
    // would give - and small near the view origin; the vertex positions
    // computed from it round with the distance from the view origin, not from
    // the world origin. precise: the compiler must not fold the subtraction
    // into the sum below, which would reintroduce the large magnitudes. A
    // skinned primitive's translation is the fp32 blend of its absolute joint
    // matrices and keeps that blend's rounding.
    ERHE_VIEW_RELATIVE_PRECISE vec3 view_relative_translation = world_from_node[3].xyz - camera.cameras[c_view_index].view_origin.xyz;
    mat4 view_relative_from_node = mat4(
        world_from_node[0],
        world_from_node[1],
        world_from_node[2],
        vec4(view_relative_translation, 1.0)
    );

    // Object space position: identical to a_position unless the vertex format
    // stores it quantized into the primitive AABB, in which case this is the
    // decode. ERHE_DRAW_ID is the same primitive record the transforms above
    // came from, so caster and lit pass always decode with the same affine.
    vec3 node_position   = erhe_decode_vertex_position(
        primitive.primitives[ERHE_DRAW_ID].position_scale.xyz,
        primitive.primitives[ERHE_DRAW_ID].position_offset.xyz
    );

    vec4 position               = world_from_node * vec4(node_position, 1.0);
    ERHE_VIEW_RELATIVE_PRECISE vec4 view_relative_position = view_relative_from_node * vec4(node_position, 1.0);
    gl_Position                 = camera.cameras[c_view_index].clip_from_view_relative * view_relative_position;
    // The bound on view_relative_position's fp32 rounding: the products
    // above round with the vertex's distance from its node origin (a large
    // mesh far from its node origin carries a large bound however near the
    // view origin its vertex is), the translation with its own magnitude.
    float position_rounding     = get_vertex_position_rounding(mat3(world_from_node), node_position, view_relative_translation);

    // Object space tangent frame. Under the optimized variant's quaternion
    // encoding there is no a_normal attribute at all and both the normal and the
    // tangent come out of a_tangent; see erhe_vertex_tbn.glsl. Unused components
    // are dead code the compiler drops.
    vec3  node_normal;
    vec3  node_tangent;
    float node_handedness;
    erhe_decode_vertex_tbn(node_normal, node_tangent, node_handedness);

#if defined(ERHE_VARIANT_SHADOW_CUBE) || defined(ERHE_VARIANT_SHADOW_DISTANCE)
    // Point-light shadow cube caster and distance-technique caster: the
    // fragment shader needs the caster position relative to the shadow pass's
    // view origin (the light camera) to compute the caster plane's distance
    // from the light. This is a position pass, so the lit-varying block below
    // is skipped; assign the position varyings here. The per-face clip-space
    // y-flip that makes the stored face match the samplerCubeArray (s,t)
    // convention is NOT done here: it is applied on the C++ side via the cube
    // caster's coordinate conventions (clip_space_y_flip derived from
    // framebuffer_origin), baked into clip_from_view_relative. See the
    // Shadow_renderer point-cube pass.
    v_position               = position;
    v_view_relative_position = view_relative_position.xyz;
    v_position_rounding      = position_rounding;
#endif

#if defined(ERHE_VARIANT_ID_RENDER)
    v_draw_id      = ERHE_DRAW_ID;
    // Emit this facet's GEO index directly, decoded from the per-vertex facet-id
    // attribute (a_custom_0 = build_polygon_id()'s vec4_from_uint(facet)). Every
    // corner of a facet carries the same value, so this flat output is the facet
    // id regardless of provoking vertex or how the facet was triangulated -- the
    // value Id_renderer's readback packs as (id_offset + facet_id) and the picking
    // resolve consumes as a GEO facet index (the same index space mesh-component
    // selection and the facet debug viz use). Deriving it arithmetically from
    // gl_VertexID/3 over the shared fan-triangulated pool was wrong for any facet
    // with more than 3 corners; this attribute is robust by construction.
#   if defined(ERHE_ATTRIBUTE_a_custom_0)
    // a_custom_0: r = (facet>>24), g = (facet>>16), b = (facet>>8), a = (facet>>0);
    // small facet indices live entirely in .a, so all four components recombine.
    v_primitive_id = int(
        (uint(a_custom_0.r * 255.0 + 0.5) << 24) |
        (uint(a_custom_0.g * 255.0 + 0.5) << 16) |
        (uint(a_custom_0.b * 255.0 + 0.5) <<  8) |
         uint(a_custom_0.a * 255.0 + 0.5));
#   else
    // Identity-only meshes (rendertarget / tools) whose vertex format lacks the
    // attribute: mesh identity comes from the id_offset range, not this value.
    v_primitive_id = 0;
#   endif
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
#   ifdef ERHE_HAS_VERTEX_NORMAL
    vec3  point_normal           = normalize(vec3(world_from_node_normal * vec4(node_normal, 0.0)));
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
    vec3 normal          = normalize(vec3(world_from_node_normal * vec4(node_normal, 0.0)));
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_TANGENT)
    vec3 tangent         = vec3(world_from_node * vec4(node_tangent, 0.0));
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_BITANGENT) && defined(ERHE_USE_VERTEX_VARYING_TANGENT) && defined(ERHE_USE_VERTEX_VARYING_NORMAL)
    vec3 bitangent       = cross(normal, tangent) * node_handedness;
#   endif

#   if (ERHE_SHADER_DEBUG != 0) && defined(ERHE_USE_SKINNING)
    if (primitive.primitives[ERHE_DRAW_ID].skinning_factor < 0.5) {
        v_bone_color = vec4(0.3, 0.0, 0.3, 1.0);
    } else {
        v_bone_color =
            node_joint_weights.x * joint.debug_joint_colors[(int(a_joint_indices_0.x) + primitive.primitives[ERHE_DRAW_ID].base_joint_index) % joint.debug_joint_color_count] +
            node_joint_weights.y * joint.debug_joint_colors[(int(a_joint_indices_0.y) + primitive.primitives[ERHE_DRAW_ID].base_joint_index) % joint.debug_joint_color_count] +
            node_joint_weights.z * joint.debug_joint_colors[(int(a_joint_indices_0.z) + primitive.primitives[ERHE_DRAW_ID].base_joint_index) % joint.debug_joint_color_count] +
            node_joint_weights.w * joint.debug_joint_colors[(int(a_joint_indices_0.w) + primitive.primitives[ERHE_DRAW_ID].base_joint_index) % joint.debug_joint_color_count];
    }
#   endif

#   if (ERHE_SHADER_DEBUG == 34) && defined(ERHE_USE_SKINNING)
    // joint_weight_ramp: sum this vertex's influence from the target joint.
    // debug_joint_indices.x == 0xffffffffu = no target joint;
    // .y != 0 = show zero-weight vertices as black (-1).
    if ((primitive.primitives[ERHE_DRAW_ID].skinning_factor < 0.5) || (joint.debug_joint_indices.x == 0xffffffffu)) {
        v_weight = -2.0;
    } else {
        uint base_joint = primitive.primitives[ERHE_DRAW_ID].base_joint_index;
        float w =
            ((joint.joints[int(a_joint_indices_0.x) + int(base_joint)].debug_flags.x != 0u) ? node_joint_weights.x : 0.0) +
            ((joint.joints[int(a_joint_indices_0.y) + int(base_joint)].debug_flags.x != 0u) ? node_joint_weights.y : 0.0) +
            ((joint.joints[int(a_joint_indices_0.z) + int(base_joint)].debug_flags.x != 0u) ? node_joint_weights.z : 0.0) +
            ((joint.joints[int(a_joint_indices_0.w) + int(base_joint)].debug_flags.x != 0u) ? node_joint_weights.w : 0.0);
        bool zero_black = joint.debug_joint_indices.y != 0u;
        v_weight = ((w <= 0.0) && zero_black) ? -1.0 : w;
    }
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_TANGENT)
    v_T              = normalize(tangent  );
#   endif

    // Guard must match the bitangent declaration above (BITANGENT alone is
    // not enough - the cross product needs normal and tangent), so a key
    // that violates the BITANGENT => TANGENT && NORMAL invariant degrades
    // to an unwritten varying instead of a compile error.
#   if defined(ERHE_USE_VERTEX_VARYING_BITANGENT) && defined(ERHE_USE_VERTEX_VARYING_TANGENT) && defined(ERHE_USE_VERTEX_VARYING_NORMAL)
    v_B              = normalize(bitangent);
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_NORMAL)
    v_N              = normal;
#   endif

#   if defined(ERHE_USE_TANGENT_TEXGEN)
    // Scale-only tangent frame for the tangent texgen mode. S is the per-axis
    // scale of world_from_node, recovered as the squared lengths of its basis
    // columns; the inverse transpose of that scale-only part is diag(1/s), so
    // transforming the node-space normal with it is a component-wise multiply
    // by inversesqrt(S). Building T and B from that normal alone (rather than
    // from the authored tangent) keeps the projection stable across a mesh and
    // dependent only on the node's scale, while the scale correction makes the
    // generated texcoords track a non-uniformly scaled node.
    vec3 texgen_node_scale_squared = vec3(
        dot(world_from_node[0].xyz, world_from_node[0].xyz),
        dot(world_from_node[1].xyz, world_from_node[1].xyz),
        dot(world_from_node[2].xyz, world_from_node[2].xyz)
    );
    vec3 texgen_node_scale = sqrt(max(texgen_node_scale_squared, vec3(1.0e-12)));
    vec3 texgen_normal     = normalize(node_normal / texgen_node_scale);
    vec3 texgen_tangent    = normalize(cross(erhe_min_axis(texgen_normal), texgen_normal));
    v_T_scale_only         = texgen_tangent;
    v_B_scale_only         = normalize(cross(texgen_normal, texgen_tangent));
    // The position the tangent texgen mode projects: node space scaled by S,
    // i.e. the node's world size without its rotation or translation. The
    // generated texcoords therefore follow the mesh as the node is moved or
    // rotated, and only stretch when it is scaled.
    v_position_scale_only  = node_position * texgen_node_scale;
#   endif
    v_position               = position;
    v_view_relative_position = view_relative_position.xyz;
    v_position_rounding      = position_rounding;

    v_material_index = primitive.primitives[ERHE_DRAW_ID].material_index;

#   if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD0)
    // xy of the primitive's texcoord affine is channel 0, zw is channel 1. The
    // decode is a no-op unless the vertex format stores these channels as
    // unorm16x2 (the optimized variant); see erhe_vertex_texcoord.glsl.
    v_texcoord_0     = erhe_decode_vertex_texcoord(
        a_texcoord_0,
        primitive.primitives[ERHE_DRAW_ID].texcoord_scale.xy,
        primitive.primitives[ERHE_DRAW_ID].texcoord_offset.xy
    );
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_NODE_POSITION)
    v_node_position  = node_position;
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD2)
    v_texcoord_2            = a_texcoord_2;
    v_lightmap_scale_offset = primitive.primitives[ERHE_DRAW_ID].lightmap_scale_offset;
#   endif
#   if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD1)
    v_texcoord_1     = erhe_decode_vertex_texcoord(
        a_texcoord_1,
        primitive.primitives[ERHE_DRAW_ID].texcoord_scale.zw,
        primitive.primitives[ERHE_DRAW_ID].texcoord_offset.zw
    );
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_COLOR)
    v_color          = a_color_0;
#   endif

#   if defined(ERHE_USE_VERTEX_VARYING_ANISO_CONTROL)
    v_aniso_control  = a_custom_1;
#   endif

#   if ERHE_SHADER_DEBUG != 0
#       ifdef ERHE_ATTRIBUTE_a_tangent
    v_tangent_scale       = node_handedness;
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
