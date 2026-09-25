# erhe documentation

Stability: stable

This directory holds erhe's documentation. Each file describes how the code
behaves now; outstanding work lives under `plans/` and is linked from the
document it extends. `py -3 scripts/check_doc_links.py` verifies the rules
below and is part of every documentation change.

## Layout

- `doc/erhe/<name>.md` is the document of the library built by CMake target
  `erhe_<name>` (purpose, key types, public API, dependencies, implementation
  notes). Every other subject whose code lives in `src/erhe/` (a renderer
  design, a backend, a geometry algorithm, the property system) is a document
  in `doc/erhe/` too.
- `doc/editor/` holds the editor: `editor.md` for the application, one
  document per source subdirectory (`tools.md`, `windows.md`, ...) and one
  per editor feature (`selection.md`, `lightmap_baking.md`, ...).
- `doc/agents/` holds documentation written primarily for AI coding agents:
  the orchestration harness, MCP tool guidelines, RenderDoc and survey
  run-books, the catalog of agent-built creations, MCP server setup guides.
  A document a human developer needs as often as an agent does stays with
  its subject.
- `doc/<subject>.md` at the top level is a cross-cutting workflow: building,
  testing, CMake conventions and platforms.
- `AGENTS.md` holds only the rules every agent session needs; its "Topic
  documents" table routes each platform and topic to its document. A rule
  that only some sessions need goes into the topic's document and, when it
  is a new topic, gets a row in that table.
- A landed design record stays a current document; its numbered section
  labels (`D5`, `R3`, `C7`) are cited from source comments and stay stable
  across edits.
- `doc/<topic>/` groups a subject that needs several documents
  (`frame_pacing/`). Create a topic directory only when one document would
  not do.
- `doc/plans/` holds future work: a design that is not built, the remaining
  phases of one that is, a session handoff. `doc/plans/<topic>/` groups plans
  of one subject. A plan names the current document it extends; that
  document links to the plan from its "Future work" section.
- `doc/reference/` holds material erhe does not own: bug reports and feature
  requests written for other projects, comparisons with other software,
  transcribed specifications, dated measurement reports.
- `doc/gltf_extensions/` is the specification of erhe's `ERHE_*` glTF
  extensions with their JSON schemas.
- `doc/images/` holds the images the documents embed.

## Header line

Every document states its standing in its first ten lines:

- A current document carries `Stability: stable`, `Stability: mostly stable`
  or `Stability: experimental`. The level describes the subsystem the document
  covers (its API and behavior), not the completeness of the text. `stable`:
  the design is settled and changes are additive. `mostly stable`: the design
  is settled but parts are still moving. `experimental`: expect the design
  itself to change.
- A plan carries `Status: proposed`, `Status: in progress` or
  `Status: blocked`.
- Reference material carries neither.

## Writing rules

- A document describes the present, not the past. A live document states
  the subsystem's CURRENT state - requirements, design, verification status -
  and the REMAINING future work. It never narrates history: no commit hashes
  or commit tables, no dated progress or "follow-up series" sections, no
  phase-by-phase records of work already landed, no "an earlier revision did
  X" or before/after narration. History belongs in commit messages
  (`git log --follow` on the document and the code it covers). When updating
  a document after landing work, rewrite the affected statements in the
  present tense and move anything still outstanding into its plan - never
  append a dated record. Settled decisions and traps stay, stated as standing
  rules with their rationale, not as stories; measured results stay only
  while they describe current behavior or are needed to interpret a future
  re-run.
- A document states what to do, not what to avoid. Plans and requirement
  lists above all specify their subject positively: the dependencies a type
  may name, the call sites that may reach an API, the states a value may
  hold, the steps a phase performs. Give the closed list of what is allowed
  and let everything outside it be excluded by omission; a requirement that
  enumerates forbidden alternatives is both longer and weaker, because the
  enumeration is never complete and the reader still has to infer the rule
  behind it. A requirement, a design constraint and a phase step each read as
  an instruction that can be carried out and checked as written. Reserve the
  negative form for a trap that has actually been hit and would otherwise be
  walked into again, state it once with the evidence for it, and give the
  positive rule to follow in its place.
- Each fact belongs in exactly one place. A document states each fact once,
  in the section that owns it, and every other section that needs it refers
  to that section by its label. This matters most in plans and requirement
  lists, where a summary, an overview, a design note and a phase step can
  each restate the same rule: the copies drift the moment one is edited, and
  a reader who finds two versions of a rule has no way to tell which is
  current. Prefer a reference (`R4`, `D9`, "the phase 3 sizing note") over a
  restatement, even a short one; where a fact genuinely reads better
  repeated, name the owning section in both places so the next edit updates
  both. A section whose whole content is a restatement of other sections - an
  overview, a summary of the design, a recap of requirements - is the form
  that drifts fastest, so write the document without one.
- File names are `snake_case.md`, named after the subject (a library after
  its CMake target). A plan is named after the work, without a `-plan`
  suffix; its directory already says what it is.
