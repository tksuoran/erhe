# Selection outline from draw lists

Status: proposed

Moves every draw of the selection outline onto the scene's draw lists
(`doc/erhe/draw_list_renderer.md`), so that the outline follows the LOD level
a mesh is drawn with (`doc/plans/lod.md`) and gets the culling the draw lists
provide. Found while testing LOD step 3: with a forced level, the selected
fill draws the level while the outline still traces the full-detail mesh.

## 1. How the outline is drawn now

The outline is two parts that meet in stencil bit 7 (the selection
silhouette):

1. **The silhouette mask.** Bit 7 is written by whichever pass draws the
   selected meshes' triangles:
   - "Content fill selected" and "Content fill alpha tested selected" -
     already on the draw lists, so they draw the forced LOD level.
   - "Selection stencil mask (fill disabled)" and "Selection stencil mask
     (proxy hidden)" (`src/editor/app_rendering.cpp`) - on the bucket path,
     because they set `color_blend_override` (color writes off) and force
     `VARIANT_DEPTH_ONLY`, neither of which a draw-list color pass supports
     (`doc/erhe/draw_list_renderer.md`, the pass routing table). The bucket
     path draws level 0.
2. **The outline itself.** "Content outline opaque selected" is an
   `edge_lines` pass in content wide-line group 2: `Viewport_scene_view`
   feeds every edge of every selected or hovered mesh to
   `Content_wide_line_renderer`, which expands the edges into screen-space
   ribbons (`compute_before_content_line.comp`) of the pulsing selection
   width. The `outline` pipeline draws them with depth off where bit 7 is
   clear and sets bit 7. The ribbon halves outside the silhouette form the
   outline; where the selected mesh is occluded (no bit 7 there), its edges
   draw over the occluder as an x-ray wireframe.
   The edges are the base variant's edge-line buffer, which has no LOD
   levels, so the outline always traces level 0 - the mismatch seen.

The headset (`src/editor/xr/headset_view.cpp`) feeds the same passes the same
way.

## 2. Target

- Every outline draw - mask and outline - is a draw-list draw, so the LOD
  level, the culling and the GPU command path apply with no outline-specific
  selection code.
- The outline traces the silhouette of the level actually drawn.
- Same colors, pulse, active-item color, hovered handling and proxy-hidden
  sources as now.

## 3. Steps

### Step 1: stencil mask passes on the draw lists

- A dedicated base pipeline for the mask (color writes disabled, the selected
  fill's depth / stencil state) replaces `color_blend_override`; the pass
  supplies it like any other base pipeline.
- The draw-list color passes gain a depth-only sub-variant, resolved per list
  like the shadow lists' {depth-only, distance, cube} axis, so the
  `VARIANT_DEPTH_ONLY` force mask becomes a covered sub-variant instead of a
  fallback trigger.
- Both mask passes then route to the draw lists. Verification: with
  selection fill off, and with a proxy-hidden source selected, the bit 7
  footprint (captured as an image) equals the fill's at every forced level;
  `use_draw_lists` off still draws the same image through the bucket path.

### Step 2: the outline from the silhouette

The outline is drawn from the mask instead of from edges. Two forms; the
choice is decision O1 (section 4).

- **2a, screen-space (recommended).** The draw-list passes that write bit 7
  also write a small selection id (none / selected / hovered / active) into a
  mask attachment; a full-screen pass draws the outline ring where a pixel
  outside the mask has a masked pixel within the pulse width, colored by the
  nearest id. It traces exactly the triangles drawn - any LOD level, alpha
  tested cutouts, proxy-hidden sources - and has no cracks. Cost is per pixel
  of the view, bounded by the width (a direct search for the current widths;
  jump flooding if wider outlines are wanted). MSAA views read the mask per
  sample or from a resolve.
- **2b, dilated shell.** A draw-list pass draws the selected entries' triangles
  again with an outline vertex variant that pushes each vertex out along its
  projected normal by the width, with the current `outline` stencil state.
  It needs no new attachment, but it splits open at hard edges (the
  optimized variant has corner normals only; its smooth normal is dropped,
  `doc/erhe/meshoptimizer_integration.md`), and the width is only
  approximately constant on screen.

Either form then retires the group 2 feed in `Viewport_scene_view` and
`Headset_view`, and the `outline` edge-line pipeline.

### Step 3: occluded parts (decision O2)

The x-ray wireframe of occluded selected geometry exists only because the
edges draw with depth off. If it is kept, a second mask write with depth test
off feeds an "occluded" id, and 2a draws its boundary dimmed; otherwise it
is dropped.

### Step 4: docs and verification

- `doc/editor/rendering.md` "Selection outline", `doc/erhe/draw_list_renderer.md`
  (the depth-only color sub-variant, the routing table) and
  `doc/editor/lod.md` describe the result.
- Verification on the headless Vulkan editor: a creation with a LOD chain,
  each forced level, selection fill on and off, an alpha-tested mesh, a
  proxy-hidden source, an occluded selection, the active item and a hovered
  item; `render_scene_image` / `capture_screenshot` compared against the
  level drawn. Windowed check of the pulse by the user. Headset check of the
  outline in both eyes by the user.

## 4. Decisions for the user

- O1: outline form - 2a screen-space (recommended) or 2b dilated shell.
- O2: keep the x-ray outline of occluded parts, or drop it.
- O3: does the outline replace the per-level edge lines of `doc/plans/lod.md`
  D2 for the outline's purposes only (edge-line display passes keep D2), as
  assumed here.
