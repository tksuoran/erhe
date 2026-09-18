# Draw list renderer: outstanding work

Status: proposed

Extends `doc/erhe/draw_list_renderer.md`, and with it
`doc/erhe/draw_list_material_set.md` and
`doc/erhe/draw_list_performance_improvements.md`, which describe the renderer, its
material state and its cached primitive records as they are. This document
holds what those three leave open, in rough priority order.

## 1. Frustum culling on the entry AABB

Q6 in `doc/erhe/draw_list_renderer.md` deferred culling; `Draw_list_entry` already
carries a world-space AABB (R15) so no data-model change is needed. The
blocker is that the AABB is written at registration and goes stale for dynamic
objects. Either recompute it per draw from the node, or maintain it from the
transform hook - which must respect the threading contract (section 9.3): the
hook enqueues, `flush_pending()` applies.

The shadow frustum fit is the second consumer: `Shadow_renderer` still walks
every content mesh per shadow render to gather AABBs, and could read the
entries' once this update path exists.

## 2. Re-list on a negative-determinant flip

R10b reports and asserts instead of re-listing. The mechanism is the same as
R12's re-register: route the `negative_determinant` flag change that
`Mesh::handle_node_transform_update` produces to a reregister when the observed
value differs from the registered one. Interactive mirroring by negative scale
and reparenting under a mirrored parent both trip the assert today.

## 3. A static mobility source, and static uploads

R10a carries the mobility flag in `Draw_list_object_create_info` but every
non-skinned mesh registers as dynamic, because `Scene_root::register_mesh` has
no mobility information and there is no static item flag. Add the source (item
flag or asset metadata), then let a static list upload its
`primitive_records` block once instead of per frame (G4 / R9); the contiguous
GPU-layout records described in `doc/erhe/draw_list_performance_improvements.md` are
the precondition that already exists.

## 4. Translucent depth sorting

Neither the draw-list path nor the fallback depth-sorts translucent
primitives, which is why C1 makes translucent intra-class order
implementation-defined. Sorting the translucent entries would make the order
defined and the two paths comparable.

## 5. Retire `bucket_primitives` for the covered passes

Once the draw-list setting has been the default for a while, the covered
passes no longer need the bucket path; keep `bucket_primitives` for the passes
that remain on the fallback (section 3, "Not covered").

## 6. Extend the shadow prewarm to all three sub-variants

R4a resolves the distance and cube sub-variants on their first `draw()`. The
prewarm warms depth-only only, so the first shadow render with the distance
technique or a point light compiles on the spot.

## 7. Material-set follow-ups

From `doc/erhe/draw_list_material_set.md`:

- **Distinct types for the two sets.** `Scene_root::get_material_set()` and
  `Draw_list_scene::get_material_set()` return interchangeable types, so
  passing the forward set where the draw-list set belongs compiles. The
  `ERHE_VERIFY` in `Draw_list_renderer::render` (D5) catches that direction;
  the bucket direction has no guard and would render wrong materials silently.
  Wrapper types would make the mix-up a compile error.
- **Stop `Shadow_renderer` duplicating the six per-pass buffers** that
  `Scene_pass_resources` already owns, and **stop `Id_renderer` borrowing a
  joint buffer** through `Forward_renderer::get_joint_buffer()`. Both were
  unblocked by the `Scene_pass_resources` extraction.
- **Verification still owed** (section 4 of that document): V5's A/B
  screenshots of a textured scene, and an explicit run of the OpenGL
  sampler-array (non-bindless) heap path, the weakest heap path and the one
  most changed by a persistent heap.
- **V6 on the null backend.** The `Multi_copy_buffer` tests are built only
  where `_gpu_tests_supported` holds, which excludes the null device even
  though it answers the same `is_frame_completed()` contract.

## 8. Per-list draw overhead

What remains in the draw-list cost is per-list rather than per-primitive:
pipeline lookup, the formatted debug label, one ring-buffer acquire per chunk
and one indirect command write per entry (section 10 of
`doc/erhe/draw_list_renderer.md`). Caching the debug label per entry count is the
cheapest of these.
