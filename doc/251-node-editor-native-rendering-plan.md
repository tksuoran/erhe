# Issue #251: Native-resolution ax::NodeEditor rendering - plan

Status: Phases 0-5 done (crease d4dd4c7b..8b55d5a7). Phase 6 rendering bugs from
initial user testing (node width/widgets/pins/background not zoom-correct) found
and fixed (53ae6eb5, ab69475e, 46cd4341, 8d32ff6e); see the notes doc Phase 6
section. Remaining: live mouse-interaction verification (drag / box-select /
link-create / zoom-under-cursor) which headless cannot drive.
Issue: https://github.com/tksuoran/erhe/issues/251

## Problem and root cause

Node editor canvases (geometry graph, texture graph, shader graph, rendergraph
window) look pixelated/blurry when zoomed. The issue text says "rendering to
texture", but the actual mechanism is equivalent in effect and lives in the
vendored `ImGuiEx::Canvas` (`src/erhe/imgui/erhe_imgui/imgui_canvas.cpp`):

- `EnterLocalSpace()` fakes a virtual coordinate space: it rescales
  `io.MousePos` / `MousePosPrev` / `MouseClickedPos[]`, sets
  `window->Pos = (0,0)`, and rescales the ImGui viewport Pos/Size/WorkPos by
  `InvScale`. All node content then lays out and draws at BASE size in this
  fake space.
- `LeaveLocalSpace()` post-multiplies every emitted vertex (and clip rect) by
  `Scale` and adds the pan offset, and compensates AA via a `_FringeScale`
  hack.

Consequences:

1. **Pixelation**: glyphs are rasterized into the font atlas at base size;
   zooming scales the textured quads, magnifying pixels. Same for icon pins
   drawn from textures. Vector shapes (links, node frames) survive, but AA
   fringe is scaled too.
2. **Popups/combos are broken inside the canvas**: the global input/viewport
   faking means any nested ImGui window (BeginCombo popup, BeginPopup, context
   menu) opens in fake coordinates. Upstream's escape hatch
   (`ed::Suspend()/Resume()`) cannot be called between `BeginNode`/`EndNode`,
   which is exactly where per-node widgets live - hence the
   `imgui_enum_stepper` workaround throughout `src/editor/*_graph/nodes/`.

## Enablers

- **ImGui 1.92.7 (vendored at `src/imgui/imgui/`) has the dynamic font
  system**: `PushFont(font, size)` bakes glyphs at any float size on demand
  (`ImFontBaked`). Crisp text at arbitrary zoom no longer needs pre-baked
  atlases.
- **Zoom is already quantized**: `NavigateAction::s_DefaultZoomLevels`
  (imgui_node_editor.cpp) snaps zoom to a small discrete set, which bounds the
  number of distinct font sizes the atlas will bake.
- **We fully vendor ax::NodeEditor** (`src/erhe/imgui/erhe_imgui/
  imgui_node_editor*.{h,cpp,inl}`, `imgui_canvas.*`, ~9.5k lines), so
  arbitrary rewrites are sanctioned by the issue.

## Chosen design: zoom-by-layout at native resolution

Replace "lay out at base size in fake space, scale vertices afterwards" with
"lay out and draw directly in screen space at the zoomed size":

- `ImGuiEx::Canvas` (or its replacement) keeps only pan/zoom STATE and the
  coordinate mapping `screen = canvas_pos * scale + origin`. No vertex
  post-transform, no io.MousePos/viewport/window faking, no fringe hack.
- Node editor primitives (grid, node frames, pin shapes, links, selection
  rect) transform their POINTS at submission time and multiply their SIZES
  (thickness, rounding, pin radius, arrow sizes) by zoom. These are vector
  shapes, so they stay crisp automatically.
- Node CONTENT (the widgets between BeginNode/EndNode) is emitted as ordinary
  screen-space ImGui widgets: cursor placed at `canvas_to_screen(node_pos)`,
  font pushed at `base_size * zoom` via dynamic fonts, relevant style metrics
  (FramePadding, ItemSpacing, ItemInnerSpacing, FrameRounding, GrabMinSize,
  etc.) pushed scaled by zoom for the duration of the editor scope.
