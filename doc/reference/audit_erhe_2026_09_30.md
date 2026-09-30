# Architecture and API Audit -- erhe (2026-09-30)

- **Date**: 2026-09-30 (draft; two of six slices outstanding, see "Status")
- **Auditor**: Claude Code (Fable 5.1), six read-only slice audits run in
  parallel, synthesized here
- **Baseline**: `main` at c982af6d6
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
| GPU foundation (`erhe::graphics`, `gl`, `dataformat`, `buffer`, `codegen`, shaders) | `audit_erhe_2026_09_30_graphics.md` (to be written next to this file) | outstanding |
| Scene and data model (`item`, `scene`, `property`, `primitive`, `geometry`, `gltf`, `usd`, `physics`, `math`, `commands`) | `audit_erhe_2026_09_30_scene.md` (to be written next to this file) | outstanding |

## Status

The graphics-foundation and scene/data-model slices were not completed in
the session that produced this document. `doc/plans/codebase_audit_2026_09_30.md`
holds the two briefs and the steps to finish: run the two slices, then
rewrite sections 1-6 below so they draw on all six reports, then remove the
plan. Until then, statements below about `erhe::graphics` and the scene
data model come from the four finished slices looking at those layers from
the outside, not from a dedicated audit.

## 1. Executive summary

erhe is a 340k-line library set plus a 230k-line editor, developed by one
author at a commit rate that quintupled in mid-2026 when the agent-driven
workflow began (1076 commits in September). The code that carries the
engineering weight is good: the GPU memory and ring-buffer model, the
draw-list renderer, shadow verification, the build and CI hygiene and the
lifetime discipline in the editor are all above what a solo research
project normally has. The weaknesses are structural and consistent across
the four slices audited so far:

1. **Two service locators carry the editor.** `App_context` holds ~106
   pointers and every part reaches every other part through it; `Editor` is
   a 4000-line local class whose constructor is ~1580 lines. The render
   layer inherits the same shape through `Render_context`, which hands
   `App_context&` to every tool, renderable and pass. The library layer
   (`erhe::scene_renderer`) stays clean of editor includes; the coupling is
   all one way, which makes it fixable.
2. **The render graph is a node-ordering helper.** `Rendergraph` sorts
   nodes and calls them; there is no resource table, lifetime analysis,
   aliasing or barrier derivation, and consumers reach shadow data by
   `static_cast` of the producer node. It works because every pass is
   hand-wired, and it will keep working until a pass needs transient
   resources shared across nodes.
3. **The project's own rules are broken in the hot path.** The
   "no per-frame allocation" and "no update each frame" rules from
   `AGENTS.md` are honored in the newest code (shadow scratch buffers,
   draw-list change serials) and violated in older code that every frame
   still runs: `std::vector<Render_bucket>` locals per pass, `fmt::format`
   debug labels per bucket, editor settings pushed into renderers each
   tick, gizmo render paths allocating.
4. **Mutations do not all go through operations.** MCP `edit_light` /
   `edit_camera`, lightmap tile overrides and brush forking mutate the
   document directly while the Properties window uses
   `Property_set_operation` for the same fields; there is no operation
   merging, `Compound_operation` has no rollback, and closing any scene drops
   the global undo history.
5. **The MCP server is the largest editor subsystem** (30k lines, 272
   statically dispatched tools, ~300 handler declarations in one class over
   17 files) and is mostly hand-written argument parsing and serialization
   that the property metadata could generate.
6. **Cheap infrastructure items stay open across audits.** Of the ten
   2026-06-21 recommendations, four are done or partly done; the two
   one-line pins (concurrentqueue on `master`, 25 copies of the GoogleTest
   pin) are still open, there is no `-Werror` on Clang/GCC, no version
   embedding, no sanitizer CI job.
7. **Vision and investment diverge.** XR is named first in the vision and
   funded last (~3% of commits); human-facing editor UX (UV editor, timeline)
   is unstarted while agent-facing tooling grew to 272 tools; the three
   secondary consumers are build canaries, not clients, and there is no
   install target, version tag or library-consumer document.

## 2. Strengths

Specific, verified in the slice reports:

- **GPU memory and frame safety.** `Pool_block` retires instead of frees
  and is applied from the frame-completion handler, so the multi-draw
  base-vertex lockstep holds by construction; ring-buffer completion is
  driven by the device frame fence, one mechanism for every per-frame buffer
  including CPU readback shadows.
- **`Scene_pass_resources` / `Base_render_parameters`** describe the pass
  prologue without naming a renderer, so bucket, draw-list and fullscreen
  paths share one bind sequence. This is the seam every rendering proposal
  in section 6 builds on.
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
  material-set and post-processing documents.
