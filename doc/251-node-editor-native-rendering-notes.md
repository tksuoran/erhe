# Issue #251 - implementation notes / inventory (Phase 0)

Companion to `doc/251-node-editor-native-rendering-plan.md`. Captures the Phase 0
inventory so later sessions do not re-derive it. All line numbers are approximate
(as of branch `crease`, 2026-07-04) and drift as edits land - use them as a
starting map, re-grep before touching.

## Vendored files

- `src/erhe/imgui/erhe_imgui/imgui_canvas.{h,cpp}` - `ImGuiEx::Canvas`, the
  fake-space transform. This is where the invariant lives.
- `src/erhe/imgui/erhe_imgui/imgui_node_editor.cpp` (~5818 lines) - the editor.
- `imgui_node_editor_internal.h` (~1563) - `EditorContext`, `NavigateAction`,
  `NodeBuilder`, `Node`/`Pin`/`Link`, the editor's OWN fringe machinery.
- `imgui_node_editor_api.cpp` - thin API wrappers.
- `imgui_node_editor.h` - public `ax::NodeEditor` surface.

Important: this vendored copy was refactored from upstream's free-function API
(`ed::Begin`) into **member functions on `ax::NodeEditor::EditorContext`**. The
`ed` alias (`= ax::NodeEditor::Detail`) is internal to the .cpp only. Consumers
own a `std::unique_ptr<EditorContext>` and call methods on it.

## The single invariant to break

`Canvas::EnterLocalSpace()` (imgui_canvas.cpp ~407):
- fakes `io.MousePos/MousePosPrev/MouseClickedPos` into local space (~484-487)
- sets `window->Pos=(0,0)` and rescales viewport Pos/Size/WorkPos by InvScale (~450-472)
- `fringeScale *= InvScale` (~491-493)

`Canvas::LeaveLocalSpace()` (imgui_canvas.cpp ~496):
- post-multiplies **every emitted vertex** by `Scale` and adds pan (~513-555)
- post-multiplies clip rects likewise
- strips the `ImDrawCallback_ImCanvas` sentinel, restores fringe/input/viewport

Everything the editor draws happens in base/local space and depends on that
post-transform. Remove it and every draw site must transform points + scale
sizes itself, and node content must be laid out at zoomed size in true screen
space.

## Existing routing layer (already present - reuse for the strangler)

- `CanvasView { Origin, Scale, InvScale }` with `ToLocal/FromLocal/ToLocalV/
  FromLocalV` (+ per-view overloads), `CalcViewRect` (imgui_canvas.cpp 298-344).
- Editor shims: `EditorContext::ToCanvas(p) = m_Canvas.ToLocal(p)`,
  `ToScreen(p) = m_Canvas.FromLocal(p)` (internal.h 1363-1364). These are the
  canvas<->screen mapping the plan's "View" helpers want. In the CURRENT model
  drawing does NOT go through them (vertices are base-space, scaled later), so a
  strangler needs draw-site helpers that are IDENTITY in Phase 1 and become the
  real transform in Phase 2.

## NavigateAction view math (zoom/pan state)

- State: `m_Zoom` (float), `m_Scroll` (ImVec2). View = `CanvasView(-m_Scroll, m_Zoom)`.
- Per frame: `m_Canvas.SetView(m_NavigateAction.GetView())` (imgui_node_editor.cpp ~1202).
- Accessors: `GetView/GetViewOrigin/GetViewScale/SetViewRect/GetViewRect` (~3564-3589).
- Pan: `m_Scroll = m_ScrollStart - m_ScrollDelta * m_Zoom` (~3292, 3377).
- Zoom-under-cursor `HandleZoom` (~3397-3441): anchors the screen point under the
  mouse across the zoom change via `FromLocal(mouse, oldView)` -> `ToLocal(screen, newView)`.
  Uses `io.MousePos` which is faked-local during the canvas scope. When faking is
  removed this must read the true-screen mouse and map with the real view.
- Discrete zoom levels: `s_DefaultZoomLevels[]` (~3253) - bounds the number of
  distinct baked font sizes.
- Persisted: settings load `m_ViewScroll/m_ViewZoom` (~2134), save (~2166).

