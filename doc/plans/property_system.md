# Property system: remaining work

Status: proposed

Extends `doc/erhe/property_system.md`, the design record of `erhe::property` and
its editor integration, whose section 6 links here.

## Property serialization to glTF

Three gaps remain in what a save carries:

- The expression text of a driven property (D22).
- Material local values: materials export field by field, and default elision
  plus `Material::set_values` keep a round trip from turning effective values
  into local ones (D32), but a local value no native field carries -
  `reflectance` is the example - is lost.
- One carrier per item type, in place of the `properties` / `mesh_properties`
  members scattered across `ERHE_node`, `ERHE_light` and `ERHE_camera` (D14,
  D23).

`doc/plans/gltf_properties_extension.md` is the draft of the
`ERHE_*_properties` extensions that would close them. It is explicitly
incomplete and not ready to implement, and it is the only place the decisions
taken so far are recorded. The `native_gltf` flag and the elision pass it
depends on exist (D32); the extensions do not.

## A USD save states values, so an inherited one reloads as local

A physics item's schema attributes state effective values: a
`PhysicsMaterialAPI` prim spells its friction, a `PhysicsLimitAPI:<axis>`
instance its bounds. The erhe-only local values of the prim travel beside
them as `erhe:<Owner>:<name>` attributes, but that list says nothing about the
values the schema already carries, so on load the reader applies each of those
as a local opinion of the item. A friction or an axis bound that the item
inherits from its folder or takes from a style is therefore inherited before a
USD save and local after the reload, while the same value round trips through
the glTF path, where the `ERHE_scene` `physics_materials` /
`physics_joints` entry is the item's complete local set (D32,
`doc/erhe/property_system.md` sections 4.12, 4.21 and 4.22). What is missing
is a statement in the file of which schema-carried values the item holds
itself, readable by erhe and ignorable by every other USD consumer, so that a
foreign file - which carries no such statement and whose values are all the
item's own - keeps loading as it does today.

## Style users beyond the content library's style items

D25's style layer currently serves the content library's `Style` items. The
graphics presets join them once `Graphics_settings` is an item with registered
properties.

## Further computed properties

As their consumers appear (D26): a node's world bounds over its subtree, a
scene's item counts. `Rendertarget_mesh`'s size (section 4.15) and
`Animation`'s time range and counts (section 4.16) are computed already.

## Entry storage for graph node parameters

The geometry graph and texture graph node parameters (section 4.5) are the one
member-backed (D18) family left whose values a node or a style could hold and a
descendant inherit (D30): a bridged property is always local, so it is neither
offered on a holder nor inherited. The work is large - 59 member registrations
across 16 files plus the texture graph bridges - and the value is low, since
sharing a node parameter through a style is rarely wanted, so do it when a user
asks for it. The recipe is section 4.18.

`Node`'s transform stays bridged, for the reasons D18 gives.

## Shader graph node parameters as properties

The oldest graph editor (`src/editor/graph/`) has no parameter serialization
and no undo operations at all, so migrating it starts by adopting the
`Graph_editor_node` base or retiring the prototype (see
`doc/plans/graph_editor.md`), not by adding registrations.

## Editor per-item state as attached properties

Item tree expansion and sheet-window formulas, registered by the editor. The
naming, lookup and listing side exists (D3, D12, section 4.14); each needs its
own `visible_when` and a registering owner type on the editor side.