- Only ASCII characters.
- A current document's "Future work" section is a list of links into
  `doc/plans/`, one line per link. The work itself is described in the plan.

## Index


### Libraries and library-level subsystems (`erhe/`)

- [erhe/box3d_physics.md](erhe/box3d_physics.md) (experimental): Box3D physics backend: capabilities, behavior, verification status
- [erhe/buffer.md](erhe/buffer.md) (stable): Provides buffer allocation primitives used by both GPU and CPU buffer systems
- [erhe/bvh_scene_acceleration.md](erhe/bvh_scene_acceleration.md) (mostly stable): Hybrid asynchronous TLAS acceleration for the bvh raytrace backend
- [erhe/catmull_clark.md](erhe/catmull_clark.md) (mostly stable): Catmull-Clark subdivision performance: measured costs and optimization backlog
- [erhe/circular_ring_buffer.md](erhe/circular_ring_buffer.md) (stable): Provides `Circular_ring_buffer_algorithm`: the pure-arithmetic core of a circular ring buffer with producer/consumer wrap counts and deferred, frame-indexed reclamation
- [erhe/codegen.md](erhe/codegen.md) (stable): A Python code generator that produces C++ structs with versioned JSON serialization, deserialization, and rich reflection from Python definitions
- [erhe/commands.md](erhe/commands.md) (stable): Input command system that maps physical input events (keyboard, mouse, controller, XR actions, menus) to application commands
- [erhe/dataformat.md](erhe/dataformat.md) (stable): Graphics-API-agnostic pixel and vertex data format definitions
- [erhe/debug_renderer_multiview.md](erhe/debug_renderer_multiview.md) (stable): Multiview port of Debug_renderer: view UBO, pipelines, bucket internals
- [erhe/defer.md](erhe/defer.md) (stable): Provides a scope-guard utility for deferred execution
- [erhe/draw_list_material_set.md](erhe/draw_list_material_set.md) (stable): Material_set: material buffer and texture heap owned per draw-list set (D-labels cited from code)
- [erhe/draw_list_performance_improvements.md](erhe/draw_list_performance_improvements.md) (mostly stable): Caching of primitive records in the draw-list renderer, with measured results
- [erhe/draw_list_renderer.md](erhe/draw_list_renderer.md) (stable): Persistent Draw_list_scene renderer: requirements, scope, components
- [erhe/file.md](erhe/file.md) (stable): Filesystem utility library providing file read/write, path conversion, file existence checks, directory creation, and native file open/save dialogs
- [erhe/frame_pacing.md](erhe/frame_pacing.md) (mostly stable): Frame pacing algorithm core: decides how many display refresh periods each frame is visible (cadence), when the CPU may start per-frame work (release scheduling), which vsync slot each frame targets, and how many images may be queued for presentation
- [erhe/geogram.md](erhe/geogram.md) (mostly stable): Geogram contract: FMA build rule, serialization lock, degenerate-hull guard, diagnosis tooling
- [erhe/geometry.md](erhe/geometry.md) (mostly stable): Polygon mesh geometry library built on Geogram
- [erhe/geometry_renderer.md](erhe/geometry_renderer.md) (stable): Debug visualization of `erhe::geometry::Geometry` meshes
- [erhe/gl.md](erhe/gl.md) (stable): Python-generated type-safe OpenGL API wrappers
- [erhe/gl_worker_context_enforcement.md](erhe/gl_worker_context_enforcement.md) (mostly stable): Enforcing the GL worker-context blocking invariant against taskflow deadlocks
- [erhe/gl_worker_thread_contexts.md](erhe/gl_worker_thread_contexts.md) (stable): OpenGL worker-thread contexts: publication fencing, per-context containers, traps
- [erhe/gltf.md](erhe/gltf.md) (mostly stable): glTF file import and export using the fastgltf library
- [erhe/gpu_coding_rules.md](erhe/gpu_coding_rules.md) (stable): Rules for graphics backend and shader code: Vulkan validation cleanliness, UBO/SSBO layout
- [erhe/graph.md](erhe/graph.md) (stable): Generic directed acyclic graph (DAG) framework
- [erhe/graphics.md](erhe/graphics.md) (stable): Vulkan-style abstraction over OpenGL, Vulkan, and Metal
- [erhe/graphics_test_coverage.md](erhe/graphics_test_coverage.md) (stable): GPU test coverage matrix for erhe::graphics
- [erhe/graphics_test_nonheadless_port.md](erhe/graphics_test_nonheadless_port.md) (stable): Running erhe_graphics_gpu_tests on non-headless OpenGL and Metal
- [erhe/hash.md](erhe/hash.md) (stable): Provides lightweight hashing utilities for runtime FNV-1a hashing of floats and glm vectors, plus a compile-time constexpr XXH32 implementation used to hash string literals at compile time (e.g., for fast string-to-ID mapping)
- [erhe/imgui.md](erhe/imgui.md) (stable): Custom ImGui backend and window management layer for erhe
- [erhe/item.md](erhe/item.md) (stable): Foundational entity system for erhe
- [erhe/khr_physics_rigid_bodies_support.md](erhe/khr_physics_rigid_bodies_support.md) (mostly stable): KHR_physics_rigid_bodies and KHR_implicit_shapes glTF support and limitations
- [erhe/layout.md](erhe/layout.md) (stable): Layout nodes (Stack / Grid / Flow) design and behavior
- [erhe/log.md](erhe/log.md) (stable): Logging infrastructure built on spdlog
- [erhe/math.md](erhe/math.md) (stable): Collection of math utilities for 3D graphics: AABB and bounding sphere types, viewport projection/unprojection, input axis smoothing, and various vector/matrix helper functions used throughout erhe
- [erhe/mesh_memory.md](erhe/mesh_memory.md) (stable): Mesh_memory GPU vertex / index pools
- [erhe/mesh_memory_deferred_free.md](erhe/mesh_memory_deferred_free.md) (stable): Shared-primitive draw-list records and deferred free of render shapes
- [erhe/meshoptimizer_attribute_encodings.md](erhe/meshoptimizer_attribute_encodings.md) (mostly stable): Compact optimized-vertex attribute encodings (TBN quaternion, UV affine, weights)
- [erhe/meshoptimizer_integration.md](erhe/meshoptimizer_integration.md) (stable): meshoptimizer-based mesh optimization: source / optimized separation, caching, edit bracket
- [erhe/message_bus.md](erhe/message_bus.md) (stable): Generic typed publish-subscribe message bus
- [erhe/metal_backend.md](erhe/metal_backend.md) (mostly stable): Metal graphics backend architecture
- [erhe/metal_headless.md](erhe/metal_headless.md) (experimental): Metal backend running headless through an emulated swapchain
- [erhe/multiview.md](erhe/multiview.md) (stable): Single-pass stereo (Vulkan multiview) for OpenXR on Quest 3
- [erhe/net.md](erhe/net.md) (experimental): A cross-platform (Windows/Linux/macOS) TCP networking layer built on raw BSD sockets with `select()`
- [erhe/pch.md](erhe/pch.md) (stable): Precompiled header (PCH) target for the erhe project
- [erhe/physics.md](erhe/physics.md) (stable): Thin abstraction layer over physics engines (Jolt Physics and Box3D, plus a null backend)
- [erhe/point_light_shadows.md](erhe/point_light_shadows.md) (stable): Cube-map point-light shadows and the resolved face-flip defect
- [erhe/primitive.md](erhe/primitive.md) (stable): Converts geometric data (from `erhe::geometry` Geometry or Triangle_soup) into GPU-ready vertex/index buffers
- [erhe/primitive_shape_locking.md](erhe/primitive_shape_locking.md) (stable): Primitive_shape build / state lock split for async geometry and BVH builds
- [erhe/procedural_sky.md](erhe/procedural_sky.md) (mostly stable): Hillaire atmospheric-scattering sky mode
- [erhe/profile.md](erhe/profile.md) (stable): Profiling abstraction layer that provides a unified macro API for instrumenting code with scoped profiling zones, GPU profiling, memory tracking, and mutex annotation
- [erhe/property.md](erhe/property.md) (mostly stable): A port of the WPF dependency-property system (`DependencyProperty`, `PropertyMetadata`, `DependencyObject`, `EffectiveValueEntry`, `DependencyPropertyKey`, the `Inherits` metadata flag) restricted to the value types erhe items need
- [erhe/property_inventory.md](erhe/property_inventory.md) (stable): Inventory of every property-system registration per item type
- [erhe/property_system.md](erhe/property_system.md) (stable): erhe::property dependency-property system design record (D-labels cited from code)
- [erhe/raytrace.md](erhe/raytrace.md) (stable): GPU ray-query raytracing
- [erhe/renderer.md](erhe/renderer.md) (stable): GPU rendering utilities for debug visualization and text overlay in 3D viewports
- [erhe/rendergraph.md](erhe/rendergraph.md) (stable): A directed acyclic graph (DAG) framework for organizing rendering operations
- [erhe/ring_buffer_memory.md](erhe/ring_buffer_memory.md) (mostly stable): Bounded ring-buffer memory for scene loads
- [erhe/scene.md](erhe/scene.md) (mostly stable): A glTF-like 3D scene graph providing hierarchical transforms, prim classes (see "Prim levels"), per-node value groups (physics, layout, grid, ...), animations, and scene management
- [erhe/scene_renderer.md](erhe/scene_renderer.md) (stable): Renders `erhe::scene` content (meshes, lights, shadows, skinning) to the GPU
- [erhe/shader_variants.md](erhe/shader_variants.md) (stable): standard.{vert,frag} uber-shader variant system
- [erhe/shader_workarounds.md](erhe/shader_workarounds.md) (stable): Driver-capability shader defines and workaround policy
- [erhe/shadow_tight_fit.md](erhe/shadow_tight_fit.md) (mostly stable): Shadow tight-fit optimization: landed steps and remaining candidates
- [erhe/shadows.md](erhe/shadows.md) (stable): Directional and point-light shadow mapping
- [erhe/smoke.md](erhe/smoke.md) (stable): A standalone smoke test executable that stress-tests the `erhe::item` hierarchy system
- [erhe/subdivision_crease_edges.md](erhe/subdivision_crease_edges.md) (stable): Catmull-Clark semi-sharp crease edges
- [erhe/texgen.md](erhe/texgen.md) (experimental): Procedural texture shader-code composition core: the codegen layer under the editor's texture graph (`doc/editor/texture_graph.md`)
- [erhe/time.md](erhe/time.md) (stable): Time-related utilities providing high-precision sleep, scoped timers for profiling initialization and frame phases, and timestamp string formatting
- [erhe/ui.md](erhe/ui.md) (stable): Font rasterization and text layout utilities
- [erhe/usd.md](erhe/usd.md) (mostly stable): `erhe::usd` is the only erhe library that includes LightUSD headers
- [erhe/usd_compatibility.md](erhe/usd_compatibility.md) (stable): erhe <-> OpenUSD concept and naming mapping tables
- [erhe/usd_compatibility_design.md](erhe/usd_compatibility_design.md) (mostly stable): USD compatibility design record and current state (C/U/M/X labels cited from code)
- [erhe/usd_node_graphs.md](erhe/usd_node_graphs.md) (mostly stable): Texture and geometry node graphs as UsdShade NodeGraph / Shader prims
- [erhe/utility.md](erhe/utility.md) (stable): Small standalone utility classes and functions used across the erhe codebase: memory alignment helpers, bitwise test functions, a fixed-size pimpl smart pointer, and an interned debug label type backed by a thread-safe string pool
- [erhe/verify.md](erhe/verify.md) (stable): Provides two foundational assertion macros used throughout the entire erhe codebase: `ERHE_VERIFY(expression)` for runtime condition checks and `ERHE_FATAL(format, ...)` for unconditional abort with a formatted error message
- [erhe/vertex_position_quantization.md](erhe/vertex_position_quantization.md) (experimental): Quantized vertex positions across backends
- [erhe/voxel.md](erhe/voxel.md) (experimental): Sparse voxel signed distance fields (SDF) built on OpenVDB narrow-band level sets
- [erhe/vulkan_backend.md](erhe/vulkan_backend.md) (stable): Vulkan graphics backend: device, frame lifecycle, binding model, sync, swapchain
- [erhe/window.md](erhe/window.md) (stable): Platform windowing abstraction over SDL and GLFW
- [erhe/xr.md](erhe/xr.md) (stable): OpenXR integration for VR/AR headset support
- [erhe/xr_controller_render_model.md](erhe/xr_controller_render_model.md) (mostly stable): XR controller render models (XR_FB_render_model)