- **Documentation** is mechanically checked and clean; every library but
  one (`erhe::task`) has a document; the assertion vocabulary
  (`ERHE_VERIFY` / `ERHE_FATAL`, live in Release) and logging categories are
  consistent.

## 3. Architecture findings

### 3.1 Layering (infra report, section 1)

The dependency graph derived from every `target_link_libraries` is a clean
ten-level stack from leaf utilities to `scene_renderer` and `imgui`, with
four leaks in the base layer: `erhe_log` links geogram (for
`log_geogram.hpp`), `erhe_utility` links SDL3 (for `clipboard.cpp`),
`erhe_rendergraph` links `erhe::ui` without using it, `erhe::ui` links
`erhe::primitive` for one include, and `erhe_commands` links `erhe::xr`
unconditionally. `erhe_physics` links `erhe::renderer` for its debug
renderer, which puts a physics library above the render layer. 44 library
directories is more than the code needs: `defer`, `hash`, `message_bus`,
`utility`, `smoke` and `task` are each under 400 lines.

### 3.2 Render graph and frame composition (rendering report, section 1)

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

### 3.3 Editor object model (editor report, section 1)

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

### 3.4 Undo, mutation paths and the MCP surface

The MCP `set_item_property` tool and the Properties window share
`Property_set_operation`, which shows the intended design; `edit_light`,
`edit_camera`, lightmap tile overrides and brush forking do not use it.
There is no operation merging and no per-scene undo history. The MCP
server's queue, main-thread drain, deferral protocol, undo groups, health
and auth design is sound; its size is the problem, and the property
metadata that already drives `set_item_property` could replace most
per-type handlers.

### 3.5 Node-graph editors

`geometry_graph` and `texture_graph` share `Graph_editor_window_base` with
templated serialization and operations; the shader graph under `graph/`
bypasses the base entirely. `texture_node_descriptors.cpp` is 4269 lines of
data expressed as C++ (one 918-line function, 2852 string literals).

## 4. API findings

Recurring across slices, with counts from the reports:

- **Boolean parameters** despite the `AGENTS.md` rule: 76 in editor
  headers, 17 in rendering-slice headers, and in base-layer APIs
  (`make_logger(name, bool tail)`, `Imgui_host(..., bool imgui_ini, ...)`).
  `Shadow_render_node::reconfigure` takes eight positional scalars including
  a bool.
- **`struct` instead of `class`**: 48 declarations in the editor, 22 of them
  in `app_message.hpp` mixed with `class`.
- **`auto` locals**: 1018 in the editor slice alone.
- **Header weight**: `scene/scene_view.hpp` includes `<geogram/mesh/mesh.h>`
  (37 includers); `tools/tools.hpp` includes 12 concrete tool headers (30
  includers); `scene_root.hpp` is included by 128 files.
- **Hidden ordering requirements** in the render layer (`m_light_projections`
  must be cleared before any early exit; `defer_resource` for mid-frame
  resizes) are documented where they live, which is the right mitigation,
  but they remain conventions rather than types.
- **Public mutable flags** on renderers (`Id_renderer::enabled`) and
  duplicated concepts (several viewport/camera notions in the editor;
  four light-creation sites).
- **Dead header**: `texture_renderer.hpp` has missing includes and no user.

## 5. Code health

| Metric | Value | Source |
|---|---|---|
| Own code under `src/erhe` | 393,567 lines (about 92k vendored single-file libs, about 70k tests) | infra 0 |
| `src/editor` | 230k lines; `mcp/` 30k is the largest directory | roadmap 0 |
| Largest renderer files | `lightmap_baker.cpp` 6120, `radiance_cascades_renderer.cpp` 2909, `ddgi_renderer.cpp` 1939 | rendering 3.1 |
| Longest editor functions | `build_japanese_glyphs` 918, `Headset_view::render_headset` 689, `Lightmap_window::imgui` 668 | editor 3 |
| TODO/FIXME in editor | 186 (34 in `physics_window.cpp`); 257 commented-out lines; 17 `#if 0` | editor 3 |
| `printf` family | only `crash_handler` and `--help` (defensible) | editor 3 |
| Plans | 50 documents, 9182 lines; 40 proposed, 10 in progress; 8 drifted from the code | roadmap 1 |

Steady-state allocations found on the frame path (rendering report 1.10,
editor report 7): bucket vectors per pass and per light, per-bucket
`fmt::format` labels, `filtered_meshes` per ID render, XR view-input vectors
per frame, lightmap override vector per tick, pending-vector swaps that
discard capacity, and gizmo/handle visualization vectors in six tool files.

## 6. Test coverage

