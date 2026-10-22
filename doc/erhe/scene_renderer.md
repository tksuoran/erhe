# erhe_scene_renderer

Stability: stable

## Purpose
Renders `erhe::scene` content (meshes, lights, shadows, skinning) to the GPU. Provides forward rendering, shadow map generation, and GPU buffer management for uploading camera, light, material, primitive, joint, and cube instance data via ring buffers. Defines the standard shader interface (uniform/SSBO block layouts) shared between C++ and GLSL.

## Key Types
- `Forward_renderer` -- Renders meshes with full lighting, materials, and shadows, in two halves. `prepare(Prepare_parameters, Render_bucket_snapshot&)` (const, any thread that owns the meshes; the editor's prepare phase) buckets the mesh spans with the pass's filter, primitive mode, blending policy, forced shader bits and shader debug mode and writes each bucket's complete primitive records (`Primitive_buffer::append_records`) and draw commands (`Draw_indirect_buffer::append_commands`). `render(Draw_parameters, const Render_bucket_snapshot&)` (render phase) draws a snapshot without reading any mesh: it copies the records and commands into the ring buffers and applies only the environment shader-key axes the draw owns (light counts, shadow filter / bias / technique / depth bits, multiview count, `USE_DDGI`), which `Shader_key::derive` copies from the environment and never sets itself, so the keys equal a single derive with the full environment. `render(Render_parameters)` does both in one call through a snapshot the renderer keeps.
- `Render_bucket_snapshot` / `Prepared_bucket` -- One pass's prepared draws: buckets (buffer set, shader key without the render-phase axes, winding and culling partition, primitive mode, entry range), the primitive records and the draw commands, plus the pass's forced-off shader bits. `clear()` keeps every capacity, so a caller that keeps one per pass, view and frame in flight allocates nothing in the steady state.
- `Shadow_renderer` -- Generates shadow maps by rendering meshes from each light's perspective into a texture array. With a `Draw_list_scene` it draws the shadow draw lists published for the frame (`Draw_list_scene::publish`) and fits the shadow frusta to the caster and receiver bounds the prepare phase gathered into the frame's slot (`Draw_list_scene::prepare_shadow_bounds` / `get_shadow_bounds`, `doc/erhe/draw_list_renderer.md` Q6); without one it draws the casters `prepare(Prepare_parameters, Render_bucket_snapshot&)` bucketed (const; shadow_caster_filter, opaque, optimized variant, unlit left out when asked, no pass variant bits) - the snapshot in `Render_parameters::casters`, or one it prepares from `mesh_spans` itself when handed none - ORing the depth-only / distance / cube bits into the prepared keys per pass. The fit bounds come from `Render_parameters::shadow_bounds` when set, else from the `Draw_list_scene`, else from `mesh_spans`. `shadow_caster_filter` is the shadow pass filter both paths use.
- `Draw_indirect_buffer` -- Builds GPU draw-indirect command buffers from a span of meshes filtered by `Item_filter` (`update(meshes, mode, filter)`), from a `Render_bucket` or a draw list, or by copying prepared commands (`update(std::span<const Draw_indexed_primitives_indirect_command>)`, which also applies the debug index count clamp; `append_commands(bucket, mode, out)` prepares them).
- `Program_interface` -- Defines the shader resource layout (vertex format, camera/light/material/primitive/joint/cube blocks). Creates shader prototypes with all blocks pre-configured.
- `Camera_buffer` -- Ring buffer client uploading camera matrices, viewport, exposure, and grid settings, plus the pass's view origin and `clip_from_view_relative` (`get_view_origin()`, `get_clip_from_view_relative()`; doc/erhe/shadows.md "View-relative positions").
- `Light_buffer` -- Ring buffer clients uploading the lights: the `light_block` header (light counts, ambient, shadow map sentinels, DDGI volume, the light cluster grid; uniform block, binding 1), the `light_array_block` lights (position, direction, color, shadow transforms; storage block `light_array.lights[]`, binding 9, the shaded lights only, in slot order) and the `light_cluster` lists (storage block `light_cluster.data[]`, binding 10, layout in `light_cluster.hpp`). `write_view(light_projections, cluster_view, binning)` runs once per view and frame on the render phase after `Light_projections::apply()`, outside any render pass: it writes the view's light array and cluster lists into `Light_projections::view_buffers` (`Light_view_buffers`), binned into the grid of `cluster_view` (`Light_cluster_view`: the camera, viewport and projection conventions of the view's color passes) when the partition's light shading is `Light_shading::clustered`, else the single-cluster grid. `binning` (`Light_view_binning`) selects the binner: with `Light_cluster_binning::gpu`, a supported `Light_cluster_gpu_binner`, the frame's command buffer and the view's `Light_cluster_view_state`, the lists are written by the GPU binner's dispatch into a `GPU_access` range at fixed per-cluster offsets (`Light_cluster_binner::prepare()` builds only the grid and the inputs); otherwise `Light_cluster_binner::bin()` writes compact lists into a `CPU_write` range. The view state receives the binning stats in both modes. `update()` writes the header per pass and binds the view's array and lists when `write_view()` wrote them; otherwise it writes its own light array and the single-cluster grid. It returns `Light_buffer_ranges`, which `bind_light_buffer()` binds and `release()` releases together.
- `Light_cluster_binner` -- The light binner (`light_cluster.hpp`) and the reference of the GPU binner. `prepare(light_projections, view, config)` builds the view's `Light_cluster_grid` - `ceil(width / tile_size)` by `ceil(height / tile_size)` tiles over normalized device xy and `slice_count` view depth slices, exponential from the camera's near plane to the farthest depth a binned light reaches (`Light_cluster_config`, from `Program_interface_config::light_cluster`) - and the fp32 inputs of the tests (per slice its near and far depth and the bounds widening, per tile corner the view-space ray, per binned slot the light's range sphere in the cluster camera's node space and, for a spot light, its cone, each composed in double and rounded once); `bin_prepared()` runs the tests on the CPU, and `bin()` does both. The tests check every spot and point light's range sphere against every cluster's view-space bounds, both widened by 2^-10 relative (the sphere by its radius, the bounds by the slice's far depth) so an fp32 cluster lookup that rounds across a boundary still finds every light reaching it. A spot light that passes the sphere test of a closed cluster (no open side, see below) is also tested by its cone: the cone's half angle is `acos(outer_cos - 2^-16)`, `outer_cos` being the fp32 outer cosine the light array holds, so a fragment whose fp32 `get_spot_attenuation()` rounds across the cone's edge still finds the light, and the smooth falloff region (between the inner and the outer angle) is inside it. The cluster is replaced by the sphere around its widened bounds (radius the smaller of the half extents' L1 norm and sqrt(3) times the largest half extent, widened by 2^-10 relative); with `V` the sphere centre relative to the apex, `along` and `perp` its components along and across the emission axis, the sphere misses the cone when `cos(a) perp - sin(a) along > r` (the signed distance from the cone's lateral line, a lower bound of the distance to the cone for every half angle below 180 degrees) or, for a half angle up to 90 degrees, when `along < -r` (behind the apex plane). The cone test shares the sphere test's far-from-origin residual: its margins are relative, so a fragment of a camera far from the world origin that the cluster lookup rounds across a boundary can fall in a cluster whose cone test culled the light, contributing at most its attenuation over that LSB. Both are evaluated on squares, without a square root, from the per-slot inputs `axis * sin(a)` and `cot(a)` (`cone_reaches_bounds()`), so they stay bit-identical between the binners; a point light, an unbounded spot light and a cone widened to the whole sphere are not cone-tested. The margin is relative while the lookup's rounding grows with the distance of the camera from the world origin (the cluster-relative position is formed from fp32 view origins), so a camera far from the origin can put a fragment within about an fp32 LSB of a cluster boundary into a cluster that misses a light reaching it there; such a light contributes at most its attenuation over that LSB. The boundary clusters are open toward the outside (the outer tiles past the frustum's sides, the last slice past its far end), and slice 0 is open toward and behind the camera and sideways in every tile, because a fragment behind the cluster camera has no meaningful tile; the fragment lookup (`erhe_light_cluster_index()`, `res/shaders/erhe_light_cluster.glsl`) clamps an outside position into the boundary clusters and puts a position behind the camera in slice 0, so the lists are conservative for every fragment. The grid assumes the camera node carries no scale (positions and depths in node space are compared with ranges in world units). Each list is ascending in slot order, so its spot shadow-mapped, spot unshadowed, point shadow-mapped and point unshadowed lights are consecutive runs whose counts the cluster header carries (16 bits each). Directional lights are not binned (the shader loops them with the header's runtime counts); a spot light with `range <= 0` reaches everywhere and is in every list. A cluster asking for more than `capacity` lights keeps its lowest slots; `get_overflow_cluster_count()` and `get_max_list_length()` report the cut. The header stride is `c_light_cluster_header_uint_count`, which `Program_interface` passes to the shaders as `ERHE_LIGHT_CLUSTER_HEADER_UINT_COUNT`; `get_cluster_slot_end(partition)` is one past the last slot a list can name, and both writers of `Light_buffer` verify that the light array they bind holds it. The tests are fp32 and are the operations `res/shaders/light_cluster_bin.comp` evaluates with every intermediate `precise` (`light_cluster.cpp` is compiled with `-ffp-contract=off` on Clang, clang-cl and GCC; MSVC's cl does not contract under `/fp:precise`), so the CPU and GPU lists of the same inputs are equal bit for bit. The one residual is denormals: a GPU that flushes them to zero (Metal's fast math) can decide differently from the CPU only for a squared distance or radius below the smallest normal float, that is a light radius around 1e-19 world units, which no scene has; the cone test's squares (lateral, `t^2`, the behind-apex term) flush likewise only for lengths below about 1e-19 world units. `write_inputs()` writes the inputs in the GPU binner's input block layout; `get_fixed_byte_count()` / `get_fixed_list_stride()` size the fixed-offset layout the GPU binner writes (the same cluster headers; each list at `3 * cluster_count + cluster * ceil(capacity / 2)` uints), which the fragment lookup reads unchanged. It costs `cluster_count * (3 + ceil(capacity / 2)) * 4` bytes per binned view per frame in flight: 1.3 MB for Bistro's 1334 x 1120 viewport (9072 clusters) and about 7 MB at 3840 x 2160 (60 x 34 x 24 clusters) at the default capacity 64. The binner keeps its buffers across calls. `write_single_cluster()` writes the single-cluster grid (one cluster listing every shaded spot and point slot), which a default `Light_cluster_grid` selects for every fragment; passes whose `Light_projections` were not written by `write_view()` (previews, the BRDF slice, null-light passes) use it.
- `Light_cluster_gpu_binner` -- The GPU light binner (`light_cluster_gpu_binner.hpp`; doc/plans/clustered_lights.md C4), one per device: the compute pipeline of `res/shaders/light_cluster_bin.comp` and the ring buffer clients of its inputs, overflow counters and readbacks. `dispatch(command_buffer, binner, clusters, byte_offset, byte_count, view_state, frame_index, label)` (render phase, outside any render pass) writes the inputs of `binner`'s last `prepare()` into a `CPU_write` range, dispatches one invocation per cluster (each computes its cluster's widened bounds, walks the spot and point slots in ascending order with the CPU binner's tests and writes its list and category counts at the fixed offset; a cluster over the capacity keeps its lowest slots), records the barrier before the frame's fragment reads, and copies the dispatch's counters (overflowing clusters, longest list) into `view_state`'s readback slot of the frame. `is_supported()` is false when the pipeline did not build; views are then binned on the CPU. `request_check()` / `poll_check()` (main thread) compare every GPU-binned view of one frame with the CPU binner's lists of the same inputs (`Light_cluster_check`, MCP `get_light_cluster_check`); the checked frame starts its render phase after the request, so all its views are recorded.
- `Light_cluster_view_state` / `Light_cluster_stats` / `Light_view_binning` / `Light_cluster_binning` -- A binned view's state, owned by the view's renderer (the editor's `Shadow_render_node`): a persistent host-readable buffer with one slot per frame in flight for the GPU binner's counters (outside the ring buffers, so a slot keeps its counters however many frames the view skips), decoded by `retire(frame_index)` when the view's next frame in that slot starts binning (the dispatch frame retired), the dispatch's `Gpu_timer` (`Light cluster binning <view>`) and the latest `Light_cluster_stats` (grid counts, binned lights, capacity, overflowing clusters, longest list; for GPU binning those of the newest retired frame, `frame_index`). Steady state allocates nothing. `Light_view_binning` is `write_view()`'s binning selection; `Light_cluster_binning` is `cpu` or `gpu`.
- `Material_buffer` -- Ring buffer client uploading PBR material properties and texture handles.
- `Primitive_buffer` -- Ring buffer client uploading per-primitive world transforms, normal transforms, color, material index, and skinning data. `append_records(bucket, material_source, mode, settings, out)` writes a bucket's records into a CPU byte vector (the bytes `update(bucket, ...)` writes, id offset 0, no id ranges); the id pass overload `append_records(..., io_id_offset, id_ranges, out)` carries the id offset across buckets (start at `first_id_offset`, 1: id 0 is the background) and appends each record's `Id_range` (offset, length, the mesh held weakly, primitive index), the table an id readback resolves against. `update(std::span<const std::byte>)` copies prepared records into a ring buffer range; `get_record_size()` is one record's size.
- `Joint_poses` -- The posed joints of a scene's skins copied for a render phase: `capture(skins)` on the thread that owns the skins (after the frame's node transforms) assigns each skin's `Skin_data::joint_buffer_index` and computes one `Joint_pose` (`world_from_bind`, normal transform, joint node as an identity key) per joint slot, in skin order.
- `Joint_buffer` -- Ring buffer client uploading the `Joint_poses` entries for skinned meshes; reads no joint node.
- `Light_set` -- The resolved set of lights a scene is shaded with (which lights get a light array slot, in slot order, which are shadow-mapped), as `erhe::scene::Light_snapshot` copies: `resolve(lights, limits)` recomputes the slot layout when invalidated or the limits changed and refreshes every slot's copy, once per frame on the thread that owns the lights (an owner whose render phase overlaps the next frame's prepare keeps one `Light_set` per frame in flight, as the editor's `Scene_root` does); `Light_projections`, `Light_buffer` and `Shadow_renderer` read the copies and index by slot. `resolve()` and `compute_light_layer_partition()` take the light array capacity (`Light_interface::max_light_count`), and the shaded total stops at the smaller of it and the limits' `max_light_count`, so every slot fits the light array. The limits (`Light_count_limits`) hold per type shadow-mapped and unshadowed counts, `max_light_count` (a light that finds the shaded total full is not shaded) and `light_shading`, which the partition (`Light_layer_partition`) carries to the shader key: `set_light_partition_axes()` sets the six `LIGHT_COUNT_*` axes for `Light_shading::loops` and `LIGHT_SHADING_CLUSTERED` for `Light_shading::clustered` (doc/erhe/shader_variants.md). A clustered owner sets the unshadowed counts to `max_light_count`, so every active light is shaded and the shadow counts pick the shadow-mapped ones.
- `Cube_renderer` / `Cube_instance_buffer` / `Cube_control_buffer` -- Instanced voxel cube rendering system with packed 11-11-10 bit positions.
- `Glyph_interface` / `Glyph_buffer` -- Static SSBO holding quadratic bezier glyph curve data (from `erhe::ui::extract_glyph_outlines()`) for GPU curve-based text rendering, e.g. grid axis labels in the editor's grid shader. Fixed slot convention: 0..9 = digits '0'..'9', 10 = '-', 11 = '.'. `ERHE_GRID_LABELS` is always defined for shaders (SSBOs are a hard device requirement). Bound unconditionally by `Forward_renderer` (binding point 8) so the shared bind group stays complete.
- `Texel_renderer` -- Simplified renderer for texel-space operations.
- `Light_projections` -- Computes and stores shadow projection transforms for all lights in a frame; `view_buffers` (`Light_view_buffers`) holds the frame's light array and cluster lists of the view once `Light_buffer::write_view()` wrote them (`apply()` and `clear()` empty it).
- `Content_wide_line_renderer` -- Draws mesh edge lines as wide lines: a compute pre-pass expands each edge into screen-space triangles (`compute_before_content_line.comp`) that the graphics stages draw. The feed has two halves: the static `prepare_mesh(mesh_memory, mesh, color, line_width, group, Content_wide_line_feed&)` (on the thread that owns the mesh; the editor's prepare phase) appends one `Content_wide_line_record` per primitive with edge lines - the original variant's edge-line and joint buffer ranges, `world_from_node`, color, width, group, whether the mesh is skinned and its joint slot - and counts the mesh; `add_feed(feed)` (render phase) queues the records' dispatches without reading any mesh, the backend deciding whether a skinned record takes the skinned compute path. `add_mesh()` is both on one thread. The tent method controls (`Content_wide_line_tent_settings`: `use_tent`, `line_bias_margin`, `line_bias_clamp`) are passed per frame to `set_view_params()` with the views; the renderer keeps no settings of its own, so the editor passes the render phase's copy of its `content_edge_lines` setting.

## Public API
- Create `Program_interface` with a vertex format and config.
- Create `Forward_renderer` / `Shadow_renderer` with the program interface.
- Each frame, call `forward_renderer.render(params)` or `shadow_renderer.render(params)`; a renderer that prepares on one thread and draws on another calls `forward_renderer.prepare(...)` into a `Render_bucket_snapshot` it keeps per frame in flight and `forward_renderer.render(draw_parameters, snapshot)` later.
- Buffer classes (`Camera_buffer`, etc.) have `update()` methods that return `Ring_buffer_range` for binding.

## Dependencies
- erhe::graphics (Device, Ring_buffer_client, Shader_resource, Shader_stages, Texture, Sampler, Render_pass, Buffer, Gpu_timer)
- erhe::renderer (renderer_config: generated renderer configuration)
- erhe::scene (Camera, Light, Mesh, Skin, Node, Mesh_layer)
- erhe::primitive (Material, Primitive_mode)
- erhe::dataformat (Format, Vertex_format)
- erhe::math (Viewport)
- erhe::ui (Glyph_outline_set for GPU glyph curve data)
- glm

## Notes
- Buffer binding points are defined as macros in `buffer_binding_points.hpp` (0-10). The GPU light binner's compute layout has its own bindings 0-2 (inputs, lists, counters).
- All GPU buffers use the ring buffer pattern for lock-free multi-frame usage, except `Cube_instance_buffer` and `Glyph_buffer` which are static (uploaded once at init).
- `Primitive_buffer` supports ID-based GPU picking by assigning unique ID offsets to each primitive.
- `Primitive_interface_settings` picks one of three constant colors per primitive: `constant_color_active` for a selected entry that also carries every bit of `active_item_flag_bits` (the selection outline pass sets both, see `doc/editor/active_item.md` D5), `constant_color1` for an entry that carries a bit of `hovered_flag_bits` without being selected, and `constant_color0` otherwise. The two bit masks are the application's (the editor passes `Editor_item_flags::active_item` / `Editor_item_flags::hovered`); the library names no hover or active-item bit. `constant_color_active` is optional; when it is unset the writers substitute `constant_color0`, so a pass that does not distinguish the active item needs no change.

### Content edge line widths

The line width passed to `Content_wide_line_renderer::prepare_mesh()` / `add_mesh()` is a full
line width:
- **Negative** `w`: a constant screen-space line `-w` logical pixels wide,
  multiplied by `Camera_view_input::pixel_scale` (physical pixels per logical
  pixel of the render target: the window display scale for a desktop
  viewport, 1.0 for a headset eye) to get framebuffer pixels. It does not
  depend on the viewport size, the projection or the field of view, and
  matches `erhe::renderer::Primitive_renderer::set_thickness()` for the same
  value (`doc/erhe/renderer.md` "Line widths").
- **Positive** `w`: a distance-scaled width.

The `Content_line_width_gpu_test` cases of `erhe_scene_renderer_gpu_tests`
feed one edge through the renderer and check the exact pixel width across
viewport sizes, fields of view, orthographic projection and pixel scales.

### When `Material_set::update()` writes

`update()` writes a fresh copy of the whole record payload, and resets and
repopulates the texture heap from it, when membership changed, when
`invalidate()` was called (device-level events the materials know nothing
about), or when any live slot's material reports a change serial other than
the one recorded at that slot's last record write. Otherwise it returns and
the copy bound this frame is the one already there.

Every call, written or clean, publishes the buffer copy and the texture heap
the caller's device frame (the role-keyed `Device::get_frame_index()`) binds,
so `update()` runs once per frame on the thread that prepares the frame,
before the frame's first `bind()`; `bind()` / `unbind()` read only what was
published for their frame and verify it was. A write goes to a copy and a
heap no unretired frame was published with, so the render thread may record
frame F while the main thread updates for frame F + 1
(`doc/erhe/draw_list_material_set.md` D9).

Every input a record is built from is a property of
`erhe::primitive::Material`, and `Material::get_change_serial()` advances on
every one of them, plus on a texture graph bake that lands a different texture
behind a slot's `Texture_reference` (`Material` registers as a
`Texture_reference_user` of the slots it holds). So the per-frame cost of a
frame in which nothing was edited is one integer compare per live slot, and a
material that was edited is detected without re-reading its 26 layered
properties and five texture slots.

`Material_slot::recorded_serial` is empty while the slot has never been
written. A material's serial starts at zero, so "not yet written" cannot be
spelled as a serial value; `allocate_slot()` clears it, which is what makes a
slot handed to a new material gather on the next update.

`Draw_list_scene::check_material_changes()` gates the same way on the same
serial: it re-derives a watched material's shader-variant identity hash only
when the serial moved, and re-registers the objects using a material whose
hash then changed. The two are separate because they answer different
questions - a record input change dirties the material buffer, while a
shader-variant axis change moves an object between draw lists.

### The default material slot

Slot `Material_set::default_material_slot_index` (0) is reserved: it is alive
from construction, holds no `Material`, is never handed out by
`allocate_slot()`, and every `Material_set::update()` writes
`get_default_material_record_inputs()` into it, and slot 1 beside it (see
below). Slots for materials therefore start at 2.

That is the slot a primitive with no material of its own names: the primitive
record writer stores index 0 for a null `Mesh_primitive::material`, so an
unbound mesh renders erhe's default look - `Material_values` at its defaults
(roughness 0.5, metallic 0, opaque, no textures) with base color 0.18 grey,
which is UsdPreviewSurface's unbound `diffuseColor` and the same constant the
USD importer uses for an unauthored one. Slot 1
(`vertex_colored_default_material_slot_index`) is the same record with a
white base color, and the writer names it for an unbound primitive whose
mesh authored vertex colors (`Buffer_mesh::has_vertex_colors`): the shader
multiplies base color by vertex color, so white makes the vertex colors the
albedo, the way Storm shades a mesh with no material binding by its
`primvars:displayColor` (a bound material's base color multiplies the vertex
color, glTF `COLOR_0` semantics). Material slots start at
`first_material_slot_index` (2).

The reserved record is written once per buffer update, not per primitive; the
set holds no `Material` object for it, so it costs one record and no
membership.

### Where a material record's base color and alpha come from

A material record carries `uvec2 input_sources` -
`erhe::primitive::Material_input_source` for the base color and for the
fragment alpha - beside `uvec4 texture_channels` at the tail of the struct,
with an explicit `uvec2` of padding after it so the struct size stays a
multiple of 16 bytes. `value` (0) is the multiply the shader has always done:
the material's factor, the slot's texture where one is bound, and the mesh's
vertex color where the mesh carries one. `vertex_color` (1) makes the mesh's
color attribute that input on its own, the factor and the texture unread;
`standard.frag` and `erhe_ray_hit.glsl` both branch on it. That is what lets
an ordinary material take its base color from `primvars:displayColor`, which
slot 1 above can only do because its own base color is white.

### Specular anti-aliasing

`anisotropic_brdf()` (`res/shaders/erhe_bxdf.glsl`) widens its GGX lobe by a
kernel roughness added to each alpha^2 (Kaplanyan et al. 2016, Tokuyoshi
2017), so a highlight that changes faster than a pixel does not alias. Both
kernel forms below use the pixel filter's standard deviation `sigma` 0.5 and
the clamp `Kappa` 0.18; they are not equal estimates (the half vector form
squares an L1 footprint, the normal form sums squared derivatives, so they
can differ by up to a factor of 2). The function takes the kernel as an
argument and takes no derivative itself: the light loops of `standard.frag`
are non-uniform control flow (the per-light `N_dot_L` and range branches,
and in clustered mode neighbouring pixels walking different cluster lists),
where a derivative is undefined. `standard.frag` takes every derivative the
kernel needs once per fragment, in uniform control flow next to the shadow
receiver plane and ahead of the lightmap branch, and evaluates the per-light
part without derivatives, so the loop and clustered paths shade the same
pixels identically.

The mode is `Specular_anti_aliasing` (`shader_key.hpp`), the
`SPECULAR_ANTI_ALIASING` variant axis (doc/erhe/shader_variants.md
"Specular anti-aliasing"): the renderers' `specular_anti_aliasing`
parameters (default `normal_kernel`) set it on the variants that call
`anisotropic_brdf()`, and each variant holds only its mode's code.

- `off` - no kernel.
- `normal_kernel` - from the screen space derivatives of the shading normal
  (after normal mapping), once per fragment: `var_x = sigma^2 (dot(dN/dx,
  T)^2 + dot(dN/dy, T)^2)`, `var_y` likewise with `B`, kernel `min(2 var,
  Kappa)`. Filament evaluates this kernel on the geometric normal; the
  shading normal is used here so that normal map detail finer than a pixel
  is filtered as well, since that is the normal the BRDF sees. A
  flat-shaded face without a normal map gets no kernel.
- `finite_difference` - the kernel the function took before the modes
  existed: the footprint `|dH/dx| + |dH/dy|` of the shading frame half
  vector's xy, per light. Once per fragment (`specular_aa_begin_fragment()`)
  it forms the view direction in the shading frame and the position, `T`,
  `B` and `N` of the fragment's x and y quad neighbours: value plus or minus
  the fine derivative (`dFdxFine` where the GLSL version has it), the sign
  from the derivative of the pixel's own column / row parity, so both pixels
  of a pair difference the same two pixels as `fwidth()` does whatever the
  quad alignment. Per light (`specular_aa_light_kernel()`) the half vector
  is evaluated at the fragment and at both neighbours, the light vector
  moving with the position for spot and point lights. That is six
  normalizes per light where the former code had three (its `wo`, `wi`,
  `wh`, which `anisotropic_brdf()` no longer computes), plus three per
  fragment; `off` and `normal_kernel` have none per light.

  Comparison with the former in-loop `fwidth()` (the parent of this change,
  Debug, Metal, 960x540): with one directional light on the default scene's
  brushed-metal polyhedra (two cameras) the 8-bit images are identical. With
  one spot or one point light over a brushed-metal (Titanium) sphere and a
  Gold torus, 455 to 2718 pixels differ, by 2 on average and at most 74,
  along silhouettes and the `N_dot_L` terminator: the pixels whose quads
  were split by the former per-light branch, where its derivative was
  undefined, plus small rounding differences of the neighbour light vector.
  The former derivative's own coarseness was implementation-defined
  (`fwidth()` may be coarse), so it is not a reference beyond the
  hardware it was measured on.

Compute shaders (`erhe_ray_hit.glsl`) do not define the axis and pass a
zero kernel. The BRDF slice (`brdf_slice.frag`, a constant normal) defines
`finite_difference` and takes `fwidth()` of the half vector (uniform control
flow there). Every editor color pass, the material, brush and scene
previews and the thumbnails included, draws through `Composition_pass`,
which passes the frame's graphics preset mode
(`Graphics_preset_entry::specular_anti_aliasing`);
`Scene_preview::prewarm_variants()` warms the preset's mode and shadow
axes, and `Thumbnails` marks its slots stale on a graphics preset change so
each re-renders on its next draw.

## Future work
- [../plans/clustered_lights.md](../plans/clustered_lights.md): clustered light culling phases 3-5 (GPU binning, the headset grid, refinements)
