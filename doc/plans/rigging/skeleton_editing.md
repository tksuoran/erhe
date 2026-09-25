# Skeleton Editing and Posing Basics - Phase 3 Requirements

Status: in progress

This document specifies Phase 3 of the rigging roadmap in `rigging_tools.md`:
authoring skeletons in the editor rather than only importing them, plus the
non-IK posing verbs. Phases 1-2 (`fabrik_ik.md`, `ik_settings.md`,
`pole_target.md`, `ik_drag_options.md`) pose imported skeletons; this phase
makes a skeleton something the editor can create, change and pose by verb.

## 0. Where erhe stands

- A bone is an ordinary node with `Item_flags::bone`, and that flag is
  derived: `erhe::scene::mark_skin_joints` sets it on every joint a `Skin`
  lists (`src/erhe/scene/erhe_scene/skin.{hpp,cpp}`). A node no skin lists is
  not a bone, so a skeleton without a skinned mesh cannot exist today.
- There is no stored rest pose. The bind pose is derived from a skin's
  inverse bind matrices (`get_bind_pose_local_rotation`, `Scene_commands::
  reset_bones_to_bind_pose`), and `Ik.rest_rotation` defaults to it.
- A bone has a head (its node's origin) and no tail. `bone_tail_in_joint_space`
  (`src/editor/tools/bone_visualization.cpp`) infers one for display: the
  local translation the child joints agree on, otherwise a length from the
  skinned vertices' bounds along a hierarchy-chosen direction.
- Undoable structure edits use `Item_insert_remove_operation`,
  `Node_transform_operation` and `Compound_operation`; `Scene_commands::
  add_bone_tip_nodes` and its Hierarchy context-menu entry are the pattern.
- The MCP `create_skin` tool builds a `Skin` with inverse binds taken from the
  joints' current world transforms and rigid weights.

## 1. Foundations (decisions)

Three decisions come before the editing verbs, because every verb reads them.
Each is stated as the requirement; the alternative considered is recorded
with the reason it lost. All three were confirmed by the user on 2026-09-25
(persistent bone flag, `Rig.rest_transform`, stored `Rig.tail` vector).

### 1.1 Bone identity

**R1.** Being a bone is an authored, persistent property of a node:
`Item_flags::bone` is saved and loaded with the scene (glTF: the node's
`ERHE_rig` extension, beside the IK values it already carries). Skin import
still marks its joints (`mark_skin_joints`), so imported rigs are unchanged;
a node stays a bone when no skin lists it, so a skeleton exists before and
without any mesh. Alternative: keep bone-ness derived and give an authored
skeleton a mesh-less `Skin` - rejected, because a skin's inverse binds would
have to be invented and kept current through every structural edit before any
mesh is bound.

### 1.2 Rest pose

**R2.** Every bone has a rest transform: a local TRS held as the attached
property `Rig.rest_transform`. Its default is computed, not stored: for a
skinned joint the bind pose from the skin's inverse bind matrices (the value
`get_bind_pose_local_rotation` and Reset Bones to Bind Pose already derive,
extended to translation and scale); for any other bone the local TRS it
was created with, which the creating operation (section 2) records as a
local value. `Ik.rest_rotation`'s default becomes the rotation of
`Rig.rest_transform`, so the limits frame and the rest pose are one thing.
"Apply pose as rest" (write the current local TRS as the rest) is in scope
only for unbound bones; for a bound bone it is Phase 6 (it would invalidate
the inverse binds). Alternative: rest = bind pose only - rejected, because
unbound authored skeletons have no bind pose.

### 1.3 Bone tail

**R3.** A bone's tail is its head plus the local vector `Rig.tail` (an
attached vec3 in the bone's local frame). Its default is computed: the
existing `bone_tail_in_joint_space` inference (single child's head, else the
bounds-derived length along the hierarchy direction); a bone created in
the editor records its tail as a local value (R4). Setting it stores a
local value. The bone visualization draws from `Rig.tail` instead of calling
the inference itself. Roll is the bone's rotation about its head-to-tail
axis; no separate roll value exists.

**R4.** A **connected** child (`Rig.connected`, bool attached property,
default false) keeps its head on its parent's tail: setting it snaps the
child's local translation to the parent's `Rig.tail`, and editing the
parent's tail moves connected children with it. A new bone created without a
parent tail to derive from gets tail `(0, length, 0)` with the length of the
creation gesture (section 2), so +Y is the bone axis, matching glTF exporters
and `derive_twist_axis`.

