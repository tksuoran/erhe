# Draw list renderer

Stability: stable

`erhe::scene_renderer::Draw_list_scene` is a scene's persistent,
incrementally maintained rendering-side representation: registering an object
once produces draw lists that later frames reuse, so per-frame CPU cost is
proportional to what changed and to what is drawn rather than to the total
primitive count times the number of passes. It is the third per-domain system
a `Scene_root` owns, beside the physics world and the raytrace scene.

This document states the requirements the renderer meets and the design that
follows from them. Its labels (`G*`, `R*`, `C*`, `P*`, `Q*`) are cited from
source comments and keep their meaning across edits. Two companion documents
own subjects this one only refers to: `doc/erhe/draw_list_material_set.md` owns the
material buffer and texture heap a draw list binds, and
`doc/erhe/draw_list_performance_improvements.md` owns the cached per-primitive
records the draw path copies out.

## 1. Problem statement

`erhe::scene_renderer::Forward_renderer` (color passes) and
`Shadow_renderer::draw_shadow_casters` (shadow maps) recompute everything from
scratch on every call. They remain the fallback path for every pass a draw
list does not express, so this cost is still paid there. With thousands of
primitives in the scene it does not scale, because these are called many times
per frame (shadow map per light / cube face, opaque content, translucent
content, selected / not-selected passes, edge line composition passes, ID
render), and each call repeats work whose inputs did not change since the
previous frame:

- `bucket_primitives()` walks every mesh and every primitive, derives a
  `Shader_key` per primitive, and linearly scans the bucket vector
  (`Render_bucket::accept()`) to place each primitive. This is O(meshes x
  primitives x buckets) per `render()` call, and it runs once per entry of
  `base_render_pipelines` inside each call.
- Bucket contents are identical frame to frame whenever the scene composition
  did not change, yet the buckets are rebuilt and thrown away every call.

## 2. Goals

- G1: Registering an object once produces persistent draw lists; subsequent
  frames reuse them without re-bucketing.
- G2: Per-frame CPU cost of issuing a pass is proportional to the number of
  draw lists and entries drawn in that pass, with cache-friendly iteration.
  "No per-entry pointer chasing" is the goal for static lists; dynamic and
  skinned lists necessarily read per-frame scene state (node world transform,
  joint data) while building their primitive upload.
- G3: Output parity: for supported passes the rendered image is identical to
  what `Forward_renderer::render()` / `Shadow_renderer` produce, and the
  pipeline / state selection per draw list is the same as per bucket. The draw
  call *count* may differ where draw list identity is deliberately finer
  (layer id, R13) or coarser (shadow material coarsening, R4) than bucket
  identity.
- G4: The design leaves room for further optimizations on static objects
  (cached per-primitive GPU data, culling structures) without API changes.

## 3. Scope

### Covered

- Shadow map rendering (triangle fill; all three caster variants: depth-only,
  depth-only + distance technique, point-light cube; material ignored except
  opacity / blending classification).
- Main color rendering (triangle fill, opaque and translucent).

### Not covered

- Shader debug override visualizations.
- Edge line passes (all methods) and other non-`polygon_fill` primitive modes.
- ID render / picking passes.
- Entry-granular mirroring of mesh / primitive / material edits. Edits are
  handled, but coarsely, by re-registering the object or rebuilding (R12).
- Culling (Q6): every registered entry that passes the flag filter is drawn,
  exactly as `Forward_renderer` does. `Draw_list_entry` carries the world-space
  AABB (R15) so culling slots in without a data-model change.
- GPU-driven culling or draw generation (the design must not preclude it).

### Fallback

`Forward_renderer` (color) and `Shadow_renderer::draw_shadow_casters` (shadow)
are intact and serve every pass that `Draw_list_scene` does not support. Both
fallback paths and `Draw_list_scene` can run in the same frame against the same
scene. The editor setting `use_draw_lists` selects the path at
runtime; ineligible passes always take the fallback.

## 4. Draw list identity

The implementation lives in
`src/erhe/scene_renderer/erhe_scene_renderer/draw_list_scene.*`, `draw_list.*`,
`draw_list_entry.*`, `draw_list_object.*`, `draw_list_key.*` and
`draw_list_renderer.*`.

Bucket identity, which draw list identity mirrors, is defined by
`Render_bucket` / `bucket_primitives()` (`mesh_memory.cpp`):

- `primitive_mode`
- vertex input key (`bucket_vertex_input_key`)
- index buffer identity and vertex buffer identities (pool id + buffer id)
- `Shader_key` hash (derived from material, vertex format, skinned flag, plus
  the pass "environment" key)
- `negative_determinant` flag (mirrored transforms select the front-face
  flipped pipeline variant)
- blending mode (opaque vs translucent, via `Blending_mode_policy`). Note the
  null-material case: `derive(nullptr)` yields no blending mode, which is
  excluded from opaque passes and included in translucent passes
  (`mesh_memory.cpp` policy switch); R1 reproduces this.

Mesh layer id is not part of bucket identity (content and controller meshes
share buckets); R13 adds it to draw list identity deliberately (Q3).

The two purposes use different environment keys: the color path builds a full
environment key (light counts, shadow settings, multiview, debug); the shadow
path uses an *empty* environment key plus force masks. Draw list resolution
reproduces this per purpose so shadow resolution hits the same variants
`Shadow_renderer::prewarm_pipelines` warms (R22).

### Environment component inventory - what is baked

`Shader_key` and `Render_parameters` mix primitive-derived inputs (material
features, vertex format, skinned flag, blending mode - trivially bakeable per
draw list) with "environment" inputs. The table inventories every environment
input, with how the editor drives it for the covered passes (shadow map plus
content fill opaque / translucent, from `app_rendering.cpp` /
`composition_pass.cpp` / `shadow_renderer.cpp`), and its bake classification:

