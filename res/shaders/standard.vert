#include "erhe_camera_view.glsl"

layout(location = 0) out vec4      v_position;

#ifdef ERHE_ATTRIBUTE_a_texcoord_0
layout(location = 1) out vec2      v_texcoord;
#endif

#ifdef ERHE_ATTRIBUTE_a_color_0
layout(location = 2) out vec4      v_color;
#endif

#ifdef ERHE_ATTRIBUTE_a_custom_1
layout(location = 3) out vec2      v_aniso_control;
#endif

#if defined(ERHE_ATTRIBUTE_a_normal) && defined(ERHE_ATTRIBUTE_a_tangent)
layout(location = 4) out vec3      v_T;
layout(location = 5) out vec3      v_B;
#endif

#ifdef ERHE_ATTRIBUTE_a_normal
layout(location = 6) out vec3      v_N;
#endif

layout(location = 7) flat out uint v_material_index;

void main()
{
    mat4 world_from_node;
    mat4 world_from_node_normal;

#if defined(ERHE_ATTRIBUTE_a_joint_weights_0) && defined(ERHE_ATTRIBUTE_a_joint_indices_0)
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

#ifdef ERHE_ATTRIBUTE_a_normal
    vec3 normal          = normalize(vec3(world_from_node_normal * vec4(a_normal, 0.0)));
#endif

#if defined(ERHE_ATTRIBUTE_a_tangent)
    vec3 tangent         = vec3(world_from_node * vec4(a_tangent.xyz, 0.0));
#endif

#if defined(ERHE_ATTRIBUTE_a_normal) && defined(ERHE_ATTRIBUTE_a_tangent)
    vec3 bitangent       = cross(normal, tangent) * a_tangent.w;
#endif
    vec4 position        = world_from_node * vec4(a_position, 1.0);

#if defined(ERHE_ATTRIBUTE_a_tangent)
    v_T              = normalize(tangent  );
#endif

#if defined(ERHE_ATTRIBUTE_a_normal) && defined(ERHE_ATTRIBUTE_a_tangent)
    v_B              = normalize(bitangent);
#endif

#ifdef ERHE_ATTRIBUTE_a_normal
    v_N              = normal;
#endif
    v_position       = position;

    gl_Position      = clip_from_world * position;
    v_material_index = primitive.primitives[ERHE_DRAW_ID].material_index;

#ifdef ERHE_ATTRIBUTE_a_texcoord_0
    v_texcoord       = a_texcoord_0;
#endif

#ifdef ERHE_ATTRIBUTE_a_color_0
    v_color          = a_color_0;
#endif

#ifdef ERHE_ATTRIBUTE_a_custom_1
    v_aniso_control  = a_custom_1; //aniso_control;
#endif
}