### Editor (`editor/`)

- [editor/active_item.md](editor/active_item.md) (stable): The one explicit active item in Selection: rules, message, undo, MCP
- [editor/asset_browser_scan.md](editor/asset_browser_scan.md) (stable): Two-phase asset browser scan: worker directory walk, main-thread tree build
- [editor/asset_manager.md](editor/asset_manager.md) (mostly stable): Single-loader asset manager: identity, ownership, usership, workflow verbs
- [editor/async_asset_loading.md](editor/async_asset_loading.md) (mostly stable): Asynchronous glTF / asset loading pipeline: tasks, threads, budgets
- [editor/async_asset_loading_design.md](editor/async_asset_loading_design.md) (mostly stable): Design record behind async asset loading (numbered sections cited from code)
- [editor/brushes.md](editor/brushes.md) (stable): Implements the brush system for placing parametric mesh shapes onto surfaces
- [editor/child_prim_creation.md](editor/child_prim_creation.md) (stable): Creating a typed child prim under any prim from the Hierarchy context menu and MCP
- [editor/coding_rules.md](editor/coding_rules.md) (stable): Rules for editor code: part construction, logging, scene-hosted references, config JSON
- [editor/command_script.md](editor/command_script.md) (stable): Startup commands.json scene script: commands, execution and undo model
- [editor/config.md](editor/config.md) (stable): Editor configuration loading
- [editor/content_library.md](editor/content_library.md) (mostly stable): Indexes a scene's reusable resources - materials, brushes, styles, textures, physics items, animations, skins and node graphs - which live as prims in the scene's own tree
- [editor/content_library_folders.md](editor/content_library_folders.md) (stable): Folder scopes inside content-library kind scopes: creation, inheritance, persistence
- [editor/content_library_ownership.md](editor/content_library_ownership.md) (stable): Content_library ownership and host resolution; owning vs reference entries
- [editor/create.md](editor/create.md) (stable): Provides the Create tool and shape generator classes for interactively creating new mesh primitives in the scene
- [editor/ddgi.md](editor/ddgi.md) (experimental): Dynamic diffuse global illumination: probe volume, tracing, atlases, sampling
- [editor/editor.md](editor/editor.md) (mostly stable): The editor is the main application built on the erhe C++ graphics engine
- [editor/four_view.md](editor/four_view.md) (experimental): Four linked viewports (top, front, right, perspective) docked as a 2 x 2 grid with a cross splitter
- [editor/grid.md](editor/grid.md) (mostly stable): Editor grid: rendering, per-view plane in orthographic views, hover and snap, depth mode
- [editor/geometry_graph_mesh.md](editor/geometry_graph_mesh.md) (mostly stable): Geometry node graph as a first-class Graph_mesh asset
- [editor/geometry_graph_transform_from_node.md](editor/geometry_graph_transform_from_node.md) (mostly stable): transform_from_node geometry-graph node driven by a scene node
- [editor/geometry_nodes.md](editor/geometry_nodes.md) (mostly stable): Geometry Nodes status and Blender architecture analysis
- [editor/gltf_scene_roundtrip.md](editor/gltf_scene_roundtrip.md) (mostly stable): glTF-only scene persistence: build record and open items
- [editor/graph_editor.md](editor/graph_editor.md) (mostly stable): Shared graph-editor layer; geometry and texture graph editors; legacy shader graph
- [editor/graph_texture.md](editor/graph_texture.md) (mostly stable): Texture node graph as a first-class Graph_texture asset
- [editor/graphics.md](editor/graphics.md) (stable): Editor-level graphics utilities: icon management, thumbnail generation, and gradient textures
- [editor/import_undo_reference_clearing.md](editor/import_undo_reference_clearing.md) (mostly stable): Clearing stale editor references after an undo removes imported content
- [editor/lattice_deform_geometry_node.md](editor/lattice_deform_geometry_node.md) (mostly stable): Lattice free-form deformation geometry-graph node
- [editor/lightmap_baking.md](editor/lightmap_baking.md) (experimental): Interactive lightmap baker: architecture, texel density, bake and sampling features
- [editor/lightmap_texture_viewer.md](editor/lightmap_texture_viewer.md) (stable): Lightmap Texture viewer window: atlas display, edge and hover overlays
- [editor/mesh_component_selection.md](editor/mesh_component_selection.md) (mostly stable): Face / edge / vertex selection and viewport overlay
- [editor/operations.md](editor/operations.md) (stable): Implements the undo/redo operation system and all concrete editor operations
- [editor/parsers.md](editor/parsers.md) (mostly stable): File format importers for loading 3D content into the editor, plus the erhe-authored glTF scene persistence entry points (doc/editor/gltf_scene_roundtrip.md)
- [editor/physics.md](editor/physics.md) (stable): Physics-related tools, UI, and collision shape generation for the editor
- [editor/post_processing.md](editor/post_processing.md) (mostly stable): Bloom post-processing pipeline: textures, passes, synchronization
- [editor/prewarm.md](editor/prewarm.md) (stable): Init-time GPU shader and pipeline prewarming
- [editor/properties_window.md](editor/properties_window.md) (stable): Properties window single registered-property row path
- [editor/raytrace.md](editor/raytrace.md) (experimental): GPU ray-query raytracing
- [editor/raytrace_materials.md](editor/raytrace_materials.md) (mostly stable): Material-aware ray-traced rendering: textures, glass, light sampling
- [editor/reloadable_asset_loads.md](editor/reloadable_asset_loads.md) (mostly stable): Undone glTF imports drop their payload and re-read on redo
- [editor/renderers.md](editor/renderers.md) (stable): Low-level rendering infrastructure for the editor: shader programs, GPU memory management, ID-based picking, render pass composition, and viewport configuration
- [editor/rendergraph.md](editor/rendergraph.md) (stable): Editor-specific render graph nodes that extend `erhe::rendergraph` for shadow mapping, scene rendering, and post-processing
- [editor/rendering.md](editor/rendering.md) (stable): > This document was mostly written by Claude and may contain inaccuracies
- [editor/scene.md](editor/scene.md) (mostly stable): Manages 3D scene data for the editor: scene roots (the top-level scene container), scene views (camera + viewport rendering), viewport management, scene commands (create camera/light/rendertarget), physics-scene coupling, raytrace integration, and scene serialization
- [editor/scene_serialization.md](editor/scene_serialization.md) (stable): erhe glTF scene save / open pipeline and what is persisted
- [editor/selection.md](editor/selection.md) (stable): Per-scene selection and active scene
- [editor/settings_codegen_scene_reference.md](editor/settings_codegen_scene_reference.md) (stable): Reference map for three subsystems that are easy to lose track of: the editor settings model, the `erhe_codegen` struct generator, and scene save / load
- [editor/style_library.md](editor/style_library.md) (mostly stable): Style items: live property inheritance, assignment, persistence
- [editor/texture_graph.md](editor/texture_graph.md) (mostly stable): Procedural texture graph status against Material Maker
- [editor/tools.md](editor/tools.md) (stable): Defines the Tool abstraction and the Tools container, plus several concrete tools for interacting with the 3D scene
- [editor/transform.md](editor/transform.md) (stable): Transform gizmo system for interactive translate, rotate, and scale operations
- [editor/weight_paint.md](editor/weight_paint.md) (experimental): Blender-style weight painting
- [editor/window_target_items.md](editor/window_target_items.md) (mostly stable): Editor windows with independent target items
- [editor/windows.md](editor/windows.md) (stable): ImGui window implementations for the editor UI, including viewport display, property inspection, settings, and configuration