| Component | Source in the editor | Changes when | Bake classification |
|---|---|---|---|
| Light counts (6 axes: directional / spot / point x shadowed / not) | `compute_light_layer_partition(lights)` per color `render()` call; depends on each light's type, `cast_shadow`, and `is_active()` (point range > 0) | Light add / remove goes through `Scene_root::register_light`; type / range / cast_shadow are edited by direct member writes with no event (properties window, MCP) | **Scene config** - baked; because there is no reliable edit event, the partition is recomputed (O(lights), trivial) at the start of each color `draw()` and re-resolved only if it differs from the cached one |
| `SHADOW_FILTER`, `SHADOW_BIAS`, `SHADOW_TECHNIQUE`, `SHADOW_DEPTH_BITS` (color path) | Graphics preset (`app_settings`), identical for every pass | User changes the graphics preset | **Global config** - baked, and recomputed and compared alongside the light partition (R18). The shadow path's distance-technique toggle is the same preset input but is modelled as the R4a sub-variant, not as config |
| `SHADER_MULTIVIEW_COUNT` | key value 0 when `views.size()==1` (desktop viewports), N when `views.size()==N>=2` (XR) | Per scene view; desktop mirror plus headset can render the same scene in one frame | **View-config axis** - enumerated up front from the same source prewarm uses (`multiview_view_counts`); a draw list caches a resolved variant per enumerated view config; a not-yet-enumerated config seen at draw time resolves lazily once (R19) |
| `SHADER_DEBUG` | 0 for covered passes (the bone N.V pass has an override - not covered) | Debug UI | **Fixed to 0** - non-zero falls back to `Forward_renderer` |
| Force-enable mask | 0 for content fill; shadow: `VARIANT_DEPTH_ONLY` (directional / spot), `+ VARIANT_SHADOW_DISTANCE` when the distance technique is active, `VARIANT_SHADOW_CUBE` for point-light cube faces (on a cull_none base pipeline); `EDGE_LINES_FROM_ID` toggled onto the fill passes when the ID-buffer edge method is active (fallback while active - Q9); other masks (`SOLID_WIREFRAME`, `EDGE_LINES_CORNER_CAP`, `VARIANT_BRUSH_PREVIEW`) belong to passes that are not covered | Light type per shadow pass; graphics preset; editor settings toggles | **Baked as a shadow sub-variant axis** {depth-only, depth-only+distance, cube} - resolved per shadow list, selected by the caller per shadow pass; `EDGE_LINES_FROM_ID` takes the fallback (Q9) |
| Force-disable mask | Always 0 in the editor | - | **Fixed to 0** |
| `Blending_mode_policy` | `opaque_primitives_only` / `translucent_primitives_only` for fill passes; opaque-only for shadow | Static per pass | **Already baked** - this is the draw list's blending classification (R7) |
| `Item_filter` | Selected / not-selected / visible splits | Selection and visibility change constantly | Not a shader input - see Q1 / Q2 |
| `base_render_pipelines` | Static pass definitions (depth / stencil / raster state) | Never at runtime | Supplied by the caller at draw time; pipeline resolution cached per (draw list x pass) |
| `shader_stages_override`, `color_blend_override` | Only passes that are not covered (selection stencil mask, macOS GL edge lines) | - | **Not supported** - such passes use the fallback |
| `Primitive_interface_settings`, exposure, camera / viewport, light *data*, joint matrices, material contents | Per-pass / per-frame UBO and primitive-buffer contents | Every frame | Not variant-affecting - per-frame upload, orthogonal to draw list identity |

### Environment components affect resolution, not partitioning

The environment key is the shared base that per-primitive `Shader_key::derive`
builds on, so a uniform environment shifts every primitive's key identically:
it can change which variant / pipeline a draw list resolves to, but never which
entries a list contains. Consequence: an environment configuration change (R18)
re-resolves cached pipelines only - it never re-buckets and never touches
entries. This invariance is required going forward (R21).

The only components that interact per-primitive, and therefore *would* affect
partitioning, are:

- `SHADER_DEBUG` x `shader_debug_filter` (per-mesh axis drop) - this is why
  shader debug stays out of scope and fixed to 0 rather than becoming a config
  axis.
- `Blending_mode_policy` x material blending mode - membership, already baked
  as the list's blending classification.
- Force masks in general can coarsen partitioning (merging lists that differ
  only in a forced bit). The covered bits (`VARIANT_DEPTH_ONLY`,
  `VARIANT_SHADOW_DISTANCE`, `VARIANT_SHADOW_CUBE`; the ID-buffer edge method
  uses the fallback, Q9) are set exclusively by force masks -
  `Shader_key::derive` never sets them from a material - so they are guaranteed
  uniform, pipeline-only effects. Any future variant that forces a bit
  materials can also set must be re-analyzed.

Unrelated to environment, but the one identity component that could change at
runtime: `negative_determinant` is transform-driven. A runtime sign flip on a
registered object is reported and asserts in debug (R10b).

Requirements following from this:

- R17: Each draw list caches its fully resolved shader stages
  (`Shader_variant_cache` result) per resolution config, resolved at
  registration time and on configuration change (plus the one-off lazy paths of
  R19 / R4a) - not per frame, not per draw, and never per entry. The *render
  pipeline* additionally depends on inputs known only at draw time
  (`Base_render_pipeline`, render-pass descriptor, color blend);
  `Base_render_pipeline::get_pipeline_for` is a hash-cached lookup keyed on
  those, so per-list pipeline lookup at draw time is one hash probe, not a
  compile. Cache key for the resolved stages: (list identity x purpose
  sub-variant x view config x environment configuration).
