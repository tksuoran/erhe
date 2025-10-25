out vec3 v_pos_world;
out vec3 v_color;

vec3 remap(vec3 x, vec3 to_low, vec3 to_high)
{
    return x * (to_high - to_low) + to_low;
}

void main()
{
    const uint indices[36] = uint[36](
        1u, 7u, 3u,  7u, 1u, 5u,
        0u, 6u, 4u,  6u, 0u, 2u,
        2u, 7u, 6u,  7u, 2u, 3u,
        0u, 5u, 1u,  5u, 0u, 4u,
        4u, 7u, 5u,  7u, 4u, 6u,
        0u, 3u, 2u,  3u, 0u, 1u
    );
    uint instance_id  = gl_VertexID / 36;
    uint cube_corner  = indices[gl_VertexID % 36];
    uint packed_xyz   = instance.instances[instance_id].packed_position;
    uint x            =  packed_xyz        & 0x7ffu;
    uint y            = (packed_xyz >> 11) & 0x7ffu;
    uint z            = (packed_xyz >> 22) & 0x3ffu;
    vec3 instance_pos = vec3(float(x), float(y), float(z));

    vec3 instance_size = cube_control.cube_control[0].cube_size.xyz;
    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    ); 
    vec3 local_camera_pos = view_position_in_world - instance_pos;
    uvec3 xyz = uvec3(cube_corner & 0x1, (cube_corner & 0x4) >> 2, (cube_corner & 0x2) >> 1);

    // if (local_camera_pos.x > 0) xyz.x = 1 - xyz.x;
    // if (local_camera_pos.y > 0) xyz.y = 1 - xyz.y;
    // if (local_camera_pos.z > 0) xyz.z = 1 - xyz.z;

    vec3 uvw             = vec3(xyz);
    vec3 pos_node        = instance_pos + instance_size * (uvw - 0.5);

    mat4 world_from_node = primitive.primitives[gl_DrawID].world_from_node;
    vec3 pos_world       = vec3(world_from_node * vec4(pos_node, 1.0));

    mat4 clip_from_world = camera.cameras[0].clip_from_world;

    v_pos_world = pos_world;
    vec3 color  = cube_control.cube_control[0].color_scale.xyz * instance_pos + cube_control.cube_control[0].color_bias.xyz;
    v_color     = remap(color, cube_control.cube_control[0].color_start.xyz, cube_control.cube_control[0].color_end.xyz);
    gl_Position = clip_from_world * vec4(pos_world, 1.0);
}

