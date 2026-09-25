# Skeleton Editing and Posing Basics - Phase 3 Requirements

Status: in progress

This document specifies Phase 3 of the rigging roadmap in `rigging_tools.md`:
authoring skeletons in the editor rather than only importing them, plus the
non-IK posing verbs. Phases 1-2 (`fabrik_ik.md`, `ik_settings.md`,
`pole_target.md`, `ik_drag_options.md`) pose imported skeletons; this phase
makes a skeleton something the editor can create, change and pose by verb.

## 0. Where erhe stands

- A bone is an ordinary node with `Item_flags::bone`, an authored,
  persistent flag (R1); `erhe::scene::mark_skin_joints` also sets it on every
  joint a `Skin` lists (`src/erhe/scene/erhe_scene/skin.{hpp,cpp}`), and a
  node keeps it when no skin lists it.
- A bone's rest pose is the attached `Rig.rest_*` values (R2), which default
  to the bind pose derived from a skin's inverse bind matrices
  (`erhe::scene::get_bind_pose_parent_from_node`, also what `Scene_commands::
  reset_bones_to_bind_pose` writes); `Ik.rest_rotation` defaults to
  `Rig.rest_rotation`.
- A bone has a head (its node's origin) and a tail, `Rig.tail` (R3), whose
  default for a skinned joint is `infer_skinned_bone_tail`
  (`src/editor/rig/bone_tail.cpp`): the local translation the child joints
  agree on, otherwise a length from the skinned vertices' bounds along a
  hierarchy-chosen direction.
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
`Item_flags::bone` is saved and loaded with the scene (glTF: the name `bone`
in the node's `ERHE_node` `flags` list, the persistent flag names of
`doc/gltf_extensions/flags.md`, next to the `properties` map that carries the
`Ik.*` and `Rig.*` values). Skin import still marks its joints
(`mark_skin_joints`), so imported rigs are unchanged, and that marking is
authoring: the flag it sets is saved like one set by hand, so a saved rig
lists its joints as bones itself. Nothing clears the flag but an edit (the
`bone` property on nodes, Rig > Bone in the Properties window, or
`set_item_flags`): a node stays a bone when no skin lists it - a skin's
removal, the undo of an import that brought the skin - so a skeleton exists
before and without any mesh. Alternative: keep bone-ness derived and give an authored
skeleton a mesh-less `Skin` - rejected, because a skin's inverse binds would
have to be invented and kept current through every structural edit before any
mesh is bound.

### 1.2 Rest pose

**R2.** Every bone has a rest transform: a local TRS held as the attached
property `Rig.rest_transform`. The property system has no TRS value type, so
it is three attached properties, one per channel: `Rig.rest_translation`
(vec3), `Rig.rest_rotation` (quat) and `Rig.rest_scale` (vec3). Its default
is computed, not stored: for a skinned joint the bind pose from the skin's
inverse bind matrices (`erhe::scene::get_bind_pose_parent_from_node`, the
value Reset Bones to Bind Pose writes: relative to the parent joint's bind
frame, a skin root's bind-time world anchored at the skinned mesh node); for
any other bone the local TRS it was created with, which the creating
operation (section 2) records as a local value (identity until then). `Ik.rest_rotation`'s default becomes the rotation of
`Rig.rest_transform`, so the limits frame and the rest pose are one thing.
"Apply pose as rest" (write the current local TRS as the rest) is in scope
only for unbound bones; for a bound bone it is Phase 6 (it would invalidate
the inverse binds). Alternative: rest = bind pose only - rejected, because
unbound authored skeletons have no bind pose.

### 1.3 Bone tail

**R3.** A bone's tail is its head plus the local vector `Rig.tail` (an
attached vec3 in the bone's local frame). Its default is computed
(`compute_default_bone_tail`, `src/editor/rig/bone_tail.hpp`): for a joint a
skin lists, `infer_skinned_bone_tail` (the head the child joints agree on,
else the bounds-derived length along the hierarchy direction, else the
joint's own offset length along +Y); for any other bone, the first bone
child's head; for an unskinned leaf, +Y as long as its bone parent's tail
(a chain grown by hand keeps its bone length at the end), and one scene unit
along +Y under a non-bone. A bone created in the editor records its tail as
a local value (R4). Setting it stores a local value; on a bone a skin lists
the write is refused (R9). The bone visualization draws from `Rig.tail` instead of calling
the inference itself. Roll is the bone's rotation about its head-to-tail
axis; no separate roll value exists.

**R4.** A **connected** child (`Rig.connected`, bool attached property,
default false) keeps its head on its parent's tail: setting it snaps the
child's local translation to the parent's `Rig.tail`, and editing the
parent's tail moves connected children with it, each in the same undo step
as the edit. A new bone created without a
parent tail to derive from gets tail `(0, length, 0)` with the length of the
creation gesture (section 2), so +Y is the bone axis, matching glTF exporters
and `derive_twist_axis`.

## 2. Bone creation and structure (slice C)

Depends on section 1. Every verb is one undoable `Compound_operation`, an
entry in the Hierarchy context menu for a bone (the `add_bone_tip_nodes`
pattern) and an MCP tool with explicit arguments.

- **R5. Create bone**: a new bone node, last child of the node the verb names
  (the clicked Hierarchy row, the scene root from the Scene row). The editor
  has no 3D cursor, so the head is the parent's tail (`Rig.tail`) when the
  parent is a bone and the parent's origin otherwise (the scene origin under
  the root); the local rotation is identity and the tail is one scene unit
  along the parent's bone axis (the direction of the parent's `Rig.tail`),
  +Y under a non-bone. The new bone carries the bone flag, a local
  `Rig.tail` and its creation local TRS as local `Rig.rest_*` values; it is
  not connected, and it becomes the selection and the active item. Its name
  is `Bone` (or the name the MCP tool is given), made unique among the
  parent's children and the parent's skeleton as `Bone.001`, ...
- **R6. Extrude**: from each target bone, a new child bone whose head is the
  parent's tail, connected, same direction and length (the tail vector copied,
  local rotation identity), rest recorded as for R5; the new bones become the
  selection, the first one active (so a repeated extrude grows a chain).
