# Draw list renderer — implementation plan

Companion to `doc/draw_list_renderer_requirements.md` (the "req doc"; requirement
IDs below refer to it). Status: DONE 2026-08-15 — all six phases landed
(results: `doc/draw_list_renderer_results.md`). Written 2026-08-15 after the
req doc passed independent review; revised after plan review pass 1 (threading
contract, material-content trigger, P3a chunking layer, Phase 3 verification)
and pass 2 (flush site, no transform hook, R1b null checks) — APPROVED, implementing.

Workflow per phase (project convention): edit → build (VS2026 Vulkan +
GL/OpenGL, null backend for compile coverage) → independent review of the
phase diff → fix severe → commit. Each phase is one or a few logically
separate commits; do not fold phases together.

## 0. Design summary (what the code will look like)

All new code lives in `src/erhe/scene_renderer/erhe_scene_renderer/draw_list*`
plus small hooks in `erhe::scene` (`Scene_host`, `Mesh`) and `editor`
(`Scene_root`, `Forward_renderer`/`Shadow_renderer` call sites, prewarm).

### 0.1 Identity and keys

```
Draw_list_key                                  // hashable; one Draw_list per distinct key
  purpose             : enum { color, shadow }
  mobility            : enum { static, dynamic, skinned }        // R10a
  layer_id            : erhe::scene::Layer_id                    // R13/Q3 (Mesh::layer_id)
  blending            : enum { opaque, translucent }             // R7; null material -> translucent (R1)
  negative_determinant: bool                                     // R10b, sampled from world_from_node()
  buffer_set          : Buffer_set (vertex_input_key, index buffer id, vertex buffer ids)
  primitive_mode      : Primitive_mode (polygon_fill only in v1)
  primitive_key       : Shader_key   // primitive-derived ONLY:
                                     //   color : Shader_key{}.derive(material, vertex_format, skinned) minus env
                                     //   shadow: {USE_SKINNING} only (R4)
  primitive_key_hash  : uint64
```

Environment is NOT in the key (R21). Resolution config (R17/R18/R19/R4a):

```
Color_env_config   { Light_layer_partition partition; shadow_filter/bias/technique/depth_bits; }
View_config        { uint16 multiview_count; }             // 0 = single view
Shadow_sub_variant { depth_only, depth_only_distance, cube } // R4a
```

### 0.2 Classes

```
class Draw_list_entry                          // R15/R16, fixed-size POD, ~64 B
  uint32_t object_index;                       // -> Draw_list_scene::m_objects (cold data)
  uint16_t mesh_primitive_index;               // index into Mesh::get_primitives()
  uint16_t pad;
  uint64_t flag_bits;                          // mirrored Item_flags (R12a)
  uint32_t index_count, first_index, base_vertex; // from Buffer_mesh index_range(primitive_mode)
  erhe::math::Aabb world_aabb;                 // Q6, unused by v1 draw
  // per-frame upload reads Mesh/Node/Mesh_primitive live via object_index (R12b, R8a)

class Draw_list                                // R13/R14
  Draw_list_key                     key;
  std::vector<Draw_list_entry>      entries;   // contiguous; swap-remove on unregister (R2)
  // resolved stages cache (R17):
  //   color : map<(View_config), const Reloadable_shader_stages*>   (env config lives on scene, invalidates all)
  //   shadow: array<Shadow_sub_variant, const Reloadable_shader_stages*>
  std::vector<Entry_ref> ...                   // n/a — reverse index lives on the object

class Draw_list_object                         // R10/R11
  Draw_list_object_create_info      info;      // kept verbatim for rebuild (R1a): mesh shared_ptr, mobility, layer id
  std::vector<Entry_location>       locations; // (draw_list index, entry index) per entry, for O(entries) unregister/flag/rebuild
  uint64_t                          flag_bits; // last mirrored value

class Draw_list_scene                          // R0..R9
  ctor(Mesh_memory&, Shader_variant_cache&, std::span<const uint32_t> multiview_view_counts)   // R1b, R19
  register_object(const Draw_list_object_create_info&) -> Draw_list_object_id
  unregister_object(Draw_list_object_id)
  set_object_flags(Draw_list_object_id, uint64_t item_flag_bits)                                 // R12a
  reregister_object(Draw_list_object_id)  // = unregister+register from stored info; used by R12 hooks
  rebuild_all()                                                                                   // R1a
  set_color_environment(const Color_env_config&)   // R18; called by draw_color after recompute-and-compare
  draw_color (const Draw_color_parameters&)        // R6/R6a/R7/R7a/R8/R8a
  draw_shadow(const Draw_shadow_parameters&)
```