- R18: `Draw_list_scene` holds one **environment configuration**
  (`Color_environment`) for the color purpose: light-count partition plus the
  shadow settings of the graphics preset. The shadow purpose has no environment
  configuration: its only varying input, distance technique on / off, is
  expressed as the caller-selected sub-variant of R4a. Changing the
  configuration invalidates and re-resolves the cached stages of the affected
  lists - never their contents (R21). Both parts are recomputed and compared at
  the start of each color `draw()` (`set_color_environment`), because light
  type / range / cast_shadow and the preset are edited by direct member writes
  with no event; the comparison is cheap and needs no message-bus subscription.
- R19: Multiview is a small enumerable axis of the cached resolution
  ({single-view, multiview-N}), not a rebuild trigger: a desktop viewport and an
  XR view of the same scene render in the same frame. The set of view configs is
  enumerated up front from the same source prewarm uses (the
  `multiview_view_counts` list) and resolved at registration; a config not in
  that set encountered at draw time resolves lazily once and is added to the
  set. This lazy path and R4a's first-use shadow sub-variant resolution are the
  only two exceptions to R20; both are by construction one-offs, not steady
  state.
- R20: Steady state (no configuration change, no new view config, no first-use
  shadow sub-variant): `draw()` performs zero `Shader_key` derivation and zero
  `Shader_variant_cache` lookups; it uses the cached stages directly and one
  hash probe per list for the pipeline (R17).
- R21 (environment invariance): Draw list contents - the set of draw lists,
  their identities, and their entries - are invariant under every environment
  change (light-set changes, graphics preset changes, view config, editor
  setting toggles such as the ID-buffer edge method). Environment changes may
  only invalidate and re-resolve cached variants / pipelines (R17); they never
  add, remove, move, or re-partition entries. Entries change only on scene-side
  events: registration / unregistration (R1 / R2), re-registration on mesh /
  material edits (R12), and the rebuild hook (R1a); flag changes update entry
  state in place (R12a).
  Corollary: any future variant axis or forced shader bit that would break this
  invariance (per-primitive interaction, as analyzed in this section) must be
  redesigned or kept on the `Forward_renderer` fallback - it must not be bolted
  onto `Draw_list_scene`.
- R22 (variant compilation, resolves Q10): `Forward_renderer` and
  `Draw_list_scene` share the same `Shader_variant_cache` and pipeline caches -
  the same variant is never compiled twice because two renderers want it. The
  prewarm paths (`prewarm_standard_variants`,
  `Shadow_renderer::prewarm_pipelines`) are authoritative: they cover the
  fallback passes regardless, and with a shared cache draw-list resolution
  (R17) is a cache hit for prewarmed content - provided the keys match.
  Consequently, wherever draw list identity intentionally differs from bucket
  identity, the corresponding prewarm warms the draw-list key too (concretely:
  shadow prewarm warms the coarsened shadow key of R4). On a cache miss (an
  unwarmed variant appears via registration or a config change), compilation
  happens at registration / invalidation time - off the per-frame hot path.
  `draw()` itself never triggers compilation (except the one-off lazy paths of
  R19 and R4a); a list whose variant is not yet resolved is skipped with a
  warning, exactly like the "No shader variant for bucket" behavior of the
  fallback.

## 5. Components and requirements

Ownership chain: `Scene_root` owns one `Draw_list_scene` (unique_ptr, same
pattern as `m_physics_world`). `Draw_list_scene` owns its `Draw_list_object`s
and `Draw_list`s. A `Draw_list` owns its `Draw_list_entry`s.

### 5.1 Draw_list_scene

Registration:

- R0 (registration pattern, resolves Q4): registration follows the
  physics / raytrace pattern in `Scene_root`: `Scene_root::register_mesh` /
  `unregister_mesh` (driven by the item-host attach / detach path, as with
  `attach_rt_to_scene` / `Node_physics_system`) also register / unregister the mesh
  with `Draw_list_scene`. There are no separate editor-side call sites.
- R0a (library layering): `Mesh` lives in `erhe::scene`, which does not and
  must not link `erhe::scene_renderer`. Every scene-to-draw-list notification
  named in this document (registration, primitive-set change, material
  reassignment, flag mirroring, transform update) therefore goes through the
  `erhe::scene::Scene_host` interface (`scene_host.hpp`) - virtuals alongside
  `register_mesh` / `unregister_mesh` - implemented by `Scene_root`, which owns
  the `Draw_list_scene`. `Mesh` calls its host; it never sees
  `Draw_list_scene`. This is how `attach_rt_to_scene` and `Node_physics_system` are
  reached.
- R1: `register` function taking `Draw_list_object_create_info`. For each
  primitive of the object it determines the draw list(s) the primitive belongs
  to (creating draw lists on demand), and appends a `Draw_list_entry` per
  (primitive x draw list). The classification mirrors `bucket_primitives()`
  (same skips: null primitive, null renderable mesh, zero index count; same
  null-material blending classification - no blend mode means excluded from
  opaque, included in translucent).
- R1a (rebuildability, resolves Q7): `Draw_list_scene` retains, for every
  registered object, a record sufficient to re-run its classification from
  scratch (the `Draw_list_object` keeps the create-info-level inputs, not just
  the resulting entries). An explicit invalidation hook drops and rebuilds all
  draw lists from these records. Its clients are material *content* edits that
  change list identity (R12) and any future wholesale invalidation of baked
  state. `Mesh_memory` / `Buffer_pool` never *move* existing allocations at
  runtime - pools only append blocks - so pool-level residency churn is not a
  client of this hook. A live `Primitive`'s `Buffer_mesh` can however be
  replaced in place: `Primitive_render_shape::commit_geometry_buffer_mesh()`
  and `make_buffer_mesh()` move-assign a new `Buffer_mesh` over
  `m_renderable_mesh` (freeing the old ranges) on attached, registered meshes
  in ordinary use (deferred glTF / prefab finalize via
  `async_raytrace_kickoff_operation`). That path calls
  `Mesh::update_rt_primitives()`, which is why R12 hangs the re-register hook
  there: the per-object re-register, not this wholesale hook, is what keeps
  baked buffer ranges valid.
