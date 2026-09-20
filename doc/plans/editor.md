# Editor: outstanding feature work

Status: proposed

Extends `doc/editor/editor.md` and the editor documents it indexes - `doc/erhe/layout.md`,
`doc/editor/active_item.md`, `doc/editor/settings_codegen_scene_reference.md`. Each
section is one piece of outstanding work; architectural cleanups live in
`doc/plans/editor_improvements.md` instead.

## Layout: a dirty scheme for the re-flow pass

`Scene::update_layouts()` recomputes every layout on every pass. A correct
dirty scheme re-flows on child add / remove / reorder, on a child's content
resizing and on a parameter edit, and NOT merely because the layout node's own
transform changed - the world-transform pass would otherwise mark it dirty
every frame. A per-layout input signature (child ids, content extents, item
parameters) is the suggested trigger.

## Layout: the Dock layout type

Places children inside the volume of the parent Dock layout node, in cells of
an X-Y-Z grid up to 3 x 3 x 3, each child selecting a cell.

## Layout: per-track extent UI at scale

The custom-size row in the layout properties becomes unwieldy past a handful
of tracks. Cosmetic.

## Layout: type icons

`Item_flags::index_layout` and `index_layout_item` have no `icon_set` glyph, so
they render without a type icon.

## Per-scene overrides whose consumers read their setting once

`src/editor/scene/scene_settings_resolve.hpp` resolves every override, but a
consumer that reads its setting once at init time never sees one. The viewport
clear color (`get_effective_clear_color`) and post processing
(`get_effective_post_processing`) have no caller at all; refactor those
consumers to ask per scene, per frame or on a settings message, so the
overrides take effect.

## Select-menu entries keyed off the active item

Blender's Select menu entries that key off the active object, now that
`doc/editor/active_item.md` gives erhe the same reference: select the children, the
parent or the siblings of the active item, select everything of the active
item's type, select the items that share the active item's material or mesh.

## Grid: screen-space label size in orthogonal views

Grid axis labels (`doc/editor/grid.md`) are sized in world units (`label_text_fraction`
of `label_spacing`), so in an orthogonal view they shrink and fade out as the
view zooms out and grow large when it zooms in. In an orthogonal view the
labels keep a fixed size on screen: `grid.frag` derives the text height from
the view's pixels per world unit (constant across an orthogonal view, already
available as `fwidth(uv)`), and the label spacing steps through the grid LOD
levels so neighbouring labels keep a minimum screen distance. Perspective
views keep the world-space size.
