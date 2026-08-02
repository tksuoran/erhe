# Lightmap texture viewer - implementation plan

Status: IMPLEMENTED 2026-08-02 (all phases including the phase-4 viewport
hover sync) as src/editor/windows/lightmap_texture_window.{hpp,cpp}
("Lightmap Texture", developer windows menu). Colors per user direction:
edge lines red, hovered mesh hot pink, hovered facet orange. Hover
priority: atlas mouse hover first, else the 3D viewport hover
(Hover_scene_view_message subscription + Hover_entry content slot; the
whole hovered facet highlights on a viewport hit). Companion to
doc/lightmap_baking_plan.md; assumes the interactive baker
(Lightmap_baker) as of branch lightmap-baking.

## Goal

A new ImGui window that displays the lightmap atlas texture with optional
overlays:

- a) edge lines of all lightmapped meshes (their chart layout in atlas UV
  space),
- b) highlighted edges of the hovered mesh (primitive),
- c) highlighted edges of the hovered triangle (facet).

"Hovered" primarily means the mouse hovering over the atlas image in this
window; syncing with the 3D-viewport hover is a cheap extension (phase 4).

## Non-goals (initial version)

- Editing anything (pure viewer).
- Mip/level selection, channel isolation, HDR analysis tooling.
- XR support (desktop ImGui window only).

## Architecture

New window class `Lightmap_texture_window` in
`src/editor/windows/lightmap_texture_window.{hpp,cpp}`, patterned on
`Lightmap_window` (same ctor signature, registered next to it in
editor.cpp ~line 1851 + member ~3704, added to App_context if other code
needs it - it does not). All data comes from `App_context::lightmap_baker`
(atlas texture, `Atlas_layout` regions) and from each region's
`erhe::geometry::Geometry`; no baker-side rendering changes are needed.

The window body has a small toolbar (texture selector, overlay checkboxes,
zoom controls) above a large image canvas.

### Phase 1 - texture display

- Draw `lightmap_baker->get_lightmap_texture()` with
  `m_context.imgui_renderer->image(erhe::imgui::Draw_texture_parameters{...})`
  (precedents: developer/post_processing_window.cpp,
  developer/ray_trace_window.cpp, preview/material_preview.cpp).
- The atlas is RGBA32F linear HDR; the first version displays it raw
  (values > 1 clamp) plus an exposure slider applied via the tint color
  multiplier. If that proves insufficient, add a small tone-map option
  reusing the Reinhard+gamma mapping `debug_write_lightmap_png` uses (would
  need a CPU copy or a tiny compute pass into an 8-bit display texture -
  defer until wanted).
- Texture selector combo: published atlas (default), plus the G-buffer
  debug views position / normal / albedo (`get_position_texture()`,
  `get_normal_texture()` exist; add a `get_albedo_texture()` getter).
- Interactive pan and zoom with the mouse (REQUIRED in the first landable
  step, not a later polish item). View state is two members, `m_zoom`
  (atlas-texel-to-screen-pixel scale) and `m_pan` (screen offset of atlas
  origin); two helpers derived from them are used by everything below:
  `screen_from_uv(vec2) -> ImVec2` and `uv_from_screen(ImVec2) -> vec2`.
  - Zoom: mouse wheel over the image zooms about the cursor - keep the
    atlas UV under the cursor fixed by adjusting `m_pan` after scaling
    (`pan' = mouse - uv_under_cursor * zoom'`). Exponential steps
    (e.g. *1.2 per wheel notch), clamped to roughly [0.1x fit, 64x]
    so a texel can be inspected up close. Use `ImGui::IsItemHovered()` +
    `io.MouseWheel`, and claim the wheel so the window does not scroll
    (`ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY)` or equivalent).
  - Pan: drag with middle or right mouse button while over the image
    (`IsMouseDragging` + `io.MouseDelta` added to `m_pan`). Left button
    stays free for future picking. Panning must not move the ImGui window
    - render the image inside a child region / use an InvisibleButton the
    size of the canvas as the interaction surface.
  - Fit: initial state and a "Fit" toolbar button (plus double-click on
    the canvas) recompute zoom/pan so the atlas fills the canvas centered.
    Refit automatically when the layout's page size changes.
  - Nearest-neighbor magnification is desirable when zoomed in past 1:1
    (texel boundaries visible); Draw_texture_parameters has a `linear`
    flag - pass false when zoom > 1.
