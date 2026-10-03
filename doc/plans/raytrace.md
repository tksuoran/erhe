# Ray tracing follow-ups

Status: proposed

Extends [../raytrace.md](../editor/raytrace.md) (the GPU ray query path),
[../raytrace_materials.md](../editor/raytrace_materials.md) (its material shading) and
[../bvh_scene_acceleration.md](../erhe/bvh_scene_acceleration.md) (the CPU bvh
backend's TLAS).

## Give the BLAS cache an identity beyond the pointer

`Scene_tlas::m_blas_cache` (and `Lightmap_baker::m_blas_cache`) is keyed by a
raw `Buffer_mesh*`, and `Primitive_render_shape::commit_geometry_buffer_mesh()`
move-assigns a new `Buffer_mesh` in place, so the key survives while the pool
ranges the structure was built over are freed and recycled. The trap is stated
in the current document. Make the key carry identity - a generation counter
bumped by `commit_geometry_buffer_mesh()`, or the buffer ranges themselves - and
drop entries whose generation no longer matches.

## Skinned meshes

`Scene_tlas::update()` skips every mesh with a skin, because its BLAS would need
post-skinning positions. Build one over a post-skinning position buffer so they
are traceable.

## TLAS refit

Every top level build runs in full build mode (Vulkan
`VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR`, Metal
`buildAccelerationStructure`). Use update / refit mode
(`VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR` on a structure built with
`VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR`, Metal
`refitAccelerationStructure`) where the instance set is
unchanged and only transforms moved.

## Steady-state allocations in the Metal TLAS build

`Acceleration_structure_impl::build()` for a Metal top level structure creates a
local `std::unordered_map` to deduplicate bottom level structures and a new
`NS::Array` for `setInstancedAccelerationStructures` on every build, which runs
every frame. Keep the deduplication map as a cleared-and-reused member and
replace the array only when the referenced set changes, so steady-state frames
allocate nothing.

## Volume attenuation and transmission textures

KHR_materials_volume (thickness, attenuation color and distance) needs
Beer-Lambert attenuation over the traveled in-medium distance, which the bounce
loop already provides as `t` between the entry and exit hits. It costs an
import, UI and GPU-struct triple for a second-order visual effect; the GPU
struct ordering leaves room to append the fields. `transmissionTexture` (the
factor is supported) belongs with it.

## Compose the ray traced image into the viewport

The ray traced output texture is viewport sized (divided by the configured
downscale) and is shown only in the developer Ray Trace window. Composite it
into the viewport through a `Texture_rendergraph_node`, with sRGB and tone
mapping consistent with post processing and a policy for mixing raster and ray
traced output. Accumulation and denoising belong to the same step.

## Measure and tune the bvh scene TLAS

The hover-path win has not been measured on a large scene (Bistro) in the
profiler, and `k_static_delay_ticks` / `k_rebuild_cooldown_ticks` are set from
reasoning rather than from numbers. Capture with point hover over a large scene
and tune both from the capture.