- R1b (dependencies): classification needs `Mesh_memory`
  (`get_vertex_input` for the vertex format that feeds `Shader_key::derive`),
  resolution needs `Shader_variant_cache`, and records need
  `Primitive_interface`; `Scene_root` has access to none of them on its own.
  They are injected through `Draw_list_scene_dependencies` when
  `Draw_list_scene` is created. A `Scene_root` constructed without them has no
  `Draw_list_scene` (`m_draw_list_scene == nullptr`); registration and flag
  mirroring are no-ops in that case, mirroring how `enable_physics == false`
  leaves `m_physics_world` null. The null graphics backend is still a `Device`
  with a `Mesh_memory`; it gets a `Draw_list_scene` like any other backend and
  stays in sync. The preview roots (material / brush previews) and the tools
  root deliberately have none: they are constructed inside parallel init tasks
  on worker threads, while a `Draw_list_scene` binds its owner thread, and they
  are tiny scenes. Their passes take the fallback through the null check in the
  routing rule.
- R2: `unregister` function that removes all entries belonging to an object.
  Removal does not invalidate the draw lists' suitability for hot-path
  iteration (compaction is an implementation choice; stale entries are never
  drawn).
- R3: A primitive is added to every draw list variant it participates in (one
  entry in a color list and one in a shadow list, for example).

Variants - each registered primitive is classified along two axes:

- Purpose: main color | shadow map.
- Mobility: static (transform never changes; enables further optimizations) |
  dynamic (transform can change) | skinned.

giving six variants:

| Variant | Purpose | Mobility |
|---|---|---|
| color / static | main color | static |
| color / dynamic | main color | dynamic |
| color / skinned | main color | skinned |
| shadow / static | shadow map | static |
| shadow / dynamic | shadow map | dynamic |
| shadow / skinned | shadow map | skinned |

- R4: Shadow variants ignore material except for what affects shadow casting,
  so primitives that differ only in other material features share shadow draw
  lists. This is a deliberate coarsening relative to shadow buckets (which
  derive the key from the full material even though the position-only vertex
  path ignores those bits). Concretely, the shadow list *shader key* is exactly
  {`USE_SKINNING`, the forced VARIANT bits of the sub-variant} - no
  material-derived bits at all, including the material-gated
  `USE_VERTEX_VARYING_*` bits, which `ERHE_VARIANT_POSITION_PASS` ignores.
  `Shader_variant_cache` is keyed on `Shader_key` only - the vertex format is
  used just at first compile (attribute locations are sequential per format) -
  so the coarsened key yields one variant per {USE_SKINNING} regardless of
  format. That is correct only because position is location 0 in every mesh
  format and joints / weights sit at the same locations in the skinned formats;
  the shadow prewarm asserts it. Shadow list *identity* additionally keeps
  `negative_determinant`, buffer set, layer id and blending classification
  (opaque-only membership, as the shadow pass uses `opaque_primitives_only`).
  `Shadow_renderer::prewarm_pipelines` builds this identical coarsened key
  (R22).
