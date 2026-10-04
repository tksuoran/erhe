# Architecture and API Audit -- erhe (2026-09-30)

- **Date**: 2026-09-30
- **Auditor**: Claude Code (Fable 5.1), six read-only slice audits run in
  parallel, synthesized here
- **Baseline**: `main` at c982af6d6 (four slices) and 7445e65e6 (graphics
  and scene slices; the two commits differ only by the audit documents)
- **Scope**: the `erhe::*` libraries and the editor, judged on architecture
  and API design: layering, ownership, abstraction quality, API ergonomics,
  adherence to the project's own rules (`AGENTS.md`), test coverage, and
  options for future development. The 2026-06-21 audit
  (`doc/reference/audit_erhe_2026_06_21.md`) covered foundations, DRY/KISS
  and security; this audit does not repeat it, but section 7 checks its
  recommendations.
- **Evidence**: every claim in the slice reports carries a `file:line`
  reference that the auditing agent verified by reading the code. Counts are
  scoped to own code under `src/erhe/` and `src/editor/`.

## Slice reports

| Slice | Report | State |
|---|---|---|
| Editor application (`src/editor` except renderers) | `doc/reference/audit_erhe_2026_09_30_editor.md` | complete |
| Rendering pipeline (rendergraph, renderer, scene_renderer, raytrace, texgen, editor renderers) | `doc/reference/audit_erhe_2026_09_30_rendering.md` | complete |
| Infrastructure (platform libs, build, tests, docs, layering, statistics) | `doc/reference/audit_erhe_2026_09_30_infra.md` | complete |
| Roadmap and positioning (plans, effort, capability matrix, options) | `doc/reference/audit_erhe_2026_09_30_roadmap.md` | complete |
| GPU foundation (`erhe::graphics`, `gl`, `dataformat`, `buffer`, `codegen`, shaders) | `doc/reference/audit_erhe_2026_09_30_graphics.md` | complete |
| Scene and data model (`item`, `scene`, `property`, `primitive`, `geometry`, `gltf`, `usd`, `physics`, `math`, `commands`) | `doc/reference/audit_erhe_2026_09_30_scene.md` | complete |

## Status of the section 8 options (2026-10-04)

The audit text below is the 2026-09-30 snapshot and is not rewritten; the
slice reports stay verbatim. This table records which section 8 options have
been worked since, by commit. Remaining work of the worked items and the
next selected items are in `doc/plans/audit_2026_09_30_followups.md`.

| # | Option | State | Commits |
|---|---|---|---|
| 1 | Cheap infrastructure items | Done: concurrentqueue and cpp-terminal pinned by hash, one GoogleTest pin, `erhe_smoke` under ctest, the base-layer leaks removed (`erhe_physics` via `IDebug_draw`, XR in `erhe_commands` only with OpenXR, `erhe_log` without geogram, `erhe_utility` without SDL, dead edges dropped), `ERHE_FATAL` / `ERHE_VERIFY` reported to `logs/log.txt` | 238830d45 |
| 2 | Delete the dead code | Done: dead sources and the 41 in-tree review / readme files deleted, `Node_data::diff_mask` removed, the fatal copy constructors `= delete`, non-ASCII lines and stale doc snippets fixed | 428368553 |
| 3 | glTF unit tests | Done: `erhe_gltf_tests` (device-free parse and GLB round trips); the `variants.gltf` bug fixed at its root | 7f2335b3a |
| 4 | Steady-state allocations and per-frame pushes | Done for the section 5 list; `Light` getters read a mirror | c14cf38e1 |
| 5 | Route every mutation through operations | Done, with `Mcp_test.document_edits_record_one_undo_entry_each` | 5da541602 |
| 6 | Shared `Ring_buffer_pool` and Metal parity | Pool done for all three backends; the Metal parity items (timers, `blit_framebuffer`, resize) are open and need macOS | 64cce60f6 |
| 7 | Vestigial taskflow annotations | Done | ce897bfe6 |
| 8 | Plan hygiene | The eight drifted plans rewritten; deleting plans was declined by the user | ac3f76786 |
| 9 | Frustum culling on draw-list entries | Done: color passes cull on the entry AABB; the shadow passes cull against each light frustum and the fit reads the entries' bounds; the mesh-component drag keeps bounds current | e476817cf, 55923790f |
| 10 | Per-subresource Vulkan layouts, blit region types | Done, plus the review follow-ups and the OpenGL failures the GPU tests found | 4fc17d988, 1d5d45f28, 51dfc09fb, e35933c80, 8628689ad, 5ef90f13c, e80b64ab2, 6837a428f, cafc0773e |
| 11 | Editor bits out of `Item_flags` / `Item_type` | Done: application ranges with registered label / name tables, `hosted_selection` in `Selection`, the tables in their own headers (`doc/erhe/item.md` "Application bits") | 414c4e285 |
| 12 | Include diet, interface / backend split | Done | afbe3d5b5, 5342524d6, b9eb1061b |
| 17 | Sanitizers, `-Werror`, CI GPU tests, version | `ERHE_USE_ASAN` applies with GCC and Clang; the CI job, `-Werror`, CI GPU tests and version embedding are open | 091b5879f |
| 18 | Shader pipeline persistence | glslang SPIR-V cache key and atomic writes, persisted `VkPipelineCache` done; Metal `MTLBinaryArchive` open, needs macOS | f9ab02789, b67d84982 |

