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
- `Light_buffer` -- Ring buffer clients uploading the lights: `update()` writes the `light_block` header (light counts, ambient, shadow map sentinels, DDGI volume; uniform block, binding 1) and the `light_array_block` lights (position, direction, color, shadow transforms; storage block `light_array.lights[]`, binding 9, the shaded lights only) and returns both as `Light_buffer_ranges`, which `bind_light_buffer()` binds and `release()` releases together.
- `Material_buffer` -- Ring buffer client uploading PBR material properties and texture handles.
- `Primitive_buffer` -- Ring buffer client uploading per-primitive world transforms, normal transforms, color, material index, and skinning data. `append_records(bucket, material_source, mode, settings, out)` writes a bucket's records into a CPU byte vector (the bytes `update(bucket, ...)` writes, id offset 0, no id ranges); the id pass overload `append_records(..., io_id_offset, id_ranges, out)` carries the id offset across buckets (start at `first_id_offset`, 1: id 0 is the background) and appends each record's `Id_range` (offset, length, the mesh held weakly, primitive index), the table an id readback resolves against. `update(std::span<const std::byte>)` copies prepared records into a ring buffer range; `get_record_size()` is one record's size.
- `Joint_poses` -- The posed joints of a scene's skins copied for a render phase: `capture(skins)` on the thread that owns the skins (after the frame's node transforms) assigns each skin's `Skin_data::joint_buffer_index` and computes one `Joint_pose` (`world_from_bind`, normal transform, joint node as an identity key) per joint slot, in skin order.
- `Joint_buffer` -- Ring buffer client uploading the `Joint_poses` entries for skinned meshes; reads no joint node.
- `Light_set` -- The resolved set of lights a scene is shaded with (which lights get a light array slot, in slot order, which are shadow-mapped), as `erhe::scene::Light_snapshot` copies: `resolve(lights, limits)` recomputes the slot layout when invalidated or the limits changed and refreshes every slot's copy, once per frame on the thread that owns the lights (an owner whose render phase overlaps the next frame's prepare keeps one `Light_set` per frame in flight, as the editor's `Scene_root` does); `Light_projections`, `Light_buffer` and `Shadow_renderer` read the copies and index by slot.
- `Cube_renderer` / `Cube_instance_buffer` / `Cube_control_buffer` -- Instanced voxel cube rendering system with packed 11-11-10 bit positions.
- `Glyph_interface` / `Glyph_buffer` -- Static SSBO holding quadratic bezier glyph curve data (from `erhe::ui::extract_glyph_outlines()`) for GPU curve-based text rendering, e.g. grid axis labels in the editor's grid shader. Fixed slot convention: 0..9 = digits '0'..'9', 10 = '-', 11 = '.'. `ERHE_GRID_LABELS` is always defined for shaders (SSBOs are a hard device requirement). Bound unconditionally by `Forward_renderer` (binding point 8) so the shared bind group stays complete.
- `Texel_renderer` -- Simplified renderer for texel-space operations.
- `Light_projections` -- Computes and stores shadow projection transforms for all lights in a frame.
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
- Buffer binding points are defined as macros in `buffer_binding_points.hpp` (0-9).
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

## Future work
- [../plans/clustered_lights.md](../plans/clustered_lights.md): clustered light culling (every light shaded, per-cluster light lists)
