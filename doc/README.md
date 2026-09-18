# erhe documentation

Stability: stable

This directory holds erhe's documentation. Each file describes how the code
behaves now; outstanding work lives under `plans/` and is linked from the
document it extends. `py -3 scripts/check_doc_links.py` verifies the rules
below and is part of every documentation change.

## Layout

- `doc/<subject>.md` describes a subsystem, feature or workflow as it is
  today. A landed design record stays here; its numbered section labels
  (`D5`, `R3`, `C7`) are cited from source comments and stay stable across
  edits.
- `doc/erhe_<name>.md` is the document of the library built by CMake target
  `erhe_<name>` (purpose, key types, public API, dependencies, implementation
  notes). `doc/editor.md` and `doc/editor_<subdir>.md` are the editor's.
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

- A document describes the present. History belongs in commit messages
  (`git log --follow` on the document and the code it covers); see the
  "Live documents" rule in AGENTS.md.
- File names are `snake_case.md`, named after the subject (a library after
  its CMake target). A plan is named after the work, without a `-plan`
  suffix; its directory already says what it is.
- Only ASCII characters.
- A current document's "Future work" section is a list of links into
  `doc/plans/`, one line per link. The work itself is described in the plan.

## Index


### Libraries (`erhe::*`)

- [erhe_buffer.md](erhe_buffer.md) (stable): Provides buffer allocation primitives used by both GPU and CPU buffer systems
- [erhe_circular_ring_buffer.md](erhe_circular_ring_buffer.md) (stable): Provides `Circular_ring_buffer_algorithm`: the pure-arithmetic core of a circular ring buffer with producer/consumer wrap counts and deferred, frame-indexed reclamation
- [erhe_codegen.md](erhe_codegen.md) (stable): A Python code generator that produces C++ structs with versioned JSON serialization, deserialization, and rich reflection from Python definitions
- [erhe_commands.md](erhe_commands.md) (stable): Input command system that maps physical input events (keyboard, mouse, controller, XR actions, menus) to application commands
- [erhe_dataformat.md](erhe_dataformat.md) (stable): Graphics-API-agnostic pixel and vertex data format definitions
- [erhe_defer.md](erhe_defer.md) (stable): Provides a scope-guard utility for deferred execution
- [erhe_file.md](erhe_file.md) (stable): Filesystem utility library providing file read/write, path conversion, file existence checks, directory creation, and native file open/save dialogs
- [erhe_frame_pacing.md](erhe_frame_pacing.md) (mostly stable): Frame pacing algorithm core: decides how many display refresh periods each frame is visible (cadence), when the CPU may start per-frame work (release scheduling), which vsync slot each frame targets, and how many images may be queued for presentation
- [erhe_geometry.md](erhe_geometry.md) (mostly stable): Polygon mesh geometry library built on Geogram
- [erhe_geometry_renderer.md](erhe_geometry_renderer.md) (stable): Debug visualization of `erhe::geometry::Geometry` meshes
- [erhe_gl.md](erhe_gl.md) (stable): Python-generated type-safe OpenGL API wrappers
- [erhe_gltf.md](erhe_gltf.md) (mostly stable): glTF file import and export using the fastgltf library
- [erhe_graph.md](erhe_graph.md) (stable): Generic directed acyclic graph (DAG) framework
- [erhe_graphics.md](erhe_graphics.md) (stable): Vulkan-style abstraction over OpenGL, Vulkan, and Metal
- [erhe_hash.md](erhe_hash.md) (stable): Provides lightweight hashing utilities for runtime FNV-1a hashing of floats and glm vectors, plus a compile-time constexpr XXH32 implementation used to hash string literals at compile time (e.g., for fast string-to-ID mapping)
- [erhe_imgui.md](erhe_imgui.md) (stable): Custom ImGui backend and window management layer for erhe
- [erhe_item.md](erhe_item.md) (stable): Foundational entity system for erhe
- [erhe_log.md](erhe_log.md) (stable): Logging infrastructure built on spdlog
- [erhe_math.md](erhe_math.md) (stable): Collection of math utilities for 3D graphics: AABB and bounding sphere types, viewport projection/unprojection, input axis smoothing, and various vector/matrix helper functions used throughout erhe
- [erhe_message_bus.md](erhe_message_bus.md) (stable): Generic typed publish-subscribe message bus
- [erhe_net.md](erhe_net.md) (experimental): A cross-platform (Windows/Linux/macOS) TCP networking layer built on raw BSD sockets with `select()`
- [erhe_pch.md](erhe_pch.md) (stable): Precompiled header (PCH) target for the erhe project
- [erhe_physics.md](erhe_physics.md) (stable): Thin abstraction layer over physics engines (Jolt Physics and Box3D, plus a null backend)
- [erhe_primitive.md](erhe_primitive.md) (stable): Converts geometric data (from `erhe::geometry` Geometry or Triangle_soup) into GPU-ready vertex/index buffers
- [erhe_profile.md](erhe_profile.md) (stable): Profiling abstraction layer that provides a unified macro API for instrumenting code with scoped profiling zones, GPU profiling, memory tracking, and mutex annotation
- [erhe_property.md](erhe_property.md) (mostly stable): A port of the WPF dependency-property system (`DependencyProperty`, `PropertyMetadata`, `DependencyObject`, `EffectiveValueEntry`, `DependencyPropertyKey`, the `Inherits` metadata flag) restricted to the value types erhe items need
- [erhe_raytrace.md](erhe_raytrace.md) (stable): Abstraction layer for CPU ray tracing / ray intersection queries
- [erhe_renderer.md](erhe_renderer.md) (stable): GPU rendering utilities for debug visualization and text overlay in 3D viewports
- [erhe_rendergraph.md](erhe_rendergraph.md) (stable): A directed acyclic graph (DAG) framework for organizing rendering operations
- [erhe_scene.md](erhe_scene.md) (mostly stable): A glTF-like 3D scene graph providing hierarchical transforms, prim classes (see "Prim levels"), node attachments (physics, layout, grid, ...), animations, and scene management
- [erhe_scene_renderer.md](erhe_scene_renderer.md) (stable): Renders `erhe::scene` content (meshes, lights, shadows, skinning) to the GPU
- [erhe_smoke.md](erhe_smoke.md) (stable): A standalone smoke test executable that stress-tests the `erhe::item` hierarchy system
- [erhe_texgen.md](erhe_texgen.md) (experimental): Procedural texture shader-code composition core (Phase 1 of `doc/texture_graph.md`, issue #199)
- [erhe_time.md](erhe_time.md) (stable): Time-related utilities providing high-precision sleep, scoped timers for profiling initialization and frame phases, and timestamp string formatting
- [erhe_ui.md](erhe_ui.md) (stable): Font rasterization and text layout utilities
- [erhe_usd.md](erhe_usd.md) (mostly stable): `erhe::usd` is the only erhe library that includes LightUSD headers
- [erhe_utility.md](erhe_utility.md) (stable): Small standalone utility classes and functions used across the erhe codebase: memory alignment helpers, bitwise test functions, a fixed-size pimpl smart pointer, and an interned debug label type backed by a thread-safe string pool
- [erhe_verify.md](erhe_verify.md) (stable): Provides two foundational assertion macros used throughout the entire erhe codebase: `ERHE_VERIFY(expression)` for runtime condition checks and `ERHE_FATAL(format, ...)` for unconditional abort with a formatted error message
- [erhe_voxel.md](erhe_voxel.md) (experimental): Sparse voxel signed distance fields (SDF) built on OpenVDB narrow-band level sets
- [erhe_window.md](erhe_window.md) (stable): Platform windowing abstraction over SDL and GLFW
- [erhe_xr.md](erhe_xr.md) (stable): OpenXR integration for VR/AR headset support

### Editor

- [editor.md](editor.md) (mostly stable): The editor is the main application built on the erhe C++ graphics engine
- [editor_brushes.md](editor_brushes.md) (stable): Implements the brush system for placing parametric mesh shapes onto surfaces
- [editor_config.md](editor_config.md) (stable): Editor configuration loading
- [editor_content_library.md](editor_content_library.md) (mostly stable): Indexes a scene's reusable resources - materials, brushes, styles, textures, physics items, animations, skins and node graphs - which live as prims in the scene's own tree
- [editor_create.md](editor_create.md) (stable): Provides the Create tool and shape generator classes for interactively creating new mesh primitives in the scene
- [editor_graphics.md](editor_graphics.md) (stable): Editor-level graphics utilities: icon management, thumbnail generation, and gradient textures
- [editor_operations.md](editor_operations.md) (stable): Implements the undo/redo operation system and all concrete editor operations
- [editor_parsers.md](editor_parsers.md) (mostly stable): File format importers for loading 3D content into the editor, plus the erhe-authored glTF scene persistence entry points (doc/gltf_scene_roundtrip.md phase 4)
- [editor_physics.md](editor_physics.md) (stable): Physics-related tools, UI, and collision shape generation for the editor
- [editor_renderers.md](editor_renderers.md) (stable): Low-level rendering infrastructure for the editor: shader programs, GPU memory management, ID-based picking, render pass composition, and viewport configuration
- [editor_rendergraph.md](editor_rendergraph.md) (stable): Editor-specific render graph nodes that extend `erhe::rendergraph` for shadow mapping, scene rendering, and post-processing
- [editor_scene.md](editor_scene.md) (mostly stable): Manages 3D scene data for the editor: scene roots (the top-level scene container), scene views (camera + viewport rendering), viewport management, scene commands (create camera/light/rendertarget), physics-scene coupling, raytrace integration, and scene serialization
- [editor_tools.md](editor_tools.md) (stable): Defines the Tool abstraction and the Tools container, plus several concrete tools for interacting with the 3D scene
- [editor_transform.md](editor_transform.md) (stable): Transform gizmo system for interactive translate, rotate, and scale operations
- [editor_windows.md](editor_windows.md) (stable): ImGui window implementations for the editor UI, including viewport display, property inspection, settings, and configuration

### Subsystems, features and workflows

- [251-node-editor-native-rendering-notes.md](251-node-editor-native-rendering-notes.md) (experimental): Node editor native-resolution rendering migration log (to be folded into graph_editor.md)
- [active_item.md](active_item.md) (stable): The one explicit active item in Selection: rules, message, undo, MCP
- [agent_orchestration_harness.md](agent_orchestration_harness.md) (stable): Orchestrator / coder / scout roles and brief format for delegated coding work
- [ai_creations.md](ai_creations.md) (mostly stable): MCP-built showcase scenes and the editor features each exercises
- [android.md](android.md) (experimental): Android (mobile flavor) port of the editor: build, packaging, verification ladder
- [asset_browser_scan.md](asset_browser_scan.md) (stable): Two-phase asset browser scan: worker directory walk, main-thread tree build
- [asset_manager.md](asset_manager.md) (mostly stable): Single-loader asset manager: identity, ownership, usership, workflow verbs
- [async_asset_loading.md](async_asset_loading.md) (mostly stable): Asynchronous glTF / asset loading pipeline: tasks, threads, budgets
- [async_asset_loading_design.md](async_asset_loading_design.md) (mostly stable): Design record behind async asset loading (numbered sections cited from code)
- [box3d_physics.md](box3d_physics.md) (experimental): Box3D physics backend: capabilities, behavior, verification status
- [building.md](building.md) (stable): Build instructions, platform requirements, CMake options, build scripts
- [bvh_scene_acceleration.md](bvh_scene_acceleration.md) (mostly stable): Hybrid asynchronous TLAS acceleration for the bvh raytrace backend
- [catmull_clark.md](catmull_clark.md) (mostly stable): Catmull-Clark subdivision performance: measured costs and optimization backlog
- [command_script.md](command_script.md) (stable): Startup commands.json scene script: commands, execution and undo model
- [content_library_folders.md](content_library_folders.md) (stable): Folder scopes inside content-library kind scopes: creation, inheritance, persistence
- [content_library_ownership.md](content_library_ownership.md) (stable): Content_library ownership and host resolution; owning vs reference entries
- [ddgi.md](ddgi.md) (experimental): Dynamic diffuse global illumination: probe volume, tracing, atlases, sampling
- [debug_renderer_multiview.md](debug_renderer_multiview.md) (stable): Multiview port of Debug_renderer: view UBO, pipelines, bucket internals
- [draw_list_material_set.md](draw_list_material_set.md) (stable): Material_set: material buffer and texture heap owned per draw-list set (D-labels cited from code)
- [draw_list_material_set_context.md](draw_list_material_set_context.md) (stable): Root-cause narrative behind the material-set design (to be folded into draw_list_material_set.md)
- [draw_list_performance_improvements.md](draw_list_performance_improvements.md) (mostly stable): Caching of primitive records in the draw-list renderer, with measured results
- [draw_list_renderer.md](draw_list_renderer.md) (stable): Persistent Draw_list_scene renderer: requirements, scope, components
- [draw_list_renderer_plan.md](draw_list_renderer_plan.md) (stable): Six-phase rollout record of the draw-list renderer (to be folded into draw_list_renderer.md)
- [draw_list_renderer_results.md](draw_list_renderer_results.md) (stable): Measured results of the draw-list renderer rollout (to be folded into draw_list_renderer.md)
- [editor_rendering.md](editor_rendering.md) (stable): Editor rendergraph, composer passes, forward renderer, stencil protocol, tool rendering
- [editor_settings_codegen_scene_reference.md](editor_settings_codegen_scene_reference.md) (stable): Map of editor settings, the codegen struct generator and scene save/load
- [frame-time-after-usd-import-plan.md](frame-time-after-usd-import-plan.md) (stable): Post-USD-import frame time regression: causes and the change-driven fixes (R-labels cited from code)
- [geogram.md](geogram.md) (mostly stable): Geogram thread-safety contract, serialization lock and degenerate convex-hull guard
- [geometry_graph_mesh.md](geometry_graph_mesh.md) (mostly stable): Geometry node graph as a first-class Graph_mesh asset
- [geometry_graph_transform_from_node.md](geometry_graph_transform_from_node.md) (mostly stable): transform_from_node geometry-graph node driven by a scene node
- [geometry_nodes.md](geometry_nodes.md) (mostly stable): Geometry Nodes status and Blender architecture analysis
- [gl_worker_context_enforcement.md](gl_worker_context_enforcement.md) (mostly stable): Enforcing the GL worker-context blocking invariant against taskflow deadlocks
- [gl_worker_thread_contexts.md](gl_worker_thread_contexts.md) (stable): OpenGL worker-thread contexts: publication fencing, per-context containers, traps
- [gltf-load-speedup-plan.md](gltf-load-speedup-plan.md) (stable): glTF load deferral and parallelism speedups (partly superseded by async loading)
- [gltf_scene_roundtrip.md](gltf_scene_roundtrip.md) (mostly stable): glTF-only scene persistence: build record and open items
- [graph_editor.md](graph_editor.md) (mostly stable): Shared graph-editor layer; geometry and texture graph editors; legacy shader graph
- [graph_texture.md](graph_texture.md) (mostly stable): Texture node graph as a first-class Graph_texture asset
- [graphics_test_coverage.md](graphics_test_coverage.md) (stable): GPU test coverage matrix for erhe::graphics
- [graphics_test_nonheadless_port.md](graphics_test_nonheadless_port.md) (stable): Running erhe_graphics_gpu_tests on non-headless OpenGL and Metal
- [import_undo_reference_clearing.md](import_undo_reference_clearing.md) (mostly stable): Clearing stale editor references after an undo removes imported content
- [intermittent_main_loop_hang.md](intermittent_main_loop_hang.md) (experimental): Investigation log of the Quest main-loop hang traced to Geogram concurrency
- [khr_physics_rigid_bodies_support.md](khr_physics_rigid_bodies_support.md) (mostly stable): KHR_physics_rigid_bodies and KHR_implicit_shapes glTF support and limitations
- [lattice_deform_geometry_node.md](lattice_deform_geometry_node.md) (mostly stable): Lattice free-form deformation geometry-graph node
- [layout.md](layout.md) (stable): Layout nodes (Stack / Grid / Flow) design and behavior
- [lightmap_baking.md](lightmap_baking.md) (experimental): Interactive lightmap baker: architecture, texel density, bake and sampling features
- [lightmap_texture_viewer.md](lightmap_texture_viewer.md) (stable): Lightmap Texture viewer window: atlas display, edge and hover overlays
- [mcp_api_guidelines.md](mcp_api_guidelines.md) (stable): MCP tools take explicit parameters and never depend on UI state
- [mesh_component_selection.md](mesh_component_selection.md) (mostly stable): Face / edge / vertex selection and viewport overlay
- [mesh_memory.md](mesh_memory.md) (stable): Mesh_memory GPU vertex / index pools
- [mesh_memory_deferred_free.md](mesh_memory_deferred_free.md) (stable): Shared-primitive draw-list records and deferred free of render shapes
- [meshoptimizer_attribute_encodings.md](meshoptimizer_attribute_encodings.md) (mostly stable): Compact optimized-vertex attribute encodings (TBN quaternion, UV affine, weights)
- [meshoptimizer_integration.md](meshoptimizer_integration.md) (stable): meshoptimizer-based mesh optimization: source / optimized separation, caching, edit bracket
- [metal_backend.md](metal_backend.md) (mostly stable): Metal graphics backend architecture
- [metal_headless.md](metal_headless.md) (experimental): Metal backend running headless through an emulated swapchain
- [msvc_build_issues.md](msvc_build_issues.md) (experimental): MSVC stale-object / ODR incident: diagnosis recipe and prevention options
- [multiview.md](multiview.md) (stable): Single-pass stereo (Vulkan multiview) for OpenXR on Quest 3
- [node_attachment_editing.md](node_attachment_editing.md) (stable): Adding and removing Node_attachments on a node from the UI and MCP
- [operation-stack-reentrancy-plan.md](operation-stack-reentrancy-plan.md) (stable): Operation_stack main-thread-only contract (to be folded into editor_operations.md)
- [point_light_shadows.md](point_light_shadows.md) (stable): Cube-map point-light shadows and the resolved face-flip defect
- [post_processing.md](post_processing.md) (mostly stable): Bloom post-processing pipeline: textures, passes, synchronization
- [prewarm.md](prewarm.md) (stable): Init-time GPU shader and pipeline prewarming
- [primitive_shape_locking.md](primitive_shape_locking.md) (stable): Primitive_shape build / state lock split for async geometry and BVH builds
- [procedural_sky.md](procedural_sky.md) (mostly stable): Hillaire atmospheric-scattering sky mode
- [properties_window.md](properties_window.md) (stable): Properties window single registered-property row path
- [property_inventory.md](property_inventory.md) (stable): Inventory of every property-system registration per item type
- [property_system.md](property_system.md) (stable): erhe::property dependency-property system design record (D-labels cited from code)
- [quest.md](quest.md) (mostly stable): Building, installing and running the Quest 3 flavor
- [quest_renderdoc_capture.md](quest_renderdoc_capture.md) (mostly stable): RenderDoc Meta Fork capture workflow on Quest
- [raytrace.md](raytrace.md) (experimental): GPU ray-query raytracing
- [raytrace_materials.md](raytrace_materials.md) (mostly stable): Material-aware ray-traced rendering: textures, glass, light sampling
- [reloadable_asset_loads.md](reloadable_asset_loads.md) (mostly stable): Undone glTF imports drop their payload and re-read on redo
- [renderdoc_fork.md](renderdoc_fork.md) (mostly stable): Desktop GPU-debugging workflow with the RenderDoc fork MCP server
- [ring_buffer_memory.md](ring_buffer_memory.md) (mostly stable): Bounded ring-buffer memory for scene loads
- [scene_serialization.md](scene_serialization.md) (stable): erhe glTF scene save / open pipeline and what is persisted
- [sdf-mesh-picking-fix-plan.md](sdf-mesh-picking-fix-plan.md) (stable): Resolved SDF-graph mesh picking defect (to be deleted after folding the standing rule)
- [selection.md](selection.md) (stable): Per-scene selection and active scene
- [shader_variants.md](shader_variants.md) (stable): standard.{vert,frag} uber-shader variant system
- [shader_workarounds.md](shader_workarounds.md) (stable): Driver-capability shader defines and workaround policy
- [shadow_tight_fit.md](shadow_tight_fit.md) (mostly stable): Shadow tight-fit optimization: landed steps and remaining candidates
- [shadows.md](shadows.md) (stable): Directional and point-light shadow mapping
- [style_library.md](style_library.md) (mostly stable): Style items: live property inheritance, assignment, persistence
- [subdivision_crease_edges.md](subdivision_crease_edges.md) (stable): Catmull-Clark semi-sharp crease edges
- [texture_graph.md](texture_graph.md) (mostly stable): Procedural texture graph status against Material Maker
- [usd-wg-assets.md](usd-wg-assets.md) (mostly stable): Script-generated survey of the ASWF USD-WG sample assets
- [usd_compatibility.md](usd_compatibility.md) (stable): erhe <-> OpenUSD concept and naming mapping tables
- [usd_compatibility_design.md](usd_compatibility_design.md) (mostly stable): USD compatibility design record and current state (C/U/M/X labels cited from code)
- [usd_survey_gap_loop.md](usd_survey_gap_loop.md) (stable): How the USD-WG asset survey is driven to zero gaps
- [vertex_position_quantization.md](vertex_position_quantization.md) (experimental): Quantized vertex positions across backends
- [vulkan_backend.md](vulkan_backend.md) (stable): Vulkan graphics backend: device, frame lifecycle, binding model, sync, swapchain
- [weight_paint.md](weight_paint.md) (experimental): Blender-style weight painting
- [window_target_items.md](window_target_items.md) (mostly stable): Editor windows with independent target items
- [xr_controller_render_model.md](xr_controller_render_model.md) (mostly stable): XR controller render models (XR_FB_render_model)

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
- [gltf_extensions/ERHE_layout.md](gltf_extensions/ERHE_layout.md) (mostly stable): ERHE_layout
- [gltf_extensions/ERHE_light.md](gltf_extensions/ERHE_light.md) (mostly stable): ERHE_light
- [gltf_extensions/ERHE_material.md](gltf_extensions/ERHE_material.md) (mostly stable): ERHE_material
- [gltf_extensions/ERHE_node.md](gltf_extensions/ERHE_node.md) (mostly stable): ERHE_node
- [gltf_extensions/ERHE_node_graphs.md](gltf_extensions/ERHE_node_graphs.md) (mostly stable): ERHE_node_graphs
- [gltf_extensions/ERHE_physics.md](gltf_extensions/ERHE_physics.md) (mostly stable): ERHE_physics
- [gltf_extensions/ERHE_scene.md](gltf_extensions/ERHE_scene.md) (mostly stable): ERHE_scene

### Plans (`plans/`)

- [plans/animation_keyframing.md](plans/animation_keyframing.md) (proposed): Keyframing and timeline for the Animation window
- [plans/doc_restructure.md](plans/doc_restructure.md) (in progress): Content sweep worklist for this documentation layout
- [plans/editor_improvements.md](plans/editor_improvements.md) (proposed): Prioritized backlog of editor architecture improvements
- [plans/geometry_graph/attribute_projection.md](plans/geometry_graph/attribute_projection.md) (proposed): project_attribute geometry-graph node: design research
- [plans/geometry_graph/attribute_projection_handoff.md](plans/geometry_graph/attribute_projection_handoff.md) (proposed): project_attribute node: implementation handoff
- [plans/geometry_graph/creation_tools.md](plans/geometry_graph/creation_tools.md) (in progress): AI creation tools and geometry-graph follow-ups
- [plans/geometry_graph/openvdb_sdf.md](plans/geometry_graph/openvdb_sdf.md) (in progress): OpenVDB SDF support in the geometry graph (phase 3 onward)
- [plans/geometry_graph/sdf_handoff.md](plans/geometry_graph/sdf_handoff.md) (in progress): SDF / OpenVDB geometry-graph work handoff
- [plans/gltf_prefabs.md](plans/gltf_prefabs.md) (in progress): glTF scene prefabs: remaining phases
- [plans/gltf_properties_extension.md](plans/gltf_properties_extension.md) (in progress): ERHE_*_properties glTF extensions (steps 2-5)
- [plans/init_status_display.md](plans/init_status_display.md) (proposed): Multi-threaded init status reporting
- [plans/lightmap/seam_driven_unwrap.md](plans/lightmap/seam_driven_unwrap.md) (in progress): Seam-driven lightmap unwrap (phases 2-4)
- [plans/lightmap/tiling.md](plans/lightmap/tiling.md) (in progress): Lightmap spatial tiling and world-space partition
- [plans/node_editor_native_rendering.md](plans/node_editor_native_rendering.md) (in progress): Node editor native-resolution rendering: live-interaction verification
- [plans/rigging/fabrik_ik.md](plans/rigging/fabrik_ik.md) (proposed): FABRIK inverse kinematics requirements
- [plans/rigging/rigging_tools.md](plans/rigging/rigging_tools.md) (proposed): Rigging tools roadmap (IK, constraints, skinning, drivers)
- [plans/timeline_editor.md](plans/timeline_editor.md) (proposed): Animation timeline and curve editor
- [plans/todo.md](plans/todo.md) (proposed): Unsorted future-work items awaiting a home
- [plans/usd_texture_graphs.md](plans/usd_texture_graphs.md) (proposed): Texture and geometry graphs as native UsdShade prims
- [plans/uv_editor.md](plans/uv_editor.md) (proposed): UV editor modeled on Blender's
- [plans/virtualcity_vanishing_meshes.md](plans/virtualcity_vanishing_meshes.md) (proposed): Open defect: overlapping meshes vanish on first hover
- [plans/wasm_webgpu_port.md](plans/wasm_webgpu_port.md) (proposed): WebAssembly + WebGPU port of the editor

### Reference (`reference/`)

- [reference/claude_review_2026_03_22.md](reference/claude_review_2026_03_22.md): Dated ad hoc code review of the editor
- [reference/esoterica_rendering.md](reference/esoterica_rendering.md): Esoterica vs erhe rendering comparison
- [reference/forge_erhe.md](reference/forge_erhe.md): SDL3 GPU concepts mapped to erhe's graphics API
- [reference/geogram_atlas_packing_feature_request.md](reference/geogram_atlas_packing_feature_request.md): Feature request to Geogram / xatlas maintainers
- [reference/gl_spec_section_5.md](reference/gl_spec_section_5.md): Transcribed OpenGL spec chapter 5 (shared objects, multiple contexts)
- [reference/glslang_bug_report_debugglobalvariable.md](reference/glslang_bug_report_debugglobalvariable.md): glslang DebugGlobalVariable SPIR-V bug report
- [reference/gltf_2_1_item_flags_comment.md](reference/gltf_2_1_item_flags_comment.md): glTF 2.1 per-node flags survey and issue comment
- [reference/gltf_sample_renderer_comparison.md](reference/gltf_sample_renderer_comparison.md): erhe vs Khronos glTF-Sample-Renderer feature comparison
- [reference/lsai_usage_playbook.md](reference/lsai_usage_playbook.md): LSAI MCP server usage playbook
- [reference/nova3d_comparison.md](reference/nova3d_comparison.md): Nova3D vs erhe AI creation tooling comparison
- [reference/nvidia_present_timing_driver_report.md](reference/nvidia_present_timing_driver_report.md): NVIDIA VK_EXT_present_timing driver issue report
- [reference/property_system_wpf_comparison.md](reference/property_system_wpf_comparison.md): erhe::property vs WPF dependency properties
- [reference/quest_profiling_2026_05_01.md](reference/quest_profiling_2026_05_01.md): Quest 3 GPU profiling report (2026-05-01)
- [reference/semantic_cpp_mcp_setup_xmp4_lsai.md](reference/semantic_cpp_mcp_setup_xmp4_lsai.md): LSAI + xmp4 MCP server setup