- Node geometry stays STORED in canvas units: measured screen-space group size
  is divided by zoom before storing; hit testing and all editor actions
  (drag, box select, link create/cut, navigate) work on canvas coordinates
  obtained via `FromScreen(io.MousePos)` - real mouse coordinates, no faking.
- Because there is no fake space anymore, popups/combos/context menus opened
  from inside node content just work; `Suspend()/Resume()` become
  near-no-op compatibility shims.

### Alternatives rejected

- **Supersampled/MSAA render-to-texture**: band-aid; memory cost, still soft,
  does not unblock popups.
- **Native-resolution text overlay only** (draw text suspended in screen
  space, keep vertex transform for the rest): two text paths, widgets/icons
  stay blurry, popups stay broken.
- **Wait for upstream**: thedmd/imgui-node-editor has had this open for years
  (blurry-zoom issues); during execution, do a quick check of upstream
  `develop` for dynamic-font canvas work worth cherry-picking, but do not
  block on it.

## Scope decision: combos/popups

Include the ENABLEMENT in this task - it falls out of the rewrite for free and
is the acceptance test that proves the fake space is really gone. Concretely:
verify `BeginCombo` + `BeginPopupContextItem` work inside a node, and convert
ONE pilot site (e.g. the Conway op stepper in
`src/editor/geometry_graph/nodes/conway_node.cpp`) to a real combo, plus one
canvas context menu.

DEFER the broad conversion of all steppers to combos and full context-menu UX:
that belongs to the graph-editor de-duplication work ("Phase C" in
prompt_queue.txt / doc/texture-graph-plan.md), which will create the shared
widget layer those conversions should land in. Doing it per-window now would
add a fourth copy of widget code that Phase C then unifies.

## Sequencing constraints

- The two main consumers with smoke-test coverage (geometry graph 126-check,
  texture graph 266-check sweeps) live on branch `texture` (unmerged as of
  writing). Execute #251 on a branch that CONTAINS those consumers and their
  sweeps (after `texture` merges, or based on it) - the sweeps are the
  regression net.
- Independent of Phase C (editor-level dedup); either order works, but do not
  interleave them. If Phase C lands first, the pilot combo conversion goes
  into its shared widget layer instead.

## Implementation phases

Each phase: build clean (`scripts\build_ninja_win_vulkan.bat editor` AND the
headless VS build), headless-verify, independent diff review, own commit.

### Phase 0 - Inventory + baseline harness

- Grep-catalog the `ax::NodeEditor` / `ed::` API surface actually used by the
  4 consumers (`src/editor/graph/graph_window.cpp`,
  `geometry_graph/geometry_graph_window.cpp`,
  `texture_graph/texture_graph_window.cpp`,
  `developer/rendergraph_window.cpp`) plus the node base classes. Anything
  unused by erhe may be deleted rather than ported.
- Inside the vendored editor, inventory every consumer of the local-space
  invariant: places assuming `GetCursorScreenPos()` is canvas-space, the
  `m_OldCanvas` debug remnants (imgui_node_editor.cpp ~1480), NavigateAction
  view math, `ImDrawCallback_ImCanvas` sentinel handling, channel splitter
  interplay.
- Add a zoom-quality screenshot harness: headless MCP script that opens the
  Geometry Graph window with a known graph, sets a sequence of zoom levels
  (add a small MCP tool or reuse navigate-to-content + zoom steps if needed),
  and captures screenshots. Record BASELINE images at zoom ~0.5 / 1.0 / 2.0 /
  4.0 for before/after comparison. Commit the script, not the images.

### Phase 1 - Strangler prep (no behavior change)

- Introduce explicit `View` transform helpers (canvas<->screen point/vector/
  rect) and route ALL drawing and hit-test code in imgui_node_editor.cpp
  through them, while the canvas still runs local space (helpers are identity
  + pan at this stage). Pure mechanical refactor; screenshots must be
  pixel-identical; sweeps green.

### Phase 2 - Flip primitives to screen space

- Disable the vertex post-transform and input/viewport faking for the
  editor's OWN drawing: grid, node frames/backgrounds, pins, links, flow
  animation, selection rect now submit pre-transformed points with
  zoom-scaled thickness/rounding/radii. Hit testing switches to
  `FromScreen(mouse)`.