Draw parameter structs (R8a — buffers are the caller's):

```
Draw_color_parameters
  Render_command_encoder&; const Render_pass*; Base_render_pipeline&;
  Primitive_buffer&; Draw_indirect_buffer&; Primitive_interface_settings;
  Item_filter filter; std::span<const erhe::scene::Layer_id> layers;
  Blending_selection { opaque, translucent, both };
  View_config view; Color_env_config env;               // env recomputed by caller each pass, cheap
  const Color_blend_state* color_blend_override;        // nullptr = pick by blending class as today
Draw_shadow_parameters
  Render_command_encoder&; const Render_pass*; Base_render_pipeline&; const Color_blend_state*;
  Primitive_buffer&; Draw_indirect_buffer&;
  Item_filter filter; std::span<const erhe::scene::Layer_id> layers; Shadow_sub_variant sub_variant;
```

Draw body per list (R8): skip lists not matching layers/blending; fetch cached
stages (lazy resolve only per R19/R4a); `get_pipeline_for(pass descriptor,
color_blend, stages, vertex_input, vertex_format, negative_determinant)`; bind
index/vertex buffers; `Primitive_buffer::update(entries, filter, ...)` and
`Draw_indirect_buffer::update(entries, filter, ...)` — the new entry-based
overloads apply the flag filter while writing, so both produce the same N
commands; `multi_draw_indexed_primitives_indirect(N)`; release ranges.
Ordering (R7): iterate opaque lists first, then translucent, when
`Blending_selection::both`. Chunking (P3a): entries per list are drawn in
chunks of ≤ `Primitive_interface::max_primitive_count`, each chunk with its
own primitive range + indirect range + multi-draw (a shared helper
`draw_chunk(entries[begin,end))`).

Combine rule for color resolution (verified against `Shader_key::derive`):
`resolved_key.bool_mask = primitive_key.bool_mask | env.bool_mask`; int axes
merged per axis (env sets the light-count / shadow / multiview axes,
primitive key sets its own; no axis is set by both when SHADER_DEBUG == 0);
`blending_mode` from the primitive key. Debug-assert in Phase 3 that this
equals today's bucket key for every registered primitive.

### 0.3 Scene-side hooks (R0/R0a/R12/R12a)

`erhe::scene::Scene_host` gains pure virtuals (implemented by `Scene_root`,
no-op when `m_draw_list_scene == nullptr`):

```
virtual void on_mesh_primitives_changed(const std::shared_ptr<Mesh>&) = 0;  // from Mesh::update_rt_primitives + clear_primitives
virtual void on_mesh_flags_changed     (Mesh&, uint64_t old, uint64_t new) = 0; // from Mesh::handle_flag_bits_update (guard lifted)
virtual void on_mesh_material_changed  (const std::shared_ptr<Mesh>&) = 0;  // from new Mesh::set_primitive_material()
// NO transform hook in v1: handle_node_transform_update runs on workers and per moving mesh per
// frame; R10a has no static source in v1, and R10b's flip is observed through the flags hook
// (handle_node_transform_update flips Item_flags::negative_determinant, which fires
// handle_flag_bits_update only on change).
```

`Scene_root::register_mesh` / `unregister_mesh` call
`m_draw_list_scene->register_object / unregister_object`, storing the id on
the `Mesh` side (a `Draw_list_object_id` member on `erhe::scene::Mesh`, or a
`unordered_map<Mesh*, id>` in `Scene_root` — choose the map: keeps
`erhe::scene` free of renderer types).

**Threading contract (mandatory).** `Mesh::update_rt_primitives()` and
`Mesh::handle_node_transform_update()` — and thus the R12 and flags hooks —
run on `tf::Executor` worker threads (`Async_raytrace_kickoff_operation`
deferred finalize, under `item_host_mutex`), while the render thread never
takes that mutex and `Shader_variant_cache::get` is an unlocked
`unordered_map` + compile. Therefore **no `Scene_host` hook reads or mutates
draw-list state or resolves variants directly.** Every hook only enqueues
`(Mesh*, kind{register, unregister, reregister, flags(new_bits)})` into a
mutex-protected pending queue on `Draw_list_scene`;
`Draw_list_scene::flush_pending()` runs on the main thread once per frame
and performs unregister/register/resolve there. Rules:
- Flush site: `Editor` tick, after transform propagation
  (`update_transforms`, so registration samples current world transforms,
  R10b) and BEFORE the rendergraph; one flush per registered scene root per
  frame covers viewports, shadow nodes and the headset node. Thumbnails
  (material / brush previews) render earlier in the tick but their roots
  have no `Draw_list_scene` (see below). `App_rendering` has no per-tick
  entry — the call lives in `Editor::tick`.
- `flush_pending()` takes the scene root's `item_host_mutex` (lock order:
  item_host_mutex → pending mutex; swap the queue out under the pending
  mutex, then process under item_host_mutex only) so registration never
  reads a `Buffer_mesh` mid move-assign.
