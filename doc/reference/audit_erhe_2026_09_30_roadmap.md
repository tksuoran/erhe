# erhe roadmap and positioning audit (2026-09-30)

Read-only assessment of where erhe stands against its own roadmap
(doc/plans/), against its own comparison documents (doc/reference/), and
against comparable open engines/editors; with prioritized options for the
next 6-12 months. Evidence is file:line or doc reference; sizes are
S = days, M = weeks, L = months for one developer plus agents.

Method: git statistics; full read of all 50 plan documents (spot-checked
against src/); the six comparison docs and the 2026-06-21 audit section 8;
CHANGELOG.md; res/, scripts/, src/example, src/hello_swap, src/hextiles;
keyword greps over src/ and res/shaders for the capability matrix.

## 0. Headline numbers

- 3842 commits since 2021-05-01; 2572 of them since 2026-06-01 (67%).
  Monthly 2026: Jan 68, Feb 18, Mar 86, Apr 102, May 142, Jun 377, Jul 518,
  Aug 602, Sep 1076. The memory bank was initialized 2026-06-19; the
  acceleration is the agent-driven workflow, not a change of scope.
- Two committers since 2026-01-01: Timo Suoranta 2977, Ladislav Sopko 12.
  This is a solo project with occasional external contribution.
- Source: src/erhe 340k lines across 43 libraries (graphics 84k, usd 34k,
  geometry 24k, scene_renderer 19k, scene 17k, physics 16k, imgui 16k);
  src/editor 230k lines (mcp 30k is the largest editor directory,
  renderers 24k, scene 20k, tools 19k, windows 18k, transform 15k).
  Test sources 80k lines across 27 test directories.
- 50 plan documents (9182 lines): 36 top-level, 7 rigging, 4 geometry_graph,
  3 lightmap. 40 "proposed", 10 "in progress", 0 "blocked".
- 272 MCP tools (config/editor/mcp_tools.json); 107 scripts plus 27
  creation scripts; 14 memory-bank topic files.

## 1. Roadmap inventory

Column "Stale" is judged from code spot checks, not from the plan's git
date: ~25 plans share the 2026-09-18 date of a bulk documentation move.

