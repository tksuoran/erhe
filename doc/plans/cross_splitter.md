# Cross splitter for four docked windows

Status: proposed

Four windows docked as a 2 x 2 grid share one cross-bar splitter: a vertical
and a horizontal bar that meet in one point. The user drags the crossing to
resize all four windows at once, or drags a bar segment to move that bar
alone. The feature is generic Dear ImGui docking code, written in the
`<imgui>` fork clone (branch `erhe`); erhe only opts a dock node into it.
Extends `doc/erhe/imgui.md`.

## Requirements

- R1. A cross is one dock split node `P` (the cross node) that carries the
  flag `ImGuiDockNodeFlags_CrossSplit` and whose two children are both
  visible split nodes on the axis perpendicular to `P->SplitAxis`. `P`'s own
  splitter is the outer bar; the children's splitters are the two inner
  segments. While this shape holds the cross is *engaged*; in every other
  shape `P` and its children behave as plain dock nodes and the flag stays
  set, so the cross re-engages when the shape returns (a closed window is
  reopened).
- R2. While engaged, the two inner segments are collinear: the children of
  `P->ChildNodes[1]` take their size on the inner axis from the children of
  `P->ChildNodes[0]` (the master) in every layout pass, including host
  resize.
- R3. Dragging either inner segment moves both. The resize limits are the
  intersection of the limits of both segments.
- R4. Dragging the outer bar moves the outer bar only; this is the existing
  splitter behavior.
- R5. A center handle covers the crossing: the square where the bars meet,
  grown by `g.WindowsBorderHoverPadding` on every side. Dragging it applies
  the mouse delta to the outer bar (outer axis) and to the inner segments
  (inner axis) in the same frame, each clamped by its own limits. Mouse
  cursor is `ImGuiMouseCursor_ResizeAll`.
- R6. Highlight: hovering or dragging an inner segment highlights both inner
  segments; hovering or dragging the center handle highlights all three
  bars. Colors and the hover delay are those of `SplitterBehavior`
  (`ImGuiCol_SeparatorHovered`, `ImGuiCol_SeparatorActive`,
  `WINDOWS_RESIZE_FROM_EDGES_FEEDBACK_TIMER`).
- R7. The flag is saved in the docking ini line of the node as ` Cross=1`
  and restored on load.
- R8. `ImGuiDockNodeFlags_NoResize` / `NoResizeX` / `NoResizeY` on the
  involved nodes disable the matching drag exactly as they do for plain
  splitters; the center handle drags only the axes left resizable and is not
  submitted when neither is.
- R9. Steady-state frames allocate nothing beyond what
  `DockNodeTreeUpdateSplitter` allocates today (the touching-node vectors,
  only while a splitter is active).

## Design

- D1. Flag. `ImGuiDockNodeFlags_CrossSplit = 1 << 23` in the private flag
  extension block of `imgui_internal.h`, added to
  `ImGuiDockNodeFlags_SavedFlagsMask_`. It is a local flag of the split node
  and is not part of `LocalFlagsTransferMask_` (it describes tree shape, not
  window content). `ImGuiDockNode::IsCrossSplitEngaged()` evaluates R1.
- D2. Layout (R2). For an engaged `P`, `DockNodeTreeUpdatePosSize` recurses
  into the master child first, then writes the master grandchildren's
  resulting `Size[inner]` into the slave grandchildren's `Size[inner]` and
  `SizeRef[inner]` and sets `WantLockSizeOnce` on slave grandchild 0 before
  recursing into the slave. The slave split thereby takes the locked-size
  path (step 2) and never the central-node remainder policy (step 3), so a
  central node in any of the four leaves leaves the segments collinear.
