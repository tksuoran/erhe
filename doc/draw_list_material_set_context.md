# Draw list material set - planning context

Background for `doc/draw_list_material_set_plan.md`: where the work stands, how
much of the plan has been reviewed, and why the work exists at all. The plan
itself is self-contained for implementing; this is what to read before picking
it up.

## Status: the plan is implemented and verified

**The reported bug is fixed**, and its regression test says so: V3
(`mcp_server_tests.cpp` `material_drag_to_second_mesh_uses_same_record_slot`)
is green in both assignment orders. Its MCP surface is `assign_mesh_material`
and `get_draw_lists`'s per-mesh `entries` array, where `material_index` is what
a cached record resolved to and `material_set_slot` is where the material
actually sits in that scene's draw-list set.

**Every phase of the plan has landed**, plus the prerequisite
`Draw_list_renderer` split and the D11 cheap path. The shape now in the tree:

- a material's slot is a property of the `Material_set` that issued it.
  `Material::material_buffer_index` and `preview_slot` are **gone**, and with
  them the legacy ring-based `Material_buffer::update()` and its
  `Ring_buffer_client` base. The bug is no longer expressible;
- `Scene_root` owns the forward set and references its meshes' materials into
  it from its four mesh hooks; `Draw_list_scene` owns the draw-list set; the
  BRDF slice window, the standalone example and the shared empty set each own
  a library-only one;
- `Material_set_factory` (`src/editor/renderers/`) owns the one fallback
  texture / sampler pair and the shared empty set, and is published into
  `App_context` before the construction taskflow - **not** `App_rendering` as
  D3 says, because `App_rendering` is itself built inside that taskflow
  alongside the objects that need a set from their own constructors;
- `App_scenes::update_material_sets()` runs the D6 schedule after
  `flush_draw_lists()`; the previews and the BRDF slice run their own at their
  render entry points;
- `Base_render_parameters` carries a `Material_set* material_source`. No
  renderer owns a material buffer, texture heap or fallback pair, and none
  resets a heap per pass. `Primitive_buffer` and `Scene_tlas` look slots up in
  the set their own pass binds; `Draw_list_object` holds `Material_slot_id`s
  and `write_slot_fields` resolves through them with a verify;
- the content hash and the record writer are generated from one list
  (`Material_record_inputs` in `material_buffer.hpp`, built by
  `gather_material_record_inputs()`), so a field the writer reads is a field
  the hash covers by construction.

**Three erhe_graphics bugs were found and fixed along the way**, none of them
reachable before this work gave one heap more than one consumer or sized one
down:

- descriptor-set recycling keyed on the frame a set was *acquired*, so a
  persistent heap could have the set frames were still reading recycled under
  it (D2b). It is stamped on every bind now;
- `Texture_heap` built its own set-1 descriptor set layout sized
  `max_textures`, while the pipeline layout's set 1 is always
  `max_texture_heap_size` wide. Any heap sized down produced descriptor sets
  `vkCmdBindDescriptorSets` rejects outright, which aborts the editor on the
  first bind. **D2a is wrong as written**: `max_textures` may size the variable
  descriptor count, never the layout;
- `Texture_heap` bound its set with the pipeline layout of the
  `Bind_group_layout` it was *constructed* with. Set 1 is compatible only with
  a pipeline layout that agrees on every lower-numbered set, so that held only
  while a heap had one consumer. It binds with the encoder's layout now.

**Confidence, by part.** The membership analysis (section 1 below, and D1, D1a,
D1d in the plan) has seven independent review rounds behind it, and the seventh
found no blocking or major defect. Newer and **unreviewed**: the two-set
separation (D0, D1c, D3, D4, D6, D11), the single-type `Material_set` (D0, D1,
D2), the persistence design (D9, D10), the standalone-`Material_set` shape (D0,
D2, D5), and the phase and test lists as rewritten around all of them. The
citations were re-checked against the tree twice; the second pass also fixed
eighteen internal inconsistencies in the plan and left the design unchanged.
Citations neither pass touched are still unverified - if one looks wrong, check
it.