Options 11, 13-16, 19-27 have not been started. Of the 2026-06-21
recommendations in section 7, number 2 (pin concurrentqueue) and number 4
(centralize GoogleTest) are done by 238830d45.

## 1. Executive summary

erhe is a 340k-line library set plus a 230k-line editor, developed by one
author at a commit rate that quintupled in mid-2026 when the agent-driven
workflow began (1076 commits in September). The code that carries the
engineering weight is good: the GPU memory and ring-buffer model, the
pimpl backend seam, the property system, the transform-propagation funnel,
the draw-list renderer, shadow verification, the build and CI hygiene and
the lifetime discipline in the editor are all above what a solo research
project normally has. The weaknesses are structural and consistent across
the six slices:

1. **Two service locators carry the editor.** `App_context` holds ~106
   pointers and every part reaches every other part through it; `Editor` is
   a 4000-line local class whose constructor is ~1580 lines. The render
   layer inherits the same shape through `Render_context`, which hands
   `App_context&` to every tool, renderable and pass. The library layer
   (`erhe::scene_renderer`) stays clean of editor includes; the coupling is
   all one way, which makes it fixable.
2. **Editor state is baked into the library data model.** About 22 of the
   52 `Item_flags` bits and 24 of the 51 `Item_type` indices name
   editor-only state or classes, and the editor's selection bucket lives in
   `Item_host`. `erhe::item` cannot be reused by another application without
   editing its tables. Naming a `Node` pulls in 19 erhe headers (the whole
   property system, 3.9k lines); naming a `Mesh` pulls in geogram and
   `texture.hpp` (6.9k lines).
3. **The neutral GPU API is the union of its backends.** The pimpl seam is
   clean (no backend header leaks out, encoders are heap-free), but the
   neutral enums carry Vulkan image layouts and VMA flags beside literal GL
   barrier bits, `Device_info` and `Surface` change shape under `#ifdef`,
   and there are 131 API `#ifdef` lines in neutral sources plus 79 in 21
   consumer files. One known correctness hole is open: one tracked
   `VkImageLayout` per texture while blits transition from `UNDEFINED`.
4. **The render graph is a node-ordering helper.** `Rendergraph` sorts
   nodes and calls them; there is no resource table, lifetime analysis,
   aliasing or barrier derivation, and consumers reach shadow data by
   `static_cast` of the producer node. It works because every pass is
   hand-wired, and it will keep working until a pass needs transient
   resources shared across nodes.
5. **The project's own rules are broken in the hot path.** The
   "no per-frame allocation" and "no update each frame" rules from
   `AGENTS.md` are honored in the newest code (shadow scratch buffers,
   draw-list change serials, `Scene::update_node_transforms`) and violated in
   older code that every frame still runs: `std::vector<Render_bucket>`
   locals per pass, `fmt::format` debug labels per bucket, editor settings
   pushed into renderers each tick, gizmo render paths allocating, `Light`
   walking five inherited properties per `is_active()` call per frame.
6. **Four change-notification designs coexist.** 24 editor message buses
   (none used by any `src/erhe` library), `Scene_host` virtual callbacks,
   `INode_system`, and property/transform observers. Mutations do not all go
   through operations either: MCP `edit_light` / `edit_camera`, lightmap
   tile overrides and brush forking mutate the document directly while the
   Properties window uses `Property_set_operation`; there is no operation
   merging, `Compound_operation` has no rollback, and closing any scene
   drops the global undo history.
7. **The native file format has no unit tests.** `erhe::gltf` is 9.3k lines
   verified only by an editor-driven round-trip script; `erhe::message_bus`,
   `erhe::gl` and `erhe::buffer` have no tests either. The GPU golden suite
   (195 cases) does not run in CI.