## Primitive draw sites (Phase 2 - transform points, scale sizes)

- Links/beziers/arrows: `ImDrawList_AddBezierWithArrows` (~431-506),
  `Link::Draw` (~842-889). Extra thickness constants 4.5/2.0/3.5 (~856-870),
  flow overlay 2.0 (~3088).
- Pin: `Pin::Draw` (~518-537) AddRectFilled/AddRect, wrapped in `FringeScaleScope(1.0f)`.
- Node/group/border: `Node::Draw` (~607-676), `Node::DrawBorder` (~678-687),
  group border under `FringeScaleScope(1.0f)`.
- Grid/background: End() ~1444-1465. `GRID_SX/SY = 32.0f` (base px), grid color
  fades with `ViewScale*ViewScale`.
- Flow animation: `FlowAnimation::Draw` (~3088), marker radius `4*(1-p)+2` (~3095).
- Selection rect: `SelectAction::Draw` (~4169-4186).
- Canvas outer border: ~1547-1553 - drawn AFTER `m_Canvas.End()`, already in
  true screen space. LEAVE UNTOUCHED (do not scale).

## Node content layout / bounds (Phase 3)

- `NodeBuilder::Begin` (~5166): `SetCursorScreenPos(m_Bounds.Min)` - in the fake
  space this places content at base size at canvas coords. Target: cursor to
  `ToScreen(m_Bounds.Min)`, push font at base*zoom (quantized), push zoom-scaled
  style vars.
- `NodeBuilder::End` (~5248): `m_NodeRect = ImGui_GetItemRect()`, stores size into
  `m_Bounds.Max = Min + size`. Target: measure in screen space, store `size/zoom`.
- Node/pin bounds (`m_Bounds`, `m_GroupBounds`, pin `m_Bounds`/`m_Pivot`) are all
  stored in CANVAS units and only converted via `ToScreen` where needed
  (HintBuilder ~5531). Keep them canvas-space; hit-test against `ToCanvas(mouse)`.

## True-screen exceptions (already screen-space - do NOT touch)

- `NavigateAction::MoveOverEdge` (~1141-1146, ~3511-3545): runs BEFORE
  `m_Canvas.Begin()`, so its `io.MousePos`/cursor are already true screen.
- Canvas outer border draw (~1547-1552): after `Canvas.End()`, screen space.

## Removable once native (Phase 4)

- Vertex/clip post-multiply (imgui_canvas.cpp 513-555) + `fringe *= InvScale` (493).
- Editor's own fringe machinery `FringeScaleScope(1.0f)` guards (internal.h 115-130,
  live uses at draw ~529, ~627) - become no-ops.
- `ImDrawCallback_ImCanvas` sentinel emit/strip (imgui_canvas.cpp 31-33, 446, 558-564).
- `m_OldCanvas` dead `#if 0` block (imgui_node_editor.cpp ~1477-1498).
- Input backup/restore + viewport faking in the canvas.
- Keep `Suspend/Resume` as balanced no-ops (vendored code + HintBuilder call them).

## API surface consumers bind to (must preserve)

`EditorContext` ctor `(const Config*)` (only nullptr passed) + these methods:
`GetStyle`, `PushStyleVar`(vec2/float)/`PopStyleVar`, `Begin/End`,
`BeginNode/EndNode`, `BeginPin/EndPin`, `PinRect`, `Get/SetNodePosition`,
`GetNodeSize`, `IsNodeSelected`, `Link`, `BeginCreate/QueryNewLink/AcceptNewItem/
RejectNewItem/EndCreate`, `BeginDelete/QueryDeletedNode/QueryDeletedLink/
AcceptDeletedItem/EndDelete`. Types: `NodeId/LinkId/PinId`, `PinKind`. StyleVars
used (5): `SourceDirection/TargetDirection/PivotAlignment/PinArrowSize/PinArrowWidth`.
Style fields used: `PinArrowSize/PinArrowWidth/LinkStrength/NodePadding/PinRounding/
PinBorderWidth/PinRadius/SourceDirection/TargetDirection/PivotAlignment/PivotSize/
PivotScale`. No consumer touches `ImGuiEx::Canvas`, `Suspend/Resume`, or any
zoom/view API - **the zoom/view control path is greenfield**.