### Building, testing and platforms

- [android.md](android.md) (experimental): Android (mobile flavor) port of the editor: build, packaging, verification ladder
- [building.md](building.md) (stable): Build instructions, platform requirements, CMake options, build scripts
- [cmake_conventions.md](cmake_conventions.md) (stable): CMake conventions, dependency pins and forks, code generation
- [msvc_build_issues.md](msvc_build_issues.md) (experimental): MSVC stale-object / ODR incident: diagnosis recipe and prevention options
- [quest.md](quest.md) (mostly stable): Building, installing and running the Quest 3 flavor
- [testing.md](testing.md) (stable): Unit test suites, the editor MCP test suite, test build trees, CI

### Agents (`agents/`)

- [agents/creations.md](agents/creations.md) (mostly stable): MCP-built showcase scenes and the editor features each exercises
- [agents/debugging.md](agents/debugging.md) (stable): Crash, hang and GPU debugging: Visual Studio MCP, lldb, RenderDoc, missing tools
- [agents/editor_runs.md](agents/editor_runs.md) (stable): Launching and driving the editor as an agent: MCP server, screenshots, user hand-off
- [agents/linux.md](agents/linux.md) (stable): Linux sessions: build trees, memory growth diagnostics
- [agents/lsai_usage_playbook.md](agents/lsai_usage_playbook.md) (mostly stable): LSAI usage playbook (erhe, C++)
- [agents/macos.md](agents/macos.md) (stable): macOS sessions: Xcode build trees, Xcode MCP, lldb
- [agents/mcp_api_guidelines.md](agents/mcp_api_guidelines.md) (stable): MCP tools take explicit parameters and never depend on UI state
- [agents/mcp_server_usage.md](agents/mcp_server_usage.md) (mostly stable): In-editor MCP server reference: transport, ports, auth, registration, Quest forwarding, every tool with its arguments
- [agents/mcp_ui_driving.md](agents/mcp_ui_driving.md) (mostly stable): Run-book for driving the editor user interface over MCP: ImGui introspection and input gestures
- [agents/orchestration_harness.md](agents/orchestration_harness.md) (stable): Orchestrator / coder / scout roles and brief format for delegated coding work
- [agents/quest.md](agents/quest.md) (stable): Quest / Android sessions: skills, launch protocol, validation, MCP over adb
- [agents/quest_renderdoc_capture.md](agents/quest_renderdoc_capture.md) (mostly stable): RenderDoc Meta Fork capture workflow on Quest
- [agents/renderdoc_fork.md](agents/renderdoc_fork.md) (mostly stable): Desktop GPU-debugging workflow with the RenderDoc fork MCP server
- [agents/semantic_cpp_mcp_setup_xmp4_lsai.md](agents/semantic_cpp_mcp_setup_xmp4_lsai.md) (mostly stable): Semantic C++ code intelligence for Claude Code: xmp4 + LSAI
- [agents/usd_survey_gap_loop.md](agents/usd_survey_gap_loop.md) (stable): How the USD-WG asset survey is driven to zero gaps
- [agents/usd_wg_assets.md](agents/usd_wg_assets.md) (mostly stable): Script-generated survey of the ASWF USD-WG sample assets
- [agents/windows.md](agents/windows.md) (stable): Windows sessions: build trees, edit-build loop, clangd database, shell hygiene

