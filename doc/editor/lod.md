# Mesh level of detail in the editor

Stability: experimental

The editor's side of mesh level of detail (LOD): the LOD window, which
generates LOD chains for the selected meshes, shows them, and forces a level
on the selection, and the editor config section its generation settings
start from. The chain itself (generation, storage as index ranges of the
optimized variant, `request_lod_generation()`) is
`doc/erhe/meshoptimizer_integration.md` "Level of detail"; the `Lod`
properties that pick a level are `doc/erhe/property_system.md` section 4.29
and `doc/erhe/scene.md` "Level of detail control"; the remaining steps are
`doc/plans/lod.md`.

## LOD window

`Lod_window` (`src/editor/windows/lod_window.{hpp,cpp}`, title "LOD", in the
Window menu with the other windows). It works on the meshes of the current
selection that belong to the active scene (`erhe::scene::get_mesh()` of each
selected item; a node holding a mesh counts as that mesh).

**Generation.** One widget per `erhe::primitive::Lod_settings` field: levels
(level 0 included, 1 to 8), ratio, attributes, normal and color weights
(disabled under positions only), border, seams, fallback and normal merge
angle. The window starts from the editor config section `lod` (below); edits
in the window apply to the session. "Generate for selection" and "Clear for
selection" queue `request_lod_generation()` with the window's settings or
with none (`Imgui_windows::queue`, `doc/editor/coding_rules.md` "ImGui-time
work"); the request runs at the start of the next prepare phase and queues
one deferred finalize task per distinct primitive. The window shows "Request
queued" until the request ran, then the editor's pending and running async
task counts while tasks run, and the request's counts (meshes, primitives,
queued, unchanged, held, not optimized).

**Forced level.** A level slider (0 to the longest chain of the selection)
and a "Release" button. Dragging the slider writes `Lod.apply_lod = true`,
`Lod.mode = forced` and `Lod.forced_level` on each selected mesh live, as the
Properties window rows write a dragged value, so the viewport follows the
drag; when the edit ends (`ImGui::IsItemDeactivatedAfterEdit`) the window
queues one `Compound_operation` of `Property_set_operation`s holding each
mesh's local states from before the drag and after it, so one undo restores
the levels from before the drag. "Release" queues one compound operation
that clears the local `Lod.apply_lod` of each selected mesh that has one;
the other values stay and take no effect while the key is unset. The meshes of a drag
are held only for its length: a closing scene (`close_scene` message) or a
removal (`items_removed`) drops them, and before each live write and before
the compound is recorded every mesh whose host is no longer a registered
scene (`App_scenes::is_host_registered()`) is dropped, so neither touches
content that left the editor. A frame in which the slider is not drawn (no
active scene, no selected meshes) ends the drag. Outside a
drag the slider shows the first selected mesh's forced level.

**Chains.** One table per distinct primitive of the selected meshes, labeled
with the primitive and the first selected mesh that uses it. Rows are the
levels of the optimized variant's chain (a variant without chain shows its
fill range as level 0): triangles, ratio to level 0, object space error, and
the error in pixels in a viewport of the scene, `error * max_axis_scale *
projection_scale / distance` (`doc/plans/lod.md` section 2.3). The viewport
is the last one the pointer was in when it shows the active scene, else the
first viewport window that shows it. `projection_scale` is the viewport
height over `tan(up) - tan(down)` of the camera's field of view sides
(`viewport_height / (2 tan(fov_y / 2))` for a symmetric frustum); for an
orthographic camera it is the height over the view's vertical extent, with
distance 1. `distance` runs from the camera position to the closest point of
the mesh's world AABB; inside the box the column reads "inside". The
`Drawn` column marks the level that viewport drew the mesh's color
draw-list entry for that primitive at in the last rendered frame. Lines
under the table give the distance and the mesh's max axis scale, and the
drawn level with its source ("automatic selection" when the viewport's LOD
column selected it, "template range" for the forced level or level 0) and
the frame. Each draw of the window requests the selected meshes' drawn
levels (`Lod_drawn_levels`, below), so they arrive one rendered frame
later and a closed window requests nothing.

The table is read when the window draws, under the scene's item host lock,
into member scratch vectors that are cleared after each draw, so no
reference to scene content outlives the draw.