8. **The MCP server is the largest editor subsystem** (30k lines, 272
   statically dispatched tools, ~300 handler declarations in one class over
   17 files) and is mostly hand-written argument parsing and serialization
   that the property metadata could generate.
9. **Cheap infrastructure items stay open across audits.** Of the ten
   2026-06-21 recommendations, four are done or partly done; the two
   one-line pins (concurrentqueue on `master`, 25 copies of the GoogleTest
   pin) are still open, there is no `-Werror` on Clang/GCC, no version
   embedding, no sanitizer CI job. Twelve dead files sit under
   `src/erhe/graphics` outside CMake (one a stale non-compiling duplicate of
   `pipeline.cpp`), and 41 tracked `claude_review.md` / `Readme.md` files
   sit beside scene-layer sources, some describing a Node/attachment model
   that no longer exists.
10. **Vision and investment diverge.** XR is named first in the vision and
    funded last (~3% of commits); human-facing editor UX (UV editor,
    timeline) is unstarted while agent-facing tooling grew to 272 tools;
    the three secondary consumers are build canaries, not clients, and
    there is no install target, version tag or library-consumer document.

## 2. Strengths

Specific, verified in the slice reports:

- **GPU memory and frame safety.** `Pool_block` retires instead of frees
  and is applied from the frame-completion handler, so the multi-draw
  base-vertex lockstep holds by construction; ring-buffer completion is
  driven by the device frame fence, one mechanism for every per-frame buffer
  including CPU readback shadows. On Vulkan the ring-buffer pool spills and
  reclaims (graphics report 1.5), which corrects the rendering report's
  "no grow path": true of `Ring_buffer::acquire`, not of the device.
- **The backend seam.** Compile-time pimpl over Vulkan, OpenGL, Metal and
  null with no backend header leaking out (grep clean), heap-free encoders
  (`pimpl_ptr<Impl, 128, 16>`), usage-derived barriers, command-buffer-named
  sync objects, one uniform deferred-destruction path (29 sites),
  reflection-free `Shader_resource` layout math that is unit tested, shader
  defines limited to capabilities and workarounds, and exhaustive categorized
  logging (graphics report 4).
- **The property system** (scene report 1.3): a WPF-shaped dependency
  property system with one precedence walk, sealing, an animated layer,
  expressions, styles and references, snapshots on reparent, default elision
  on import, and 148 tests. MCP `set_item_property` is already a reflective
  binding over it.
- **`Scene::update_node_transforms`** is the reference implementation of the
  no-allocation rule: one funnel for every transform write, capacity-kept
  scratch, a `static_cast` walk and sampled statistics (scene report 1.2,
  3.6).
- **`Scene_pass_resources` / `Base_render_parameters`** describe the pass
  prologue without naming a renderer, so bucket, draw-list and fullscreen
  paths share one bind sequence. This is the seam every rendering proposal
  in section 8 builds on.
- **`Draw_list_scene`**: incremental, main-thread-owned,
  enqueue-from-anywhere, with change-serial gated material re-derivation
  and counters for every maintenance path, each cross-referenced to a
  numbered requirement in `doc/erhe/draw_list_renderer.md`.
- **Multiview** reaches the camera UBO and the compute passes as a span of
  view inputs, so XR did not fork the renderer.
- **Shader variants**: one uber shader with an X-macro key, a variant cache
  and a startup prewarm that mirrors the runtime key exactly.
- **Editor lifetime discipline**: a 60-frame scene-close leak watchdog, an
  O(1) `Items_removed` contract, operation reference collection for the
  asset manager, main-thread verification, and a `get_editor_references`
  MCP tool with a smoke test. The part-construction rule in
  `doc/editor/coding_rules.md` is applied consistently.
- **Change-driven persistence** of settings and input bindings, one
  property-consequence mapping (`app_context.cpp`), explicit per-message
  dispatch policy with rationale, async asset loading with frame budgets and
  one commit point at the top of `tick()`.
- **Build and dependency hygiene beyond the norm**: closed-list options,
  SHA256-pinned downloads, SDL sync-pin guard, dependency commits embedded
  as defines, a written fork policy naming the upstream version that would
  retire each fork, codegen that understands ninja restat semantics.
- **CI**: seven build matrix entries plus a Quest APK job, all running
  ctest with JUnit upload, per-test and per-step bounds, a line-flushed log,
  and a structural guard (`check_task_spawns.py`) for an invariant the type
  system cannot express.