### Frame pacing (`frame_pacing/`)

- [frame_pacing/requirements.md](frame_pacing/requirements.md) (stable): Frame pacer requirements
- [frame_pacing/algorithm.md](frame_pacing/algorithm.md) (stable): Frame pacer scheduling algorithm (verified design)
- [frame_pacing/behavior.md](frame_pacing/behavior.md) (stable): Frame pacer normative behavior specification
- [frame_pacing/capability_tiers.md](frame_pacing/capability_tiers.md) (experimental): Capability tiers (W / A / S / OFF) and measured driver behavior
- [frame_pacing/control_model.md](frame_pacing/control_model.md) (stable): Control-theory model of the pacer
- [frame_pacing/inputs.md](frame_pacing/inputs.md) (stable): Inputs and data the pacer consumes
- [frame_pacing/user_interface.md](frame_pacing/user_interface.md) (mostly stable): Verification window and simulated workload
- [frame_pacing/implementation_plan.md](frame_pacing/implementation_plan.md) (stable): Implementation work order (to be trimmed to its standing content)

### glTF extensions (`gltf_extensions/`)

- [gltf_extensions/README.md](gltf_extensions/README.md) (mostly stable): Overview of the ERHE_* extensions
- [gltf_extensions/flags.md](gltf_extensions/flags.md) (mostly stable): Item flag encoding
- [gltf_extensions/ERHE_asset_reference.md](gltf_extensions/ERHE_asset_reference.md) (mostly stable): ERHE_asset_reference
- [gltf_extensions/ERHE_brushes.md](gltf_extensions/ERHE_brushes.md) (mostly stable): ERHE_brushes
- [gltf_extensions/ERHE_camera.md](gltf_extensions/ERHE_camera.md) (mostly stable): ERHE_camera
- [gltf_extensions/ERHE_collections.md](gltf_extensions/ERHE_collections.md) (mostly stable): ERHE_collections
- [gltf_extensions/ERHE_geometry.md](gltf_extensions/ERHE_geometry.md) (mostly stable): ERHE_geometry
- [gltf_extensions/ERHE_light.md](gltf_extensions/ERHE_light.md) (mostly stable): ERHE_light
- [gltf_extensions/ERHE_material.md](gltf_extensions/ERHE_material.md) (mostly stable): ERHE_material
- [gltf_extensions/ERHE_node.md](gltf_extensions/ERHE_node.md) (mostly stable): ERHE_node
- [gltf_extensions/ERHE_node_graphs.md](gltf_extensions/ERHE_node_graphs.md) (mostly stable): ERHE_node_graphs
- [gltf_extensions/ERHE_scene.md](gltf_extensions/ERHE_scene.md) (mostly stable): ERHE_scene

