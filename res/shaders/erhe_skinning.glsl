#ifndef ERHE_SKINNING_GLSL
#define ERHE_SKINNING_GLSL

// Linear-blend skinning matrix sum.
//
// Computes world_from_bind and world_from_bind_normal as a 4-influence
// weighted sum of joint matrices, reading from the `joint` interface
// block (binding point 7). The caller is responsible for declaring the
// `joint` block (matching the layout produced by
// erhe::scene_renderer::Joint_interface) and for deciding when to call
// this function -- typically gated on ERHE_USE_SKINNING plus a runtime
// skinning_factor toggle from the per-primitive struct.
//
// The same function is reused from vertex shaders (joint indices /
// weights read as vertex attributes) and from compute shaders (joint
// indices / weights read from an SSBO). It contains no global reads
// other than the `joint` block.

void erhe_skin_matrices(
    uint  base_joint_index,
    uvec4 joint_indices,
    vec4  joint_weights,
    out mat4 world_from_bind,
    out mat4 world_from_bind_normal
)
{
    int i0 = int(joint_indices.x) + int(base_joint_index);
    int i1 = int(joint_indices.y) + int(base_joint_index);
    int i2 = int(joint_indices.z) + int(base_joint_index);
    int i3 = int(joint_indices.w) + int(base_joint_index);

    world_from_bind =
        joint_weights.x * joint.joints[i0].world_from_bind +
        joint_weights.y * joint.joints[i1].world_from_bind +
        joint_weights.z * joint.joints[i2].world_from_bind +
        joint_weights.w * joint.joints[i3].world_from_bind;

    world_from_bind_normal =
        joint_weights.x * joint.joints[i0].world_from_bind_normal +
        joint_weights.y * joint.joints[i1].world_from_bind_normal +
        joint_weights.z * joint.joints[i2].world_from_bind_normal +
        joint_weights.w * joint.joints[i3].world_from_bind_normal;
}

#endif // ERHE_SKINNING_GLSL
