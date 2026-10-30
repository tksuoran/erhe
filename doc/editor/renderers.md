# renderers/

Stability: stable

## Purpose

Low-level rendering infrastructure for the editor: shader programs, GPU memory management, ID-based picking, render pass composition, and viewport configuration.

## Key Types

- **`Programs`** -- Loads and manages all shader programs (standard, debug visualizations, tools, sky, grid, etc.). Provides `get_variant_shader_stages()` for selecting debug visualization modes. Uses `Shader_stages_builder` for deferred shader compilation.

- **`Mesh_memory`** -- Allocates and manages shared GPU buffers for vertex and index data. Provides three vertex buffer streams (position, non-position attributes, custom attributes) and a single index buffer. Includes a `Buffer_transfer_queue` for staging uploads. All editor meshes share this memory pool. Uses `Free_list_allocator` per `Pool_block` for reclaimable allocation; a destroyed mesh's ranges are RETIRED (Pool_block implements `Buffer_allocation_owner`) and freed from a device frame-completion handler registered in `Mesh_memory::flush()`, so no in-flight frame can still read memory a new mesh is uploaded into.

- **`Id_renderer`** -- GPU-based object picking. Renders mesh IDs and triangle IDs to an offscreen framebuffer, then reads back a small region around the cursor. Uses a ring buffer for async readback across frames. Returns `Id_query_result` with mesh, primitive index, triangle ID, and depth. The id-range table that maps an id back to its mesh holds each mesh weakly (`Primitive_buffer::Id_range::lock_mesh()`): a readback resolves frames after the draw, and a mesh destroyed in between (its scene closed) resolves to no hit. The pass is prepared in M2 (`Id_renderer::prepare`, from `App_rendering::prepare_id` in `Viewport_scene_view::prepare_composition`, the last M2 step, when the view is hovered, a region scan is pending or a pick probe is armed; the skinning filter is decided there) into the view's frame slot (`Composer_prepared::id_pass`, an `Id_pass_prepared`: bucketed records with the id offsets, draw commands, the id range table and the frame's region scan when this view claimed it). A region scan request (`request_scan`, main thread) is handed to the prepare frame by `begin_frame` (M2) and claimed by the first view that prepares an id pass for that frame (`take_frame_scan`, which forces the all-meshes filter for that pass); the render phase reads the scan only from the prepared pass, so the request and its claim are main-thread state and every frame in flight carries its own scan. `Id_renderer::render(params, prepared)` reads no mesh: it copies the records and commands, and copies the table into the transfer entry it records (and into the region entry of a scan); `get()` resolves an id against the table of the transfer entry its pixel came from, `take_scan_result()` against the region entry's copy. The headset prepares its pick pass where it renders it (`Id_renderer::render_immediate`). The id pass covers a primitive where the color pass draws it: the buckets keep the material's blending mode (`Blending_mode_policy::allow_all`; the id pipeline never blends) and carry the draw-list identity's `negative_determinant` and `double_sided`, which select the pipeline as `Forward_renderer` does. A single-sided primitive culls its back faces; a mirrored one (negative determinant) gets the front-face-flipped pipeline, so it culls the same faces as unmirrored; a double-sided one (`erhe::scene::is_double_sided`: the material's or the gprim's `double_sided`) culls no face; an alpha-tested one (`alpha_test`, `screen_door`) is drawn by the `ERHE_VARIANT_ID_RENDER_ALPHA_TEST` variant, which runs the material inputs and the color pass's discard and writes the id where a fragment survives (`doc/erhe/shader_variants.md`). Opaque and translucent primitives cover their whole surface. The records name the slots of the scene's forward material set (`Prepare_parameters::material_set`), which the render phase binds around the pass.

- **`Composer`** -- Composites multiple render passes (content, selection, tools, etc.) for final viewport output.

- **`Composition_pass`** -- Configurable render pass with fill mode (polygon, wireframe), blend mode, and selection mode.

- **`Render_context`** -- Data passed to rendering functions: scene root, camera, viewport, shadow node, etc.

- **`Viewport_config`** -- Per-viewport rendering options (grid, shadows, edge lines, selection outline, etc.).

- **`Ray_trace_scene`** -- One per `Scene_root`: the traced instances, per-instance records, shared bottom level structures and content bounds of the content layer, maintained from the mesh hooks in M2 and published per frame-in-flight slot (doc/editor/raytrace.md "Ray_trace_scene").
- **`Scene_tlas`** -- One per ray-query consumer (`Ray_trace_renderer`, `Ddgi_renderer`, `Radiance_cascades_renderer`), render phase: builds the slot's missing bottom level structures, one top level structure per frame-in-flight slot, and the per-instance record SSBO the shaders read through buffer device addresses. `Lightmap_baker` still carries its own copy (its records also hold texcoord-2 addresses).

