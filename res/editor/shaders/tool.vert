#include "erhe_camera_view.glsl"
#include "erhe_vertex_position.glsl"

layout(location = 0) out vec3      v_position;
layout(location = 1) out vec3      v_normal;
layout(location = 2) flat out uint v_material_index;

void main()
{
    mat4 world_from_node        = primitive.primitives[ERHE_PRIMITIVE_INDEX].world_from_node;
    mat4 world_from_node_normal = primitive.primitives[ERHE_PRIMITIVE_INDEX].world_from_node_normal;
    mat4 clip_from_world        = camera.cameras[c_view_index].clip_from_world;
    vec3 node_position          = erhe_decode_vertex_position(
        primitive.primitives[ERHE_PRIMITIVE_INDEX].position_scale.xyz,
        primitive.primitives[ERHE_PRIMITIVE_INDEX].position_offset.xyz
    );
    vec4 position               = world_from_node * vec4(node_position, 1.0);

    v_position       = position.xyz;
    v_normal         = normalize(vec3(world_from_node_normal * vec4(a_normal, 0.0)));
    gl_Position      = clip_from_world * position;
    v_material_index = primitive.primitives[ERHE_PRIMITIVE_INDEX].material_index;
}