- **Verification culture**: FLIP golden images with a per-backend coverage
  matrix, MCP fixture editors under ctest, 57 Python verification scripts
  including a full glTF round-trip, measured gates recorded in the shadow,
  material-set and post-processing documents; 405 USD tests and 195 item
  tests at the library level.
- **Documentation** is mechanically checked and clean; every library but
  one (`erhe::task`) has a document; the assertion vocabulary
  (`ERHE_VERIFY` / `ERHE_FATAL`, live in Release) and logging categories are
  consistent.

## 3. Architecture findings

### 3.1 Layering (infra report, section 1; confirmed by graphics 1.9 and scene 1.11)

The dependency graph derived from every `target_link_libraries` is a clean
ten-level stack from leaf utilities to `scene_renderer` and `imgui`, with
four leaks in the base layer: `erhe_log` links geogram (for
`log_geogram.hpp`), `erhe_utility` links SDL3 (for `clipboard.cpp`),
`erhe_rendergraph` links `erhe::ui` without using it, `erhe::ui` links
`erhe::primitive` for one include, and `erhe_commands` links `erhe::xr`
unconditionally. `erhe_physics` links `erhe::renderer` for its debug
renderer, which puts a physics library above the render layer; the scene
report finds an unused `IDebug_draw` interface already in place that would
remove the edge. The scene report adds: `erhe_item` links `erhe::message_bus`
without including it, `erhe_gltf` makes `erhe::scene` PUBLIC contrary to its
document, and `texture.hpp` in `erhe::graphics` drags 2348 lines of item and
property headers into every graphics consumer. 44 library directories is
more than the code needs: `defer`, `hash`, `message_bus`, `utility`, `smoke`
and `task` are each under 400 lines.

### 3.2 GPU abstraction (graphics report, section 1)

One library, four backends, compile-time selection; `Device` is a 300-line
god class that also owns frame pacing and OpenXR handles. The neutral API is
honest about being "Vulkan-style" and disciplined about includes, but it
carries backend concepts in both directions: `Image_layout`, VMA flags and
`view_mask` from Vulkan; literal GL barrier bits converted by `static_cast`
under a "TODO Proper conversion". Synchronization is correct and
conservative: one queue, one layout per image, one global barrier per pass;
frames in flight are 2 (Vulkan), 3 (GL), 3 (Metal), 2 (null). The ring-buffer
allocator is copied into all three backends and only Vulkan spills and
reclaims. Error handling is "abort on programmer error, message on data
error": 662 `ERHE_VERIFY`, 99 `ERHE_FATAL`, 71 bare `abort()`, one `throw`;
`Buffer`'s constructor is `noexcept` and aborts. Metal parity gaps: GPU timers
read 0, `blit_framebuffer` is fatal, no present-wait, no ring reclaim,
CPU-looped multi-draw indirect. Four snippets in `doc/erhe/graphics.md` no
longer match the code.

### 3.3 Scene and data model (scene report, section 1)

The object model is a USD prim class chain (`Item_base -> Hierarchy -> Typed
-> Imageable -> Xformable -> Xform | Boundable -> Gprim -> Mesh`) with camera
and light as child prims; `Node` is an alias slated for retirement, and the
Node/attachment model the audit brief still names no longer exists. Ownership
is shared pointers down the hierarchy with weak parents; the
`item_host_mutex` discipline is comment-only, with 11 uses in `src/erhe`
against 51 in `src/editor`. The property system solves per-node overrides,
styles and animation, but four member/property arrangements coexist
(bridged, mirrored, direct-read, derived flag bits), `Mesh_primitive` keeps
public members beside "the one writer" property, and an `Xform` costs on the
order of 800 bytes by member inspection. glTF is the native format and
round-trips what erhe holds through 9 `ERHE_*` extensions; USD is a 20.5k-line
importer/exporter over a fork that builds on two platforms. `erhe::math` is
a thin layer over glm. 24 editor message buses exist and none is used by a
library.

### 3.4 Render graph and frame composition (rendering report, section 1)

`Rendergraph` owns a node vector, a topological sort and a deferred-resource
list. Edges carry an integer key and nothing else; a consumer walks the
first link of the matching input pin and asks the producer for its output
texture, so a key with several producers is silently truncated to one.
Shadow data travels by `static_cast<Shadow_render_node*>`. The editor
bypasses the graph in several places (ID renderer, preview, capture),
`Composition_pass::render` mutates another pass's `primitive_settings` on
every call (marked "a bit hacky" in the code), and both draw paths author
the indirect buffer CPU-side every frame with no culling of any kind on the
color path.