- **`Ddgi_renderer`** -- Dynamic diffuse global illumination (`doc/editor/ddgi.md`). Fits one scene-wide probe grid to the padded content bounding box, then each tick traces a budgeted round-robin slice of probes (`ddgi_trace.comp`), blends the rays into octahedral irradiance / distance atlases (`ddgi_blend.comp`, two variants) and runs relocation + classification (`ddgi_relocate.comp`). The atlases reach `standard.frag` through texture heap slots 5-7 and the `USE_DDGI` shader variant axis, replacing the flat ambient term for non-lightmapped draws. Requires `Device_info::use_ray_query`. Also a `Renderable`: the probe overlay (`debug_draw_probes`) draws in the CPU phase only -- lines submitted in the encoder phase miss the debug renderer's compute dispatch and trip its buffer bookkeeping.

## Stencil budget and composition-pass ordering (IMPORTANT)

The viewport stencil buffer is shared by several unrelated effects, and passes run in the order they were created (`Composer::render` walks `composition_passes` in sequence; `overlay` passes are split out and run after post-processing). Two rules follow, and violating either produces "geometry mysteriously missing" bugs that look like a draw problem but are not:

- **Bit 7 (`0x80`) is the selection silhouette mask.** Written with reference 128 by the "Content fill selected" pass over every pixel of a selected mesh, and read by the outline pass. Every *other* stencil user must exclude it from both `write_mask` and `test_mask` -- the convention in this codebase is masks of `0b01111111`. Reading it by accident is a real historical bug: the debug renderer's hidden pass tested with `0xff` while its visible pass used `0x7f`, so with the comparison `reference > stencil` every hidden-pass fragment inside a selected mesh failed, and all hidden debug lines (grid, tools, selection, bones) vanished over the selected object and nowhere else.
- **Tag values live in bits 0..6 and are left behind.** Allocated in `app_rendering.hpp`: edge lines 1, tool mesh hidden/visible 2/3, bone mesh hidden/visible 4/5, line renderer grid minor/major, selection, tools 8..11. A pass that writes tags there must be ordered *after* every pass that requires a zero stencil in those bits -- notably the edge-line pipelines, which draw only where `stencil == 0` under mask `0x7f`. This is why the bone pass sits after all other content passes rather than next to the other bone-related work.

The tool handles and the solid bones share one technique: a single `Composition_pass` carrying SIX pipelines in `base_render_pipelines` -- tag hidden parts in the stencil, tag visible parts, clear depth under the geometry, lay down its own depth, then draw the visible part solid and the hidden part blended. The depth clear + own-depth pair is not optional: the hidden colour pass's fragments are behind content by construction and would otherwise fail the depth test entirely. It does not make the geometry float, because occlusion is still decided by the stencil tags; what it buys is correct sorting of the overlay geometry against *itself*. See `Tools_pipeline_renderpasses` (tool1..tool6) and `Pipeline_renderpasses` (bone1..bone6).

Two per-pass knobs worth knowing, both used by the bone pass:

- `Composition_pass_data::shader_debug_override` forces a `SHADER_DEBUG` variant for the pass's own meshes. The default path can only ever DROP the view's debug mode per mesh, never turn one on -- which is what a pass needs when the variant IS the intended look rather than a debug view.
- `shader_debug_override_filter` selects which meshes it applies to, and is evaluated **per mesh** in `bucket_primitives`. That is why one pass can give most meshes a forced variant while letting others fall back to their normal material, instead of needing a separate pass per appearance.

Diagnosing a pass that draws nothing: the GPU debug marker uses the *pipeline's* `debug_label`, not the composition pass name, so searching a frame capture for a pass name finds nothing even when it drew. The gating runs in `Composition_pass::prepare()` (M2), and a pass that does not pass it draws nothing in `Composition_pass::render()`, so no marker is emitted, so a capture cannot distinguish a gated-off pass from missing geometry. Use the MCP tool `get_composition_passes`, which reports each pass's `last_result` (`never_rendered` / `disabled` / `is_enabled_false` / `no_scene_root` / `primitive_mode_disabled` / `no_mesh_layers` / `submitted`) plus the scene view and mesh count, as of the last rendered frame (`Composition_pass_stats`, recorded by the render phase's render calls; `never_rendered` also when the pass was not rendered in that frame). Note `submitted` only means it reached draw submission -- the item filter may still have rejected every mesh.

## Public API / Integration Points

- `Programs::get_variant_shader_stages()` -- get shader for a debug visualization mode
- `Mesh_memory::buffer_info` -- used by `erhe::primitive` to build GPU meshes
- `Id_renderer::prepare()` / `render()` / `get()` -- prepare (M2), render (R) and query object IDs
- `Render_context` -- passed to all `tool_render()` and `render_viewport_*()` calls

## Dependencies

- erhe::graphics, erhe::scene_renderer, erhe::renderer, erhe::primitive
- editor: Mesh_memory buffers are shared by all mesh-creating subsystems