- Ordering edge cases: `flags` for a not-yet-registered mesh is ignored;
  unregister+register of the same mesh in one queue is normal (mesh ops
  detach/re-attach); teardown drops the queue without processing
  (`~Scene_root` may run on a worker holding the last `shared_ptr`).
- R10b determinant assert lives in `flush_pending()` when processing a
  `flags` item: compare the incoming `negative_determinant` bit with the
  object's registered value → debug assert / release log. No transform hook.
- Flag updates coalesce (last value wins). `register_object` /
  `unregister_object` / `flush_pending` are main-thread-only (assert
  thread id in debug).
This still satisfies R17/R20/R22: resolution is off the per-frame draw hot
path and never inside `draw()`.

Determinant at registration: `glm::determinant(node->world_from_node()) < 0`
(R10b). R10a (static mobility) has no source in v1 — everything non-skinned
is dynamic — so it has no assert path yet; R10b's assert is in
`flush_pending()` (above).

## 1. Phases

### Phase 1 — Core data model + registration (no drawing yet)

Files: `draw_list_key.hpp` (new), `draw_list_entry.{hpp,cpp}`,
`draw_list.{hpp,cpp}`, `draw_list_object.{hpp,cpp}`, `draw_list_scene.{hpp,cpp}`,
`CMakeLists.txt`.

- Implement `Draw_list_key` + hash; `Draw_list_scene` map `unordered_map<Draw_list_key, index>` (P2 — no linear scan).
- Classification function `classify_primitive(mesh, i, mode, purpose)` mirroring `bucket_primitives()` skips + null-material blending (R1). Reuse `bucket_vertex_input_key` / `bucket_vertex_ranges` — currently in an anonymous namespace in `mesh_memory.cpp`; expose them in `mesh_memory.hpp` first.
- Layer id type is `erhe::scene::Layer_id` (`Mesh::layer_id`), not the editor's `Mesh_layer_id` constants class.
- Unregister uses swap-remove; the moved entry's owning object `locations` record must be patched — entries carry `object_index` so the fix-up is a direct lookup + O(locations of that object) search, or store a back-index in the entry.
- Object storage uses a free-list with STABLE indices (`Draw_list_object_id` = index + generation), so entries' `object_index` never needs patching.
- Pending queue + `flush_pending()` (threading contract, §0.3).
- Color primitive key: `Shader_key{}.derive(material, vertex_format, skinned)`; shadow key: `Shader_key{}` with `USE_SKINNING` set iff `mesh->skin` and vertex format has joints (mirror `derive` logic exactly — factor a helper `shader_key_use_skinning(vertex_format, skinned)` out of `Shader_key::derive` if not already exposed).
- register / unregister / reregister / set_object_flags / rebuild_all.
- Unit test (`src/erhe/scene_renderer` test target if present; else a small headless test under `src/tests` or the null-backend test app): register N meshes → expected list count; unregister → entries removed; flag set → entry bits updated; rebuild_all → identical lists.
- Debug: `Draw_list_scene::describe()` (per list: key.describe(), entry count) for logging.