**Drawn levels.** `Lod_drawn_levels` (`src/editor/renderers/lod_drawn_levels.{hpp,cpp}`,
owned by `App_rendering`) carries the levels the views drew to the main
thread (`doc/plans/lod_gpu_selection.md` section 5). A requester (the LOD
window, MCP `get_lod_info`) names meshes of one scene (`request()`,
one-shot); the next prepare phase resolves them, after the draw lists were
published, to their primitives' color entries in the frame's published
lists (`Draw_list_scene::find_published_color_entry()`) into the frame's
query slot; in the render phase each viewport (in its rendergraph node's
execute, after the frame's culling classification) and the headset record
the level each entry draws in their color culling view
(`Draw_list_scene::get_published_entry_lod_level()`) into a
`Render_frame_results` slot, which the main thread reads for the last
rendered frame (`doc/editor/render_thread.md`).

## Config section

`Editor_settings_config::lod` (`Lod_config`,
`src/editor/config/definitions/lod_config.py`, with the enums
`Lod_attribute_set`, `Lod_border_mode`, `Lod_seam_mode` and
`Lod_fallback_mode` mirroring the `erhe::primitive` ones) holds the
generation defaults the window starts from: `level_count` 6, `ratio` 0.5,
`attributes` normals and colors, `normal_weight` and `color_weight` 1.0,
`border` locked, `seams` keep, `fallback` none, `normal_merge_angle` 20
degrees. Values not present in `editor_settings.json` take these defaults.

## Graphics preset settings

The graphics preset (`Graphics_preset_entry`, version 8) holds the automatic
selection's settings: `lod_enabled` (default off), `lod_threshold_pixels`
(the pixel error a selected level stays within, default 1.0), `lod_bias`
(added to every selected level, default 0) and `lod_hysteresis_band` (0 to
0.9, default 0.25; below). The Settings window shows them as "LOD
Selection", "LOD Threshold (pixels)", "LOD Bias" and "LOD Hysteresis Band";
the MCP `set_graphics_preset` takes `lod_enabled`, `lod_threshold_pixels`,
`lod_bias` and `lod_hysteresis_band` (session only). Each frame's render
settings snapshot carries them to `Draw_list_scene::publish()`
(`Lod_frame_settings`), and `get_draw_lists` reports what the last publish
recorded (`lod`).

## Automatic selection

With "LOD Selection" on and some mesh with a chain in the scene, the
culling classification selects a level for every color draw-list entry in
every viewport, four view cell and `render_scene_image` capture (their
culling views carry a `Lod_view`: the camera position and the projection
scale of the view's height), and in the headset's union view (the head
origin and the largest per-eye projection scale; not yet run on a headset):
the coarsest level whose error stays within
"LOD Threshold (pixels)" at the distance from the camera to the closest
point of the mesh's world box (`error * max_axis_scale * projection_scale /
distance <= threshold`), plus the mesh's `Lod.bias` and "LOD Bias", clamped
to `Lod.min_level` / `Lod.max_level` and to the chain; a mesh under
`Lod.mode` forced or off draws its forced level or level 0 as before
(`doc/erhe/draw_list_renderer.md` R1c). The selection runs in the culling
classification on the CPU and, with `culling_classification` gpu on a
device with the count draw, also in its compute pass with the same
arithmetic, bit for bit (`doc/plans/lod_gpu_selection.md` steps 2a and
2b), so every color draw-list pass draws it, whether the CPU writes its
commands or the GPU generates them; `get_cull_classification_check`
compares the two (`lod_mismatch_count`). The edge lines and the selection
outline trace the selected level (below), the LOD window's "Drawn" column
and MCP `get_lod_info` (`drawn`) report it per view, and MCP
`get_lod_stats` the entries and triangles each draw-list color pass drew
per level: counted by the CPU command writer
(`Draw_indirect_buffer::update(..., level_counts)`), or with GPU-generated
commands by the compaction per job, read back with the retired frame's
counts (`Retired_gpu_color_counts::lod_counts`; frames in flight late, so
they include occlusion like the drawn counts). Skinned meshes draw the
template range (the forced level, or level 0).

The draw-list shadow passes select too (`doc/plans/lod_gpu_selection.md`
step 5): each viewport's `Shadow_render_node` names its scene view as the
view sampling its maps, and every spot, point (one selection for the six
cube faces) and directional pass gets a column of its own. A caster inside
the frustum of a sampling viewport is drawn at the level that viewport
draws it at, so a mesh shadows itself and its surroundings at the level it
is seen at; occluded casters inside the frustum count. A spot or point
pass shared by several viewports (four view) takes the finest level of the
viewports whose frustum holds the caster, so in a coarser viewport the
receiver at level L and its finer caster differ by up to the error of
level L, which the shadow bias does not cover: shadow acne is possible
there. A caster the pass draws outside every sampling frustum takes the
light's own selection: from the light position through the shadow map's
projection (a spot cone, a cube face at the point map's resolution; a
directional pass is orthographic, so by the map's texel size), with its own
hysteresis history. A mesh outside the light's frustum or reach is not
drawn in the pass and has level 0 in its column (`get_lod_selection`
shows 0 there); when it enters the reach its own selection starts at the
level the coarsening threshold gives (the finer end of the hysteresis
band). A pass whose sampling viewport has no LOD column (its color passes
draw no draw-list view with a LOD view, or it is past the color column
limit) draws every caster at the template range, as that viewport's
receivers do. The shadow map cache redraws a pass when a level of a caster
it draws changes (`doc/erhe/shadows.md` "Shadow map cache"); a camera move
that changes no level keeps every cached spot and point pass.

MCP `get_lod_selection` reports the levels of one frame for a
mesh, per column, with each column's origin, projection scale, `lod_key`,
matched previous column, purpose and, for a shadow column, its sampling
color columns and level change, the shadow views drawing the template
range, and the scene's hysteresis restart and shift counts.

Levels pop with hysteresis: a view moves a mesh to a coarser level only
once that level's error stays within "LOD Threshold (pixels)" times (1 -
"LOD Hysteresis Band"), and back to a finer level as soon as the current
level's error exceeds the threshold, so a camera that stops between the
two keeps the level it arrived with. With a band of 0.25 a level that
switches at distance d on the way in switches at d / 0.75 on the way out;
0 is the plain threshold rule. Each view keeps its history by its scene
view (a view added or removed beside it keeps it); the history restarts
after a frame the scene was not rendered in, a mesh added or removed, and
a `culling_classification` switch (`doc/erhe/draw_list_renderer.md` R1c
"Hysteresis").

## Edge lines and the selection outline

The edge-line passes (the selection outline, "Content edge lines" selected /
not selected, the ghost edge lines) draw the edges of the mesh's LOD level
(level edge lines: `doc/erhe/meshoptimizer_integration.md` "Level of detail"), so the outline traces the drawn
silhouette. `App_rendering::prepare_wide_line_feed()` (prepare phase)
writes one record per mesh primitive at `get_forced_lod_level()` of its
`Draw_list_scene` record, with the edge lines of every level of the
primitive's chain and the position of its color entry in the lists
published for the frame (`Content_wide_line_renderer::prepare_mesh()`,
`doc/erhe/scene_renderer.md`). The render phase queues the feed after the
frame's culling classification (`Content_wide_line_renderer::add_feed()`)
with the view's color culling view (`Composer_prepared::color_cull_view`,
set by `Composer::add_cull_views()`; the headset's union view): a record
whose entry's level was selected by the view's LOD column draws that
level's edge lines, the same level the view's fill draws; otherwise (the
selection off, a view without a column or without draw-list passes) the
record draws the forced level it was prepared with. Edge lines are not
content: the view's shader debug mode and the path its fill takes do not
change them. The selection silhouette the outline is masked by is drawn at
the same level: the selected fills and, with the selection fill off or for
proxy-hidden sources, the selection stencil masks
(`doc/editor/rendering.md` "Selection stencil mask") all draw through the
draw lists. A fill on the bucket path (`use_draw_lists` off) draws level 0,
so the outline then shows the mesh's level over a level-0 fill. The viewports feed in
their composition prepare and the headset in `Headset_view::
prepare_wide_line_feed()`, both in the prepare phase; the render phase only
queues the frame's feed. The draw list entries take their variant at the
draw-list flush and the feed resolves the live variant later in the same
prepare phase, so a variant a worker commits in between shows the old
variant's fill under the new variant's edges for one frame (both stay
allocated until the frame retires).

## Verification

`render_scene_image` renders through the same composition passes as a
viewport (`Scene_image_view::prepare_composition()` ->
`App_rendering::prepare_composer()` -> `Composition_pass::prepare()`), so with
`use_draw_lists` on (the editor config) and no shader debug its fill passes
draw from the draw lists, where the forced level's range sits; with
`use_draw_lists` off the bucket path draws level 0. The headless Metal
recipe: create a torus, select it, open the window
(`set_window_visibility`), press "Generate for selection" (`imgui_click`),
wait for `get_async_status` idle, drag the slider (`mouse_drag`), and compare
`get_lod_info` and `render_scene_image` md5s per level, after undo / redo,
after "Release" (equal to the image without `Lod`) and after "Clear for
selection".

`scripts/lod_dolly_verify.py` checks the automatic selection: the switch
distances while it dollies the camera, `render_scene_image` at automatic
level L against forced level L, and with `--outline` the selected torus's
viewport (the selection outline, with the selection fill on and off) at
automatic level L against forced level L, and `get_lod_info`'s drawn level.
With `--hysteresis BAND` it also dollies back in (the outward switches at
d_L / (1 - BAND), the inward ones at d_L) and stops the camera inside each
level's band from either side, requiring the level it arrived with for 300
frames, with the classification check under gpu at every hold.