### Plans (`plans/`)

- [plans/android.md](plans/android.md) (proposed): Android: full editor on a phone
- [plans/animation_keyframing.md](plans/animation_keyframing.md) (proposed): Keyframing and timeline for the Animation window
- [plans/asset_loading.md](plans/asset_loading.md) (proposed): Asset loading: outstanding work
- [plans/brushes.md](plans/brushes.md) (proposed): Brushes: outstanding work
- [plans/build_tooling.md](plans/build_tooling.md) (proposed): Build tooling: make a stale VS build fail loudly
- [plans/catmull_clark.md](plans/catmull_clark.md) (proposed): Catmull-Clark optimization candidates
- [plans/command_script.md](plans/command_script.md) (proposed): Editor command scripts: outstanding work
- [plans/content_library.md](plans/content_library.md) (proposed): Content library: outstanding work
- [plans/crash_signal.md](plans/crash_signal.md) (proposed): Positive crash signal for harness-run apps
- [plans/ddgi.md](plans/ddgi.md) (proposed): DDGI follow-ups
- [plans/draw_list_renderer.md](plans/draw_list_renderer.md) (proposed): Draw list renderer: outstanding work
- [plans/editor.md](plans/editor.md) (proposed): Editor: outstanding feature work
- [plans/editor_improvements.md](plans/editor_improvements.md) (proposed): Prioritized backlog of editor architecture improvements
- [plans/frame_pacing.md](plans/frame_pacing.md) (proposed): Frame pacing: outstanding work
- [plans/geometry_graph/attribute_projection.md](plans/geometry_graph/attribute_projection.md) (proposed): project_attribute geometry-graph node: design research
- [plans/geometry_graph/creation_tools.md](plans/geometry_graph/creation_tools.md) (in progress): AI creation tools and geometry-graph follow-ups
- [plans/geometry_graph/geometry_nodes.md](plans/geometry_graph/geometry_nodes.md) (proposed): Geometry nodes: field system and further node types
- [plans/geometry_graph/openvdb_sdf.md](plans/geometry_graph/openvdb_sdf.md) (in progress): OpenVDB SDF support in the geometry graph (phase 3 onward)
- [plans/gl_worker_contexts.md](plans/gl_worker_contexts.md) (proposed): GL worker contexts: outstanding work
- [plans/gltf.md](plans/gltf.md) (in progress): glTF: outstanding work
- [plans/gltf_prefabs.md](plans/gltf_prefabs.md) (in progress): glTF scene prefabs: remaining phases
- [plans/gltf_properties_extension.md](plans/gltf_properties_extension.md) (in progress): ERHE_*_properties glTF extensions (steps 2-5)
- [plans/graph_editor.md](plans/graph_editor.md) (proposed): Graph editor: remaining shared-layer work
- [plans/graphics_tests.md](plans/graphics_tests.md) (proposed): Graphics tests: outstanding work
- [plans/id_renderer.md](plans/id_renderer.md) (proposed): ID renderer coverage
- [plans/init_status_display.md](plans/init_status_display.md) (proposed): Multi-threaded init status reporting
- [plans/lightmap/lightmap_baking.md](plans/lightmap/lightmap_baking.md) (in progress): Lightmap baking follow-ups
- [plans/lightmap/seam_driven_unwrap.md](plans/lightmap/seam_driven_unwrap.md) (in progress): Seam-driven lightmap unwrap (phases 2-4)
- [plans/lightmap/tiling.md](plans/lightmap/tiling.md) (in progress): Lightmap spatial tiling and world-space partition
- [plans/mesh_component_selection.md](plans/mesh_component_selection.md) (proposed): Mesh component selection: outstanding work
- [plans/mesh_memory.md](plans/mesh_memory.md) (proposed): Mesh memory and primitive shapes: outstanding work
- [plans/meshoptimizer.md](plans/meshoptimizer.md) (proposed): Mesh optimization: outstanding work
- [plans/node_editor_native_rendering.md](plans/node_editor_native_rendering.md) (in progress): Node editor native-resolution rendering: live-interaction verification
- [plans/occlusion_culling.md](plans/occlusion_culling.md) (proposed): Raster occlusion culling
- [plans/physics.md](plans/physics.md) (in progress): Physics: outstanding work
- [plans/post_processing.md](plans/post_processing.md) (proposed): Post-processing follow-ups
- [plans/procedural_sky.md](plans/procedural_sky.md) (proposed): Procedural sky verification
- [plans/property_system.md](plans/property_system.md) (proposed): Property system: remaining work
- [plans/raytrace.md](plans/raytrace.md) (proposed): Ray tracing follow-ups
- [plans/rigging/fabrik_ik.md](plans/rigging/fabrik_ik.md) (proposed): FABRIK inverse kinematics requirements
- [plans/rigging/rigging_tools.md](plans/rigging/rigging_tools.md) (proposed): Rigging tools roadmap (IK, constraints, skinning, drivers)
- [plans/rigging/skeleton_editing.md](plans/rigging/skeleton_editing.md) (proposed): Rigging Phase 3 - skeleton editing and posing basics requirements
- [plans/shadows.md](plans/shadows.md) (proposed): Shadow follow-ups
- [plans/spirv_cache.md](plans/spirv_cache.md) (proposed): SPIR-V cache robustness
- [plans/texture_graph.md](plans/texture_graph.md) (proposed): Texture graph backlog
- [plans/texture_memory.md](plans/texture_memory.md) (proposed): Per-scene texture memory cost
- [plans/timeline_editor.md](plans/timeline_editor.md) (proposed): Animation timeline and curve editor
- [plans/usd_compatibility.md](plans/usd_compatibility.md) (proposed): USD compatibility: remaining work
- [plans/uv_editor.md](plans/uv_editor.md) (proposed): UV editor modeled on Blender's
- [plans/virtualcity_vanishing_meshes.md](plans/virtualcity_vanishing_meshes.md) (proposed): Open defect: overlapping meshes vanish on first hover
- [plans/vulkan_backend.md](plans/vulkan_backend.md) (proposed): Vulkan backend: known issues
- [plans/wasm_webgpu_port.md](plans/wasm_webgpu_port.md) (proposed): WebAssembly + WebGPU port of the editor
- [plans/weight_paint.md](plans/weight_paint.md) (proposed): Weight painting: outstanding work
- [plans/xr.md](plans/xr.md) (proposed): XR: outstanding work

