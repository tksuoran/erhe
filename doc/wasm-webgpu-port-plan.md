# WebAssembly + WebGPU port of the erhe editor — incremental plan

Status: plan only (2026-08-11). No wasm/WebGPU work exists in the repo yet.

## Ground truth this plan is based on

- `erhe::graphics` selects a backend at **compile time** via `ERHE_GRAPHICS_API`
  (`opengl|vulkan|metal|none`), with pimpl `_impl` classes per backend and no virtual
  dispatch (`src/erhe/graphics/erhe_graphics/device.cpp:11-22`). Adding a backend is
  mechanical: ~22 `_impl` class pairs. Sizes: vulkan ~21k LOC, gl ~14k, metal ~7.7k,
  null ~2.8k.
- `null/` is the minimal skeleton to fork; **`metal/` is the closest functional
  analogue to WebGPU** (explicit encoders, up-front pipelines, no geometry shaders).
  `doc/metal_backend.md` is the backend-authoring recipe; `src/erhe/graphics/notes.md`
  documents the frame lifecycle / bind-group / texture-heap design.
- Shaders are GLSL assembled at runtime; Vulkan path already does GLSL → glslang →
  SPIR-V with a disk cache (`spirv_cache.cpp`); Metal adds SPIRV-Cross → MSL.
  Geometry shaders exist but the compute-based replacement path is already the
  supported path on Vulkan/Metal (`debug_renderer.cpp:230-304`).
- Windowing is SDL3 (fork `tksuoran/SDL`); SDL3 has an Emscripten backend upstream.
  Surface coupling is one seam: `Surface_create_info` takes only a `Context_window*`.
  ImGui is rendered by erhe's own renderer — no separate ImGui backend needed.
- Threading is the biggest structural risk: Taskflow executor sized to hardware
  concurrency, parallel init taskflow polled from a loading-screen loop
  (`editor.cpp:2441`), ~16 `silent_async` sites (background geometry-graph eval,
  lightmap work), shader-monitor filesystem thread, watchdog thread.
- Hard dependency blockers for Emscripten: `erhe::net` + `cpp-httplib` + the editor
  MCP server are linked **unconditionally** (`src/editor/CMakeLists.txt:305-317`) and
  need an off switch. embree/mimalloc/tracy are already avoidable (Android precedent,
  `CMakeLists.txt:117-134`). geogram has no wasm `VORPALINE_PLATFORM` profile — the
  largest third-party unknown. Jolt has documented Emscripten support.
- File I/O funnels through `erhe::file` (`file.cpp`), which already has an Android
  `SDL_IOFromFile` asset branch — the exact seam for an Emscripten branch.
- Small apps exist as milestone ladder: `hello_swap` (device+swapchain only) →
  `erhe_graphics` GPU test suite (`test_m1_device_up` … `test_m5_*`) →
  `rendering_test` → `example` → `hextiles` → editor.
- Configure-time codegen: a new backend needs
  `src/erhe/graphics/definitions/webgpu_config.py`, and generated
  `*_serialization.cpp` files are compiled per-executable.

## Strategy

**Develop the WebGPU backend natively on Windows first (Dawn), then cross-compile to
wasm.** Native WebGPU gives fast iteration, real debuggers, RenderDoc-adjacent
tooling (PIX via D3D12 backend of Dawn), and the same API surface the browser build
will use. The wasm toolchain, filesystem, and threading problems are attacked
separately, on the *null* graphics backend, so the two hard problems never block each
other. They meet in the middle at "hello_swap in the browser".

Two independent tracks, then convergence:

- Track A — WebGPU graphics backend (native).
- Track B — Emscripten platform bring-up (null graphics).
- Convergence — WebGPU on wasm, then walk the app ladder up to the editor.

---

## Track A: WebGPU backend, native

### A1. Scaffolding (no rendering)
- Add `webgpu` to `ERHE_GRAPHICS_API` allowed values; `ERHE_GRAPHICS_API_WEBGPU`
  define; new source block in `src/erhe/graphics/CMakeLists.txt`.
- CPM-fetch Dawn (or wgpu-native; pick Dawn — it is Google's reference, ships
  `webgpu.h`, and matches emdawnwebgpu on the Emscripten side so the same code
  compiles both ways).
- Fork `null/` → `webgpu/` (~44 files), rename `Null_*` → `Webgpu_*`, everything
  still no-ops. Add `definitions/webgpu_config.py` and wire generated serialization
  sources.
