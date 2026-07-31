# transform/

## Purpose

Transform gizmo system for interactive translate, rotate, and scale operations.

## Key Types

- **`Transform_tool`** -- Coordinates the three sub-tools (move, rotate, scale). Manages gizmo visualization meshes, handle hit detection, and drag state. Subscribes to hover and selection messages. Binds keyboard shortcuts for switching between transform modes and toggling coordinate space (local/world). Hover previews (plane grid for a plane-translate handle, rotation plane circle + disc for a ring) show pre-drag only; axis handles have no hover preview (the infinite axis guide line was dropped as noise). During an active translate drag `render_translate_drag_guides()` draws the NODE's travel: the traveled segment (axis drag; yellow, with a short end-stop marker line across each end, orthogonal to the segment in screen space) or an axis-aligned rectangle + diagonal spanning the anchor's initial and current positions (plane drag; edges color-coded per axis, diagonal yellow), 1 px, reduced alpha. `render_drag_readout()` adds drag text: translate shows bare coordinate values - initial position at its own window projection, yellow delta at the travel midpoint, current position below the hover mesh name (mirrors Hover_tool's +50 px x anchor and 16 px line step, so it needs a valid hover and the viewport path); scale shows labeled initial/current anchor scale under the gizmo; rotation text is drawn by `Rotate_tool::render()`.

- **`Move_tool`** -- Subtool for translation. Renders axis arrows and plane handles. Computes translation from pointer ray intersection with the appropriate constraint plane/line.

- **`Rotate_tool`** -- Subtool for rotation. Computes rotation angle from pointer position relative to the gizmo center. During an active drag `render()` draws a protractor in place of the gizmo ring: radius = view scale * `Transform_tool_config::rotate_ring_size` (default 4.0 = the gizmo rings' own radius; slider in the Transform window), thin (1 px) axis-colored ring, step markers (inner radii scale with the ring) and the two indicator spokes (initial = reference direction, current = snapped angle). The swept sector is emphasized in yellow: 1.41 px ring arc (per-segment classification of one polyline, not overdraw) and in-sector step markers, plus a 0.14-alpha fill. Angle labels: initial and current (white) sit just outside the ring at their directions - initial is the swing-twist decomposition of the anchor's start orientation about the rotation axis, current = initial + snapped drag angle (continuous past +/-180 deg); the on-screen upper label is bottom-anchored and the lower top-anchored (exact font measure bounds, framebuffer-origin aware) so they can never overlap. The yellow delta label sits on the sector bisector at the same radius and is skipped when it would collide with either.

- **`Scale_tool`** -- Subtool for scaling. Renders scale handles. Computes scale factor from drag distance.

- **`Subtool`** -- Base class for transform sub-tools with shared handle visualization logic.

- **`Handle_visualizations`** -- Draws the gizmo handles (arrows, plane quads, rings, scale cones/cube) with the debug primitive renderer (x-ray lines and filled triangles; no scene meshes) and hit tests them analytically (`pick()`). Rendering and picking share the same per-handle visibility rules, so a handle is pickable exactly when it is drawn. The translate presentation is view-dependent: without negative handles, each axis shows the one camera-facing arrow (placed outside the rotate-ring sphere so arrows and rings never contest the same radius) and each plane quad sits at the gizmo center extending into the camera-facing quadrant (min corner on the center, so the three quads share edges). Draw order is inside-out (quads, rings, arrows) because the x-ray debug lines layer by submission order. Every rotate ring is hidden during ANY active drag (the drag-time axis-mask match would otherwise keep the same-axis ring up during axis translate/scale drags); the rotate protractor / travel guides replace them. In visible-arcs mode only SHOWN rings occlude arcs; the single-shown-ring presentation (full circle + tangent/bitangent guides fading in from the center) is currently unreachable because drags hide all rings.

- **`Handle_enums`** -- Enumerations for handle types (axis, plane) and coordinate spaces.

- **`Transform_tool_settings`** -- Settings for gizmo behavior (snap, local/world space).

- **`Rotation_inspector`** -- Debug window showing rotation decomposition.

## Public API / Integration Points

- `Transform_tool` is registered as a tool and activated from the hotbar
- `Transform_tool::update_target_nodes()` resolves the selection to the nodes the gizmo actually drives, via `resolve_transform_target()`. This is identity for everything except a skinned mesh whose joints live outside its own subtree: glTF 2.0 requires skinning to ignore the mesh node's transform (and erhe's `Joint_buffer` / `standard.vert` do), so dragging the host node would move the gizmo and leave the mesh in place. Such a selection redirects to `erhe::scene::get_skin_transform_root()`, and the Transform window shows a note naming the driven node. Targets are de-duplicated, so several meshes sharing one skin produce a single entry (otherwise a drag would apply its delta once per mesh). Selection itself is untouched - the host node stays selected, so delete / duplicate / Properties keep acting on what the user picked.
- Creates `Node_transform_operation` entries on the operation stack for undo/redo
- Uses `Time::begin_transform_animation()` for animated transform transitions

## Dependencies

- erhe::scene, erhe::commands, erhe::imgui, erhe::renderer (debug lines)
- editor: App_context, Tools, Operation_stack, Selection, Mesh_memory