| Plan | Status | Subject | Size | Stale? (evidence) |
|---|---|---|---|---|
| android.md | proposed | Editor usable on a phone (touch, IME, DPI) | L | current; applicationId still SDL placeholder android-project/app/build.gradle:13 |
| animation_keyframing.md | proposed | Autokey, Create/Delete Key, timeline strip | S left | STALE: items 1-3,6 built (animation_keying.hpp:23 Autokey_mode; mcp animation_create_key) |
| asset_loading.md | proposed | 11 async-asset items (child tasks, ring buffers) | M | current; asset_manager.cpp:479 "still parses inline" |
| brushes.md | proposed | Share geometry slot between brush copies | S | current; brush.cpp:118 |
| build_tooling.md | proposed | Stale-VS-build canary; Android SHA256 | S | current |
| catmull_clark.md | proposed | CC optimization items 4-10 | M | current; file moved to operation/subdivision/ |
| command_script.md | proposed | Re-run commands.json without restart | S | current; editor.cpp:2809 single call |
| content_library.md | proposed | Managed asset types beyond materials | M | current |
| crash_signal.md | proposed | Run marker + postmortem hook + smoke_test.py | S | PARTIALLY STALE: crash_handler.cpp:172 already has SetUnhandledExceptionFilter + minidump |
| ddgi.md | proposed | Sky radiance, authored volumes, specular, shared TLAS, change-driven refit | L | current; ddgi_renderer.cpp:1471 per-tick refit |
| debug_renderer_anti_aliasing.md | in progress | Joined polylines, content wide lines, Metal run | S-M | current (done 2026-09-29 except follow-ups) |
| draw_list_renderer.md | proposed | Frustum culling, static mobility, translucent sort, retire bucket path | M-L | current; scene_root.cpp:1655 mobility always dynamic |
| editor.md | proposed | Layout dirty scheme, dock layout, per-scene override consumers | M | current; scene_settings_resolve.cpp:42 no callers |
| editor_improvements.md | proposed | 11 architecture cleanups (atomics, god files, reinterpret_cast) | S-M | current; editor.cpp 4482 lines, transform_tool.cpp 2930 |
| frame_pacing.md | proposed | Android display timing, VRR, async present | L | current; hardware gated |
| gl_worker_contexts.md | proposed | Narrow GL slot scopes, nested taskflow wait | S-M | current; gltf_fastgltf.cpp:1351,1763 .wait() |
| gltf.md | in progress | EXT_mesh_polygon, ERHE_ registration, .gltf+.bin, brush placement persist | M | current |
| gltf_prefabs.md | in progress | Prefab from selection, skin/anim remap, physics in prefabs | M | current; prefab_library.cpp:631 "static for now" |
| gltf_properties_extension.md | in progress | Per-type ERHE_*_properties extensions | S-M | current, but plan says design not ready; importer gained apply_persistent_flags_and_properties unmentioned |
| graph_editor.md | proposed | Finish shared window base, dedupe MCP boilerplate | M | current; handle_link_create duplicated |
| graphics_tests.md | proposed | GPU tests in CI under lavapipe | S-M | current |
| graphics_tests_agfx_port.md | proposed | Remaining agfx tests, gated on 12 engine features | L | current; effectively a feature backlog |
| id_renderer.md | proposed | Automated Id_renderer coverage | S | PARTIALLY STALE: mcp query_pick_at (mcp_server_scene_query.cpp:981) predates nothing in plan |
| init_status_display.md | proposed | Per-thread command buffers for parallel init status | M | PARTIALLY STALE: mutex already present, editor.cpp lines gone |
| mesh_component_selection.md | proposed | Transform selected vertices, skinned, compute select | M | STALE item 1: mesh_component_transform.hpp:25 built 2026-08-29 |
| mesh_memory.md | proposed | Buffer_pool destroy, WAR barrier, quantization | M | current |
| meshoptimizer.md | proposed | Measure runtime win, encodings, seams | M | current |
| node_editor_native_rendering.md | in progress | Live mouse verification of canvas | S (user) | PARTIALLY STALE: texture_graph_set_view exists since 2026-07-19 |
| occlusion_culling.md | proposed | Raster occlusion culling + MDI | L | current; no early_fragment_tests |
| physics.md | in progress | Box3D sweep, drag gaps, cone tool | M | current |
| post_processing.md | proposed | Debug per-mip barrier tracker | S | current |
| procedural_sky.md | proposed | Metal + Quest verification of atmosphere | S (Mac) | current |
| property_system.md | proposed | glTF/USD serialization gaps, computed props | L | current |
| radiance_cascades.md | in progress | RC GI producer; remaining gates, 32-ray, c0 relocation | M | current (799 lines; phases 0-7 built) |
| raytrace.md | proposed | BLAS cache identity, Metal, skinned BLAS, TLAS refit | L | PARTIALLY STALE: metal_acceleration_structure.cpp builds AS (2026-08-26); doc/editor/raytrace.md:65 also stale |
| shadow_robustness.md | proposed | Metal run, G7 forward-pass cost | M | current |
| shadows.md | proposed | Shadow perf follow-ups (QuickHull, cube budget) | M | current; QuickHull.cpp:195 std::cerr |
| spirv_cache.md | proposed | Settings salt hash, atomic writes | S | current; spirv_cache.cpp:34 manual :v7 tag |
| texture_graph.md | proposed | Node families, async compile, bake driver home | L | current |
| texture_memory.md | proposed | Per-texture breakdown; 688 MB empty scene | S | current |
| timeline_editor.md | proposed | Dope sheet + curve editor (issue #243) | S-M left | STALE Status: animation_window.cpp 1707 lines (plan: "1-line placeholder"); Dope Sheet + tangents missing |
| usd_compatibility.md | proposed | Async USD load, usdc/usdz out, imaging | M-L | current; usd_export.cpp:1157 usda only |
| uv_editor.md | proposed | Blender-style UV editor (issue #250) | L | current; nothing built |
| virtualcity_vanishing_meshes.md | proposed | Open defect; needs non-repo asset | S-M | unverifiable |
| vulkan_backend.md | proposed | Edge-id caps depth discard; enable only used features | S-M | current; vulkan_device_init.cpp 81 query_ copies |
| wasm_webgpu_port.md | proposed | WebGPU backend + Emscripten editor | L | current; 0% (CMakeLists.txt:54) |
| weight_paint.md | proposed | Blur/smear/mirror brushes | M | current |
| xr.md | proposed | EXT render models, battery quad, multiview >2 | M-L | current; runtime gated |
| geometry_graph/attribute_projection.md | proposed | project_attribute node with seam imprinting | M | current; 0% |
| geometry_graph/creation_tools.md | in progress | AI-creation workflow gaps | S-M | current |
| geometry_graph/geometry_nodes.md | proposed | Field system, uid refs, mat4 pin, node backlog | L | current; no Float_field |
| geometry_graph/openvdb_sdf.md | in progress | SDF box, band width, resample | M | current; build traps belong in current doc |
| lightmap/lightmap_baking.md | in progress | Metals, adaptive budget, GLB ext, CLI bake | M-L | PARTIALLY STALE: sky section done in lightmap_baker.cpp:634 (4a46cf15b) |
| lightmap/seam_driven_unwrap.md | in progress | Measured seam placement phases 2-4 | M | current |
| lightmap/tiling.md | in progress | Tile partition defects, quality | M | current (unverified defects) |
| rigging/rigging_tools.md | proposed | Master plan phases 1-7 | L (4-7) | LAGGING: table marks only phase 1 DONE; phases 1-3 are built |
| rigging/fabrik_ik.md | in progress | Phase 1 FABRIK | 0 | done 2026-08-23; Status wrong |
| rigging/ik_settings.md | in progress | Per-bone IK settings, locks | 0 | done; hands-on only |
| rigging/pole_target.md | in progress | Pole target / swivel | 0 | done; hands-on only |
| rigging/ik_drag_options.md | in progress | Effector orientation, chain viz | 0 | PARTIALLY STALE: "out of scope" limit cones now built (joint_constraint_visualization.hpp) |
| rigging/interactive_test_pass.md | in progress | User hands-on pass | user | current; F6 open |
| rigging/skeleton_editing.md | in progress | Phase 3 skeleton authoring | 0 | done 2026-09-25; 220 lines are code description |

Aggregate: 8 plans stale or partially stale in a way that misleads (animation
keyframing, timeline editor, mesh component selection, crash signal, raytrace
Metal, lightmap sky, rigging master table, ik_drag_options), 4 with only
line-number drift, 7 rigging plans whose implementation is complete and whose
R-numbered content is cited from current docs (doc/editor/transform.md:11,
doc/editor/tools.md:50) and therefore belongs under doc/editor/ per
doc/README.md "Layout". Roughly 12 plans are gated on hardware, a runtime
or a user-at-display session (android, frame_pacing, xr, procedural_sky,
node_editor_native_rendering, interactive_test_pass, shadow_robustness Metal,
debug AA Metal, graphics_tests lavapipe, virtualcity asset, physics
interactive checks, creation_tools confirmation).

Remaining size by group (rough): rendering follow-ups (ddgi, rc, shadows x2,
draw_list, occlusion, raytrace, lightmap x3, mesh_memory, meshoptimizer)
~ 6-9 months; editor features (uv_editor, timeline, weight_paint, editor x2,
graph_editor, texture_graph, geometry_nodes, attribute_projection,
content_library) ~ 6-8 months; data model (property_system, gltf x3, usd,
asset_loading) ~ 4-6 months; platforms (wasm, android, xr, frame_pacing)
~ 6+ months; rigging phases 4-7 ~ 3-5 months. Total roadmap: 2-3 years of
solo work at the pre-agent pace; the plans are a backlog, not a schedule.

## 2. Where effort goes vs the stated vision

Commit prefixes since 2026-06-01 (2572 commits): editor 560, doc 305,
memory-bank/mb/memory bank 209, erhe 92, usd 63+13 survey+11, scripts and
creations 28+58+18+21 skills, graphics 55+13, config 43, scene 41, gltf 32,
scene_renderer 30, property 29, lightmap 29, mcp 27+7+6, geometry 25,
physics 21, cmake 21, texture graph 19, meshoptimizer 17, geometry_graph 16,
shadows 14, primitive 14, editor(xr) 12 + xr 5 + android 7, ddgi 9,
raytrace 9, animation 8, erhe_graphics_gpu_tests 10.

By directory touched: doc 916, src/editor/mcp 395, scripts 340, memory-bank
332, src/editor/windows 295, src/editor/tools 198, scene_renderer 168,
graphics 164, editor/renderers 160, geometry_graph 123, usd 116, gltf 99,
res/shaders 90, geometry 84, texture_graph 78, property 64, editor/xr 54,
physics 49, src/example 29, animation 27, erhe/xr 17, rig 7 (rig code landed
in one burst), hextiles 6, hello_swap 3.

Reading against productContext.md's four pillars:

1. Realtime rendering: strong and rising. Shadow robustness (D0-D12,
   verification gates), DDGI, radiance cascades, lightmap baking, debug AA,
   GPU golden tests (38 -> 176), compressed textures. About 20% of commits.
2. Scene authoring: the largest share (editor + windows + tools + mcp +
   property + gltf + usd ~ 45%). Much of it is agent-facing (MCP, 272 tools)
   rather than human-facing UI; the two biggest human-UX plans (uv_editor,
   timeline dope sheet) are unstarted or half done.
3. Geometry experiments: steady but small (geometry 84 + geometry_graph 123
   + texture_graph 78 + catmull_clark/meshoptimizer ~ 12%). The field
   system and attribute projection are designed, not built.
4. XR: the smallest slice (~3%: editor/xr 54, erhe/xr 17, android 7,
   quest scripts). Quest CI job exists; no XR feature work since spring
   beyond keeping it building. XR is in the vision statement but not in the
   investment.

Meta-work: doc + memory-bank + scripts + skills + config ~ 1,800 directory
touches, i.e. roughly a third of all activity is documentation, memory and
tooling for the agent workflow. That is the cost of the 5x commit rate; it
also explains why 8 plans drifted: docs are rewritten by the same agents
that move fast, and plan/doc coherence is checked only for links
(scripts/check_doc_links.py), not for claims.

Gaps between vision and investment:
- XR: named first-class, funded last. Either fund it (phase 3 android plan,
  xr.md, frame_pacing tier A) or reword the vision to "Quest kept building".
- Human UX: the README sells an editor, the investment builds an
  agent-driven scene service. That is a defensible pivot (see 6c) but it
  should be stated.
- Library use by third parties: zero investment (section 4).

## 3. Capability matrix vs comparable projects

Comparators: Godot 4 (general engine + editor), O3DE (AAA-style engine),
Bevy (ECS engine, no editor), Filament (rendering library), The Forge
(rendering framework), Wicked Engine (solo-author engine + editor), Blender
(authoring tool). erhe is closest in spirit to Wicked Engine (one author,
research renderer, ImGui editor) and Filament (library-first rendering).
Status: P present, ~ partial, A absent, with grep evidence.

Rendering
| Feature | erhe | evidence | Godot/O3DE/Wicked | Filament/Forge |
|---|---|---|---|---|
| Forward PBR metallic-roughness | P | standard.frag, erhe_bxdf.glsl | P | P |
| Depth prepass | P | app_rendering.cpp:291 VARIANT_DEPTH_ONLY | P | P |
| Shadow maps dir/spot/point, tight fit, verified bias | P | doc/erhe/shadows.md, 135-cell gate | P (CSM) | P |
| Cascaded shadow maps | A | shadow_renderer.hpp:269 TODO only; tight fit instead | P | P |
| Indirect diffuse GI (DDGI + radiance cascades) | P | erhe_ddgi.glsl, rc_*.comp | Godot SDFGI/VoxelGI; Wicked DDGI | Filament none |
| Lightmap baking (GPU ray query) | P experimental | lightmap_baker.cpp | Godot P | A |
| IBL / prefiltered environment | A | standard.frag:677 flat ambient_light or probes | P | P (core) |
| Procedural atmosphere sky | P | sky_atmosphere*.comp/frag | P | A |
| Bloom + tonemap | P | post_processing.frag | P | P |
| SSAO / GTAO | A | no hits | P | P |
| TAA / SMAA / FXAA | A (MSAA only) | viewport_scene_view.cpp:582 | P | P |
| SSR / reflection probes | A | 0 hits | P | ~ |
| Volumetric fog | A | 0 hits | P | ~ |
| Transparency | ~ (blend pass, no sort) | composition_pass.cpp:306 policy; draw_list plan "translucent sort" | P sorted/OIT | P |
| Frustum culling of meshes | A (shadow casters only) | draw_list_entry.hpp:41 "culling is future work" | P | P |
| Occlusion culling | A (plan) | occlusion_culling.md | P | ~ |
| LOD | A | 0 hits | P | A |
| GPU-driven / indirect draw | ~ (MDI, no GPU culling/count) | draw_indirect_buffer.cpp | P | P |
| Mesh shaders / meshlets | A | 1 hit (comment) | O3DE/Wicked ~ | Forge P |
| Bindless / descriptor indexing | P (4-path texture heap) | doc/erhe/graphics.md "Texture heap" | P | P |
| Skinning | P | erhe_skinning.glsl, joint_buffer | P | P |
| Morph targets | A | gltf_fastgltf.cpp:1563 "no morph-target support" | P | P |
| Animation playback + keyframing | P | animation_player, animation_keying | P | Filament A |
| GPU ray tracing (ray query) | P | ray_trace.comp, VK_KHR_ray_query | Godot A; Wicked P | Forge P |
| CPU raytrace (bvh/embree) picking | P | erhe::raytrace | ~ | A |
| Compressed textures KTX2/Basis/DDS | P | image_loader_ktx2.*, b71c3deb0 | P | P |
| Multiview stereo | P | doc/erhe/multiview.md | P | ~ |
| Particles, decals, terrain | A | 0 hits | P | A |
| GPU timers + Tracy | P | Gpu_timer, ERHE_PROFILE_LIBRARY | P | P |
| Golden-image GPU tests with FLIP | P | gpu_test_flip.cpp, 176 tests | Godot ~ | Filament P |

Editor
| Feature | erhe | evidence | Godot/Wicked/Blender |
|---|---|---|---|
| Undo/redo everywhere | P | Operation_stack, 203 files | P |
| Transform gizmo, snapping, multi-select | P | src/editor/transform 15k lines | P |
| Hierarchy/outliner, properties, content library | P | item_tree_window.cpp 2552 lines | P |
| Property system (dependency properties, styles, expressions) | P (unusual) | erhe::property 9.8k lines | Godot ~ (theme), Blender drivers |
| Mesh component selection (face/edge/vertex) + vertex transform | P | mesh_component_transform.hpp | Blender P; Godot A |
| UV editor | A (plan L) | uv_editor.md | Blender P |
| Sculpting | A | 0 hits | Blender P |
| Node-based procedural geometry | P | geometry_graph 10k lines, SDF via OpenVDB | Blender geometry nodes P; Godot A |
| Node-based procedural textures | P (~75 nodes) | texture_graph, erhe::texgen | Blender shader nodes |
| Rigging: IK, skeleton authoring | P (phases 1-3) | src/editor/rig | Blender P |
| Weight painting | ~ | weight_paint_tool.cpp | Blender P |
| Animation timeline / curve editor | ~ (curves yes, dope sheet no) | animation_window.cpp | P |
| Physics authoring + live drag | P | Jolt/Box3D, joints, MCP | Godot P |
| Prefabs / instancing | ~ (glTF externalAssets, static) | prefab_library.cpp:631 | P |
| Multi-scene, multi-viewport, VR HUD | P | App_scenes, four_view | P |
| Play mode / game runtime | A | no game loop separate from editor | Godot P |
| Scripting language | A (no Lua/etc.); instead MCP + commands.json + property expressions | mcp_tools.json 272 tools | GDScript/C#; Blender Python |
| Plugin system | A | 0 hits | P |
| Localization | A | 0 hits | Godot P |
| Asset browser + thumbnails | P | asset_browser, Thumbnails | P |
| Hot reload shaders | P | Shader_monitor | P |
| Agent driving (MCP, screenshots, UI introspection, input injection) | P (unique) | mcp_server_ui.cpp | Blender MCP add-ons exist; none built-in |

Asset pipeline
| Feature | erhe | evidence | others |
|---|---|---|---|
| glTF import/export incl. KHR physics, variants, instancing, ERHE_* extensions with JSON schemas | P | doc/gltf_extensions | Godot import only |
| USD import/export (LightUSD), composition arcs, variants | P partial | erhe::usd 34k lines, usda export only | Blender P, O3DE ~, Godot A |
| OBJ | ~ | parsers | P |
| FBX/Alembic | A | | Godot/Blender P |
| Async loading with budgets | P | asset_load_task | P |
| Asset cooking / offline pipeline | A (BVH + SPIR-V disk caches only) | spirv_cache.cpp | O3DE P |
| Sample assets in repo | small (11 MB: test rooms, RiggedFigure, Deccer cubes) | res/editor/assets | |

Platforms: Windows, Linux, macOS (Vulkan + Metal), Quest/Android (Vulkan +
OpenXR) all in CI (.github/workflows/build.yml 7 desktop jobs + Android job).
Web/WebGPU absent (plan L). iOS absent. Headless builds on all three desktop
backends with emulated swapchain (doc/erhe/metal_headless.md).

XR: OpenXR session/swapchain/actions, hand tracking, controller render
models (XR_FB_render_model), passthrough, foveation config, floating HUD;
no EXT render models, no multiview beyond 2 views, no XR-specific rendering
performance work since spring.

Honest reading for a solo research project: erhe already exceeds Wicked
Engine and Godot in three narrow areas (verified shadows with analytic
gates, USD round-trip with LightUSD, agent-driven editing) and in GI
research breadth (DDGI + RC + lightmaps in one codebase). It lacks the
"table stakes" a general engine user expects (IBL, AO, TAA, frustum
culling, LOD, morph targets, sorted transparency, scripting, play mode).
A solo project should chase the table stakes that unlock its own research
(culling, IBL as GI fallback/specular, sorted transparency) and should not
chase Godot's breadth (game runtime, scripting language, particles,
terrain, plugin ecosystem).

## 4. Secondary consumers and library usability

- src/hello_swap (363 lines): Device + Window + Render_pass clear loop.
  30 commits, 23 in 2026, all API ripple. Cleanest minimal consumer.
- src/example (1098 lines): windowed glTF viewer without ImGui; uses
  Scene, Forward_renderer, Shader_variant_cache, Material_set, gltf loader.
  159 commits, 71 in 2026, all ripple. Compiles graphics codegen outputs
  from the build tree and links erhe::raytrace with a TODO to make it
  optional (src/example/CMakeLists.txt). The only out-of-editor demo of
  scene + scene_renderer.
- src/hextiles (9389 lines): a hex strategy game/map editor; the only
  external exercise of Rendergraph + Imgui_renderer + Commands +
  Text_renderer. 167 commits, 46 in 2026, all ripple; links erhe::geometry
  and erhe::net without using them (stale CMake).
- All three are alive as build canaries (AGENTS.md "Building" requires
  keeping them compiling) and not as products. Collectively they cover
  graphics, window, scene, scene_renderer, gltf, rendergraph, imgui,
  commands, renderer, ui; they do not touch physics, geometry ops,
  property, usd, xr or raytrace-as-feature.
- Packaging: project(erhe VERSION 1.0) never bumped; no git tags; no
  install() or export() anywhere (CMakeLists.txt, src/erhe/*/CMakeLists.txt,
  cmake/*.cmake); no package config; static libraries consumed only via
  add_subdirectory inside this tree; 46 CPM dependencies fetched at
  configure, several from tksuoran forks (geogram, glslang, SDL, LightUSD,
  fastgltf, mimalloc, OpenXR-SDK-Source). No document on consuming erhe as
  a library. CHANGELOG.md exists since 2026-09-26 (53 bullets, one
  [Unreleased] bucket, no versions, duplicated "### Changed" heading).
- Verdict: the editor is the only real client. A third-party integrator
  today would have to vendor the whole tree, accept the CPM fork set, and
  track API churn with no version boundary (scene_renderer had 16 public
  API changes in four days). Making erhe consumable needs, in order: a
  version + tag + release CHANGELOG cut; ERHE_BUILD_EDITOR /
  ERHE_BUILD_EXAMPLES options so a consumer can build only libraries; an
  install/export set or a documented FetchContent recipe with the
  required CPM pins; a "using erhe as a library" document that walks
  hello_swap -> example; and a stated stability policy per library
  (doc headers already carry Stability lines, which is a good start).

## 5. Tooling inventory and what it says about the process

scripts/ (107) + creations (27) by category: configure/build wrappers 37;
CI/test runners 8 (ci_run_tests.py, ci_test_summary.py, gpu_test_report.py,
run_mcp_tests.ps1 which predates the Python-only rule); MCP driving 11
(mcp_call.py, erhe_mcp.py, test_editor_mcp.py, capture_window.py,
mesh_ab_capture.py, ragdoll trio); verification 24 (gi_verify, shadow_verify,
rc_texel_verify, six ik/skeleton/joint verifiers, physics_drag_joint_sweep,
geometry/texture graph smoke tests, scene_roundtrip_verify,
frame_pacing_sim, check_task_spawns, ...); Quest/Android 8; RenderDoc 4;
profiling/memory 5; USD survey 3; git 1; Metal format-table codegen 1.

Clearly one-off with no doc references: analyze_shadow_fit_dump.py and
_dump2.py, repro_capsule_bsphere.py, geometry_graph_zoom_harness.py,
usd.py, remap_font.py (non-ASCII docstring), mcp_*_test.py trio, ragdoll
trio. Defects: disable_page_heap_verification.bat runs the same /enable
command as the enable script; creation_13 has a UTF-8 BOM.

Process reading:
- Verification is empirical and agent-executed: build headless editor,
  drive over MCP, render_scene_image/capture_screenshot, compare against
  analytic truth (shadow_verify 135 cells, gi_verify gates, rc_texel_verify
  334k texels) or against goldens (FLIP). This is stronger than most
  engines' test culture for rendering correctness and is erhe's most
  distinctive engineering asset.
- CI runs only deviceless tests (ctest --label-exclude "gpu|editor"); the
  GPU goldens and MCP suite run only on developer machines. graphics_tests.md
  (lavapipe) would close half of that gap.
- The orchestration harness (doc/agents/orchestration_harness.md) makes
  one session write briefs and fresh agents code; that is why docs and
  memory-bank are a third of the traffic and why plan drift happens.
- 25 creation scripts double as regression scenes and as the generator of
  committed test assets (creations 22-25). The "AI creations" catalog is a
  demo pipeline no comparable engine has.
- Large reference assets (Sponza, VirtualCity, ABeautifulGame, usd-wg) are
  local drops with no fetch script; measurements in docs cite them but a
  fresh machine cannot reproduce them.

## 6. Future development options

Format: why; cost; depends on; risk; evidence for ease/difficulty.

### 6a. Consolidation and debt

A1. Plan hygiene sweep (S, 2-3 days). Rewrite the 8 misleading plans to
their remaining halves; move the seven rigging R-numbered specs to
doc/editor/rigging/ and leave rigging_tools.md phases 4-7 as the plan;
fold openvdb build traps and lightmap sky facts into current docs; fix
Status lines (timeline_editor, animation_keyframing, fabrik_ik...). Add a
"claims spot-check" habit to check_doc_links.py's neighbour (e.g. a plan
must cite at least one src path that exists). Why: the roadmap is the
orchestrator's input; stale plans produce wasted briefs. Risk: none.

A2. Close the prior audit's cheap items (S). Pin concurrentqueue
(CMakeLists.txt:429 GIT_TAG master), centralize the googletest pin (now
declared in ~12 test CMakeLists), embed ERHE_GIT_COMMIT + version, add
.clang-format. All unchanged since 2026-06-21; 6 of 10 recommendations
were never acted on.

A3. Delete or archive plans that will not be worked in 12 months rather
than carry them: virtualcity_vanishing_meshes (needs an asset nobody can
fetch), init_status_display phase II (premise is serial init that nobody
plans to change), command_script, post_processing barrier tracker. Keep
the fact in the current doc's "Future work" as one line.

A4. Retire dead paths flagged in plans: bucket_primitives path
(mesh_memory.hpp:462), commented-out taskflow in programs.cpp:106-127,
glfw window library (deprecated), superluminal/nvtx profile backends
("likely stale" per doc/building.md:176), ERHE_AUDIO_LIBRARY (non-functional
per doc/building.md:188). M total, each S. Reduces the option matrix CI
and agents have to reason about.

### 6b. Rendering

B1. Frustum culling + static mobility + translucent sort in the draw-list
renderer (draw_list_renderer.md items 1-4). M. Why: every other rendering
option (occlusion culling, GPU-driven, LOD) presupposes culling; and it is
the most visible missing table stake. Evidence of ease: bounds are already
stored per entry (draw_list_entry.hpp:41), MDI exists. Risk: low.

B2. IBL split-sum as the specular term and as the fallback when no probe
volume is active (esoterica item 1.4). M. Why: DDGI/RC give diffuse only;
metals and glossy surfaces have no environment reflection
(lightmap_baking.md notes standard.frag:710 has no (1 - metallic)). Depends
on the procedural sky (source of the environment) which exists. Risk:
interaction with three GI producers; keep it a fourth source in the
Indirect_diffuse_source style enum plus a specular term.

B3. Finish RC/DDGI remaining gates only where they change a user-visible
result (radiance_cascades.md section 9: leak 0.0082, corridor far field,
light-move settle; ddgi change-driven refit shared with RC). M. Why: two GI
producers are built and measured; the cheapest wins left are the shared
change-driven refit (both plans name it) and specular fallback (B2).
Recommend not opening new GI producers.

B4. Metal parity pass in one macOS session: shadow_robustness Metal run,
debug AA Metal run, procedural_sky Metal, raytrace Metal intersector
verification, Metal GPU timer (agfx item 6). S-M. Why: four plans wait on
the same session; Metal backend claims in docs are unverified
(doc/editor/raytrace.md:65 says no-op while metal_acceleration_structure.cpp
exists).

B5. Occlusion culling, GPU-driven culling with indirect count, mesh
shaders, async compute (occlusion_culling.md, agfx items 3/9/12, esoterica
3.x). L. Recommend deferring until B1 lands and a scene exists that needs
it; erhe's test scenes are small rooms.

B6. Morph targets and sparse accessors in erhe::gltf (sample-renderer gaps).
S-M. Why: cheap correctness win for imported assets; unlocks USD
blendshapes later. Risk: low.

### 6c. Editor and UX

C1. Finish the timeline: Dope Sheet tab and tangent/interpolation UI
(timeline_editor.md phases 2 and 4) and the animation_keyframing leftovers
(scene markers, strip manipulation). S-M. Why: 80% built; issue #243 is
the most-referenced issue in the log (12 commits); finishing beats starting.

C2. Rigging phase 4 (constraint stack + dependency-ordered evaluation
pass) only after the user's hands-on pass on phases 1-3 (blocked on user).
L. Why: the roadmap calls it the single largest architectural change;
starting it without the phase 1-3 feel feedback risks building on an
untested UX. Recommend: user pass first, then requirements.

C3. UV editor (uv_editor.md). L. Why: it is the natural companion to mesh
component selection, seams for lightmaps and attribute projection all need
seam authoring. Risk: a new window family plus new geometry backend
(seam unwrap); do it after A1 and only if lightmap/attribute projection
remain priorities. Otherwise the smaller "seam attribute + seam-driven
unwrap" (seam_driven_unwrap.md phase 2) delivers most of the value.

C4. Editor architecture cleanups (editor_improvements.md, editor.md dock
layout). S-M each. Do opportunistically inside other work; the god files
(editor.cpp 4482, transform_tool.cpp 2930) will otherwise keep growing.

C5. Do not add a scripting language. MCP (272 tools), commands.json and
property expressions already fill the role for the project's actual user
(agents). A Lua/Python layer would duplicate the MCP surface. If a
scripted-runtime need appears, expose MCP tools over an in-process call
interface first.

### 6d. Data model, assets, scripting

D1. gltf_properties_extension steps 2-5 once the design question the plan
raises is settled. S-M. Why: unblocks property_system.md items and USD M4;
importer already gained apply_persistent_flags_and_properties.

D2. Prefab completeness: skin/animation remap and physics in prefabs
(gltf_prefabs.md). M. Why: prefab_library.cpp:631 makes instances static;
creations and USD references both depend on prefabs.

D3. USD: async load task (parity with glTF) and usdc/usdz export
(usd_compatibility.md top items). M each. Why: USD is 34k lines and the
second-largest library; leaving it synchronous while glTF is async is an
inconsistency users hit first. Depends on asset_loading.md item 1-2
(get_or_load_container still inline).

D4. Asset fetch script for reference scenes (Sponza, ABeautifulGame,
glTF-Sample-Assets subset, usd-wg subset) into the gitignored drop dir with
checksums. S. Why: measurements in six docs cite assets no fresh machine
has; agents cannot reproduce them.

D5. Stable per-item GUIDs and structured MCP errors / job ids (nova3d items
1, 4, 7). S-M. Why: the agent workflow polls get_async_status and parses
free-text errors; these are the two friction points every creation script
works around (scripts/creations/common.py).

### 6e. Platforms

E1. Quest: keep building (CI job) plus one frame-pacing or rendering
performance pass per quarter; do not start android phase 3 (phone editor)
unless a user exists for it. Evidence: 3% of effort; the plan is L and
mostly hardware gated.

E2. WebGPU/wasm port: do not start in the next 12 months. 0% built, L,
would add a fifth backend to a four-backend matrix that is already only
partially verified (Metal items in B4), and Geogram/Jolt/OpenVDB/LightUSD
in wasm are each their own project. Revisit only if the goal becomes
sharing creations in a browser; a cheaper path is render_scene_image over
MCP into a static gallery.

E3. iOS: not in any plan; skip.

### 6f. Developer experience and tests

F1. GPU tests in CI under lavapipe (graphics_tests.md). S-M. Why: 176 GPU
tests run only on developer machines; regressions in shaders are found by
agents, not CI. Software Vulkan will not run ray query or RC compute, but
covers the golden set.

F2. Sanitizer job and a fast/slow label split (audit item 7). S.

F3. Stale-VS-build canary (build_tooling.md). S. Why: ODR-violating
incremental links waste agent sessions; the fix is a few lines.

F4. Plan/claim checker (A1) and a rule that a coder's brief closing a plan
item edits the plan in the same commit (the harness already does this for
docs; extend to plans explicitly).

### 6g. Making erhe usable as a library

G1. Version, tag and cut CHANGELOG [Unreleased] into 1.1.0 (S). Add
ERHE_BUILD_EDITOR / ERHE_BUILD_EXAMPLES / ERHE_BUILD_HEXTILES options (S).
G2. install/export set with a package config for the library subset that
hello_swap and example need (graphics, window, scene, scene_renderer,
gltf, renderer, rendergraph, imgui, commands, math, dataformat, log, verify,
file, codegen) (M). Forks make this hard: a consumer inherits seven
tksuoran forks via CPM; document the pins.
G3. "Using erhe as a library" document walking hello_swap -> example, plus
a FetchContent recipe (S). Convert example's raytrace dependency to
optional (its own TODO).
G4. Only after G1-G3: invite one external consumer (the Ladislav Sopko
contributions suggest one exists) and let their friction drive stability
work. Do not promise API stability for scene_renderer or graphics; mark
them "mostly stable" honestly (scene_renderer had 16 API bullets in 4 days).

### Recommended 6-12 month sequence

Months 1-2: A1 plan hygiene, A2 audit leftovers, A3/A4 deletions, F1
lavapipe CI, F3 build canary, D4 asset fetch, G1 version/tag/options, B4
Metal parity session, C1 finish the timeline. Rationale: cheap, mostly
S, and they make every later brief more reliable; nothing here is
research.

Months 3-5: B1 culling + mobility + translucent sort; B2 IBL specular and
fallback; B6 morph targets; D1 properties extension; D5 GUIDs + MCP job
ids. Rationale: the rendering table stakes that the research renderer
lacks, plus the data-model items other plans are gated on.

Months 6-9: D2 prefabs; D3 USD async + usdc; B3 GI remaining gates (shared
refit only); user hands-on rigging pass then C2 phase 4 requirements; G2/G3
library packaging with one external consumer. Rationale: consolidate the
two scene formats and the agent workflow before opening new UI surfaces.

Months 10-12: C3 UV editor or seam-driven unwrap phase 2 (choose by whether
lightmaps remain a priority), one Quest performance pass (E1), B5 only if a
scene now needs it.

### Explicitly recommended NOT to do (next 12 months)

- WebGPU/wasm port (E2). - Android phone editor (android.md). - A scripting
language (C5). - New GI producers or a fourth shadow technique; finish the
gates of the two that exist. - Mesh shaders / meshlets / async queues
(agfx 9, 12) without a scene that needs them. - D3D12 backend (agfx doc
already rejects). - Particles, terrain, decals, play mode: general-engine
breadth erhe's vision does not claim. - Rigging phases 5-7 before phase 4
requirements exist and the user pass is done. - Adding plans: 50 plans for
one developer is already a 2-3 year backlog; new work should replace a
plan, not add one.

## 7. Small defects noticed (for a follow-up commit, not this audit)

- nova3d_comparison.md, esoterica_rendering.md, gltf_sample_renderer_
  comparison.md contain non-ASCII characters; nova3d, esoterica and
  forge_erhe.md cite machine-specific paths (AGENTS.md rules).
- property_system_wpf_comparison.md references a section 6 that does not
  exist and lists the animated-value layer as "not yet" though
  get_animation_base_value exists (dependency_object.hpp:169).
- gltf_sample_renderer_comparison.md "Deferred: primitive.mode" is stale
  (gltf_fastgltf.cpp:2225 honours it since 56fa08f46).
- agfx_comparison.md section 3.5 still says 38 GPU tests / Metal not run.
- CHANGELOG.md has two "### Changed" headings (lines 126 and 257).
- scripts/disable_page_heap_verification.bat enables instead of disables.
- src/hextiles/CMakeLists.txt links erhe::geometry and erhe::net unused.
- concurrentqueue GIT_TAG master (CMakeLists.txt:429).