**Open questions that outlived the implementation:**

1. **The two-set split (D0) was never independently reviewed.** It replaced a
   single per-root set after the review rounds above. Both specific worries
   have since been checked in practice: every invalidation reaches both sets
   (measured - one direct `Material_data` edit dirties both, exactly once
   each), and the footprint doubling is two copy buffers and two heaps per main
   root, with the heaps now always full-width (see the D2a correction above).
2. **Two follow-ups the `Scene_pass_resources` extraction unblocked**, neither
   of which this plan depended on: stop `Shadow_renderer` duplicating the same
   six buffers, and stop `Id_renderer` borrowing a joint buffer through
   `Forward_renderer::get_joint_buffer()`.

Settled, and recorded here so they are not re-opened: D10's content hash (now
generated from one list with the writer, above); the R8a amendment (done, in
`doc/draw_list_renderer_requirements.md`); whether phase 4 could be split (it
was - the prerequisite split, the switch, and the D11 cheap path are three
commits).

**Phase 7, what has been run** (2026-09-01, Windows):

- Builds, Debug: Vulkan (ninja), OpenGL (`build_tests`), null/headless
  (`build_vs2026_null_backend`). **Quest not built or run** - it needs the
  headset and a fresh explicit confirmation.
- `ctest` over `build_tests`: **668 pass, 0 fail** (25 disabled, 1 pre-existing
  skip).
- `mcp_server_tests`: **42 pass, 5 pre-existing skips**; V3 green in both
  assignment orders.
- Vulkan validation layers on, DDGI on (256 probes), 200 frames: **no
  validation errors**.
- `src/example/example.exe`: runs and renders textured glTF, no errors (V4.12).
  It writes its material buffer exactly once by construction - one
  `sync_library` + `update` in the constructor, nothing in its loop.
- ID picking through the null set (V4.9): `pick_at` resolves correctly.
- **The R10 / R11 measurement**: default scene, 14 materials. Each of the
  root's two sets writes once (3584 bytes) and then nothing - zero material
  bytes and zero heap resets per steady-state frame, against a whole-buffer
  rewrite plus a heap reset per pass per frame before. `get_draw_lists` reports
  the counters.
- **V4.5a's central property**: editing one material's base colour directly on
  the `Material` object, with no notification, dirties **both** of the root's
  sets exactly once each.

**Phase 7, the interactive sweep** (user-verified on the desktop, 2026-09-05):
the V4 checks - live slider / picker dragging across every `Material_data`
field (V4.2), texture re-bake (V4.3), cross-scene assignment (V4.4), preview
churn with the hotbar and inventory open (V4.6), brush preview (V4.7),
lightmap streaming (V4.8), BRDF slice window (V4.10), async glTF import and
its undo (V4.11) - and the real-mouse material paint / item-tree material
drop with Ctrl+Z taking each back (`Mesh_material_assign_operation`).
Not run and not planned: V5's A/B screenshots (the pixel comparison needs a
build where swapchain readback works) and the OpenGL sampler-array
(non-bindless) heap path on its own.

**Quest: verified 2026-09-01**; the rest of phase 7 on Quest is skipped by
the user's call - passing on desktop gives high confidence for Quest. What
ran: a fresh uninstall + clean reinstall, user-verified visually in the
headset, plus the draw-list assertions driven against the Quest editor over
MCP (`adb forward tcp:3743`; `material_index == material_set_slot` held
through assign / undo / redo). Memory `project_quest_mcp_access` has the
recipe and the two false-negative traps.

**Verification recipe.** Build `editor` and the test targets in `build_tests`
(configured **OpenGL**, so the Vulkan path needs `scripts/build_ninja_win_vulkan.bat`
as well). For V3: launch `build_tests/src/editor/Debug/editor.exe`, wait on
`127.0.0.1:3743/health`, then run `mcp_server_tests.exe` with
`ERHE_MCP_TEST_TIMEOUT_S=1` (`scripts/run_mcp_tests.ps1` does not forward
`--gtest_filter`). Baseline: 42 pass, 5 pre-existing skips, V3 green.
`erhe_scene_renderer_tests` 20 pass (V1, deviceless),
`erhe_scene_renderer_gpu_tests` 10 pass (V2, device-backed; built only where
`erhe::gpu_test_support` exists), `erhe_graphics_gpu_tests` 75 pass + 1
pre-existing skip. Running the editor rewrites
`config/editor/editor_settings.json` - revert it before committing. For a quick
smoke run of the whole frame, set `quit_after_frames` in that file and revert
it afterwards.