Commit: `scene_renderer: Draw_list_scene data model + registration`.

### Phase 2 — Scene_host hooks + Scene_root ownership

Files: `src/erhe/scene/erhe_scene/scene_host.hpp`, `mesh.{hpp,cpp}`,
`src/editor/scene/scene_root.{hpp,cpp}`, `Scene_root` construction sites
(8 `make_shared<Scene_root>` in editor — inject `Mesh_memory&` /
`Shader_variant_cache&` via existing `App_context` or a small
`Draw_list_scene_dependencies` struct; scene roots created without them get
no `Draw_list_scene`, R1b).

- Add the three/four `Scene_host` virtuals; implement in `Scene_root`; also in any other `Scene_host` implementer (grep `: public erhe::scene::Scene_host` — e.g. test/example hosts) as no-ops.
- `Mesh::update_rt_primitives()` and `Mesh::clear_primitives()` → `on_mesh_primitives_changed` (R12).
- `Mesh::handle_flag_bits_update` → call `on_mesh_flags_changed` BEFORE the visible-only early return (R12a).
- New `Mesh::set_primitive_material(index, material)`; convert every `get_mutable_primitives()` writer of `.material` (grep list in req doc R12) → `on_mesh_material_changed` → reregister.
- No transform hook (§0.3). R10b assert in `flush_pending()` on `flags` items.
- All hooks enqueue only (§0.3 threading contract); `Editor::tick` calls `flush_pending()` for every scene root after `Operation_stack` update and before `Thumbnails::update()`.
- Decide per construction site which scene roots get deps (R1b): editor scene roots (default scene, scene open / create, `open_scene_gltf`) → yes, from `App_context` (filled before any of them runs). Material / brush preview roots (`Scene_preview`) and the tools root → NO `Draw_list_scene`: they are constructed inside parallel init tasks on tf worker threads (a `Draw_list_scene` binds its owner thread) and are tiny scenes; their passes stay on the fallback (routing rule checks for a null `Draw_list_scene`). `item_tree_window`'s `#if 0` site → nullptr.
- R12 material *content* edits (bind texture, BXDF model, blending mode change list identity): decision — **per-frame material identity hash check** in `flush_pending()`: for each registered material compute a small hash of the identity-affecting fields (the same inputs `Shader_key::derive` reads: texture-use bits, BXDF model, texcoord selectors, unlit, blending mode) and compare with the value stored at registration; on mismatch `reregister` every object using that material (objects keep a material->objects index). Cheap (O(materials) per frame) and needs no new edit-path plumbing; a message-bus material-changed event can replace it later.
- `Scene_root::register_mesh/unregister_mesh` register/unregister; `~Scene_root` / `sever_host` order: destroy `Draw_list_scene` after meshes are unregistered (mirror the rt-scene ordering comment at `scene_root.cpp:299-310`).
- Verify with editor: load bistro + NegativeScaleTest, `describe()` list counts in log; mesh ops (undo/redo, material paint) keep counts consistent; no asserts on load.

Commit(s): `scene: Scene_host draw-list notification hooks`, `editor: Scene_root owns Draw_list_scene`.

### Phase 3 — Entry-based buffer updates + color draw

Files: `primitive_buffer.{hpp,cpp}`, `draw_indirect_buffer.{hpp,cpp}`,
`draw_list_scene.cpp`, `forward_renderer.{hpp,cpp}`.

