// The box the raster occlusion test rasterizes for one draw-list entry
// (doc/plans/occlusion_culling.md, doc/erhe/draw_list_renderer.md Q6
// "Occlusion test"): the entry's world AABB from the classifier's GPU bounds
// (cull_bounds.bounds[2 * entry + 0] = center.xyz, 0;
// cull_bounds.bounds[2 * entry + 1] = extent.xyz, purpose; extent.x < 0: no
// bounds), grown by a small fraction of its coordinate magnitude.
//
// The growth keeps a box face that coincides with its mesh's surface (an
// axis-aligned wall, a floor plane) in front of the depth the mesh wrote: the
// color pass and the occlusion pass reach the same depth through different
// float operations, and a box face computed one ulp behind the stored depth
// would hide the entry behind itself. Growing the box only adds visible
// fragments, so the test stays conservative. cull_classify.comp tests the
// grown box against the near plane, the box the vertex shader draws.

const float c_occlusion_bounds_growth = 1.0 / 1024.0;

vec3 occlusion_grown_extent(vec3 center, vec3 extent)
{
    float magnitude = max(
        max(max(abs(center.x), abs(center.y)), abs(center.z)),
        max(max(extent.x, extent.y), extent.z)
    );
    return extent + vec3(magnitude * c_occlusion_bounds_growth);
}