### Reference (`reference/`)

- [reference/audit_erhe_2026_06_21.md](reference/audit_erhe_2026_06_21.md): Architecture, foundations and security audit report (2026-06-21)
- [reference/esoterica_rendering.md](reference/esoterica_rendering.md): Esoterica vs erhe rendering comparison
- [reference/forge_erhe.md](reference/forge_erhe.md): SDL3 GPU concepts mapped to erhe's graphics API
- [reference/geogram_atlas_packing_feature_request.md](reference/geogram_atlas_packing_feature_request.md): Feature request to Geogram / xatlas maintainers
- [reference/geogram_thread_safety_issue.md](reference/geogram_thread_safety_issue.md): Unfiled Geogram thread-safety issue draft
- [reference/gl_spec_section_5.md](reference/gl_spec_section_5.md): Transcribed OpenGL spec chapter 5 (shared objects, multiple contexts)
- [reference/glslang_bug_report_debugglobalvariable.md](reference/glslang_bug_report_debugglobalvariable.md): glslang DebugGlobalVariable SPIR-V bug report
- [reference/gltf_2_1_item_flags_comment.md](reference/gltf_2_1_item_flags_comment.md): glTF 2.1 per-node flags survey and issue comment
- [reference/gltf_sample_renderer_comparison.md](reference/gltf_sample_renderer_comparison.md): erhe vs Khronos glTF-Sample-Renderer feature comparison
- [reference/nova3d_comparison.md](reference/nova3d_comparison.md): Nova3D vs erhe AI creation tooling comparison
- [reference/nvidia_present_timing_driver_report.md](reference/nvidia_present_timing_driver_report.md): NVIDIA VK_EXT_present_timing driver issue report
- [reference/property_system_wpf_comparison.md](reference/property_system_wpf_comparison.md): erhe::property vs WPF dependency properties
- [reference/quest_profiling_2026_05_01.md](reference/quest_profiling_2026_05_01.md): Quest 3 GPU profiling report (2026-05-01)