- When no layout/texture exists, show the same hint text the Lightmap
  window shows.

### Phase 2 - overlay a): edge lines of all meshes

- CPU edge extraction mirrors `Lightmap_baker::build_seam_vertices`
  (lightmap_baker.cpp ~1490-1575): for each `Instance_region`, get the
  primitive's `Geometry`, require `corner_texcoord_2`, walk
  `geo_mesh.facets` corners, map each corner UV through
  `region.uv_scale_offset` into atlas space.
- Dedupe shared edges by the order-normalized vertex-id pair AND UV
  equality (a seam edge appears once per side - both sides should draw,
  they lie at different atlas positions).
- Cache per window: `std::vector<Region_overlay>` where each entry holds
  the region index, a flat `std::vector<std::pair<vec2, vec2>>` of atlas-UV
  edges, and a per-facet triangle list (below). Rebuild when the layout
  changes: compare `layout.regions` size + each region's mesh pointer,
  primitive index and uv_scale_offset (cheap), or add a revision counter
  bumped in `Lightmap_baker::update_layout` if that ever proves fragile.
- Draw with `ImDrawList::AddLine` after the image call, transforming each
  edge with `screen_from_uv`. Dim neutral color (e.g. 50% gray, alpha
  ~0.5), 1 px. Scene-scale edge counts (tens of thousands of lines) are
  well within ImDrawList capacity; skip edges fully outside the visible
  canvas rect when zoomed in.

### Phase 3 - overlays b) and c): hover highlight

- Hit test on mouse-over of the image: `uv_from_screen(mouse)`, find the
  region whose atlas rect contains the UV (`Instance_region` x/y/width/
  height over the page size), then point-in-triangle over that region's
  facets. Triangulate facets as the same corner fan the G-buffer raster
  uses so the hit test matches what was baked. Store per-facet fan
  triangles (atlas UV) in `Region_overlay` at cache-build time; the region
  rect prefilter keeps the per-frame test small. Result: hovered region +
  facet (GEO::index_t), or none.
- b) Hovered mesh: redraw that region's cached edges brighter/thicker
  (e.g. white, 1.5 px) on top of the phase-2 lines. Works with overlay a)
  off - b/c are independent checkboxes.
- c) Hovered triangle: draw the hovered facet's fan-triangle edges (or
  just the facet polygon outline) in an accent color (e.g. yellow, 2 px).
- Tooltip while hovering: mesh name, primitive index, facet id, atlas
  texel coordinate. (Radiance readout would need a GPU readback - defer.)

### Phase 4 (extension) - viewport hover sync

The 3D viewport hover already carries everything needed:
`Hover_entry::scene_mesh_weak`, `scene_mesh_primitive_index`, and `facet`
(scene/scene_view.hpp ~91-100). Either subscribe to `Hover_mesh_message`
on App_message_bus or poll the last hover scene view. When the mouse is
NOT over the atlas image, highlight the viewport-hovered primitive/facet
instead (find the matching `Instance_region` by mesh + primitive index).
This makes the window answer "where does this surface live in the atlas"
for free. A small mode indicator ("hover: atlas / viewport") avoids
confusion.

## UI summary

- Combo: Atlas / Position / Normal / Albedo
- Checkboxes: [a] Mesh edges, [b] Highlight hovered mesh,
  [c] Highlight hovered triangle (b defaults on, a and c on too - all
  cheap)
- Exposure slider (atlas only)
- Zoom: mouse wheel about the cursor; pan: middle/right drag;
  double-click or "Fit" button to re-fit (see phase 1 for details)
- No persisted config needed initially; if wanted later, follow the
  lightmap_config.py codegen pattern.

## Work estimate / order

1. Window skeleton + registration + raw atlas display + zoom/pan.
2. Edge cache + overlay a).
3. Hit test + overlays b) and c) + tooltip.
4. Texture selector extras, exposure, viewport hover sync.

Each step is independently landable and testable in the running editor
(bake the default scene via the Lightmap window, open the viewer, judge
visually; MCP window screenshots are allowed per the 2026-08-01 policy).