- R4a: Shadow lists carry a resolved-stages sub-variant axis {depth-only,
  depth-only + distance, cube} (see the inventory table); `draw()` for the
  shadow purpose selects the sub-variant per pass, and the caller supplies the
  matching base pipeline and color-blend state exactly as
  `Shadow_renderer::render` does. The shadow prewarm warms depth-only;
  distance and cube compile on first use, because a sub-variant's stages are
  resolved on the first `draw()` that selects it (one-off, the same carve-out
  as R19's lazy view config), so unused sub-variants are never compiled.
- R5: The variant set is extensible (edge lines, ID render, debug
  visualizations) without reworking registration. Passes not covered by a
  variant fall back to `Forward_renderer` / `Shadow_renderer`.

Drawing:

- R6: `draw` function that renders exactly one purpose variant selected by the
  caller, into a caller-provided render encoder / pass (all mobility classes of
  that purpose in one call - the split by mobility exists for update cost and
  further optimization, not for pass structure).
- R6a (layers, resolves Q3): `Draw_list_scene` covers all mesh layers.
  `draw()` additionally takes the set of mesh layer ids to include (passes draw
  layer subsets: content plus controller for content fill, rendertarget alone
  for the rendertarget pass). Because layer id is part of draw list identity
  (R13), layer selection selects whole lists - it is never a per-entry test.
- R7 (blending class selection): `draw()` takes a blending-class selector -
  opaque only, translucent only, or both - because the editor's opaque and
  translucent content fills are separate composition passes with other passes
  (sky, grid, edge lines) in between, and the rendertarget pass uses
  `allow_all`. When both are selected in one call, all opaque lists are drawn
  before all translucent lists. Translucent draw order within the class is
  implementation-defined (see C1).
- R7a (draw-time flag filtering, resolves Q1 / Q2): `draw()` accepts a filter
  with `Item_filter` semantics (require-set / require-clear bit masks)
  evaluated against each entry's mirrored flag bits (R12a) while the indirect
  draw commands are built. Entries rejected by the filter emit no draw command
  but remain in their lists. This is how the selected / not-selected / visible
  pass splits are expressed: same lists, different filter per pass.
- R8: Per draw list, drawing performs: fetch the cached pipeline (R17 / R20 -
  resolved earlier via `Shader_variant_cache` and
  `Base_render_pipeline::get_pipeline_for`), bind index / vertex buffers, fill
  and bind per-primitive and draw-indirect ranges, issue one
  `multi_draw_indexed_primitives_indirect`. This mirrors the per-bucket body of
  `Forward_renderer::render()` minus the per-call key derivation and cache
  lookups.
- R8a (GPU buffer ownership and bind contract): `Draw_list_scene` owns its own
  **material** GPU state - a `Material_set`, and through it the material buffer
  and the texture heap (`doc/erhe/draw_list_material_set.md`) - and no other GPU
  buffers. The per-pass Camera / Light / Joint update and bind sequence is the
  responsibility of the owning renderer (`Draw_list_renderer` for color,
  `Shadow_renderer` for shadow), and `draw()` is invoked *inside* that
  sequence with the renderer's `Primitive_buffer` and `Draw_indirect_buffer`
  supplied for filling the per-primitive and indirect ranges.
  `Primitive_buffer::update` and `Draw_indirect_buffer::update` have
  entry-based overloads alongside the `Render_bucket` ones. The pass binds and
  unbinds the material set it is handed, but never creates, updates or resets
  one: that happens once per frame, before any pass.
  Consequence for R15, **joint slots only**: `joint_buffer_index` is assigned
  per `Joint_buffer::update` call and therefore cannot be baked into entries -
  an entry stores a stable reference to the skin and the slot is read at upload
  time. **Material slots are the opposite**, and deliberately so: a material's
  slot is a property of the `Material_set` that issued it, stable for as long
  as anything references the material in that set, so it *is* baked into the
  cached record and read from there. A slot assigned per renderer call would
  mean "slot 7" names different materials depending on which pass wrote last,
  which is the defect `doc/erhe/draw_list_material_set.md` exists to make
  unrepresentable.
- R9: Frame-varying per-entry GPU data (primitive transforms, joint matrices)
  is uploaded per frame for dynamic / skinned lists. Static lists are
  structured so a future change can skip or cache this upload (G4); the
  per-list `primitive_records` block
  (`doc/erhe/draw_list_performance_improvements.md`) is the upload source such a
  change would use.

### 5.2 Draw_list_object

- R10: Represents one registered scene object (mesh) inside `Draw_list_scene`
  and keeps the mesh and its primitives alive (owning references) for as long
  as it is registered.
- R10a (mobility classification, resolves Q5): static / dynamic mobility is an
  explicit flag in `Draw_list_object_create_info`, supplied by the caller at
  registration; skinned means `mesh->skin != nullptr` (the shader key's
  `USE_SKINNING` additionally requires joint vertex attributes, so a skinned
  list may still resolve a non-skinning variant - that is a key matter, not a
  mobility matter). Because registration happens inside
  `Scene_root::register_mesh` (R0), which has no mobility information, and
  there is no static item flag, every non-skinned mesh registers as
  **dynamic**. The API carries the flag so a static classification source (item
  flag, asset metadata) can be added without an API change. A transform change
  on an object registered as static is a bug and asserts in debug.
- R10b (determinant stability, resolves Q11): `negative_determinant` is baked
  into list identity. Registration samples it from the node's current world
  transform (`world_from_node()` determinant), not from the mesh's
  `Item_flags::negative_determinant` bit: the mesh's own parenting calls
  `handle_item_host_update` (and thus `register_mesh`) *before*
  `handle_node_transform_update` (which sets the flag), so a mesh attached
  under a mirrored parent would otherwise register with the flag clear and flip
  one call later. A runtime determinant sign change on a registered object is
  not supported: `flush_pending()` compares the observed bit against the
  registered value, logs an error and asserts in debug. Known cases that trip
  it: mirroring an object by negative scale interactively; reparenting a node
  under a mirrored parent within the same host (transform update only, no
  re-registration); and a cross-host reparent under a mirrored ancestor
  (`Node::handle_parent_update` registers before `update_world_from_node()`,
  and descendants' world transforms refresh only in the queued propagation
  pass, so registration can sample a stale determinant there).
- R11: Knows which entries in which draw lists belong to it, so unregistration
  and incremental updates are O(entries of this object), not a scan of all draw
  lists.
- R12a (flag mirroring, resolves Q1): dynamic per-item state is mirrored into
  the draw lists through a `Draw_list_object` API: the caller sets / clears
  flags on the object, and the implementation locates that object's entries
  (via R11) and updates a flag-bits field stored in each entry, in place. Cost
  is O(entries of that object). No entries are added, removed, or moved between
  lists by a flag change (flags are entry *state*, not list *identity*, R21).
  The item hook `Mesh::handle_flag_bits_update` (also used to mirror visibility
  into the raytrace instance) is the mirroring path, and it runs for any change
  to a mirrored bit, not only for `visible`. The mirrored value is the full
  `Item_flags` word, because filters test bits across it (visible, content,
  controller, selected, hovered_in_item_tree, shadow_cast, proxy_hidden, ...),
  so the entry field is 64 bits wide and `Item_filter` applies unchanged.
- R12 (mesh / material edits): registered meshes are not immutable in practice.
  The editor calls `Mesh::set_primitives` / `add_primitive` on attached meshes
  (mesh operations, undo / redo, geometry-graph re-bake, merge, vertex move),
  reassigns materials in place, and edits material contents that change list
  identity (bind texture, BXDF model, blending mode); and a live primitive's
  `Buffer_mesh` (index / vertex ranges, buffer ids) can be replaced in place
  without any primitive-list change (R1a). The object is therefore re-registered
  with `Draw_list_scene` (unregister plus register, O(entries of the object))
  from `Scene_host` virtuals invoked by `Mesh::update_rt_primitives()` (reached
  from `set_primitives`, `add_primitive`, and the deferred buffer-mesh commit
  path) and by `Mesh::clear_primitives()` (which does not call
  `update_rt_primitives` and is used on attached meshes by
  `geometry_graph_mesh.cpp`). For an attached mesh the editor brackets
  `update_rt_primitives` with `Scene_root::begin_mesh_rt_update` /
  `end_mesh_rt_update` (or re-parents the node) to re-attach rt instances;
  those brackets are the analogous hooks. Material reassignment on a primitive
  goes through `Mesh::set_primitive_material`, which is the only writer of
  `Mesh_primitive::material` and notifies the host (D11 in
  `doc/erhe/draw_list_material_set.md` follows that chain to the material set).
  Material *content* edits that change list identity are handled by the
  wholesale rebuild hook (R1a), driven by the per-material identity hash
  `Draw_list_scene::check_material_changes()` compares each frame. Entries never
  reference primitives by an index that can go stale without one of these hooks
  firing.
- R12b (live per-primitive upload inputs): per-primitive values edited in place
  are read live from the `Mesh` / `Mesh_primitive` / `Node` at upload time
  rather than baked into `Draw_list_entry`, unless a hook keeps a cached record
  current. The cached `Draw_list::primitive_records` block
  (`doc/erhe/draw_list_performance_improvements.md`) supersedes the live reads for
  the fields it covers and names the hook that maintains each; material slots
  are baked into the record because they are stable per set (R8a).

### 5.3 Draw_list

- R13: Carries everything that is uniform across its entries: the
  primitive-derived shader key components, buffer set (vertex input key, index
  buffer identity, vertex buffer identities), `negative_determinant`, blending
  classification, primitive mode, and mesh layer id (Q3: layer is part of draw
  list identity - entries from different layers never share a list). That is
  the full pipeline / bind state needed at draw time, resolved once per list
  per pass, not per entry.
- R14: Entries are stored contiguously for hot-path iteration.

### 5.4 Draw_list_entry

- R15: Contains only the per-primitive parameters not covered by its
  `Draw_list`: what is needed to (a) cull the entry (world-space AABB) and (b)
  emit its indirect draw command and per-primitive GPU data (index range,
  vertex offsets, transform source, stable material / skin reference - the
  material *slot* is baked into the cached record, the joint slot is not; see
  R8a - and per-entry flags).
- R16: Layout is value-type and cache friendly: fixed size, no per-entry heap
  indirection on the hot path. References to cold / owning data (the
  `Draw_list_object`) go through indices, not pointers that the hot path must
  dereference.

## 6. Correctness requirements

- C1 (parity): For scenes containing only supported content, images produced
  via `Draw_list_scene` are identical to `Forward_renderer` /
  `Shadow_renderer` output for the same passes for opaque content and shadow
  maps (order-independent). Neither renderer depth-sorts translucent
  primitives - the fallback's translucent order is layer / mesh iteration
  order, and persistent lists differ - so translucent intra-class draw order is
  implementation-defined and pixel-exact translucent parity is not required.
- C2: Mirrored transforms (negative determinant) select the front-face-flipped
  pipeline variant per draw list, as the fallback does per bucket.
- C3: Visibility / pass filtering semantics (`Item_filter` behavior for the
  supported passes) are preserved, expressed as draw-time filtering on mirrored
  per-entry flag bits (R7a / R12a). A stale mirror (flag changed on the scene
  item without the `Draw_list_object` update) is a bug in the mirroring path,
  not acceptable divergence.
- C4: Coexistence with `Forward_renderer` in the same frame does not corrupt
  shared state (ring buffers, texture heap, samplers).
- C5 (multiple scene views): several viewports or an XR view rendering the same
  scene in one frame, each with different per-view settings (render style,
  camera, exposure, shader debug, ID-buffer edge method, multiview), all render
  correctly with no per-frame re-resolution churn. Therefore the color
  environment configuration (R18) contains only scene-level and global inputs
  (the scene's light partition, the global graphics preset) - never per-view
  inputs. Per-view inputs are either draw-time parameters (filter, settings,
  layers, blending selection, camera data), the R19 view config axis
  (multiview), or per-pass fallback routing (shader debug,
  `EDGE_LINES_FROM_ID`). Verification: two viewports of one scene, one desktop
  plus one XR view, one with shader debug on - no re-resolution after the first
  frame (`get_color_environment_change_count()` and the lazy-resolution counter
  stay put).

## 7. Performance requirements

- P1: Steady-state frame with no scene changes: zero bucketing / classification
  work; per-pass CPU cost is iteration, upload and draw submission only.
- P2: Registration cost is O(primitives of the object x draw lists touched);
  draw-list lookup during registration is a hash lookup, not a linear scan over
  all existing lists (unlike `Render_bucket::accept()`).
- P3: Target scale: thousands of primitives, tens of draw lists, multiple
  passes per frame (N shadow lights plus color passes), including XR (72-90 Hz,
  multiview).
- P3a (per-draw entry cap): the binding cap on entries per multi-draw is
  `Primitive_interface::max_primitive_count` (config `renderer.json`, 6000;
  further clamped by `max_uniform_block_size` on non-SSBO devices, since
  `ERHE_DRAW_ID` indexes the `primitives[]` UBO array and
  `Ring_buffer_client::bind` verifies the range fits). `Draw_indirect_buffer`
  ring ranges grow on demand. A draw list larger than the cap - likelier than a
  bucket because R4 coarsens shadow lists - is drawn as chunks of at most cap
  entries, each with its own primitive range, indirect range and multi-draw.
  Silent truncation is not acceptable.
- P4: Measurable: per-pass timing of the passes replaced demonstrates the win
  at target scale (section 10).

## 8. Resolved design questions

- Q1: Selection changes go through a `Draw_list_object` flag API that updates
  flag bits in that object's entries in place; `draw()` filters on those bits
  (R12a, R7a).
- Q2: Both levels, with a fixed split: pass *purpose* (color / shadow) is
  registration-time list membership; dynamic per-item state (selected /
  hovered / visible) is a draw-time per-entry flag test (R7a). Mirrored bits:
  the full `Item_flags` word (R12a).
- Q3: All layers. Mesh layer id is part of draw list identity; entries from
  different layers go to separate draw lists, and `draw()` selects layers by
  selecting lists (R6a, R13).
- Q4: Same pattern as physics / raytrace: `Scene_root::register_mesh` /
  `unregister_mesh` register with `Draw_list_scene` alongside
  `m_raytrace_scene` / `m_physics_world` (R0).
- Q5: Explicit mobility flag in `Draw_list_object_create_info`; a transform
  change on a static-registered object asserts (R10a). `Scene_root::
  register_mesh` has no mobility source, so everything non-skinned registers as
  dynamic; the flag exists in the API for a future static source.
- Q6: No culling; frustum culling on the entry AABB is the first future-work
  item (section 3, "Not covered").
- Q7: An invalidation hook rebuilds the draw lists. R1a keeps registration
  records complete enough to recreate all draw lists from scratch.
  `Mesh_memory` never moves allocations at runtime, so the hook's clients are
  material-content edits (R12) and future wholesale invalidations, not buffer
  residency.
- Q8: No additional uses of the variant set beyond the covered passes; the
  fallback covers them (R5).
- Q9: While the ID-buffer edge-line method is enabled, the content fill passes
  fall back to `Forward_renderer` (`EDGE_LINES_FROM_ID` stays out of
  `Draw_list_scene` entirely). Treating it as a second baked config remains
  possible if the fallback cost hurts in practice.
- Q10: The existing prewarm stays authoritative; both renderers share the
  variant and pipeline caches; draw-list resolution compiles only on a miss and
  only off the per-frame hot path (R22).
- Q11: A registered object's determinant sign change at runtime is reported and
  asserts in debug (R10b). Registration samples the determinant from the world
  transform because of the attach ordering; the assert is known to trip on
  interactive mirroring and on reparenting under a mirrored parent.

## 9. Implementation shape

### 9.1 Identity and keys

`Draw_list_key` (`draw_list_key.hpp`) is hashable and there is one `Draw_list`
per distinct key:

```
Draw_list_key
  purpose             : { color, shadow }
  mobility            : Draw_mobility { static, dynamic, skinned }   // R10a
  layer_id            : erhe::scene::Layer_id                        // R13 / Q3
  blending            : { opaque, translucent }                      // R7; null material -> translucent (R1)
  negative_determinant: bool                                         // R10b, sampled from world_from_node()
  buffer_set          : vertex input key, index buffer id, vertex buffer ids
  primitive_mode      : Primitive_mode (polygon_fill)
  primitive_key       : Shader_key    // primitive-derived ONLY:
                                      //   color : derive(material, vertex_format, skinned) minus environment
                                      //   shadow: {USE_SKINNING} only (R4)
```

Environment is not in the key (R21). The resolution configuration is
`Color_environment` (light partition plus shadow settings, R18), the view config
(multiview count, R19) and the shadow sub-variant {depth_only,
depth_only_distance, cube} (R4a).

Combine rule for color resolution: the resolved key's boolean mask is the
primitive key's mask OR the environment mask; integer axes merge per axis (the
environment sets the light-count / shadow / multiview axes, the primitive key
sets its own; no axis is set by both while `SHADER_DEBUG == 0`); the blending
mode comes from the primitive key. The result equals the bucket key the
fallback derives for the same primitive, which is what makes R22's shared cache
a hit and G3 hold.

### 9.2 Classes

- `Draw_list_entry` (R15 / R16): fixed-size value type holding the owning
  object index, the mesh primitive index, the mirrored `Item_flags` word, the
  index range (index count, first index, base vertex) and the world-space AABB.
- `Draw_list` (R13 / R14): its key, a contiguous vector of entries, the
  contiguous `primitive_records` block that parallels it
  (`doc/erhe/draw_list_performance_improvements.md`), and the resolved-stages cache
  (per view config for color, per sub-variant for shadow).
- `Draw_list_object` (R10 / R11): the create-info-level inputs kept verbatim for
  rebuild (R1a), the (draw list, entry) locations of its entries, the last
  mirrored flag word, the sampled `negative_determinant`, and its
  `Material_slot_id`s into the draw-list material set.
- `Draw_list_scene` (R0 to R9): the key-to-list map, the object free list with
  stable ids (index plus generation, so an entry's object index never needs
  patching), the pending queue, the `Material_set`, and the register /
  unregister / reregister / flags / rebuild / draw API. Unregister is a
  swap-remove that patches the moved entry's owning object.
- `Draw_list_renderer` (`draw_list_renderer.hpp`) is the color entry point: it
  runs the same camera / light / joint / material prologue a bucket pass runs
  and then calls `draw_color()`. `Shadow_renderer` calls `draw_shadow()` in
  place of `draw_shadow_casters` when it is given a `Draw_list_scene`.

Per list, the draw body skips lists not matching the requested layers or
blending class, fetches the cached stages (resolving lazily only per R19 /
R4a), looks the pipeline up, binds index and vertex buffers, fills the
primitive and indirect ranges through the entry-based overloads (which apply
the flag filter while writing, so both produce the same command count), and
issues the multi-draw. Chunking (P3a) is applied jointly at the
`Draw_list_scene` level.

### 9.3 Scene-side hooks and the threading contract

`erhe::scene::Scene_host` carries the notification virtuals (R0a), implemented
by `Scene_root` and no-ops when `m_draw_list_scene == nullptr`: mesh
registration and unregistration, primitives changed (from
`Mesh::update_rt_primitives` and `clear_primitives`), flags changed (from
`Mesh::handle_flag_bits_update`), material changed (from
`Mesh::set_primitive_material`), transform changed (from
`Mesh::handle_node_transform_update`) and primitive data changed (from
`Mesh::set_primitive_lightmap_uv_scale_offset`).

**Threading contract (mandatory).** `Mesh::update_rt_primitives()` and
`Mesh::handle_node_transform_update()` - and thus the hooks above - run on
`tf::Executor` worker threads (deferred finalize of
`Async_raytrace_kickoff_operation`, under `item_host_mutex`), while the render
thread never takes that mutex and `Shader_variant_cache::get` is an unlocked
map plus a compile. Therefore **no `Scene_host` hook reads or mutates
draw-list state or resolves variants directly.** Every hook only enqueues an
operation (mesh plus kind: register, unregister, reregister, flags, transform,
refresh) into a mutex-protected pending queue on `Draw_list_scene`;
`Draw_list_scene::flush_pending()` runs on the main thread once per frame and
performs unregister / register / resolve there. Rules:

- Flush site: `Editor::tick`, after transform propagation (so registration
  samples current world transforms, R10b) and before the rendergraph; one flush
  per registered scene root per frame covers viewports, shadow nodes and the
  headset node. `App_scenes::flush_draw_lists()` is that loop, and the
  material-set update (`doc/erhe/draw_list_material_set.md` D6) runs right after it.
- `flush_pending()` takes the scene root's `item_host_mutex` (lock order:
  item_host_mutex, then pending mutex; swap the queue out under the pending
  mutex, then process under item_host_mutex only) so registration never reads a
  `Buffer_mesh` mid move-assign.