- `Primitive_buffer::update(const Draw_list& list, const Draw_list_scene& scene, Item_filter, Primitive_mode, settings)` — factor the per-primitive write body out of the `Render_bucket` overload into a shared static helper taking `(Mesh&, mesh_primitive_index, Buffer_mesh&, ...)` so both overloads share one implementation (no drift). Filter applied on `entry.flag_bits`.
- `Draw_indirect_buffer::update(const Draw_list&, ..., Item_filter)` same pattern. Chunking (P3a) is done jointly at the `Draw_list_scene` level: entries in chunks of ≤ `Primitive_interface::max_primitive_count`, one primitive range + one indirect range + one multi-draw per chunk (`m_max_draw_count` on `Draw_indirect_buffer` is unused today; ring ranges grow on demand). Keep the `m_id_offset` power-of-two alignment in the shared write helper even when `use_id_ranges == false`.
- Add `operator==` to `Light_layer_partition` for the env compare.
- `Draw_list_scene::draw_color`: resolution (Phase 3a: eager for view configs from ctor; lazy fallback), env config compare (`Light_layer_partition` + shadow settings ==), invalidate on change.
- `Forward_renderer::render_draw_lists(const Draw_list_render_parameters&)`: same prologue as `render()` (camera/material/joint/light/texture-heap update+bind, viewport/scissor) then `draw_list_scene.draw_color(...)` then the same epilogue. Keep `render()` untouched.
- Prewarm: `prewarm.cpp` — expose the `multiview_view_counts` list (R19); no color key change needed (color list key + env == today's bucket key).
- Verify: `rendering_test` is NOT in the build (commented out in `src/CMakeLists.txt`), so Phase 3 has no standalone visual target. Therefore Phase 3 includes a **minimal gated route** for the two content-fill passes only (`Composition_pass` -> `render_draw_lists` when the gate is on, the pass is `content_fill_*`, and `scene_root->get_draw_list_scene() != nullptr`), gated by a temporary non-codegen `bool App_rendering::use_draw_lists` settable via a small MCP tool (added in this phase), default OFF; replaced by the codegen setting in Phase 5. Image diff opaque == fallback via MCP screenshots at the end of Phase 3.

Commit(s): `scene_renderer: entry-based Primitive_buffer/Draw_indirect_buffer updates`, `scene_renderer: Draw_list_scene color draw + Forward_renderer::render_draw_lists`.

### Phase 4 — Shadow draw

Files: `shadow_renderer.{hpp,cpp}`, `draw_list_scene.cpp`, `prewarm.cpp`.

- `Draw_list_scene::draw_shadow` with `Shadow_sub_variant` → force mask {DEPTH_ONLY | DEPTH_ONLY+SHADOW_DISTANCE | SHADOW_CUBE}; lazy resolve per sub-variant (R4a).
- `Shadow_renderer::render`: when a `Draw_list_scene*` is passed in `Render_parameters`, call `draw_shadow` in place of `draw_shadow_casters` at both call sites (directional/spot at ~448, cube at ~603); AABB gathering for frustum fit unchanged (mesh-level, not draw-level).
- `Shadow_renderer::Render_parameters` gains `Draw_list_scene*` AND the layer id list (Shadow_render_node passes `layers.content()->meshes` today).
- `Shadow_renderer::prewarm_pipelines`: also warm the coarsened key {USE_SKINNING} (R22) — keep warming today's full-material key too until fallback is retired. `Shader_variant_cache` is keyed on `Shader_key` only (vertex format used at first compile); assert that position is location 0 and joints/weights locations agree across mesh formats so the single coarsened variant is valid for all.
- Verify: shadow map image diff vs fallback (hard shadows preset, distance preset, a point light).

Commit: `scene_renderer: Draw_list_scene shadow draw`.

### Phase 5 — Editor integration + toggle + fallback routing

Files: `composition_pass.cpp`, `app_rendering.cpp`, `shadow_render_node.cpp`,
`app_settings` / editor settings (codegen: `config/…` + build twice).

- Editor setting `rendering.use_draw_lists` (default ON once parity verified; OFF until then), runtime-toggleable; exposed in Settings window and via MCP.
- `Composition_pass::render`: route to `render_draw_lists` only when ALL hold: the effective scene root (`data.override_scene_root ? ... : context.scene_view.get_scene_root()`) has a non-null `Draw_list_scene` (R1b); `primitive_mode == polygon_fill`; effective `shader_debug == none`; `shader_stages_override == nullptr`; `color_blend_override == nullptr`; `shader_key_force_enable_mask == 0 && shader_key_force_disable_mask == 0` (explicit, do not rely on other conditions excluding brush/corner-cap/stencil passes); `EDGE_LINES_FROM_ID` not applied this frame (Q9); `blending_mode_policy ∈ {opaque_only, translucent_only, allow_all}` → `Blending_selection` from policy, layers from `data.mesh_layers`. Keep on the routed path: the `is_primitive_mode_enabled` render-style gate, `get_appearance`-derived `primitive_settings`, `ignore_exposure`/exposure, all `Base_render_parameters` fields. Everything else → `render()` fallback (`override_with_base_render_pipeline` passes: brush, bone, selection stencil).
- `Shadow_render_node`: pass `Draw_list_scene*` from the scene root.
- MCP: `describe` draw lists (count, per-list entries) for verification.
- Verify (feedback_verification_driven_changes): baseline screenshots via MCP with toggle OFF, then ON: opaque + shadow pixel-identical (C1), translucent visually equivalent; selection/hover/visibility toggles behave (R7a); Quest build boots (multiview view config path, R19).
- Verify R22: with the toggle on, grep `logs/log.txt` for `Shader_variant_cache miss` in the first frames (existing prewarm acceptance mechanism) — expect none for content in prewarmed scenes.
- Verify C5: open two viewport windows on the same scene with different render styles (one with shader debug on, one polygon-fill-only), plus headset if available; add a debug counter of color re-resolutions to `Draw_list_scene` and check it stays at 0 after frame 1 while both viewports render.

Commit(s): `editor: route composition passes through Draw_list_scene`, `editor: draw-list toggle + MCP inspection`.

### Phase 6 — Measurement + cleanup

- P4: `ERHE_PROFILE` zones around `render_draw_lists` vs `render`; capture bistro (thousands of prims) frame time both ways, desktop and Quest; record numbers in `doc/draw_list_renderer_results.md`.
- Remove any temp logging; update `doc/draw_list_renderer_requirements.md` status to IMPLEMENTED with deviations noted (known deliberate deviation: the graphics-preset part of the env config is re-checked per color draw alongside the light partition instead of via the `graphics_settings` bus event — equivalent, simpler).
- Follow-up list (in the results doc): frustum culling on entry AABB (Q6, first), re-register on determinant flip (R10b), static mobility source + cached static uploads (R9/G4), extend shadow prewarm to distance/cube, translucent depth sort, retire `bucket_primitives` for covered passes.

## 2. Risks / watch items

- `Shader_key::derive` internals: the color list key must equal today's
  bucket key minus environment bits — verify by asserting (debug) in Phase 3
  that `list.key.primitive_key ⊕ env == bucket.shader_key` for a sampled
  scene, else parity silently breaks (G3).
- Env compare cost: `compute_light_layer_partition` is O(lights) per color
  draw — fine; but it must run BEFORE stages fetch in the same call.
- `handle_flag_bits_update` fires often (hover); `set_object_flags` is
  O(entries) — fine, but avoid any allocation there.
- Destruction order: `Draw_list_object` keeps `Mesh` alive (R10); a
  `Scene_root` teardown must unregister before dropping `Mesh_memory` refs.
- Codegen: editor setting addition needs the double build (memory
  `project_codegen_double_build`).
- Preview scene roots (material / brush previews) and the tools root have
  no `Draw_list_scene` (constructed on init worker threads; tiny scenes) —
  their passes take the fallback via the null check in the routing rule.
- Q6 note: the entry AABB baked at registration goes stale for dynamic
  objects; unused in v1, but future culling needs an update path (per-draw
  recompute from the node, or a transform hook — the latter must respect the
  threading contract).
- Quest: multiview count 2 must be in the ctor's view-config list or the
  first XR frame takes the lazy path once (acceptable, but log it).