- Milestone: `hello_swap` links and runs (black window) with
  `ERHE_GRAPHICS_API=webgpu`.

### A2. Device, surface, swapchain, clear
- `Webgpu_device_impl`: instance/adapter/device, error callbacks into erhe logging,
  capability/limits population.
- `Surface_impl`/`Swapchain_impl` over `wgpuInstanceCreateSurface` from the SDL3
  native window handle (Win32 HWND path first).
- Command buffer/encoder impls mapping erhe's Metal-shaped encoder API onto
  `WGPUCommandEncoder` / `WGPURenderPassEncoder` / `WGPUComputePassEncoder`.
- Milestone: `hello_swap` animated clear color.

### A3. Shader pipeline
- Runtime path for development: GLSL → glslang → SPIR-V (existing `glsl_to_spirv.cpp`,
  reuse `ERHE_SPIRV` + spirv cache) → **Tint or SPIRV-Cross → WGSL**. Prefer
  SPIRV-Cross (already a dependency for Metal) unless its WGSL backend proves too
  immature, in which case use Tint (comes with Dawn anyway).
- `Webgpu_shader_stages_prototype` mirrors the Metal one, including its geometry-
  shader refusal.
- Plan (don't build yet) the offline-bake path for the browser bundle: extend the
  spirv cache concept to a WGSL cache generated at build/pack time.

### A4. Pipelines, buffers, textures, samplers, bind groups
- `Render_pipeline_impl` / `Compute_pipeline_impl`: erhe pipeline state → WGPU
  descriptors. WebGPU's static render-pipeline model matches erhe's up-front pipeline
  objects well (same shape as Metal/Vulkan backends).
- `Buffer_impl` (map/write semantics — note WebGPU has no persistent coherent
  mapping; use `writeBuffer`/staging like the Metal managed path),
  `Texture_impl`, `Sampler_impl`, `Vertex_input_state_impl`.
- **Texture heap: add a fifth `Texture_heap_path`** (`device.hpp:122`) —
  `webgpu_sampler_array`, modeled on `opengl_sampler_array`: fixed-size binding
  arrays of textures/samplers, since WebGPU has no bindless/descriptor indexing.
  This is the one genuinely new design decision in the backend.
- Milestones = the graphics test ladder run natively: `test_m1_device_up`,
  `test_m2_clear_color`, `test_m3_triangle`, `test_m4_compute_ssbo`,
  `test_m5_blend`/`m5_depth`, then the rest of the real-GPU suite
  (see `doc/graphics_test_coverage.md`).

### A5. Renderer bring-up on native WebGPU
- `rendering_test` cell grid green (textured quad, stencil, compute triangle,
  multi-texture …).
- `example` (scene renderer) renders correctly.
- `hextiles` — proves ImGui-via-erhe-renderer on WebGPU.
- Editor itself on native WebGPU (desktop, full threading, full filesystem). Fix
  feature gaps found here: known suspects are gpu timers (`Gpu_timer_impl` via
  timestamp queries), MSAA/resolve, depth formats (no D24S8 guarantees — reuse
  lavapipe lessons), acceleration-structure stubs (raytrace stays `bvh`, CPU-side).

Track A alone is valuable even if wasm stalls: a fourth real backend and a
conformance workout for the abstraction.

## Track B: Emscripten platform bring-up (graphics `none`)

Can start in parallel with A1; touches disjoint files.

### B1. Toolchain + CMake platform branch
- Extend platform detection (`CMakeLists.txt:~90-108`) with
  `if (EMSCRIPTEN)` → `ERHE_TARGET_OS_EMSCRIPTEN` / `-DERHE_OS_EMSCRIPTEN` instead of
  the current `FATAL_ERROR`.
- Copy the Android force-override block (`CMakeLists.txt:117-134`) into an Emscripten
  block: `ERHE_WINDOW_LIBRARY=sdl`, `ERHE_RAYTRACE_LIBRARY=bvh`,
  `ERHE_XR_LIBRARY=none`, `ERHE_PROFILE_LIBRARY=none`, mimalloc/ASAN/fpng off,
  graphics `none` initially.
- **New option `ERHE_NET=ON/OFF`** gating `erhe::net`, `cpp-httplib`, and the editor
  MCP sources (`src/editor/CMakeLists.txt:305-317`); off on Emscripten. Stub or gate
  cpptrace.
- Emscripten CMake preset(s) in `CMakePresets.json`; ensure configure-time Python
  codegen runs under cross-compile (it should — it's host Python).
- Milestone: `erhe::{log,math,file,graphics(null),window(sdl)}` + `hello_swap`
  compile to wasm (not necessarily run).

### B2. Runtime skeleton in the browser
- Frame-loop inversion: `emscripten_set_main_loop` driving one erhe frame per
  callback. SDL3's Emscripten backend handles events/canvas; verify the SDL fork has
  current upstream Emscripten support, rebase if not.
- `erhe::file` Emscripten branch beside the Android one (`file.cpp:48,143`): assets
  (`res/`, `config/`) from Emscripten preloaded packages (MEMFS) with the same
  read-only vs migrated-to-writable split; config writes to IDBFS (or accept
  ephemeral config first).
- Single-threaded first: `tf::Executor(1)` when `ERHE_OS_EMSCRIPTEN`, shader-monitor
  and watchdog threads compiled out, no `-pthread`. Revisit pthreads later (B/C
  follow-up), since COOP/COEP headers and non-blocking main thread are their own
  project.
- Milestone: `hello_swap` with null graphics runs in the browser (blank canvas, event
  loop alive, logging to console).

## Convergence: WebGPU on wasm, then the app ladder

### C1. hello_swap in the browser on WebGPU
- Switch Emscripten graphics to `webgpu`, linking emdawnwebgpu; the Dawn-based
  native code should mostly recompile.
- New surface path: browser `Surface_impl` created from a canvas selector
  (`#canvas`), not from SDL native handles. Async adapter/device request folded into
  init (Asyncify or pre-init before main loop).
- Shaders: browser accepts WGSL only → implement the offline WGSL bake planned in
  A3 (build step runs glslang + SPIRV-Cross/Tint on the host, ships `.wgsl` in the
  asset package; keeps multi-MB compilers out of the bundle). Runtime fallback with
  wasm-compiled glslang acceptable as a stopgap behind a size warning.
- Milestone: animated clear in Chrome.

### C2. Walk the ladder in the browser
- graphics GPU tests (as a wasm page or via headless Chrome) → `rendering_test` →
  `example` → `hextiles` (ImGui + input in browser).

### C3. Editor in the browser
- Dependency unknowns, in order of risk:
  1. **geogram**: add a wasm `VORPALINE_PLATFORM` profile (fork already exists);
     validate with `geogram_soak` compiled to wasm before touching the editor.
  2. **Jolt**: enable via its documented Emscripten support; check
     `cmake/JoltPhysicsCompatibility.cmake` SIMD flags (use wasm SIMD128, not AVX).
  3. simdjson portable kernel; per-target serialization sources under cross-compile.
- Init-flow rework: the loading-screen loop that polls `taskflow_future`
  (`editor.cpp:2441`) must become a state machine over main-loop callbacks
  (single-threaded: run init tasks incrementally per frame).
- Background work (geometry-graph eval, lightmap): acceptable degraded mode is
  synchronous on `tf::Executor(1)`; pthreads + SharedArrayBuffer is the later
  upgrade, requiring COOP/COEP hosting headers.
- File dialogs (`select_file_for_read/write/folder`): `<input type=file>` /
  File System Access shims, or hide those features initially.
- Asset packaging: `res/` + `config/` into Emscripten `--preload-file` packages;
  audit size (fonts, glTF samples) and trim.
- Milestone: editor loads, renders a scene, ImGui usable in Chrome.

### C4. Hardening / polish (as needed)
- Bundle size pass (Emscripten `-Oz`, asset trimming, WGSL bake instead of runtime
  compilers).
- Optional pthreads build (COOP/COEP, non-blocking main thread audit: no
  `wait_idle`/future-blocking on the main thread).
- Persistence: IDBFS for `config/` and saved scenes; browser download/upload for
  scene files.
- CI: an Emscripten configure+build job so the platform branch doesn't bit-rot
  (same concern as the headless null backend).

## Suggested first commit sequence

1. CMake: `webgpu` option value + Dawn fetch + `webgpu/` copied from `null/`
   (compiles, no-ops) + `webgpu_config.py`.
2. `hello_swap` native webgpu: device/surface/swapchain/clear.
3. GLSL→WGSL shader path + `test_m3_triangle`.
4. In parallel: `ERHE_NET` off switch + Emscripten platform/CMake branch (B1).

## Appendix: minimal-geogram feasibility (evaluated 2026-08-11)

Question: could a geogram fork providing only a minimal subset (attribute storage,
no complex operations) satisfy erhe, to de-risk the wasm build?

Survey result (grep of all `GEO::` usage + geogram includes across `src/`):

- **`GEO::Mesh` is erhe's mesh storage**, not an implementation detail:
  `erhe::geometry::Geometry` holds `GEO::Mesh` by value (`geometry.hpp:931`) and all
  erhe algorithms (shapes, Conway ops, subdivision, smoothing, lattice, clip,
  tangents) walk `vertices/facets/facet_corners/edges` directly. So the minimal
  subset is **containers + attributes**, not attributes alone:
  `basic/` (numeric, vecg/geometry/matrix, memory/vector, attributes, assert,
  logger, progress, counted, command_line-as-config-store) + `mesh/mesh.h`
  (surface elements only — `MeshCells`/volumetric is completely unused) + the
  trivial parts of `mesh/mesh_geometry.h`. That covers ~99% of the ~5000 `GEO::`
  references, including everything in `src/editor`, `erhe::primitive`, `erhe::log`.
- The attribute-type global registry (`AttributeStore` typeid-name machinery +
  `geo_register_attribute_type<vecNf/…>`) is load-bearing for
  `geometry_serialization.cpp` and must be kept.
- **Algorithm entry points erhe actually calls are only ~15, concentrated in ~6
  files**: CSG boolean (`geometry_operation.cpp:29`), repair/weld
  (`repair.cpp:41-51,321-327`), remesh + decimate (`remesh.cpp:350,424` — the
  heaviest: CVT/Voronoi/Delaunay/HLBFGS/OpenNL), UV atlas
  (`make_atlas.cpp:630,644` — xatlas/OpenNL), frame-field tangents
  (`generate_frame_field_tangents.cpp:43`), Delaunay convex hull
  (`geometry.cpp:910`, has an existing quickhull alternative path),
  `colocate` (`triangle_soup.cpp:156`, trivially replaceable with a hash grid),
  `.geogram` mesh IO (debug/import, 5 sites), `mesh_repair` in
  `json_polyhedron.cpp:97`.
- Already unused/off: geogram_gfx, lua, exploragram, image, voronoi (direct),
  OpenNL (direct), `mesh_reorder` (commented out), `GEO::mesh_smooth` (erhe has
  its own Taubin), `parallel_for`/FPE (never called by erhe).

**Verdict: clearly feasible, and the right shape is not a second fork but a
`GEOGRAM_CORE_ONLY`-style build option in the existing `tksuoran/geogram` fork**
(erhe already pins it and patches flags): compile only `basic/` +
`mesh/mesh.cpp` (+ small mesh_geometry subset) into the `geogram` target, drop
tetgen/triangle/FPG/HLBFGS/OpenNL/xatlas third-party entirely. On the erhe side,
gate the ~6 algorithm-calling files behind an `ERHE_GEOGRAM_FULL` define with
stub fallbacks (op returns input unmodified + logs "unavailable on this
platform"): the wasm editor loses CSG/repair/weld/remesh/decimate/atlas/
frame-field — acceptable for an initial port; convex hull switches to the
existing quickhull path; colocate gets the hash-grid replacement.

Risks/unknowns: hidden includes coupling `mesh.cpp`/`attributes.cpp` to other
modules (expected small — resolve during the trim); `basic/process.h` thread
machinery compiling under Emscripten single-threaded (erhe sets
`sys:multithread` false and never calls it, but it must still compile — may need
a stub Process backend); keeping the fork's core-only option rebased.

Fallback/complement: full geogram may well compile under Emscripten anyway (it
builds on Android-generic/arm64, i.e. non-x86 clang, and predicates are portable
expansions — the `-ffp-contract=off` patch already handles the FMA hazard).
Worth one timeboxed attempt with a wasm `VORPALINE_PLATFORM` profile before
committing to the trim; core-only remains valuable regardless for bundle size
and link-time, and `geogram_soak` (built headless, links only
erhe::geometry/log/math) is the validation vehicle for either route.

## Reading list before starting

1. `doc/metal_backend.md` — backend-authoring recipe.
2. `src/erhe/graphics/notes.md` — frame lifecycle, bind groups, texture heap paths.
3. `src/erhe/graphics/erhe_graphics/null/` — the skeleton to fork.
4. `CMakeLists.txt:90-134` — platform + forced-option blocks to extend.
5. `doc/android.md`, `doc/quest.md` — precedent for a constrained-platform port.
6. `doc/graphics_test_coverage.md` — the milestone test ladder.
