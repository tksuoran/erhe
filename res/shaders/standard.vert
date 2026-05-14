#include "erhe_camera_view.glsl"
#include "erhe_standard_variant.glsl"

#define a_valency_edge_count a_custom_2

layout(location = 0) out vec4      v_position;

#ifdef ERHE_USE_VERTEX_VARYING_TEXCOORD0
layout(location = 1) out vec2      v_texcoord;
#endif

#ifdef ERHE_USE_VERTEX_VARYING_COLOR
layout(location = 2) out vec4      v_color;
#endif

#ifdef ERHE_USE_VERTEX_VARYING_ANISO_CONTROL
layout(location = 3) out vec2      v_aniso_control;
#endif

#ifdef ERHE_USE_VERTEX_VARYING_TANGENT
layout(location = 4) out vec3      v_T;
#endif

#ifdef ERHE_USE_VERTEX_VARYING_BITANGENT
layout(location = 5) out vec3      v_B;
#endif

#ifdef ERHE_USE_VERTEX_VARYING_NORMAL
layout(location = 6) out vec3      v_N;
#endif

layout(location = 7) flat out uint v_material_index;

// Debug-visualization varyings, only emitted when the viewport has
// selected a Shader_debug != none. Each is further gated on
// attribute presence so meshes lacking the underlying attribute
// still link; the fragment shader treats absent attributes as
// neutral defaults.
#if ERHE_SHADER_DEBUG != 0
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
        world_from_node =
            a_joint_weights_0.x * joint.joints[int(a_joint_indices_0.x) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind +
            a_joint_weights_0.y * joint.joints[int(a_joint_indices_0.y) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind +
            a_joint_weights_0.z * joint.joints[int(a_joint_indices_0.z) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind +
            a_joint_weights_0.w * joint.joints[int(a_joint_indices_0.w) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind;
        world_from_node_normal =
            a_joint_weights_0.x * joint.joints[int(a_joint_indices_0.x) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind_normal +
            a_joint_weights_0.y * joint.joints[int(a_joint_indices_0.y) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind_normal +
            a_joint_weights_0.z * joint.joints[int(a_joint_indices_0.z) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind_normal +
            a_joint_weights_0.w * joint.joints[int(a_joint_indices_0.w) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind_normal;
    }
#else
    world_from_node        = primitive.primitives[ERHE_DRAW_ID].world_from_node;
    world_from_node_normal = primitive.primitives[ERHE_DRAW_ID].world_from_node_normal;
#endif

    mat4 clip_from_world = camera.cameras[c_view_index].clip_from_world;

#ifdef ERHE_USE_VERTEX_VARYING_NORMAL
    vec3 normal          = normalize(vec3(world_from_node_normal * vec4(a_normal, 0.0)));
#endif

#ifdef ERHE_USE_VERTEX_VARYING_TANGENT
    vec3 tangent         = vec3(world_from_node * vec4(a_tangent.xyz, 0.0));
#endif

#ifdef ERHE_USE_VERTEX_VARYING_BITANGENT
    vec3 bitangent       = cross(normal, tangent) * a_tangent.w;
#endif
    vec4 position        = world_from_node * vec4(a_position, 1.0);

#if ERHE_SHADER_DEBUG != 0 && defined(ERHE_USE_SKINNING)
    if (primitive.primitives[ERHE_DRAW_ID].skinning_factor < 0.5) {
        v_bone_color = vec4(0.3, 0.0, 0.3, 1.0);
    } else {
        v_bone_color =
            a_joint_weights_0.x * joint.debug_joint_colors[(int(a_joint_indices_0.x) + primitive.primitives[ERHE_DRAW_ID].base_joint_index) % joint.debug_joint_color_count] +
            a_joint_weights_0.y * joint.debug_joint_colors[(int(a_joint_indices_0.y) + primitive.primitives[ERHE_DRAW_ID].base_joint_index) % joint.debug_joint_color_count] +
            a_joint_weights_0.z * joint.debug_joint_colors[(int(a_joint_indices_0.z) + primitive.primitives[ERHE_DRAW_ID].base_joint_index) % joint.debug_joint_color_count] +
            a_joint_weights_0.w * joint.debug_joint_colors[(int(a_joint_indices_0.w) + primitive.primitives[ERHE_DRAW_ID].base_joint_index) % joint.debug_joint_color_count];
    }
#endif

#ifdef ERHE_USE_VERTEX_VARYING_TANGENT
    v_T              = normalize(tangent  );
#endif

#ifdef ERHE_USE_VERTEX_VARYING_BITANGENT
    v_B              = normalize(bitangent);
#endif

#ifdef ERHE_USE_VERTEX_VARYING_NORMAL
    v_N              = normal;
#endif
    v_position       = position;

    gl_Position      = clip_from_world * position;
    v_material_index = primitive.primitives[ERHE_DRAW_ID].material_index;

#ifdef ERHE_USE_VERTEX_VARYING_TEXCOORD0
    v_texcoord       = a_texcoord_0;
#endif

#ifdef ERHE_USE_VERTEX_VARYING_COLOR
    v_color          = a_color_0;
#endif

#ifdef ERHE_USE_VERTEX_VARYING_ANISO_CONTROL
    v_aniso_control  = a_custom_1;
#endif

#if ERHE_SHADER_DEBUG != 0
#  ifdef ERHE_ATTRIBUTE_a_tangent
    v_tangent_scale       = a_tangent.w;
#  endif
    v_line_width          = primitive.primitives[ERHE_DRAW_ID].size;
#  ifdef ERHE_ATTRIBUTE_a_custom_2
    v_valency_edge_count  = a_valency_edge_count;
#  endif
#endif
}
