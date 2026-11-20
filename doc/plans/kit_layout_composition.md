# Kit layout composition: plan

Status: proposed

A plan for GitHub issue 301, "Complex layout kit nodes": kit layout nodes
that build more than one run of repeated items, "like alternating (wall,
post) parts". The issue lists four ideas: nesting a kit layout under a kit
layout, a shape grammar, a shape graph node like the geometry graph, and
nested layout nodes like WPF. This document compares them, proposes one
model that the four share, states its requirements (N-numbers), sketches
the design and gives a build order. Nothing of it is built. It extends
`doc/editor/level_tools.md` "Kit layout nodes", "Pattern expansion" and
"Connectors", and `doc/plans/level_tools.md` R5.12.

The choices marked "proposed" are the planner's and wait for the user's
ruling ("Open questions"); the build starts after those rulings.

## Where kit layouts stand

- A kit layout node holds one pattern (`Kit_layout_pattern`: line, grid,
  ring, path, scatter, segments, stairs) and a flat list of items
  (`Kit_layout_item`: a shape id, parameter values, slot materials, a
  scatter weight). Element i takes item i modulo n, so a list (wall, post)
  already alternates, but both stand on the same kind of place: a segments
  layout puts the post on a segment's middle, a path layout spaces walls and
  posts by one common step.
- Every generated child is a kit piece without children. A locked layout
  refuses other children, and a piece under an unlocked layout cannot be
  wrapped into a layout of its own.
- A segments layout gives its piece the segment's length through the
  standard property `child.length`; no other kind hands a size to an item.
- A layout holds at most `c_max_repeat_count` (1024) elements.

## Use cases

1. Fence, railing, wall with posts: a post on every control point of a
   curve and a panel between each two posts, the panel's length the
   distance between the posts' faces.
2. Long spans: a segment longer than the panel's largest length holds
   several panels with posts between them, evenly divided.
3. Ends and corners: an end post differs from a middle post, a corner post
   from a straight joint; a gate replaces one panel.
4. Facade: a wall run divided into bays, each bay a window wall, with a
   door bay at a stated place and corner pillars; storeys stacked.
5. Colonnade: columns along a path and one entablature piece spanning each
   pair of neighbours.
6. Groups repeated: a market stall with its crates along a street; a ring
   of lamp posts each with a bench; a grid of rooms.

## The four ideas compared

| Idea | Covers | Cost | Proposed |
|---|---|---|---|
| Nesting (a layout as an item of a layout) | 6, and 1 - 5 once a nested layout receives a size | medium: generation recurses, items become a tree | yes, the data model |
| WPF-like layouts (measure and arrange) | 2, 4: a parent hands each child a length, a child states the lengths it accepts | medium: one size pass before the pose pass | yes, as the size rule of nesting |
| Shape grammar (split and repeat rules as text) | 1 - 5 | high: a language, its errors and its editor | no; a nested item tree with size rules states the same splits as values the UI can edit |
| Shape graph (a node editor like the geometry graph) | all, as a view | high: a graph asset, evaluation, a binding | later; the item tree is what such a graph would evaluate to |

Roles (which item stands on a point, a span, an end, a corner) are the
small addition that covers use cases 1, 3 and 5 on their own, so they come
first.

## Proposed model

One kit layout node holds a tree of items. A leaf is a shape item, as
today. An inner item is a layout item: a pattern plus its own item list.
Every item carries a role in its parent's run, and a run hands each item
the space of its place, which the item maps to its parameters (a shape) or
takes as its own space (a layout).

### Requirements

Roles:

- N1 [must] An item of a path or segments layout has a role: `span` (the
  place between two neighbouring stations), `joint` (a station), and the
  refinements `start`, `end` and `corner` of `joint`. A station is a
  control point of a segments layout and an element position of a path
  layout. An item list written before roles reads with every item `span`
  for segments and `joint` for path, which is today's behavior.
- N2 [must] Items of one role alternate among themselves by index modulo
  their count, as today's list does.
- N3 [must] A span item receives the free length between its two joints:
  the station distance minus the half extents along the run of the joint
  items at its ends. A span without the `child.length` mapping stands at
  its own size at the span's middle, as today.
- N4 [must] A joint item stands at its station turned to the bisector of
  the two spans that meet there; a `start` or `end` item takes the
  direction of its one span. `corner` applies where the two spans turn by
  more than a stated angle (proposed default 15 degrees, the angle the
  segment handles already use for "parallel"); a station without an item of
  its refined role takes the `joint` items.
- N5 [should] An item states a fixed place: "the n-th span" or "the n-th
  joint" (negative n counts from the end), which takes that place instead
  of the alternation (the gate, the door bay).

Subdivision:

- N6 [must] A span item states a largest length (and optionally a smallest).
  A span longer than the largest is divided into the smallest number of
  equal parts that fit, with a joint item at each division. The shape's own
  parameter range for the mapped length is the default limit.
- N7 [should] A line and a ring layout take roles as well, their stations
  the element positions.

Nesting:

- N8 [must] An item is a shape item or a layout item. A layout item holds a
  pattern of the kinds line, grid, ring and scatter, and its own items. A
  path, segments or stairs pattern needs a curve of its own and stays a
  layout node (proposed; Q3).
- N9 [must] A layout item in a span role receives the span's length as the
  `length` of its line pattern, so a nested line of bays fills a wall run
  (use case 4). In a joint role it stands at the station at its own size.
- N10 [must] Nesting depth is at most 4 and the element count of the whole
  tree at most 1024; a tree above either is refused naming the value.
