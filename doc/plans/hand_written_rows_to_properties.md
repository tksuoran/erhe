# Remaining hand-written item state as properties

Status: in progress

Extends `doc/erhe/property_system.md` (the design record of `erhe::property`),
whose section 6 links here. `doc/erhe/property_inventory.md` owns the per-field
status and its "Not yet migrated" table is the work list of this plan; each
phase below removes its row from that table in the commit that lands it.

Decision labels of this plan are `H4`..; `D<n>`, `R<n>` and section numbers
without a document name refer to the design record.

## 1. Goal

Every authored value an item holds is a registered property, so the generic
rows of the Properties window draw it, `Property_set_operation` records its
edits, MCP `get_item_properties` / `set_item_property` reach it, a Style can
hold it (D30) and a multi-selection shows it with mixed-value handling
(`doc/editor/properties_window.md`). The state in scope today is edited by
writing a member directly from an ImGui widget: it has no undo entry, no MCP
property access and no style.

## 2. Scope

The closed list of state this plan migrates, in phase order:

| Phase | Owner | State | Property form |
|---|---|---|---|
| 4 | `erhe::physics::Physics_joint_settings` | `limits`, `drives` | per-axis properties (H6) |

The scene's settings-override block (`Scene_settings`, the codegen optionals
drawn by `Properties::scene_properties`) is config state, and its route to
properties is the `Graphics_settings`-as-item work in
`doc/plans/property_system.md`, "Style users beyond the content library's
style items". The variant combos are switches of the variant table
(`doc/erhe/usd_compatibility_design.md` X4), recorded by their own compound
operation.

## 3. Decisions

- H6 A list of RECORDS (`Physics_joint_settings::limits`, `::drives`) becomes
  a fixed set of per-axis properties of the settings item itself, stated by
  [joint_limits_as_properties.md](joint_limits_as_properties.md), which that phase's
  requirements pass produced.

## 4. Phases

Each phase is one commit series through `doc/agents/orchestration_harness.md`,
verified by the section 4.18 verification loop plus the checks named here.

### Phase 1: Scene.ambient_light

Landed; `doc/erhe/property_system.md` section 4.20 states what the scene's
ambient color and its two file forms now are.

### Phase 2: Layout grid track extents

Landed; `doc/erhe/property_system.md` section 4.13 states what the three
per-axis extent lists now are and how the array row draws them.

### Phase 3: Collision_filter system lists

Landed; `doc/erhe/property_system.md` section 4.21 states what the filter's
three system lists now are, and D34 and D35 state the array row and the
`string_array` value type the phase added.

### Phase 4: Physics_joint_settings limits and drives

[joint_limits_as_properties.md](joint_limits_as_properties.md) owns this phase: its
three commits, their verification and the user's interactive checklist.

## 5. Verification

Per phase: the section 4.18 loop, the owner's gtest suite,
`scripts/scene_roundtrip_verify.py` at its baseline, a scene close with
`all N released` in `logs/log.txt`, and `py -3 scripts/check_doc_links.py`.
The user's interactive check per phase: edit the value in the Properties
window, Ctrl+Z restores it, a Style holding the value drives an item that has
no local value.