- Ordering edge cases: `flags` for a not-yet-registered mesh is ignored;
  unregister plus register of the same mesh in one queue is normal (mesh
  operations detach and re-attach); teardown drops the queue without processing
  it, because `~Scene_root` may run on a worker holding the last `shared_ptr`.
- The R10b determinant check lives in `flush_pending()` when processing a
  `flags` item: the incoming `negative_determinant` bit is compared with the
  object's registered value.
- Flag updates coalesce (last value wins). `register_object`,
  `unregister_object` and `flush_pending` are main-thread-only and assert the
  thread id in debug.
- Anything reachable from inside the rendergraph enqueues rather than acting:
  `set_exclude_unlit_from_shadows` enqueues its rebuild for the next frame's
  flush, so no rebuild rewrites cached records mid-frame after some passes have
  consumed them (`doc/erhe/draw_list_material_set.md` D1d).

This satisfies R17 / R20 / R22: resolution is off the per-frame draw hot path
and never inside `draw()`.

### 9.4 Editor routing

`Composition_pass::render` routes to `Draw_list_renderer` only when all hold:
the effective scene root has a non-null `Draw_list_scene` (R1b);
`primitive_mode == polygon_fill`; the effective `shader_debug` is none;
`shader_stages_override` and `color_blend_override` are null; the shader-key
force-enable and force-disable masks are zero; `EDGE_LINES_FROM_ID` is not
applied this frame (Q9); and the blending-mode policy maps to a
`Blending_selection`. The render-style gate, the appearance-derived
`Primitive_interface_settings`, exposure and all base render parameters are
kept on the routed path. Everything else takes the `render()` fallback (brush,
bone and selection stencil passes). `Shadow_render_node` passes the root's
`Draw_list_scene` and its layer id list to `Shadow_renderer`.

