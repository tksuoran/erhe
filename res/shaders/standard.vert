#include "erhe_camera_view.glsl"
#include "erhe_skinning.glsl"
#include "erhe_standard_variant.glsl"

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
#endif

}