## Consumers

- `src/editor/graph/graph_window.{cpp,hpp}` + `graph/shader_graph_node.*` (shader graph, base)
- `src/editor/geometry_graph/geometry_graph_window.*` + `geometry_graph_node.*` (126-check sweep)
- `src/editor/texture_graph/texture_graph_window.*` + `texture_graph_node.*` (266-check sweep)
- `src/editor/developer/rendergraph_window.*` (no sweep; visual sanity only)

Per-node widgets currently use `imgui_enum_stepper` instead of combos precisely
because popups break in the fake space (Phase 5 pilot proves this is fixed).

## As-built status

### Phase 0 (done, commit "editor: #251 Phase 0 ...")

Inventory above; `EditorContext::SetZoom` + MCP `geometry_graph_set_view` +
`scripts/geometry_graph_zoom_harness.py`. Baseline captured locally.

Harness gotcha: `capture_screenshot` grabs the WHOLE frame, and the default
startup scene has a physics-animated 3D viewport that settles to
wall-clock-dependent positions across launches - so full-frame pixel diffs are
NOT a valid identity oracle. Verify by cropping to the Geometry Graph window
region (left of the viewport, e.g. box (60,60,650,175) at 1920x1080) OR by an
apples-to-apples same-ini-state control-vs-test build comparison. The
node-editor crop is deterministic to 0 px across launches once the ini layout
is fixed. Restore `config/editor/*.{ini,json}` before each capture so both runs
start from the same window layout (an editor run rewrites them on exit).

### Phase 1 (done, commit "editor: #251 Phase 1 ...")

Added the strangler helpers `DrawPos/DrawVec/DrawLen/DrawRect` (identity now)
and `HitMouse()` on `Detail::EditorContext` (internal.h, next to ToCanvas/
ToScreen). Routed every editor-OWN primitive draw site through them:
`Pin::Draw`, `Node::Draw` + `DrawBorder`, `Link::Draw` (builds a screen-space
`screenCurve` and scales thickness/arrow sizes; the bezier helper is affine so
it is correct in either space), the grid/background in `End()`, the flow
marker, and the selection rect. The canvas outer border (drawn after
`Canvas.End()`, already screen space) is intentionally left raw. Verified
pixel-identical (0 px diff in the node-editor crop, apples-to-apples control vs
test) and geometry smoke sweep 129/129 (with `editor.graph_editor=trace`).

### Phase 2+3 (done, commit "editor: #251 Phase 2+3 ...")

Flipped to native-resolution rendering in one commit:
- Helper bodies flipped to the real mapping (DrawPos=ToScreen, DrawVec=FromLocalV,
  DrawLen=len*ViewScale, DrawRect={ToScreen,ToScreen}, HitMouse=ToCanvas(mouse),
  + ToCanvasVec for screen->canvas vectors).
- Canvas EnterLocalSpace/LeaveLocalSpace gutted: no vertex/clip post-transform,
  no io.MousePos/viewport/window faking, no _FringeScale hack; Enter just clips
  to the widget rect (screen) + publishes m_ViewRect; Begin's dummy widget is
  screen-space. End()'s ToCanvas clip pre-transform removed.
- Mouse/hit-test sites routed: BuildControl mousePos=HitMouse + screen-space
  interactive-area buttons (DrawPos/DrawVec) + IsMouseHoveringRect(Rect()),
  SetUserContext real mouse, HandleZoom/FindNodeAt/GetRegion/selection-rect/
  cursorPin via HitMouse, SelectAction start via ToCanvas. Pan delta drops
  *m_Zoom (delta is real screen px now). DRAG deltas (DragAction move,
  SizeAction resize) map via ToCanvasVec - THESE ARE EASY TO MISS: any
  GetMouseDragDelta applied to canvas-space bounds needs ToCanvasVec.