## 10. Measured effect

Per-pass CPU wall time inside `Composition_pass::render()` and
`Shadow_render_node::execute_rendergraph_node()`, over the Niagara bistro scene
(`res/editor/assets/niagara_bistro/bistro.gltf`; 2909 registered objects, 5652
entries, 132 non-empty draw lists), default viewport, nothing selected,
measured through the MCP tools `reset_composition_pass_stats` and
`get_composition_passes` with the setting off and on:

| Pass | build | classic (us) | draw lists (us) |
|---|---|---|---|
| Content fill opaque not selected | Release ninja | 1413 | 272 |
| Content fill selected (0 selected) | Release ninja | 175 | 5.6 |
| Content fill translucent not selected | Release ninja | 501 | 104 |
| Content fill translucent selected | Release ninja | 165 | 1.6 |
| Sum of the four fill passes | Release ninja | 2254 | 383 |
| Shadow node (per exec) | Release ninja | 9450 | 2513 |
| Content fill opaque not selected | Debug VS headless | 50000 | 3250 |
| Sum of the four fill passes | Debug VS headless | 74000 | 5425 |
| Shadow node (per exec) | Debug VS headless | 348000 | 48000 |

The bucketing, `Shader_key` derivation and bucket scan that the classic path
repeats per pass are gone (P1), and a pass whose filter rejects everything
early-outs before the pass prologue. What remains in the draw-list cost is
per-list overhead (pipeline lookup, debug label, ring-buffer acquire per chunk,
indirect command write per entry) rather than per-primitive derivation.

Verification recipe for a re-run, and the checks that go with a change to this
path: headless Vulkan, `capture_screenshot` at 2304x1200 with the frame-time
text masked, comparing the setting off against on for the default scene, with a
mesh selected (selected / not-selected fill passes), after moving the selected
mesh, after creating a material, with a shadow-casting point light (cube
sub-variant), and with two skinned models imported and one of them deleted
(joint slot shift). Prewarm parity (R22) is checked by grepping `logs/log.txt`
for `Shader_variant_cache miss` in the first frames with the setting on.

## Future work

- [plans/draw_list_renderer.md](../plans/draw_list_renderer.md): culling, static
  lists, determinant-flip re-listing, translucent sorting and the remaining
  material-set follow-ups.