- D3. Drag (R3 to R5). `DockNodeTreeUpdateSplitter` factors its per-node body
  into `DockNodeSplitterApply(node, new_size_0, new_size_1)` (the block that
  writes sizes, locks touching siblings, re-runs `DockNodeTreeUpdatePosSize`
  and marks the ini dirty) and `DockNodeSplitterGetLimits(node, out_limits)`.
  For an engaged `P`:
  - each inner segment's `SplitterBehavior` result is applied to both inner
    split nodes through `DockNodeSplitterApply`, with limits intersected;
  - the center handle is a `ButtonBehavior` on the R5 rectangle, id
    `"##CrossCenter"` under `PushID(P->ID)`, submitted after the three
    splitters of the cross. `SplitterBehavior` submits its item with
    `ImGuiButtonFlags_AllowOverlap`, so the later center item takes the hover
    inside its rectangle. While active it computes the target bar positions
    from `g.IO.MousePos - g.ActiveIdClickOffset` and calls
    `DockNodeSplitterApply` for `P` and for both inner nodes.
  The inner nodes are processed from `P`'s visit, and the recursion skips
  their own splitter step (their grandchildren still recurse).
- D4. Rendering (R6). `SplitterBehavior` draws only its own rectangle, so the
  cross draws the linked highlight itself: after the behaviors run, when the
  center handle or an inner segment is hovered past the delay or active,
  `P` fills the partner rectangles with the matching separator color on the
  host window draw list.
- D5. Settings (R7). `DockSettingsHandler_ReadLine` parses ` Cross=%d`;
  `DockSettingsHandler_WriteAll` appends ` Cross=1`; the flag travels in
  `ImGuiDockNodeSettings::Flags`.
- D6. Builder API. `ImGuiID DockBuilderSplitNodeCross(ImGuiID node_id,
  ImGuiAxis outer_axis, float ratio_outer, float ratio_inner, ImGuiID
  out_ids[4])` splits `node_id` into the 2 x 2 shape, sets the flag on it and
  returns the four leaf ids in row-major order (top-left, top-right,
  bottom-left, bottom-right). `DockBuilderSetNodeCrossSplit(ImGuiID node_id,
  bool enabled)` sets or clears the flag on an existing split node.
- D7. User control. The dock node window menu (`DockNodeWindowMenuUpdate`)
  gains a checkable "Cross splitter" entry, shown when the node's grandparent
  satisfies the R1 shape; it toggles the flag on that grandparent.
- D8. Metrics. The Docking section of `ShowMetricsWindow` prints
  `CrossSplit` / `engaged` for the node.
- D9. erhe opt-in. `Dock_direction` gains no value; `Dock_placement` gains
  `cross_with` (String, `added_in=2`, default empty): the titles named by a
  chain of `cross_with` entries are placed by one
  `DockBuilderSplitNodeCross` call on the target node
  (`src/editor/editor_default_layout.cpp`). The first user is a four-viewport
  layout (four `Viewport_scene_view` windows: perspective plus three
  orthogonal cameras); in any other layout the user enables the cross from
  the D7 menu and the ini keeps it.

## Working in the fork

- The work is done in the `<imgui>` clone (location in
  `memory-bank/local/context.md`), branch `erhe`, as commits separate from
  erhe's. erhe consumes ImGui as the in-tree copy `src/imgui/imgui/`, which
  is file-identical to the fork's `erhe` branch; after the fork commit, copy
  the changed files (`imgui.cpp`, `imgui_internal.h`, `imgui_demo.cpp`) into
  `src/imgui/imgui/` and commit that in erhe naming the fork commit. The user
  pushes the fork.
- Code in the fork follows Dear ImGui's style (its naming, `struct`, 4-space
  indent), not erhe's.
- A demo entry in `imgui_demo.cpp` ("Examples > Dockspace" gains a "2 x 2
  cross" layout button) makes the feature testable in the fork's own example
  applications without erhe.

## Phases

1. D1, D5, D6, D8: flag, persistence, builder, metrics. Verify: demo builds
   the 2 x 2 layout, metrics shows `engaged`, the ini round-trips
   ` Cross=1`.
2. D2: collinear layout. Verify: resize the host window and drag each plain
   inner splitter; the two inner segments stay collinear, also when one of
   the four leaves holds the central node.
3. D3, D4: linked drag, center handle, highlight. Verify: the three drags of
   R3 to R5 at the limits (`style.WindowMinSize`), with a leaf that is itself
   split, and with one window closed and reopened (R1 disengage / re-engage).
4. D7, demo entry, copy into erhe, D9. Verify in the editor: headless
   `capture_screenshot` of the engaged layout for the static part; the drags
   are user-interactive.