## 2. Bone creation and structure (slice C)

Depends on section 1. Every verb is one undoable `Compound_operation`, an
entry in the Hierarchy context menu for a bone (the `add_bone_tip_nodes`
pattern) and an MCP tool with explicit arguments.

- **R5. Create bone**: a new bone node, child of the active node (or at the
  scene root), head at the 3D cursor or the parent's tail, tail length 1 in
  scene units along the parent's bone axis (world +Y at the root);
  `Rig.rest_transform` recorded as its creation transform.
- **R6. Extrude**: from each selected bone, a new child bone whose head is the
  parent's tail, connected, same direction and length; the new bones become
  the selection (so a repeated extrude grows a chain).
- **R7. Subdivide**: a bone becomes N connected bones (N >= 2) along its
  head-to-tail segment; children re-parent to the last; names `<name>.001`...
- **R8. Delete / dissolve**: delete removes bones and re-parents their
  children to the deleted bone's parent keeping world transforms; dissolve
  additionally extends the parent's tail to the removed bone's tail when the
  removed bone was its only connected child.
- **R9. Bound skeletons**: on a bone that a `Skin` lists, R6-R8 and any
  change of `Rig.tail` / `Rig.rest_transform` are refused with a logged
  message naming the skin (inverse binds and weights would go stale; Phase 6).
  Posing a bound skeleton is unaffected.

## 3. Selection helpers (slice A - independent of section 1)

- **R10.** Select Parent, Select Children (immediate / all), Select Chain
  (the linked bones from the active bone up to the first branching or root
  and down to the first branching or leaf), Select Mirror (R12's name
  counterpart). Hierarchy context menu and MCP; they change the selection
  only (no undo entry, as selection changes are not undoable today).
  A chain is a maximal run of bones in which every link's parent has exactly
  one bone child: the walk goes up while the bone parent has one bone child
  (a branching parent belongs to the chain above) and down while the bone has
  one bone child (a branching bone is the last bone of its chain). Every verb
  replaces the selection within the scene by its result; a verb whose result
  is empty leaves the selection unchanged.

## 4. Naming conventions and symmetry (slice A for naming, slice C for symmetrize)