**Traps found while implementing, which the next session would otherwise hit
again:**

- `Buffer::begin_write(offset, count)` **ignores its offset** on the
  persistently mapped path and returns the whole-buffer map. Write at an offset
  through `get_map().subspan(offset, count)` plus `flush_bytes()`, or through
  `upload_sub_data()` - the two paths `Ring_buffer` takes.
- The GL backend plants a frame fence only when something requested a sync, so
  anything derived from fence completion alone stalls for a consumer that
  requests none. `wait_idle()` advances the watermark for that reason.
- `erhe_graphics_tests` is the **deviceless** target; anything needing a
  `Device` belongs in `erhe_graphics_gpu_tests`, which is not built for
  `ERHE_GRAPHICS_API=none`.
- The material preview's churn pattern settles at **two** slots, not one,
  because the new material is referenced before the old one is released.
- **`App_rendering` cannot own anything a scene root or a preview needs from
  its constructor.** It is built inside `Editor`'s construction taskflow, and
  so are they, with no edge ordering them; `App_context` is filled after the
  whole graph runs. Anything in that position goes before the taskflow and is
  published into `App_context` on the spot - which is what
  `Material_set_factory` does.
- `erhe::graphics::Texture` **is itself a `Texture_reference`** that returns
  itself, so a plain texture needs no wrapper; `Texture_reference` is abstract
  and cannot be constructed directly.
- The MCP test harness's readiness probe is `/health`, which answers 200
  only once the main loop serves requests (503 before). Every case prepares
  its own scene over MCP, so no test depends on the default scene existing.
- `nlohmann::json{value}` builds an **array**, not a scalar. Assign the value
  directly (`e["k"] = v;`) when writing a number into an MCP payload.

## 1. Motivation

**This section describes the code as it stood BEFORE the plan was implemented**,
and is kept in that tense deliberately: it is the root-cause analysis the design
answers, and every mechanism it names has since been removed. Read it for why
the design is shaped as it is, not for what the tree does now - the status
section above owns that. Line citations are against the pre-fix tree.

### 1.1 The reported bug

With material *Gold* selected in the material panel and its base colour edited
to red, dragging Gold onto a cube turns the cube red, but then dragging the same
Gold onto an icosahedron leaves the icosahedron nearly unchanged. Dragging in
the opposite order moves the failure to the other mesh.

Root cause: `erhe::primitive::Material::material_buffer_index`
(`erhe_primitive/material.hpp:119`) is a single mutable field on the shared
material object, rewritten by whichever `Material_buffer::update()` ran most
recently (`material_buffer.cpp:175` assigns the material's position in the list
it was handed). There are five call sites:

| Call site | Material list source |
| --- | --- |
| `erhe/scene_renderer/.../forward_renderer.cpp:165` (`begin_pass`) | `Base_render_parameters::materials` |
| `erhe/scene_renderer/.../forward_renderer.cpp:461` (`draw_primitives`) | `Base_render_parameters::materials` |
| `erhe/scene_renderer/.../shadow_renderer.cpp:394` | `Shadow_render_parameters::materials` |
| `editor/renderers/ddgi_renderer.cpp:997` | scene root's content library |
| `editor/renderers/ray_trace_renderer.cpp:463` | scene root's content library |