### 3.5 Editor object model (editor report, section 1)

`App_context` (`app_context.hpp:141-347`) is a 106-pointer service locator
with stale members for types defined nowhere. `Operation::execute(App_context&)`
gives all 63 operation classes the same universal escape hatch. Part
construction is annotated with taskflow `.name().succeed(...)` chains that
`ERHE_TASK_FOOTER` discards; initialization is 25 serial blocks with
comment-enforced ordering, and one header still claims parallel
initialization. Beyond `Editor` and `App_context`, `Scene_root` (45 members,
99 methods, 2733 lines), the Operations window (86 methods spanning geometry
operations, save/load/export and material/joint/brush creation) and
`Mcp_server` are god classes. Light creation exists in four places with
different defaults. XR is threaded through 117 `ERHE_XR_LIBRARY_OPENXR`
sites in 40 files, and nine parts take `Headset_view&` in their
constructors.

### 3.6 Undo, mutation paths and the MCP surface

The MCP `set_item_property` tool and the Properties window share
`Property_set_operation`, which shows the intended design; `edit_light`,
`edit_camera`, lightmap tile overrides and brush forking do not use it.
There is no operation merging and no per-scene undo history. The MCP
server's queue, main-thread drain, deferral protocol, undo groups, health
and auth design is sound; its size is the problem, and the property
metadata that already drives `set_item_property` could replace most
per-type handlers. The scene report notes that `read_local_state` /
`apply_local_state` already capture the exact local layer per property, which
is the hook for data-model-level undo.

### 3.7 Node-graph editors

`geometry_graph` and `texture_graph` share `Graph_editor_window_base` with
templated serialization and operations; the shader graph under `graph/`
bypasses the base entirely. `texture_node_descriptors.cpp` is 4269 lines of
data expressed as C++ (one 918-line function, 2852 string literals).

## 4. API findings

Recurring across slices, with counts from the reports:

- **Boolean parameters** despite the `AGENTS.md` rule: 76 in editor
  headers, 17 in rendering-slice headers, 16 in `erhe::graphics` public
  headers, 50 in the scene slice, and in base-layer APIs
  (`make_logger(name, bool tail)`, `Imgui_host(..., bool imgui_ini, ...)`).
  `Shadow_render_node::reconfigure` takes eight positional scalars including
  a bool.
- **Wide signatures**: six public graphics functions take 7-9 parameters
  (blit copies, `upload_to_texture`, the `Shader_resource` sampler
  constructor, `get_pipeline_for`); the `Render_pass` factory takes 15.
- **`struct` instead of `class`**: 48 declarations in the editor, 22 of them
  in `app_message.hpp` mixed with `class`; 14 in `erhe::graphics` plus 8 in
  its sibling libraries; 56 in the scene slice.
- **`auto` locals**: 1018 in the editor slice, about 320 in graphics, 421
  in the scene slice.
- **Header weight**: `scene/scene_view.hpp` includes `<geogram/mesh/mesh.h>`
  (37 includers); `tools/tools.hpp` includes 12 concrete tool headers (30
  includers); `scene_root.hpp` is included by 128 files; `device.hpp`
  includes 13 headers at its top; `commands.hpp` pulls volk and Tracy through
  `erhe_profile`; `Node` and `Mesh` consumers pay 3.9k and 6.9k lines.
- **Hidden ordering requirements** in the render layer (`m_light_projections`
  must be cleared before any early exit; `defer_resource` for mid-frame
  resizes) are documented where they live, which is the right mitigation,
  but they remain conventions rather than types.
- **Public mutable state**: renderer flags (`Id_renderer::enabled`),
  `Mesh_primitive` members, `Gpu_timer` hooks; duplicated concepts (several
  viewport/camera notions in the editor; four light-creation sites; four
  transform representations and two name/id notions in the scene slice;
  three copies of the ring-buffer allocator).
- **Backend enums leaking**: Vulkan image layouts and GL barrier bits in the
  neutral `enums.hpp`; `Texture_create_info::device` is redundant with the
  device the texture is created on.
- **Copy semantics**: 17 scene-layer copy constructors that
  `ERHE_FATAL("TODO")` instead of `= delete`; `Scene` copy is marked "This
  probably won't work".
- **Dead header**: `texture_renderer.hpp` has missing includes and no user.

## 5. Code health

