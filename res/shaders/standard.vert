#include "erhe_camera_view.glsl"
#include "erhe_standard_variant.glsl"

layout(location = 0) out vec4      v_position;

#ifdef ERHE_USE_VERTEX_VARYING_TEXCOORD0
layout(location = 1) out vec2      v_texcoord;
#endif

#ifdef ERHE_USE_VERTEX_VARYING_COLOR
layout(location = 2) out vec4      v_color;
#endif

// v_aniso_control intentionally bypasses the variant-axis machinery: the
// anisotropic material extension (custom_1 = aniso strength + tangent
// rotation) is out of scope for the standard variant cache per
// doc/shader_variants.md, so the varying is gated on raw attribute
// presence the same way it always was.
#ifdef ERHE_ATTRIBUTE_a_custom_1
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

#ifdef ERHE_ATTRIBUTE_a_custom_1
    v_aniso_control  = a_custom_1; //aniso_control;
#endif
}
