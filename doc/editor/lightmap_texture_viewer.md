# Lightmap texture viewer

Stability: stable

`Lightmap_texture_window`
(`src/editor/windows/lightmap_texture_window.{hpp,cpp}`, "Lightmap Texture" in
the developer windows menu) displays the lightmap atlas with overlays in atlas
UV space. It is a pure viewer: it reads `App_context::lightmap_baker` (the
published atlas, the G-buffer debug textures and the `Atlas_layout` regions) and
each region's `erhe::geometry::Geometry`, and changes nothing. Companion to
[`lightmap_baking.md`](lightmap_baking.md).

## Display

- The image is drawn through
  `Imgui_renderer::image(erhe::imgui::Draw_texture_parameters{...})`. The
  texture combo selects the published atlas or the G-buffer position, normal and
  albedo debug views; an exposure slider multiplies the atlas tint, because the
  atlas is linear HDR and values above 1 clamp.
- Pan and zoom are the window's two view members, `m_zoom` (atlas-texel to
  screen-pixel scale) and `m_pan` (screen offset of the atlas origin), with
  `screen_from_uv` and `uv_from_screen` derived from them and used by every
  overlay.
  - The mouse wheel over the canvas zooms about the cursor: the atlas UV under
    the cursor stays fixed by adjusting `m_pan` after scaling. The window claims
    the wheel so it does not scroll.
  - Middle or right drag pans; left stays free for picking. The interaction
    surface is an `InvisibleButton` the size of the canvas, so panning never
    moves the ImGui window.
  - "Fit" (and a double-click on the canvas) recomputes zoom and pan so the
    atlas fills the canvas centred; the view refits when the page size changes.
    "Frame Selection" (also reachable over MCP as `lightmap_frame_selection`)
    frames the selected meshes' regions instead, resolving a selected source
    mesh to its lightmap piece.
  - Magnification past 1:1 uses nearest-neighbour filtering, so texel boundaries
    stay visible.
- With no layout or texture, the window shows the same hint text the Lightmap
  window does.

## Overlays

Overlay geometry is cached per region (`Region_overlay`: atlas-UV edge list plus
the per-facet fan triangles) and rebuilt whenever the layout signature changes -
the region count, and each region's mesh pointer, primitive index and
`uv_scale_offset`. Edges come from the same walk `build_seam_vertices` uses:
for each `Instance_region`, the primitive's `Geometry` must carry
`corner_texcoord_2`, and every facet corner UV is mapped through
`region.uv_scale_offset` into atlas space. Shared edges are deduplicated by the
order-normalized vertex-id pair **and** UV equality, so a seam edge draws once
per side (the two sides lie at different atlas positions).

- **Edges**: every lightmapped region's chart edges, dim and 1 px.
- **Mesh**: the hovered region's edges redrawn brighter, in hot pink.
- **Triangle**: the hovered facet's fan-triangle edges, in orange.

Edge lines are red; the scope combo limits the triangle overlays to all tiles,
resident tiles or the active tile. Tile bounds and the camera position can be
drawn over the atlas as well, and a tile row offers Subdivide and Merge.

Hovering is hit-tested closest source first: the mouse over the atlas image
wins, otherwise the 3D viewport hover is used. The atlas hit test maps the mouse
to a UV, finds the region whose atlas rect contains it, then runs a
point-in-triangle test over that region's facets, triangulated as the same
corner fan the G-buffer raster uses, so the test matches what was baked. The
viewport source comes from the `Hover_scene_view_message` subscription and the
hovered scene view's `Hover_entry` content slot (mesh, primitive index and
facet), matched to an `Instance_region`; a source mesh resolves to its lightmap
piece. The window reports the count of broken (degenerate) triangles it found
while building the cache.