| Metric | Value | Source |
|---|---|---|
| Own code under `src/erhe` | 393,567 lines (about 92k vendored single-file libs, about 70k tests) | infra 0 |
| `src/editor` | 230k lines; `mcp/` 30k is the largest directory | roadmap 0 |
| Scene slice | 102k non-test lines; usd 20.5k, geometry 18.6k, physics 13.4k, scene 12.0k | scene 0 |
| Largest renderer files | `lightmap_baker.cpp` 6120, `radiance_cascades_renderer.cpp` 2909, `ddgi_renderer.cpp` 1939 | rendering 3.1 |
| Largest graphics functions | Vulkan `Device_impl` constructor 2300 lines, `Render_pass_impl` constructor 774 | graphics 3.1 |
| Longest editor functions | `build_japanese_glyphs` 918, `Headset_view::render_headset` 689, `Lightmap_window::imgui` 668 | editor 3 |
| TODO/FIXME | editor 186 (34 in `physics_window.cpp`); graphics 62; scene slice 100; 257 commented-out lines and 17 `#if 0` in the editor | editor 3, graphics 3.4, scene 3.4 |
| Dead files | 12 under `src/erhe/graphics` outside CMake; 41 review/readme files beside scene-layer sources | graphics 3.3, scene 3.3 |
| Non-ASCII source lines | 10 in `erhe::graphics` | graphics 3.4 |
| `printf` family | only `crash_handler` and `--help` (defensible) | editor 3 |
| Doc/code mismatches | 4 stale snippets in `doc/erhe/graphics.md`; 5 in item, scene, gltf and graph documents | graphics 3.6, scene 3.7 |
| Plans | 50 documents, 9182 lines; 40 proposed, 10 in progress; 8 drifted from the code | roadmap 1 |

Steady-state allocations found on the frame path (rendering report 1.10,
editor report 7, scene report 3.6): bucket vectors per pass and per light,
per-bucket `fmt::format` labels, `filtered_meshes` per ID render, XR
view-input vectors per frame, lightmap override vector per tick,
pending-vector swaps that discard capacity, gizmo/handle visualization
vectors in six tool files, and `Light::is_active()` resolving five inherited
properties per call. `erhe::graphics` itself is clean in its hot paths
(graphics 3.5).

## 6. Test coverage

| Area | Coverage | Gap |
|---|---|---|
| GPU foundation | 7 deviceless std140/std430 tests in CI; `erhe_graphics_gpu_tests`, 195 `TEST_F` cases (176 pass on headless Vulkan) with FLIP goldens, one golden set for all backends; Metal, macOS Vulkan and OpenGL runs recorded | not run in CI (no software Vulkan there); swapchain/present/pacing, ring-buffer pool, texture heap, hot reload, timers, texture views, multiview, fragment density maps untested; `erhe::gl` and `erhe::buffer` have no tests |
| Scene and data model | item 195, scene 167, property 148, geometry 148, usd 405 (build-gated), physics 117, math 64, primitive 46, graph 19, commands 15 | `erhe::gltf` 0 and `erhe::message_bus` 0; `update_node_transforms` ordering, `Primitive_builder`, command dispatch, `Aabb`/`Sphere` untested |
| Rendering | shadow GPU fixture, wide-line AA, material set, raytrace (7 files), texgen (8 files), RC layout | `Rendergraph`, `Forward_renderer` bucketing, `Draw_list_scene`, per-frame buffers, ID renderer, post-processing, DDGI, sky, `Lightmap_baker` (6k lines) have no direct tests |
| Editor | 87 `mcp_server_tests` cases under ctest fixtures; 141 unit tests (IK 55, rig 37, assets 17, brushes 15, renderers 17); 57 verification scripts | `Operation_stack`, compound failure, the undo holes, XR, shader graph, most of `windows/`, the startup-script interpreter |
| Infrastructure | frame pacer with a Python reference model; 27 test directories; all 7 CI entries run ctest | `erhe::net` framing, ring buffers, `erhe_smoke` not registered with `add_test`; no sanitizer CI job |

## 7. Follow-through on the 2026-06-21 audit

| # | Recommendation | Status |
|---|---|---|
| 1 | Run tests in CI | Done and exceeded (all matrix entries, JUnit, bounded) |
| 2 | Pin concurrentqueue | Open (`GIT_TAG master`) |
| 3 | Rotate the surfaced key, keep `.mcp.json` untracked | Done as far as the repo shows |
| 4 | Centralize the GoogleTest pin | Open; now 25 copies |
| 5 | Embed erhe version and git hash | Open |
| 6 | `-Werror` on Clang/GCC, format/tidy configs | Open |
| 7 | Fast/slow test labels, sanitizer CI | Partly (labels exist) |
| 8 | Refactor `Mcp_server` | Split into 21 files, still 23.5k lines |
| 9 | Upstream fork patches | No evidence |
| 10 | Typed error type | Open |