and the lists reaching those parameters differ within one frame: viewport passes
pass the scene root's whole content library
(`editor/renderers/composition_pass.cpp:160`), shadow passes pass the same list
assembled separately by `Shadow_render_node`
(`editor/rendergraph/shadow_render_node.cpp:469-470`, used at `:601`), the
material preview passes its own library holding exactly one material
(`editor/preview/material_preview.cpp:226-230`), the BRDF slice window passes an
ad-hoc one-element list (`editor/content_library/brdf_slice.cpp:116`), and two
passes pass an empty list (`composition_pass.cpp:201`,
`editor/developer/depth_visualization_window.cpp:149`).

`Editor::tick()` renders thumbnails (`editor.cpp:766`) and imgui windows
(`editor.cpp:664`), both of which render the material preview, *before*
`flush_draw_lists()` (`editor.cpp:796`). So when the draw list writes a cached
record it reads a `material_buffer_index` the preview just set to 0
(`draw_list_scene.cpp:458`).

`Draw_list_scene::sync_gpu_slots()` (`draw_list_scene.cpp:593-645`) repairs this
only by *edge detection*. `Material_watch::slot` (`draw_list_scene.hpp:308`) is
initialised only when the watch is new (`use_count == 0`,
`draw_list_scene.cpp:740-744`) and thereafter rewritten by `sync_gpu_slots()`
itself on every detected change (`draw_list_scene.cpp:605-607`). The first mesh
to receive the material is therefore repaired on the next draw, leaving the
watch holding the correct slot. A second mesh registered later writes its record
from the same preview-clobbered index, the watch already holds the post-repair
value, no difference is detected, and that record keeps the wrong slot
indefinitely. Hence the order dependence.

**Scope note.** Only `Draw_list_scene`'s *cached* records are stale today. The
other readers of `material_buffer_index` run inside the pass, after that pass's
own material update, and are correct as things stand:

- `light_buffer.cpp:567` - `Light_buffer::update` is called from
  `forward_renderer.cpp:173`, after the material update at
  `forward_renderer.cpp:165`.
- `scene_tlas.cpp:295` - `Scene_tlas::update` clears and rebuilds
  `m_instance_records` on every call (`scene_tlas.cpp:227,298`), and both
  callers update the material buffer immediately before
  (`ray_trace_renderer.cpp:463` then `:468`; `ddgi_renderer.cpp:997` then
  `:998`).
- `primitive_buffer.cpp:359` (`write_primitive`) - written during the pass. (The
  other reader, `primitive_buffer.cpp:226`, is in the `meshes` overload at
  `:107`, which has no caller anywhere in the repo and is dead code.)

They are converted anyway because the plan removes the field (D0, phase 6), not
because they are broken. The shared mutable field motivates the *scope*; the
draw-list record motivates the *urgency*.

**A second defect the content library does not cover.** The library is
populated at *mesh registration* only, so assigning a material to an
already-registered mesh never reaches it (the plan's R3 and R4; the full chain
is D11). The notification it does raise,
`Scene_root::on_mesh_material_changed` (`scene_root.cpp:1440-1445`), enqueues a
draw-list re-register, which is a no-op for a root with no draw list - which is
why the plan gives *every* root a forward `Material_set` fed by that same hook
(D1c), not a draw list. So "the set contains exactly the library's materials" is
not a safe definition of membership (D1).

### 1.2 The second motivation: the buffer is rewritten far too often

Materials are near-static data. Today the whole material buffer and the whole
texture heap are rebuilt from scratch **once per pass**: `reset_heap()` +
`Material_buffer::update()` at `forward_renderer.cpp:121,165`, again at
`:458,461` for `draw_primitives`, again at `shadow_renderer.cpp:393-394`, again
per DDGI tick and per ray-trace dispatch. For a scene with a few hundred
materials that is a few hundred kilobytes of ring-buffer writes, a few hundred
`Texture_heap::allocate()` linear searches, and one fresh Vulkan descriptor set
(`vulkan_texture_heap.cpp:289-298`) *per pass*, every frame, to reproduce bytes
that are almost always identical to the previous frame's.

Once slots are stable (D1, D9) the buffer contents are a pure function of the
set's membership and of each member's `Material_data`. Both change rarely. So
the buffer becomes persistent: written when it is invalidated, rebound
otherwise (R10).