- N11 [must] Seeds: an element's seed is derived from the layout seed and
  the element's index path (the index at each level), stable when an
  unrelated branch changes its count.

Shared behavior:

- N12 [must] Everything a layout does today holds for a tree: locked
  generation keeps the children that already match, a value write and its
  regeneration are one undo entry, live preview during the five drags,
  surface snap and orient per element, selection of a child redirects to
  the node, duplicate, copy, paste, mirror, Replace, persistence.
- N13 [must] Connectors: a joint and the spans at it carry facing
  connectors where their shapes have them, and the layout connector
  switches treat them as neighbours ("Layout connector switches"). Value
  flow over connections (`doc/plans/level_kit_connections.md` C3) does not
  write a locked child, as today.
- N14 [must] The item tree is edited in the Properties window's "Kit
  Layout" group and the Level Kit window's Layout section (rows per item:
  role, fixed place, limits; add, remove, reorder, indent under a layout
  item) and over MCP (`level_set_layout`, `level_create_layout` take the
  tree; the reply's item list reports it).
- N15 [should] Wrap with Path on a selection of a post and a panel makes a
  path layout with the post as `joint` and the panel as `span`, chosen by
  which shape maps `child.length`.
- N16 [must] Steady state: a tree that does not change costs nothing per
  frame; generation and preview reuse scratch buffers
  (`AGENTS.md` "Run-time Memory Allocation Discipline").

### Design sketch

- Values. `Kit_layout_item` gains `role` (`Kit_layout_item_role`), `place`
  (an optional signed index), `max_length` and `min_length`, and for a
  layout item a `Kit_layout_pattern` and a nested item list. The canonical
  text writes each new key only when it is off its default, the rule the
  scatter weight and the stairs values follow, so every stored text stays
  the canonical text of its values. `kit_layout_values.hpp` stays
  scene-free and unit tested.
- Resolution in two passes over the tree, both scene-free:
  1. Stations and spaces: the pattern gives stations (`expand_repeat()` and
     the curve table); roles assign items to stations and spans; span
     lengths come from station distances and the joints' extents; N6
     divides. A layout item's space becomes its pattern's `length`.
  2. Poses: each leaf gets a pose in the node's frame and the values the
     space maps to (`child.length`). The result is the flat list today's
     generation consumes, each entry with its index path.
- Generation. `Level_kits::build_layout_generation()` consumes the flat
  list as it does now: one `Kit_placement` per leaf. Proposed (Q2): the
  children stay a flat list of pieces under the one layout node, in tree
  order, so child matching, the selection redirect, connector indicators
  and persistence keep their shape; the index path is not stored, it is the
  position in the list.
- A layout item's extent along its parent's run (for N3 and gap mode) is
  the extent of its resolved children, computed bottom-up in pass 1 from
  `Kit_layout_item_sizes`.
- Limits and refusals are the existing ones plus N10, a role a kind does
  not take, a fixed place outside the run, and a span whose free length is
  not positive (joints larger than their spacing).
- Persistence: the items text of the node (`Kit_layout.items`); no new
  property, no new node type, no glTF extension change.

## Build order

One step is one commit; each runs `editor_level_tests`,
`scripts/level_tools_verify.py` (its new group, the full run at the last
step), `scripts/curve_verify.py` and `ctest -C Debug -L editor -j1` in the
headless tree.

1. Roles in the values and the resolution (N1 - N4): `Kit_layout_item`
   role, canonical text, pass 1 for path and segments, span lengths less
   joint extents. Unit tests on hand-built curves; a verify group
   `--only-layout-roles` building a fence (post on every point, panel
   between) and checking poses, lengths, undo and a corner point drag.
2. Subdivision and fixed places (N5, N6): long spans divide; a gate in the
   n-th span. Extends the group.
3. Editing (N14, N15): the item rows, the MCP arguments and replies, Wrap
   with Path roles.
4. Layout items (N8 - N11): the item tree, pass 1 recursion, the limits,
   index path seeds. A verify group `--only-layout-nesting`: a facade (a
   segments layout whose span item is a line of window bays), a ring of
   grids.
5. Roles on line and ring (N7), connectors of joints and spans (N13).
6. Final pass: full verify scripts, one review of the series, the
   documents (`doc/editor/level_tools.md` "Kit layout nodes", "Pattern
   expansion", "Kit layout rows", "MCP"), a windowed check by the user of
   use cases 1, 3 and 4.

Later, not in this plan: a graph editor view of the item tree (the fourth
idea of the issue); stacked storeys (waits for stacked walls,
`doc/plans/level_kit_connections.md` "Later"); a span item that bends
along a curved span instead of standing on its chord.

## Open questions

- Q1: Is the proposed model (roles, then an item tree with sizes handed
  down) the direction, with the grammar not built and the graph editor
  later? The alternative is to start from the graph.
- Q2: Generated children: one flat list of pieces under the layout node
  (proposed), or an Xform node per layout item so the scene hierarchy shows
  the tree?
- Q3: Curve kinds inside a tree: only at the root (proposed), or may a
  layout item carry its own curve (a nested Curve node the author edits)?
- Q4: Should the kit layout node of a child scene node also be allowed
  (the issue's "put shape kit as child of shape kit": an unlocked or
  locked layout node as a hand-placed child of another layout's element)?
  The proposal keeps a locked layout's children generated only.
- Q5: The joint's turn at a corner: the bisector (proposed), or the
  incoming span's direction?
- Q6: When a span's free length is below the panel's smallest length:
  refuse the edit (proposed), or drop that span's panel and report it?
- Q7: The names in the UI: "Joint" and "Span", or "Post" and "Panel"?