## 8. Future development options

Each slice report ends with a prioritized list with cost, benefit and the
code that helps or hinders. The cross-slice ranking, by benefit over cost:

### Near term (days each, low risk)

1. Close the cheap infrastructure items: pin concurrentqueue and
   cpp-terminal, centralize GoogleTest, register `erhe_smoke`, fix the
   base-layer layering leaks (including `erhe_physics` via the existing
   `IDebug_draw`, the unconditional XR includes in `commands.hpp`, the
   `erhe_item` message-bus link), route `ERHE_FATAL` through erhe logging.
2. Delete the dead code: the 12 files outside CMake under
   `src/erhe/graphics`, the 41 in-tree review/readme files, `imotion_state.hpp`,
   `Node_data::diff_mask`; turn the 17 fatal copy constructors into
   `= delete`; fix the 10 non-ASCII lines and the 9 stale document snippets.
3. glTF unit tests: `parse_gltf` is device-free and `export_gltf` returns a
   string, so build, export, parse and compare needs no editor; start with
   the `ERHE_node.properties` path and `data/variants.gltf`, which already
   reproduces a bug.
4. Remove the steady-state allocations and per-frame settings pushes listed
   in section 5; the scratch-buffer pattern to copy already exists in
   `Shadow_renderer` and `Scene::update_node_transforms`; cache `Light`'s
   inherited reads the way `Camera` and `Scene` do.
5. Route every mutation through operations (MCP `edit_light` /
   `edit_camera`, lightmap overrides, brush fork) and add a test asserting
   one undo entry per mutating MCP tool. This is the precondition for the
   reflective MCP replacement.
6. Lift the Vulkan ring-buffer pool spill and reclaim into a shared
   `Ring_buffer_pool` so GL and Metal get the bounded memory
   `doc/erhe/ring_buffer_memory.md` promises; the same batch closes the small
   Metal parity items (timers, `blit_framebuffer`, resize).
7. Delete the vestigial taskflow annotations in `editor.cpp` or make them
   real; fix the stale parallel-init comment.
8. Plan hygiene: rewrite the eight drifted plans, delete plans that will not
   be worked within a year (50 plans is a multi-year backlog for one
   developer).
9. Frustum culling on draw-list entries; the world AABB per entry and the
   transform hook already exist, and `Shadow_renderer` can then read the
   same AABBs instead of walking every mesh per light.

### Medium term (weeks each)

10. Per-subresource image layout tracking on Vulkan plus a region value type
    replacing the 8-9 parameter blit signatures; closes the only known GPU
    correctness hole.
11. Move the editor bits out of `Item_flags` / `Item_type` (a reserved bit
    range with application-registered label tables) and `hosted_selection`
    into the editor's `Selection`; split the flag and type tables out of
    `item.hpp`, forward-declare the registry, and move the geogram and
    profiler includes out of `primitive/build_info.hpp` and `item_host.hpp`.
12. Include diet on `device.hpp` and `texture.hpp`, then split
    `erhe_graphics` into interface and backend targets so 15 consumers stop
    rebuilding on backend-only changes and the interface compiles against the
    null backend in CI.
13. Reflection-driven MCP item edits and generated tool descriptors from
    property metadata, shrinking the 30k-line server; a scripting binding
    (Lua or Python) over the same reflective registry is the same work seen
    from the other side.
14. Data-model-level undo: record every `Property_changed_args` delivered
    while an operation runs, replacing the property halves of the 34 editor
    operation classes; structural changes stay bespoke, modelled on the
    inheritance snapshot. Pair it with one notification design (property
    observers, `Scene_host` callbacks or `INode_system`, not all three plus
    message buses).
15. Scoped contexts (graphics / scene / UI / tools) carved from `App_context`
    and a narrower `Operation_context`; split `Editor` along its existing
    breadcrumb phases.
16. Unify `Shadow_renderer` and `Id_renderer` onto `Scene_pass_resources`;
    move texture node descriptors to data; fold or retire the `graph/` shader
    graph onto `Graph_editor_window_base`.
17. Sanitizer CI job, incremental `-Werror` on Clang/GCC, software-Vulkan
    GPU tests in CI, version embedding and a first CHANGELOG release.
18. Shader pipeline persistence: the SPIR-V cache plan (settings hash in
    the salt, atomic rename), a persisted `VkPipelineCache` and Metal
    `MTLBinaryArchive`; first-frame time on Quest is dominated by pipeline
    compilation.
