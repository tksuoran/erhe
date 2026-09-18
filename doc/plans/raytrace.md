# Ray tracing follow-ups

Status: proposed

Extends [../raytrace.md](../raytrace.md) (the GPU ray query path),
[../raytrace_materials.md](../raytrace_materials.md) (its material shading) and
[../bvh_scene_acceleration.md](../bvh_scene_acceleration.md) (the CPU bvh
backend's TLAS).

## Give the BLAS cache an identity beyond the pointer

`Scene_tlas::m_blas_cache` (and `Lightmap_baker::m_blas_cache`) is keyed by a
raw `Buffer_mesh*`, and `Primitive_render_shape::commit_geometry_buffer_mesh()`
move-assigns a new `Buffer_mesh` in place, so the key survives while the pool
ranges the structure was built over are freed and recycled. The trap is stated
in the current document. Make the key carry identity - a generation counter
bumped by `commit_geometry_buffer_mesh()`, or the buffer ranges themselves - and
drop entries whose generation no longer matches.

## Metal backend

Implement `Acceleration_structure` and the intersector on Metal
(`MTLPrimitiveAccelerationStructureDescriptor`,
`MTLInstanceAccelerationStructureDescriptor`, MSL
`raytracing::intersector`), and use `gpuAddress()` where the Vulkan path uses
buffer device addresses.

## Skinned meshes

Skinned meshes have no BLAS, because it would need post-skinning positions.
Build one over a post-skinning position buffer so they are traceable.

## TLAS refit

Use UPDATE mode instead of a full rebuild where the instance set is unchanged.

## Volume attenuation and transmission textures

KHR_materials_volume (thickness, attenuation color and distance) needs
Beer-Lambert attenuation over the traveled in-medium distance, which the bounce
loop already provides as `t` between the entry and exit hits. It costs an
import, UI and GPU-struct triple for a second-order visual effect; the GPU
struct ordering leaves room to append the fields. `transmissionTexture` (the
factor is supported) belongs with it.

## Compose the ray traced image into the viewport

A `Texture_rendergraph_node` in place of the fixed 960x540 developer-window
target. This needs viewport-sized (resizing) output textures, sRGB and tone
mapping consistency with post processing, and a policy for mixing raster and ray
traced output. Accumulation and denoising belong to the same step.

## Measure and tune the bvh scene TLAS

The hover-path win has not been measured on a large scene (Bistro) in the
profiler, and `k_static_delay_ticks` / `k_rebuild_cooldown_ticks` are set from
reasoning rather than from numbers. Capture with point hover over a large scene
and tune both from the capture.
