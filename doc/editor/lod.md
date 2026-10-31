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
`Drawn` column marks the level whose index range the mesh's color draw-list
entry for that primitive holds (`find_draw_list_entry_lod_level()`, which
the MCP `get_lod_info` uses as well). A line under the table gives the
distance and the mesh's max axis scale.

The table is read when the window draws, under the scene's item host lock,
into member scratch vectors that are cleared after each draw, so no
reference to scene content outlives the draw.

## Config section

`Editor_settings_config::lod` (`Lod_config`,
`src/editor/config/definitions/lod_config.py`, with the enums
`Lod_attribute_set`, `Lod_border_mode`, `Lod_seam_mode` and
`Lod_fallback_mode` mirroring the `erhe::primitive` ones) holds the
generation defaults the window starts from: `level_count` 6, `ratio` 0.5,
`attributes` normals and colors, `normal_weight` and `color_weight` 1.0,
`border` locked, `seams` keep, `fallback` none, `normal_merge_angle` 20
degrees. Values not present in `editor_settings.json` take these defaults.

## Edge lines and the selection outline

The edge-line passes (the selection outline, "Content edge lines" selected /
not selected) draw the edges of the level the mesh's fill draws
(`doc/plans/selection_outline_lod.md`), so the outline traces the drawn
silhouette. `App_rendering::prepare_wide_line_feed()` feeds each mesh at
`get_forced_lod_level()` of its `Draw_list_scene` record when the pass's
fill draws through the draw lists, and at level 0 when it takes the bucket
path; which of the two is the first of the pass's
`Composition_pass_data::lod_level_source_passes` that submits for the view,
decided by that pass's own gating (`get_wide_line_lod_routing()`). The
selected / hovered edges follow the selected fill, or the selection stencil
mask while the selection fill is off; the other edges the unselected fill;
the ghost edge lines have no fill and stay at level 0. The viewports feed in
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