- Node content (NodeBuilder::Begin/End): cursor to ToScreen(node_pos), PushFont
  (nullptr, FontSizeBase*zoom) + 6 zoom-scaled style vars (FramePadding/
  ItemSpacing/ItemInnerSpacing/IndentSpacing/FrameRounding/GrabMinSize),
  NodePadding*zoom; EndNode measures the screen group and stores ceil(size/zoom)
  canvas units (ceil for cross-zoom stability). EndPin/Group map the measured
  screen rect back to canvas via ToCanvas.
- Harness: Geometry_graph_window gained a one-shot focus request so the docked
  window comes to the front of its tab for the capture.

VERIFIED: text crisp at 2x (baseline was blurry; READ logs/crop_*_2.png),
geometry sweep 129/129 + texture sweep 266/266 (both green), no crashes. Live
interactive drag/box-select/zoom-under-cursor NOT yet user-verified (Phase 6).

### Phase 4 (done, commit "editor: #251 Phase 4 ...")

Removed the now-inert machinery: canvas _FringeScale/_VtxCurrentOffset reflection
helpers + ImDrawCallback_ImCanvas sentinel + input/viewport Save/Restore (+ their
members), FringeScaleScope guards, and the m_OldCanvas #if 0 block. Suspend/Resume
kept as balanced clip push/pop. Sweeps green, text still crisp.

### Phase 5 (done, commit "editor: #251 Phase 5 ...")

Combo/popup pilot - proves the fake space is really gone:
- Conway op stepper -> real ImGui::BeginCombo (conway_node.cpp). Renders + hit-tests
  correctly inside a node now.
- Canvas background "Add node" context menu (geometry_graph_window.cpp) via the ax
  ShowBackgroundContextMenu gesture bracketed by Suspend()/Resume() (which swap the
  node channel splitter out for the popup) - non-conflicting with right-drag nav.
- IMPORTANT BUG the pilot exposed: a combo preview must bounds-check its index.
  `operation_names[m_operation]` overreads (0xc0000005) when the graph-file /
  MCP set_parameter abuse path drives the enum out of range - the stepper clamped,
  a raw combo does not. This was NOT a channel-splitter problem (in-node combos
  render fine); it was a plain OOB read, found via the VS debugger.
- Verified headlessly: combo renders ("Dual" + dropdown), geometry sweep 129/129
  (incl. the out-of-range param_abuse section that crashed the unchecked combo),
  no crash, normal rendering intact. The interactive OPEN of the combo/context
  menu (needs a mouse click) is deferred to Phase 6 live user verification.
- Broad stepper->combo conversion deferred to Phase C (noted in editor_improvements.md).

### Phase 6 (rendering bugs found + fixed; live interaction still open)

Initial user testing (report a-g in the old prompt_queue handoff) showed the
Phase 2+3 flip changed the coordinate model but the CONSUMER node-content layout
code was never updated for it. Root causes found and fixed:

- **Node width did not scale, per-widget widths did not scale, text clipped
  (bugs d/e).** The three graph node consumers (geometry / texture / shader)
  laid node content out with fixed pixel constants (table columns 70/150,
  `SetNextItemWidth(140)`, pin `half_extent` 10, texture preview 96) authored
  for zoom == 1. After the flip node content is authored in SCREEN space at the
  zoomed size, so every canvas-unit pixel metric must be multiplied by the view
  scale. Fix: a `content_scale()` accessor on each node base, set from
  `EditorContext::GetCurrentZoom()`, applied to all those constants. Commits
  ab69475e (geometry), 46cd4341 (texture + shader).
- **Sockets in sync in Y, out of sync in X; un-hittable (bugs f/g).** `show_pins`
  built the pin rect from `edge_x = GetNodePosition().x` (CANVAS) but
  `cell_center_y` from `GetItemRect` (SCREEN), then drew it directly and passed
  the same mixed rect to `PinRect`. Fix: compute the edge in screen space
  (`CanvasToScreen(node border).x`), build the visible square in screen space,
  and pass `ScreenToCanvas(rect)` to `PinRect` (which stores canvas units the
  editor draws/hit-tests in canvas space). Same three commits.
