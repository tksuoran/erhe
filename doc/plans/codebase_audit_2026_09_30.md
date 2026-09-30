# Plan: finish the 2026-09-30 architecture and API audit

Status: in progress

Extends `doc/reference/audit_erhe_2026_09_30.md`, the audit synthesis. Four
of its six slice reports exist; this plan holds the work to finish the
remaining two and to make the synthesis draw on all six. When the last step
is done, delete this plan and the "Status" section of the synthesis.

The audit is read-only research: no source file changes, no builds, no git
state changes other than committing the audit documents. Every finding must
carry a `file:line` reference verified by reading the code; ASCII only; no
machine-specific paths (write `src/...`, never a drive letter). The
documents live under `doc/reference/`, which the header-line check exempts,
but every `doc/...md` mention in them must resolve
(`py -3 scripts/check_doc_links.py`).

## Steps

1. Run slice 5 (GPU foundation) with the brief below; write
   `audit_erhe_2026_09_30_graphics.md` under `doc/reference/` (300-600 lines).
2. Run slice 6 (scene and data model) with the brief below; write
   `audit_erhe_2026_09_30_scene.md` under `doc/reference/` (300-600 lines).
3. Rewrite `doc/reference/audit_erhe_2026_09_30.md` sections 1-9 so they
   quote all six reports: add the graphics and scene findings to the
   executive summary, strengths, architecture, API, code health, tests,
   options and scorecard; mark the two rows in "Slice reports" complete;
   remove the "Status" section.
4. Add the index line for the synthesis under "reference" in
   `doc/README.md` (next to the 2026-06-21 audit line), run
   `py -3 scripts/check_doc_links.py`, commit, delete this plan in the same
   commit.

Steps 1 and 2 are independent and can run in parallel as two agents; each
agent writes only its own report file. Step 3 needs both.

## Brief for slice 5: GPU foundation

Scope: `src/erhe/graphics` (Device, Buffer, Texture, Sampler,
Shader_stages, Render_pass, Render_command_encoder,
Compute_command_encoder, Gpu_timer, ring buffers, shader resource layout,
std140/std430, and the Vulkan, OpenGL and Metal backends), `src/erhe/gl`,
`src/erhe/dataformat`, `src/erhe/buffer`, `src/erhe/circular_ring_buffer`,
`src/erhe/codegen`, the shader tree under `res/shaders` (structure,
includes, variant scheme), and the documents `doc/erhe/graphics.md`,
`doc/erhe/gl.md`, `doc/erhe/dataformat.md`, `doc/erhe/buffer.md`,
`doc/erhe/circular_ring_buffer.md`, `doc/erhe/codegen.md`,
`doc/erhe/shader_variants.md`, `doc/erhe/shader_workarounds.md`,
`doc/erhe/ring_buffer_memory.md`, `doc/erhe/metal_backend.md`,
`doc/erhe/gpu_coding_rules.md`, `doc/erhe/layout.md`,
`doc/plans/vulkan_backend.md`, `doc/plans/spirv_cache.md`,
`doc/plans/texture_memory.md`.

Method: public headers first (the API surface), then how the three backends
implement them, then the heaviest `.cpp` files by `wc -l`; read how
`src/erhe/scene_renderer`, `src/erhe/renderer` and `src/editor/renderers`
consume the API to judge ergonomics; check the documents against the code.

Report, with `file:line` evidence for every claim:

1. Architecture: layering; backend abstraction quality (Vulkan-isms in the
   neutral API, `#ifdef` density in neutral code, per-backend feature parity
   gaps); resource lifetime and ownership (shared, unique, raw);
   synchronization model (barriers, layout tracking, frames in flight);
   memory management (VMA, ring buffers, deferred free); error handling
   (assert/abort, return codes, exceptions); threading model.
2. API issues: naming consistency, boolean arguments, construction patterns
   (create-info classes), hidden coupling, god classes, over-wide headers,
   functions with more than six parameters, duplicated concepts, backend
   enums leaking, APIs easy to misuse, missing RAII, const and move
   semantics, unnecessary virtuals, header dependency weight.
3. Code health: largest files and functions, duplication across backends
   that could be shared, dead code, TODO/FIXME counts, commented-out code,
   style-rule violations (`struct`, `auto`, boolean arguments), allocation
   in hot paths, logging.