| Area | Coverage | Gap |
|---|---|---|
| GPU foundation | `erhe_graphics_gpu_tests`, 176 Vulkan tests with FLIP goldens; Metal and macOS Vulkan runs recorded | not run in CI (no software Vulkan there) |
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
   cpp-terminal, centralize GoogleTest, register `erhe_smoke`, fix the four
   base-layer layering leaks, route `ERHE_FATAL` through erhe logging.
2. Remove the steady-state allocations and per-frame settings pushes listed
   in section 5; the scratch-buffer pattern to copy already exists in
   `Shadow_renderer`.
3. Route every mutation through operations (MCP `edit_light` /
   `edit_camera`, lightmap overrides, brush fork) and add a test asserting
   one undo entry per mutating MCP tool. This is the precondition for the
   reflective MCP replacement.
4. Delete the vestigial taskflow annotations in `editor.cpp` or make them
   real; fix the stale parallel-init comment.
5. Plan hygiene: rewrite the eight drifted plans, delete plans that will not
   be worked within a year (50 plans is a multi-year backlog for one
   developer).
6. Frustum culling on draw-list entries; the world AABB per entry and the
   transform hook already exist, and `Shadow_renderer` can then read the
   same AABBs instead of walking every mesh per light.

### Medium term (weeks each)

7. Reflection-driven MCP item edits and generated tool descriptors from
   property metadata, shrinking the 30k-line server.
8. Scoped contexts (graphics / scene / UI / tools) carved from `App_context`
   and a narrower `Operation_context`; split `Editor` along its existing
   breadcrumb phases.
9. Unify `Shadow_renderer` and `Id_renderer` onto `Scene_pass_resources`;
   move texture node descriptors to data; fold or retire the `graph/` shader
   graph onto `Graph_editor_window_base`.
10. Sanitizer CI job, incremental `-Werror` on Clang/GCC, software-Vulkan
    GPU tests in CI, version embedding and a first CHANGELOG release.
11. GPU-driven indirect rendering (count buffer plus culling compute) once
    culling exists; a post-processing stack; temporal AA.
12. Rendering table stakes the research renderer lacks: mobility flags and
    translucent sort, IBL specular with fallback, morph targets.

### Long term (months, decide by need)

13. A real transient-resource render graph, only when a pass needs shared
    transients; the current helper is adequate for hand-wired passes.
14. Library packaging for third parties: `ERHE_BUILD_EDITOR` /
    `ERHE_BUILD_EXAMPLES` options, install/export or a documented FetchContent
    recipe, a "using erhe as a library" document walking `hello_swap` to
    `example`, a per-library stability policy (the `Stability:` doc headers
    are the start).
15. Split `erhe_graphics` into interface and backend targets.
16. Clustered/forward+ lighting, bindless materials, virtual shadow maps,
    a better XR path: each only when a scene needs it.

### Recommended not to do in the next twelve months

WebGPU/wasm port; Android phone editor; a scripting language; new GI
producers or a fourth shadow technique before the existing gates pass; mesh
shaders, meshlets or async queues without a scene that needs them; a D3D12
backend; particles, terrain, decals or a play mode; rigging phases 5-7
before the phase 4 requirements exist; C++20 modules (CMake minimum and
compiler matrix are not ready); adding plans without replacing one.

The roadmap report's month-by-month sequence (section 6, "Recommended 6-12
month sequence") orders these against the plans that already exist.

## 9. Scorecard

| Area | Grade | Basis |
|---|---|---|
| GPU memory / frame safety | A | pool retirement, fence-driven rings, verified lockstep invariant |
| Rendering architecture | B- | strong pass seam and draw list; graph is an ordering helper; no culling; hot-path allocations |
| Editor architecture | C+ | excellent lifetime discipline on top of two service locators and a 4000-line class |
| Undo / mutation model | C | one operation type shared by UI and MCP, but holes, no merging, global history |
| API hygiene vs own rules | C | bool args, struct, auto and header weight counted in every slice |
| Layering | B+ | clean ten-level stack with five mechanical leaks |
| Build / CI | A- | best-in-class pinning and CI; cheap pins still open, no sanitizer job |
| Tests | B | strong where invested (GPU goldens, MCP fixtures, shadows); core renderer and undo stack untested |
| Documentation | A- | mechanically checked and complete; 8 plans drifted |
| Roadmap discipline | C+ | 50 plans, vision/investment gap on XR and human UX |

## Appendix: method

Six agents each received one slice, the instruction to verify every claim
by reading the cited line, a ban on writing inside the repository, and a
fixed report outline (architecture, API issues, code health, strengths,
tests, future options). Four completed; their reports are committed
verbatim next to this document with only machine-specific path mentions
removed. This synthesis quotes their findings and does not add claims of
its own. The two outstanding slices are described in
`doc/plans/codebase_audit_2026_09_30.md`.