- **Node background / border missing at certain zoom levels and while dragging
  (bugs a/b).** `Detail::Object::IsVisible()` - which gates whether a node's
  background+border is drawn - passed CANVAS bounds to `ImGui::IsRectVisible()`,
  a SCREEN-space clip test. Pre-flip the window was faked into canvas space so it
  matched; post-flip a node whose canvas coords fell outside the widget's screen
  clip-rect numeric range was culled even when visible on screen, dropping its
  background/border while the (separately submitted) content stayed. Fix: map
  bounds through `Editor->ToScreen` before the test; moved out of line because
  `Object` precedes `EditorContext`. Commit 8d32ff6e. Reproduced headless with a
  before/after by positioning the graph window far from the screen origin so the
  divergence triggers (`logs/zoom_buggy_1.png` vs `logs/zoom_farwin_1.png`).
- **`EditorContext::GetCurrentZoom()` returned `InvScale`** (a latent reciprocal
  bug, its only reference commented out). Fixed to return the view `Scale` -
  node content needs the actual multiplier. Commit 53ae6eb5.
- **erhe logging + zoom display (bugs c/a).** Added an `erhe.imgui.node_editor`
  log category; zoom changes log through it (programmatic SetZoom at info,
  mouse-wheel at trace). Each graph window shows the current canvas zoom in the
  corner. Commit 53ae6eb5 + the per-window commits.

Verified headless at zoom 0.5/1/2/4: node frames, widgets and pins scale
together, text stays crisp, backgrounds render; geometry sweep 129/129, texture
sweep 266/266. The pin coordinate fix was independently reviewed as correct.

STILL OPEN (needs a display + mouse - headless cannot drive it): open the combo
and context menu by clicking; drag nodes; box-select; create/delete a link by
dragging (the PinRect fix should make sockets hittable now); mouse-wheel
zoom-under-cursor keeps the point under the cursor fixed; no node-size jitter
while zooming. Optional extras: open+screenshot the shader graph + rendergraph
windows; font atlas growth check; live texture/shader zoom (no set-view MCP tool
for those windows yet). Restore config/editor/*.{ini,json} after runs.

### Phase 2 remaining work (mouse/hit-test routing) - DONE, kept for reference

Phase 1 routed DRAWING only (screenshot-verifiable). The FLIP (Phase 2) must,
in one commit:
1. Change ONLY the helper bodies: `DrawPos(p) -> ToScreen(p)`,
   `DrawVec(v) -> m_Canvas.FromLocalV(v)`, `DrawLen(l) -> l * m_Canvas.ViewScale()`,
   `DrawRect(r) -> ImRect(ToScreen(r.Min), ToScreen(r.Max))`,
   `HitMouse() -> ToCanvas(ImGui::GetMousePos())`.
2. Disable the canvas vertex/clip post-transform + fringe hack in
   `imgui_canvas.cpp` LeaveLocalSpace/EnterLocalSpace (but KEEP the input/
   viewport handling until Phase 3 flips node content, or flip both together).
3. Route the LOCAL-space mouse/geometry sites through `HitMouse()` (they read
   the currently-faked io.MousePos): the sites catalogued as category 2 in the
   inventory EXCEPT the true-screen `MoveOverEdge` ones (imgui_node_editor.cpp
   ~1141-1146, ~3519-3536) and the pre-Begin `MoveOverEdge` cursor. Known sites:
   `SetUserContext` mousePos (~2258, ~2263 SetCursorScreenPos), `BuildControl`
   (~2299), zoom-to-F `FindNodeAt(io.MousePos)` (~3314), `HandleZoom` mousePos
   (~3414), `GetRegion(GetMousePos())` (~3815, ~3905), selection-rect endpoint
   (~4108), and the link-create `cursorPin.m_Pivot = ImRect(GetMousePos(), ...)`
   (~4615). Each is authored in canvas space today via the fake; after the flip
   they must use `HitMouse()` = `ToCanvas(real mouse)`.
Because the canvas still fakes input while Phase 2 primitives go native, node
CONTENT (ImGui widgets) will render at base size at the transformed origin
(wrong scale at zoom != 1) until Phase 3. Prefer committing Phase 2+3 together
(the plan sanctions this) unless the intermediate is verifiable.