4. Strengths, specifically.
5. Test coverage: `src/erhe/graphics` tests and `erhe_graphics_gpu_tests`;
   what is covered and what is not (see `doc/erhe/graphics_test_coverage.md`
   and `memory-bank/topics/graphics_tests.md`).
6. Future options, prioritized with cost and benefit: render-pass and
   framebuffer abstraction, bindless, descriptor model, shader compilation
   pipeline and SPIR-V cache, WebGPU feasibility per
   `doc/plans/wasm_webgpu_port.md`, Metal parity gaps, mesh shaders,
   GPU-driven rendering, splitting `erhe_graphics` into interface and
   backend targets (the infrastructure report proposes it).

## Brief for slice 6: scene and data model

Scope: `src/erhe/item` (Item, Hierarchy, Item_host, flags, filters),
`src/erhe/scene` (Scene, Node, Mesh, Camera, Light, Skin, Animation,
attachments, Scene_message_bus, Trs_transform, projection),
`src/erhe/property` (dependency properties, styles, folders, migrations,
inventory) with `doc/erhe/property.md`, `doc/erhe/property_system.md`,
`doc/erhe/property_inventory.md`,
`doc/reference/property_system_wpf_comparison.md`; `src/erhe/primitive`
and `src/erhe/geometry` (geogram-backed Geometry, Catmull-Clark, Conway);
`src/erhe/gltf` and `src/erhe/usd` with `doc/erhe/gltf.md`,
`doc/erhe/usd.md`, `doc/erhe/usd_compatibility.md`,
`doc/erhe/scene_format_support.md`, `doc/gltf_extensions/`;
`src/erhe/physics`, `src/erhe/math`, `src/erhe/message_bus`,
`src/erhe/commands`, `src/erhe/graph`; documents `doc/erhe/item.md`,
`doc/erhe/scene.md`, `doc/erhe/primitive.md`, `doc/erhe/geometry.md`,
`doc/erhe/geogram.md`, `doc/erhe/catmull_clark.md`, `doc/erhe/physics.md`,
`doc/erhe/box3d_physics.md`, `doc/erhe/math.md`,
`doc/erhe/message_bus.md`, `doc/erhe/commands.md`, `doc/erhe/graph.md`,
`doc/erhe/khr_physics_rigid_bodies_support.md`.

Method: public headers first, then the heaviest `.cpp` files, then trace
how a Node with Mesh and Material is created, transformed, serialized to
glTF and read back, and how a property change propagates; check the
documents against the code.

Report, with `file:line` evidence for every claim:

1. Architecture: the Item/Hierarchy/Node/attachment object model (fit,
   strain, editor concepts leaking into `erhe::scene`); ownership (shared
   pointers, cycles, weak pointers, the `item_host_mutex` discipline); the
   property system (problem solved, per-node cost, interaction with plain
   members and with serialization, any dual state between members and
   properties); threading (thread-safe parts, geogram rules); glTF as the
   native format (what round-trips, extension proliferation); physics
   abstraction; math library versus glm; message bus design (typed, message
   count, use versus direct calls); commands and input bindings.
2. API issues: naming, boolean arguments, mutable getters, header weight
   (what a consumer includes to use Node), hidden ordering, god classes,
   duplicated concepts (transform representations, name and id notions, flag
   systems), APIs easy to misuse.
3. Code health: largest files and functions, duplication, dead code,
   TODO/FIXME counts, style-rule violations, allocation in hot paths
   (`Scene::update_node_transforms` and similar).
4. Strengths, specifically.
5. Test coverage per library.
6. Future options, prioritized with cost and benefit: data-oriented
   alternatives versus keeping the object model, property system completion,
   USD as native versus glTF, data-model-level undo, scripting bindings,
   multi-scene and streaming, instancing, LOD, an asset database.

## Reading the finished reports first

The four finished reports name the graphics and scene layers from the
outside; the two new slices should confirm or correct these observations:

- rendering report 1.3 and 1.10 (per-frame upload model, ring buffers,
  pending-vector swaps that discard capacity);
- rendering report 2.7 (ownership between renderers and scene items);
- infra report 1 (layering leaks: `erhe_log` linking geogram,
  `erhe_physics` linking `erhe::renderer`, `erhe_commands` linking
  `erhe::xr`);
- editor report 1 (item_host_mutex use from MCP handlers, `Scene_root` as
  a god class, `scene_view.hpp` including geogram).