19. GPU-driven indirect rendering (count buffer plus culling compute) once
    culling exists; a post-processing stack; temporal AA.
20. Rendering table stakes the research renderer lacks: mobility flags and
    translucent sort, IBL specular with fallback, morph targets.

### Long term (months, decide by need)

21. A real transient-resource render graph, only when a pass needs shared
    transients; the current helper is adequate for hand-wired passes.
22. Dynamic rendering on Vulkan, removing the per-format compatible render
    pass, the 15-parameter factory and the 774-line constructor;
    simplification more than speed.
23. Descriptor model and bindless (buffers, storage images, push constants)
    as the prerequisite for GPU-driven rendering, mesh shaders and async
    compute; each only once scene sizes exceed the draw-list renderer's CPU
    budget.
24. Library packaging for third parties: `ERHE_BUILD_EDITOR` /
    `ERHE_BUILD_EXAMPLES` options, install/export or a documented FetchContent
    recipe, a "using erhe as a library" document walking `hello_swap` to
    `example`, a per-library stability policy (the `Stability:` doc headers
    are the start). Item 11 is its precondition on the data-model side.
25. Instancing as an instance prim with a draw-list instance record and an
    ID-pass mapping; LOD on the reserved `Purpose::proxy`; a load policy for
    deferred subtrees and a uid to file/type/dependency index for streaming
    and an asset database.
26. Data-oriented transform storage behind the object model (a
    structure-of-arrays pool behind `Node_data`, which every write already
    funnels through); not before frustum culling.
27. Clustered/forward+ lighting, bindless materials, virtual shadow maps,
    a better XR path: each only when a scene needs it.

### Recommended not to do in the next twelve months

WebGPU/wasm port (the neutral API already has the WebGPU-shaped concepts and
a null-backend fork is mechanical, but the blockers are GLSL-to-WGSL,
threading, geogram and httplib); making USD the native format (keep glTF
native, USD stays an importer/exporter); Android phone editor; a scripting
language beyond a binding over the reflective registry; new GI producers or
a fourth shadow technique before the existing gates pass; mesh shaders,
meshlets or async queues without a scene that needs them; a D3D12 backend;
particles, terrain, decals or a play mode; rigging phases 5-7 before the
phase 4 requirements exist; C++20 modules (CMake minimum and compiler matrix
are not ready); adding plans without replacing one.

The roadmap report's month-by-month sequence (section 6, "Recommended 6-12
month sequence") orders these against the plans that already exist.

## 9. Scorecard

| Area | Grade | Basis |
|---|---|---|
| GPU memory / frame safety | A | pool retirement, fence-driven rings, verified lockstep invariant |
| GPU abstraction / backend parity | B | clean pimpl seam and golden-backed tests; backend enums leak both ways, one open layout-tracking hole, Metal and GL parity gaps only partly documented |
| Scene / data model | B | well-documented, well-tested USD-shaped model and a complete property system; editor state baked into the library tables, heavy headers, four notification mechanisms, zero glTF unit tests |
| Rendering architecture | B- | strong pass seam and draw list; graph is an ordering helper; no culling; hot-path allocations |
| Editor architecture | C+ | excellent lifetime discipline on top of two service locators and a 4000-line class |
| Undo / mutation model | C | one operation type shared by UI and MCP, but holes, no merging, global history |
| API hygiene vs own rules | C | bool args, struct, auto and header weight counted in every slice |
| Layering | B+ | clean ten-level stack with seven mechanical leaks |
| Build / CI | A- | best-in-class pinning and CI; cheap pins still open, no sanitizer job |
| Tests | B | strong where invested (GPU goldens, MCP fixtures, shadows, item/property/usd); core renderer, undo stack and the native file format untested |
| Documentation | A- | mechanically checked and complete; 8 plans drifted, 9 stale snippets, 41 stray in-tree notes |
| Roadmap discipline | C+ | 50 plans, vision/investment gap on XR and human UX |

## Appendix: method

Six agents each received one slice, the instruction to verify every claim
by reading the cited line, a ban on writing inside the repository other than
their own report, and a fixed report outline (architecture, API issues, code
health, strengths, tests, future options). Four ran in one session and two
(graphics, scene) in a follow-up session against the same code; each later
slice checked the earlier reports' observations about its layer and records
the confirmations and corrections in its own section (graphics 1.9, scene
1.11). The reports are committed verbatim next to this document with only
machine-specific path mentions removed. This synthesis quotes their findings
and does not add claims of its own.