- **R7. Subdivide**: a bone becomes N connected bones (N >= 2; the menu offers
  2, 3 and 4) along its head-to-tail segment. The bone keeps its name, head
  and rest, and its tail becomes tail / N; N - 1 new bones follow as a chain
  of children, each translated by tail / N with identity rotation, rest
  recorded. The bone's other children re-parent to the last piece keeping
  their world transforms (a connected child's head exactly on the last
  piece's tail), and an unbound child bone's `Rig.rest_translation` moves by
  the same offset so its rest stays where it was. The bone and its pieces
  become the selection.
- **R8. Delete / dissolve**: delete removes bones and re-parents their
  children to the deleted bone's parent keeping world transforms; an unbound
  child bone's rest becomes rest(removed) * rest(child), and a connected
  child is disconnected (its head is no longer on its parent's tail).
  Dissolve additionally extends the parent's tail to the removed bone's tail
  when the parent is an unbound bone and the removed bone was its only
  connected child; the removed bone's connected children then stay connected
  (their heads are on the extended tail). Both clear the selection.
- **R9. Bound skeletons**: on a bone that a `Skin` lists, R6-R8, R5 under
  such a bone, and any write of `Rig.tail` / `Rig.rest_transform` are
  refused with a logged message naming the skin (inverse binds and weights
  would go stale; Phase 6). A verb whose targets include such a bone is
  refused whole, and its Hierarchy menu entries are disabled with the
  message as tooltip. Posing a bound skeleton is unaffected.

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
  bone whose name has a side and that has no counterpart (R12; for a
  skeleton root, no sibling of the flipped name), creates the mirror bone
  across the X = 0 plane of the **skeleton frame**: the frame of the
  skeleton root's parent node, the frame the root's own local transform and
  rest sit in (Blender's armature space; world when the root has no parent).
  With S = diag(-1, 1, 1) and S_w = F * S * inverse(F) the reflection across
  that plane in world (F the skeleton frame's world transform), the mirror
  bone's world transform is S_w * W(bone) * S: a point with local
  coordinates (x, y, z) in the bone is at (-x, y, z) in the mirror bone, whose
  frame stays proper. Selected bones are mirrored parents first, so a whole
  selected side is mirrored in one undo step. The mirror bone is named
  `flip_side_name(name)` and is the last child of the mirror bone created
  for its parent in the same step, else its parent's counterpart, else its
  parent (a parent without a side or without a counterpart, a skeleton
  root's parent). It carries:
  - local transform: S_w * W(bone) * S expressed under the new parent -
    S * local * S (translation x negated, quaternion (w, x, y, z) ->
    (w, x, -y, -z)) when the new parent is the mirror of the bone's parent or
    the bone is a skeleton root;
  - `Rig.tail` (x, y, z) -> (-x, y, z); `Rig.rest_*` the mirror of the bone's
    rest in the skeleton frame (the rest product from the root down),
    expressed under the new parent's rest;
  - `Rig.connected` copied, and cleared (logged) when the mirrored head is
    not on the new parent's tail;
  - the bone's local `Ik.*` values: locks, limit flags and stiffness copied;
    `limit_min` / `limit_max` with the X range kept and the Y and Z ranges
    negated and swapped ([min, max] -> [-max, -min]), because the limited
    rotation inverse(rest_rotation) * rotation is conjugated by S, which
    keeps an angle about X and negates angles about Y and Z;
    `rest_rotation` mirrored like the rest; `pole_angle` negated; `pole_target`
    mapped to the pole's mirror (the mirror bone created for it, the pole
    bone's counterpart, or the pole's sibling of the flipped name), not
    copied otherwise.

  The new bones become the selection. Bones without a side or with a
  counterpart are skipped (logged). R9: refused when a selected bone, or an
  existing bone a mirror bone would be created under, is a bone a skin lists.

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

  The flipped transform of an entry named `n` pasted onto the bone
  `flip_side_name(n)` (a name without a side flips onto itself), with both
  bones looked up in the target skeleton:

  - S = diag(-1, 1, 1), the reflection across the X = 0 plane of the
    skeleton root's rest frame.
  - B(b) = the rest transform of bone b relative to the skeleton root's rest
    frame: the product of the `Rig.rest_*` local transforms from the root's
    child down to b; B(root) = identity, and the root's parent frame is
    inverse(rest(root)).
  - G = inverse(B(source parent)) * S * B(target parent), the rest-pose
    mirror map from the target's parent frame to the source's.
  - D = pose * inverse(rest(source)), the entry's change from its bone's
    rest, in the parent frame.
  - pasted local transform = inverse(G) * D * G * rest(target).

  A pose equal to the source's rest pastes as the target's rest, and the
  map is an involution. When the rest frames are mirror images across S
  (symmetric Blender rigs) it reduces to S * pose * S - translation x
  negated, quaternion (w, x, y, z) -> (w, x, -y, -z). Because the change is
  carried through the mirror of the parents' rest frames rather than through
  each bone's own axes, rest frames whose rolls differ between the sides
  (RiggedFigure's) still give world transforms mirrored across the skeleton
  root's X = 0 plane, up to the mirror asymmetry of the rest pose itself.

## 6. Bone roll and orientation (slice C)

- **R16.** Recalculate Roll: rotates each selected bone about its
  head-to-tail axis (the direction of `Rig.tail`) so a chosen local axis (X
  or Z) points as close as possible to a reference: the signed angle, about
  the bone's world axis, from the chosen axis to the reference with both
  projected onto the plane perpendicular to the bone. The references are the
  world X, Y and Z axes and the view direction (toward the viewer of the last
  viewport, when it shows the bone's scene); the editor has no 3D cursor. The
  tail is on the rotation axis, so the tail point stays put in world. A bone
  whose chosen axis or reference is parallel to its bone axis is skipped.
  Align to Active: gives each selected bone other than the active bone the
  active bone's head-to-tail direction and roll - its world rotation becomes
  the active bone's, times the shortest rotation between the two bones'
  local tail directions when they differ; the head stays, the tail keeps its
  length and turns onto the active bone's direction, and a default
  `Rig.tail` is recorded as a local value so a default that follows a
  child's head does not turn it back.

  Both turn a bone's frame about its head by a change C in its own frame
  (local L -> L * C). Children keep their world transforms (local ->
  inverse(C) * local), except that a connected child bone keeps its head on
  the moved tail and its world rotation. The rest transform
  (`Rig.rest_translation` / `rest_rotation`) of every changed unbound bone
  gets the same change, so the pose relative to rest is unchanged, and a
  local `Ik.rest_rotation` turns with it. One undo step each. R9 applies to
  both (they change the rest frame of bound bones).

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

Slices A and B, the foundations and slice C (R5-R9, Symmetrize of R13,
R16) are implemented; slice D is not.

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
- R2 as slice B needs it: `editor::Rig` (`src/editor/scene/rig_properties.hpp`)
  registers `Rig.rest_translation` / `rest_rotation` / `rest_scale` with the
  computed bind-pose default (doc/erhe/property_system.md 4.28);
  `Ik.rest_rotation` defaults to `Rig.rest_rotation` (D31 `default_from`, so
  a change of it notifies `Ik.rest_rotation` and the next IK drag reads the
  new limits frame), which is the bind
  rotation `Ik.rest_rotation` defaulted to before for a joint under a joint
  parent, and the skin root's bind-time rotation (formerly identity) for a
  skin root. No verb writes a local value yet; MCP `set_item_property` and
  the Properties window's generic "Rig" rows do. Persisted like `Ik.*`, in
  the `ERHE_node` properties map.
- R14 and R15 pure parts: `clear_pose_channels`, `mirror_pose_transform`,
  `get_parent_rest_in_skeleton`, `copy_bone_pose`, `plan_clear_pose`,
  `plan_paste_pose` in `src/editor/rig/bone_pose.hpp`, unit tested by
  `editor_rig_tests` (`test_bone_pose.cpp`). Channel locks go through
  `apply_channel_locks` (`src/editor/transform/channel_locks.hpp`), the rule
  the Transform tool's `enforce_channel_locks` uses.
- R14 / R15 verbs: `clear_bone_pose` / `paste_bone_pose` in
  `src/editor/rig/bone_commands.hpp` queue one `Compound_operation` of
  `Node_transform_operation`s (nothing when no bone changes). Like Reset
  Bones to Bind Pose they first stop an animation playing on the affected
  bones (`Animation_player::stop_if_targeting`), whose animated layer would
  hide the result. The pose
  buffer is `Scene_commands::set_pose_buffer` / `get_pose_buffer` (names and
  local TRS only).
- Entry points: the bone Hierarchy context menu adds `Clear Location`,
  `Clear Rotation`, `Clear Scale`, `Clear All`, `Copy Pose` (targets as the
  slice A verbs), `Paste Pose` and `Paste Pose Flipped` (onto the clicked
  bone's skeleton; disabled while the buffer is empty); the MCP tools
  `clear_pose` (`bones`, `channels`), `copy_pose` (`bones`, returns `pose`)
  and `paste_pose` (`skeleton`, `pose`, `mode` `normal` | `flipped`), which
  never read the pose buffer, covered by
  `Mcp_test.clear_pose_respects_locks_and_paste_pose_flipped_mirrors_the_arm`
  and `Mcp_test.rig_rest_rotation_is_the_ik_limits_frame_and_posing_stops_the_animation`.
- The USD save's "not written" warning counts nodes holding a local `Rig.*`
  value together with the `Ik.*` holders (`pole_target.md` R26).
- R1: `erhe::scene::Node::bone_property` is the bridged `bone` flag property
  (Properties: Rig > Bone, on every node), and `bone` is a persistent glTF
  flag name (`gltf_item_flags.cpp`). `Xformable::handle_flag_bits_update`
  reports every change of the bit (the property, `mark_skin_joints`,
  `set_item_flags`) to the scene's node systems as a change of that property.
  `erhe::scene::find_skin_joint` answers whether and where a skin lists a node.
- R3: `Rig.tail` (`Rig::tail_property()`) with the computed default of
  `rig/bone_tail.hpp`; its `Property_bridge::validate` refuses a write on a
  node `find_skin_joint` finds (R9, the logged reason names the skin). The
  refusal covers every writer, the Properties row and MCP `set_item_property`
  included; clearing the value (back to the default) is allowed. The
  `Rig.rest_*` properties refuse the same way (slice C, below).
- Bone display: `Bone_visualization` keeps one proxy per bone of an editor
  scene, skinned or not, shaped from `Rig.tail`. `Rig_system`
  (`src/editor/rig/rig_system.hpp`, a node system of every `Scene_root`)
  queues a `Bone_changed_message` when a bone enters or leaves its scene, the
  bone flag changes or `Rig.tail` changes; the visualization reconciles the
  node's proxy and reshapes its parent's. A default tail that follows a
  child's head is refreshed by `Node_touched_message` as before. The line
  style reads the proxy's tail (`Bone_visualization::get_bone_tail`), and Add
  Bone Tip Nodes places tips at `Rig.tail` (unskinned leaf bones included).
- R4: `Rig.connected` (`Rig::connected_property()`). The snaps are follow-ups
  of the edit's `Property_set_operation` (`append_bone_connect_follow_ups`,
  `src/editor/rig/bone_connect.hpp`): its first applied execute records a
  `Node_transform_operation` per connected bone child a `Rig.tail` edit
  moves (or for the node itself when `Rig.connected` becomes true, onto its
  bone parent's tail), a redo reruns them after the write and an undo undoes
  them before restoring the value - one undo step. A write that does not go
  through an operation (a scene load, `set_value` in code) moves nothing, so
  a load never re-snaps a saved skeleton.
- Tests: `editor_rig_tests` (`test_bone_tail.cpp`: the default rules and the
  skinned inference), `Mcp_test.unskinned_skeleton_keeps_its_bones_and_connected_children_follow_the_tail`
  (a skeleton no skin lists survives save + reopen with its bones, tail and
  connection; connect and tail edits are one undo step each) and
  `Mcp_test.rig_tail_defaults_to_the_skinned_inference_and_is_refused_on_a_bound_bone`.
  `scripts/skeleton_editing_verify.py` section D drives the Properties rows
  Rig > Bone / Connected / Tail on a new skeleton, compares screenshots in
  bone selection mode along the old and the new tail, and checks the glTF
  save + reopen.
- `scripts/skeleton_editing_verify.py` drives the context-menu entries on
  RiggedFigure through the Hierarchy filter and right-click, and checks the
  selections, the names and that Flip Names is one undo step Ctrl+Z reverts
  (sections A, B), and Clear Rotation / Clear Location with a locked channel
  and Copy Pose + Paste Pose Flipped from the left arm onto the right, each
  one undo step Ctrl+Z reverts, and the USD save warning for a local
  `Rig.rest_rotation` (section C). It closes the scenes it opened before the
  editor exits, so the editor's window-visibility file is left as found.
- R5-R9 (slice C, structure): `create_bone`, `extrude_bones`,
  `subdivide_bones`, `delete_bones` (`Bone_delete_mode::delete_bones` /
  `dissolve`) and the refusal `get_bound_bone_refusal` in
  `src/editor/rig/bone_structure.hpp`. Each queues one `Compound_operation`:
  new bones are `Xform` nodes carrying the bone flag and their `Rig.*`
  values from construction, inserted with `Item_insert_remove_operation` and
  pinned to their local transform by a `Node_transform_operation` (the
  `add_bone_tip_nodes` pattern); re-parents (`Item_parent_change_operation`,
  or the child promotion of an `Item_insert_remove_operation` removal) keep
  world transforms and are bracketed by transform pins, so undo restores the
  local transforms exactly; `Rig.tail`, `Rig.connected` and `Rig.rest_*`
  changes of existing bones are `Property_set_operation`s (a tail edit's
  connected-children follow-ups run after the re-parents); the creating
  verbs end with a selection step that selects the new bones and puts the
  previous selection back on undo. Delete / Dissolve plan several targets
  parents first, tracking the planned parents, rests, connected flags and
  tails.
- R9 on the properties: `Rig.rest_translation` / `rest_rotation` /
  `rest_scale` refuse a write on a node a skin lists in their
  `Property_bridge::validate`, as `Rig.tail` does (the Properties rows, MCP
  `set_item_property` and every other writer included; clearing is allowed).
- Entry points: `Create > Bone` in the Hierarchy context menu of every node
  row and the Scene row (disabled under a bone a skin lists), and on a bone's
  menu `Extrude`, `Subdivide > 2 / 3 / 4 Bones`, `Delete Bone` and `Dissolve
  Bone` (targets as the slice A verbs; disabled, the refusal as tooltip, when
  a target is a bone a skin lists); the MCP tools `create_bone` (`parent`,
  `name`), `extrude_bones` (`bones`), `subdivide_bones` (`bones`, `count`)
  and `delete_bones` (`bones`, `mode` `delete` | `dissolve`), which return
  the refusal as their error text, covered by
  `Mcp_test.bone_structure_verbs_build_an_unskinned_chain_and_undo_exactly`
  (create + extrude x2, subdivide, dissolve and delete, each one undo step
  whose undo restores parents, transforms, tails, rests and the connected
  flag; an IK drag on the authored chain) and
  `Mcp_test.bone_structure_verbs_are_refused_on_a_bound_bone`.
  `Mcp_test.rig_rest_rotation_is_the_ik_limits_frame_and_posing_stops_the_animation`
  checks the rest-rotation limits frame on an authored chain, a bound bone
  refusing the write.
- `scripts/skeleton_editing_verify.py` section E drives the menu entries on
  a skeleton authored in a scene of its own (Create > Bone, Extrude twice,
  Subdivide > 2 Bones, Delete Bone, Dissolve Bone, each checked, one undo
  step, Ctrl+Z), checks the entries are disabled on a RiggedFigure joint
  and `extrude_bones` refused with the skin in the log, and checks that a
  glTF save + reopen keeps the authored bones, tails, connected flags and
  rest values.
- R13 Symmetrize and R16 (slice C): `symmetrize_bones`,
  `recalculate_bone_roll` and `align_bones_to_active` in
  `src/editor/rig/bone_structure.hpp`, over the pure math of
  `src/editor/rig/bone_mirror.hpp` (`mirror_trs_x`, `mirror_local_transform`,
  `mirror_ik_limits`, ...) and `src/editor/rig/bone_roll.hpp`
  (`compute_roll_angle`, `compute_align_change`, `apply_frame_change`), unit
  tested by `editor_rig_tests` (`test_bone_mirror.cpp`, `test_bone_roll.cpp`).
  Symmetrize builds its bones with the creation machinery of R5 (bone flag,
  `Rig.*` and `Ik.*` values set on the new node before its insert, transform
  pin, selection step) in one `Compound_operation`; the roll verbs queue one
  `Compound_operation` of `Node_transform_operation`s for the bones and their
  children and `Property_set_operation`s for the rest, `Ik.rest_rotation`
  and (Align) the recorded tail.
- Entry points: on a bone's Hierarchy menu `Symmetrize`, `Recalculate Roll >
  X to World X / Y / Z / View` and `Z to World X / Y / Z / View` (View
  disabled until a viewport shows the scene), and `Align to Active` (enabled
  when the selection has an active bone and another target), targets as the
  slice A verbs and disabled with the refusal as tooltip like the structure
  verbs; the MCP tools `symmetrize_bones` (`bones`), `recalculate_bone_roll`
  (`bones`, `axis` `x` | `z`, `reference` `x` | `y` | `z` | `-x` | `-y` |
  `-z` | `[x, y, z]`) and `align_bones` (`bones`, `active`), covered by
  `Mcp_test.symmetrize_mirrors_an_authored_arm_in_one_undo_step`,
  `Mcp_test.recalculate_roll_and_align_to_active_keep_axes_and_children` and
  the three tools' refusals in
  `Mcp_test.bone_structure_verbs_are_refused_on_a_bound_bone`.
- `scripts/skeleton_editing_verify.py` section F mirrors a one-sided arm
  authored off the plane with Symmetrize from the menu of a selected bone
  (world heads and tails mirrored within 1e-5), runs Recalculate Roll > X to
  World Z (the X axis aimed within 1e-4 degrees, the bone axis and the child
  kept) and Align to Active, each one undo step Ctrl+Z reverts, and checks
  the three entries are disabled on a RiggedFigure joint.