- Node content still needs to render; as an intermediate step it may render
  at base size positioned at the transformed origin (temporarily wrong scale
  at zoom != 1) - acceptable only WITHIN this phase's branch-local work; the
  phase is committed together with Phase 3 if intermediate state is too
  broken to verify meaningfully. Prefer committing 2+3 as one step if needed.

### Phase 3 - Node content at native resolution

- BeginNode: set cursor to `canvas_to_screen(node_pos)`; push font at
  `base_size * zoom` (round to a stable quantum, e.g. 0.5 px, to limit baked
  sizes and layout jitter); push zoom-scaled style vars; EndNode: measure the
  group in screen space, store `size / zoom` in canvas units.
- Scale pin/link anchor bookkeeping accordingly (pivot rects are stored in
  canvas units already; verify).
- Layout-stability pass: with fonts measured at different sizes, node canvas
  size varies slightly per zoom level. Mitigate by consistent rounding
  (ceil to whole canvas units) and verify no visible node-size jitter or
  link-anchor swimming while zooming (mouse-anchored zoom must keep the point
  under the cursor fixed - NavigateAction math check).

### Phase 4 - Remove dead machinery

- Delete `EnterLocalSpace`/`LeaveLocalSpace` vertex/clip transforms, input
  backup/restore, viewport faking, `_FringeScale` reflection hack,
  `ImDrawCallback_ImCanvas` sentinel, and the `m_OldCanvas` debug block.
  Keep `Suspend()/Resume()` as compatibility no-ops (consumers and vendored
  code still call them).
- `ImGuiEx::Canvas` shrinks to view state + mapping; consider folding it into
  the editor context entirely.

### Phase 5 - Popups/combos pilot

- Prove `ImGui::BeginCombo` and `BeginPopupContextItem`/`BeginPopupContextWindow`
  work inside node content and on the canvas background.
- Convert one stepper to a real combo (Conway op) and add one context menu
  (e.g. canvas right-click "Add node" in the geometry graph window, if not
  conflicting with existing ax create/delete gestures).
- Leave all other steppers as-is; note the follow-up in
  doc/editor_improvements.md pointing at Phase C.

### Phase 6 - Verification + polish

- Re-run the zoom screenshot matrix; compare against Phase 0 baselines: text
  must be crisp at 2x/4x (READ the PNGs, do not just diff sizes).
- Both smoke sweeps green (geometry + texture), plus the shader graph and
  rendergraph windows opened and screenshotted once each (no sweep exists;
  visual sanity only).
- Manual/live user verification for interaction feel: node drag, link create/
  delete, box select, zoom-under-cursor anchoring, no jitter.
- Check font atlas growth across all zoom levels (log atlas size before/
  after); acceptable because zoom levels are quantized.
- Restore `config/editor/desktop_window_imgui_host_imgui.ini` after runs.

## Risks

- **Hidden local-space assumptions** in the 5.8k-line editor (biggest risk).
  Mitigation: Phase 0 inventory + Phase 1 strangler routing before any
  behavior change.
- **Layout wobble across zoom levels** (font metrics are not linear in size).
  Mitigation: quantized font sizes, consistent canvas-unit rounding; accept
  <= 1 canvas-unit variance; verify with anchored-zoom screenshots.
- **Interaction regressions** (hit test off-by-scale, drag deltas, cut lines).
  Mitigation: all editor actions consume canvas coordinates from one
  `FromScreen` helper; manual verification checklist in Phase 6.
- **ImGui state across zoom change mid-interaction** (e.g. InputText active
  while zooming rebases glyph widths). Accept minor artifacts; zooming while
  text-editing is an edge case.
- **Multi-viewport / imgui_host interplay**: erhe uses its own ImGui backend
  and hosts; removing the viewport faking must be verified in both the
  desktop window host and viewport-window hosts.
- `src/rendering_test/` breakage is acceptable per CLAUDE.md (it does not use
  the node editor anyway).

## Estimated shape

Phases 0-1 one session; 2-3 the core work, one to two sessions; 4-6 one
session. Every phase leaves the tree green and committed.