- **R11.** Side suffixes recognized: `.L`/`.R`, `_L`/`_R`, `.l`/`.r`,
  `_l`/`_r`, `Left`/`Right` and `left`/`right` as a whole trailing word; the
  flip keeps the spelling family. The side is recognized at the end of the
  name after an optional trailing index group (`.` or `_` plus digits, as in
  `arm.L.001` or RiggedFigure's `arm_joint_L_1`), which the flip keeps. A
  separator suffix needs a character before it (`_L` alone has no side);
  `Left`/`Right` is a word after a non-letter or a lowercase letter
  (`HandLeft`), `left`/`right` after a character that is neither a letter nor
  a digit (`hand_left`). One free function pair (`bone_side(name)`,
  `flip_side_name(name)`) in the editor, unit tested.
- **R12.** Mirror counterpart of a bone = the bone in the same skeleton
  (same root) whose name is `flip_side_name(name)`; none when the name has
  no side or no such bone exists. Until a skeleton root concept exists, the
  root of a bone is its topmost bone ancestor-or-self (the first bone whose
  parent is not a bone), and the counterpart is the first bone of that name
  in pre-order from the root; sibling bones under a non-bone node are
  separate skeletons.
- **R13. Flip Names**: renames the selected bones to their flipped names
  (one undo step). **Symmetrize** (slice C, R9 applies): for each selected
  one-sided bone without a counterpart, creates the mirrored bone across the
  skeleton root's local X = 0 plane, parented to the counterpart of its parent
  (or the same parent when that has no side), copying `Ik.*` and `Rig.*`
  values mirrored.

## 5. Posing verbs (slice B - needs R2's default only)

- **R14. Clear Location / Rotation / Scale / All**: sets the chosen channels
  of the selected bones to their `Rig.rest_transform` values, skipping
  channels locked by `lock_translation_*` / `lock_rotation_*` /
  `lock_scale_*` (per axis, the Transform tool's rule) and items with
  `lock_edit`; one undo step. For imported skinned rigs this equals Reset
  Bones to Bind Pose restricted to the selection and channels.
- **R15. Copy Pose / Paste Pose / Paste Pose Flipped**: copy records the
  selected bones' local TRS keyed by bone name into an editor pose buffer
  (separate from the node clipboard); paste writes them to bones of the same
  names in the active skeleton (flipped: to the R12 counterparts, with the
  transform mirrored across the rest frame's X = 0 plane); channel locks and
  `lock_edit` respected; one undo step; unmatched names are reported, not an
  error.

## 6. Bone roll and orientation (slice C)

- **R16.** Recalculate Roll: rotates each selected bone about its
  head-to-tail axis so a chosen local axis (X or Z) points as close as
  possible to a reference (world axis, view direction, 3D cursor); children
  keep their world transforms. Align to Active: sets each selected bone's
  head-to-tail direction and roll to the active bone's. R9 applies to both
  (they change the rest frame of bound bones).

## 7. Display and skin stub (slice D)

- **R17.** Per-bone display color and shape (octahedral / stick / box) as
  `Rig.display_*` attached properties read by `Bone_visualization`.
- **R18. Bind (rigid)**: create a `Skin` for a selected mesh from a bone
  selection: inverse binds from the bones' rest transforms, weights rigid to
  the nearest bone segment (head-tail segment distance) - the editor-side
  counterpart of the MCP `create_skin`, for smoke-testing an authored
  skeleton before Phase 5's weighting.

## 8. Slice order

1. **A** - selection helpers (R10), side naming (R11, R12, Flip Names of
   R13): independent of section 1.
2. **B** - clear to rest and copy/paste pose (R14, R15), on R2's computed
   default; `Rig.rest_transform` is introduced here with its default only.
3. **Foundations** - R1 (persistent bone flag), R3/R4 (`Rig.tail`,
   `Rig.connected`), bone visualization reading `Rig.tail`.
4. **C** - creation and structure (R5-R9), Symmetrize (R13), roll (R16).
5. **D** - display and rigid bind (R17, R18).

## 9. Verification

Each slice extends `scripts/ik_interactive_pass_verify.py` or a sibling
script with checks driven as a user does (Hierarchy context menu, Properties
rows, key chords) and adds unit tests for the pure functions (naming,
mirroring math, tail inference) and Mcp_test cases for the MCP tools; every
structural verb is checked for exactly one undo step and a byte-identical
glTF round trip of the edited skeleton.

## Out of scope

- Editing a bound skeleton's structure or rest pose (Phase 6).
- Weight painting beyond rigid binding (Phase 5).
- B-bones, bone envelopes, bone groups / collections.

## Implementation status

Slice A is implemented; slices B, C, D and the foundations are not.

- R11 naming: `bone_side` / `flip_side_name` in `src/editor/rig/bone_naming.hpp`,
  unit tested by `editor_rig_tests` (`src/editor/rig/test/`).
- R10, R12 skeleton walks: `collect_bone_selection`, `collect_bone_chain`,
  `find_mirror_bone`, `get_skeleton_root` in `src/editor/rig/bone_hierarchy.hpp`,
  unit tested by `editor_rig_tests`.
- R10 verbs and R13 Flip Names: `select_bones` / `flip_bone_names` in
  `src/editor/rig/bone_commands.hpp`. Flip Names records one
  `Property_set_operation` per rename of `Item_base::name_property` in one
  `Compound_operation`; two sibling targets whose names flip into each other
  swap through a temporary name inside the operation, and a bone whose
  flipped name a non-target sibling holds is skipped with a logged warning.
- Entry points: the Hierarchy context menu of a bone (`Select Parent`,
  `Select Children`, `Select Children (All)`, `Select Chain`, `Select Mirror`,
  `Flip Names`; targets are the selected bones when the clicked bone is
  selected, otherwise the clicked bone), and the MCP tools `select_bones`
  (`bones`, `mode`) and `flip_bone_names` (`bones`) in
  `src/editor/mcp/mcp_server_rig.cpp`, covered by
  `Mcp_test.select_bones_modes_and_flip_bone_names_undo`.
- `scripts/skeleton_editing_verify.py` drives the context-menu entries on
  RiggedFigure through the Hierarchy filter and right-click, and checks the
  selections, the names and that Flip Names is one undo step Ctrl+Z reverts.
