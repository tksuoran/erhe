# Style library

Styles are items of the content library's Styles category: a style is a
named holder of property values of any item class (`Material.roughness`,
`Light.color` and `Camera.fov_y` can share one style), selectable, edited
in the Properties window like any item, assigned to any item through the
item's `style` property, and saved with the scene. This document is the design
record; the layer mechanics are `doc/property-system.md` D25 (the style
layer) and D30 (secondary owner types), the library reference is
`src/erhe/property/notes.md`, and the folder counterpart is
`doc/content-library-folders.md`.

## 1. Requirements

- R1 Style item. A `Style` is a library item holding value properties of
  any item class. Its own local values are the style: adding, editing and
  removing them is the D12 row machinery (Add Property, which offers every
  class's value properties grouped by class with a filter; Reset to
  default, Remove Property, undo), with the same qualified names a folder
  of that category uses (`Material.roughness`). "Create Style" on the
  Styles folder and the MCP `create_style` make an empty style.
- R2 Live edit. A change of a style's value reaches every item using the
  style at that moment: each user without a local value of its own for
  that property is notified with the old and new effective value, and the
  editor consequences of D11 run for it.
- R3 Assignment. Every item has a `style` property (a D28 object
  reference) shown as a row with the picker and the drag target; the
  candidates are the scene's styles, every one of which applies to every
  item (D3). A style item has the row too and so uses a style of its own;
  an assignment that would make the chain cycle is refused and the item
  keeps its style. A value of a class the item is not (`Light.color` on an empty
  node, on a Materials folder) reaches the item's inheritance descendants
  of that class, so a style on a node styles the lights below it.
  Clearing the row
  removes the style. "Paste Properties as Style" and the MCP
  `set_item_style` create a style item in the scene's Styles folder from
  the copied values and assign it, in one undo entry.
- R4 Folders. A content-library folder can carry a style like any item;
  the style's values are the folder's effective values and so reach the
  entries below it through inheritance (R4 of the folders record).
- R5 Persistence. A scene save keeps every style (name, values) and the
  style each material, each node and each library folder uses, by name. A load recreates the styles before anything references
  them.
- R6 Defaults. The default metals share one "Brushed metal" style item in
  the scene's Styles folder, so the library shows what the metals have in
  common and a scene keeps it across a save.

## 2. Design

- D1 Style source. The style layer of a `Dependency_object` (D25) reads
  the LOCAL values of another `Dependency_object`, its style source:
  `set_style(std::shared_ptr<const Dependency_object>)`. A style source is
  an object like any other, so it takes a style itself and the layer
  resolves through the chain of the styles' local values (the style chain
  of D25, which also carries the cycle refusal): the erhe counterpart of
  USD's `class` prims - an imported class hierarchy is a chain of style
  items, not one flattened style. `Property_style`
  stays as the library's plain named source (a `Dependency_object` filled
  from a `Property_set`, for tests and non-item users). A source keeps the
  list of objects using it (registered by `set_style`, by the copy of a
  user, and dropped by a user's destructor); when a source's local layer
  changes, `notify` forwards the change to every user without a local
  value of that property (R2), computing each user's old value from the
  old style value (or the user's inherited / default value when the
  source held none) and its new value from the user's effective value.
- D2 Style item. `editor::Style` (`content_library/style.{hpp,cpp}`) is an
  `erhe::Item` whose secondary owner type (D30) is the root owner type, so
  every class's value properties (D30's rule: not bridged, not computed,
  not on the style's own `Item_base` chain) are its secondary properties,
  the Add Property picker offers them all by qualified name, and the
  values live in the item's own store. `erhe::Item_type::style` is its
  type bit; a style is a prim of its scene's tree under the library's
  `Styles` scope (`doc/usd-compatibility-plan.md` U4). A style is clonable
  (its values copy), and "Copy to Scene" copies it like a material.
- D3 The `style` property. `Item_base::style_property` is a bridged (D18)
  object-reference property registered on `Item_base`: `get` is the
  object's style source, `set` is `set_style`, validated by
  `Item_base::style_applies`: the referenced object's secondary owner
  type is on the object's own owner chain, or is the object's own
  secondary owner type or a descendant of it - a `Style` names the root
  type and so applies to every item; the rule is what keeps a
  `Property_style` or another item, used as a style source through the
  library API, honest. `reference_item_types` is the style bit, so the
  row's picker and drop target take styles only, and
  `collect_reference_candidates` offers only the styles that apply and
  whose chain does not already reach the item (`style_chain_reaches`), so
  a style's own row offers the other styles. The
  property carries the
  draw-list and shader-variant flags, so the D11 hook rebuilds the draw
  lists after an assignment through a `Property_set_operation`;
  `Style_set_operation` (the operation the paste and MCP paths queue)
  keeps its per-property consequence loop over both styles' values.
- D4 Wire format. `ERHE_scene` gains `styles`: an array of `{"name",
  "properties", "style"}`, `properties` the D14 map of the style's local
  values by qualified name and `style` the name of the style that style
  uses itself (a `target` member of older files is ignored). A load
  creates every style of the array first and then assigns the `style`
  members by name, so the order inside the array does not matter.
  `ERHE_material` and `ERHE_node` gain `style`, the name of the item's
  style item; a `library_folders` entry gains `style` the same way. A
  load creates the styles first (one attach operation per style, before
  the folders operation), then the folders operation and one
  `Item_style_by_name_operation` per material and per node assign them
  by name; an unknown name logs a warning and assigns nothing. A light
  keeps inheriting through a reload because `ERHE_light`'s `properties`
  map is the light's complete local set (`doc/gltf_extensions/
  ERHE_light.md`).
- D5 Defaults. `add_default_materials` makes the "Brushed metal" style
  item in the library's Styles folder and assigns it to each metal.

## 3. Verification

Headless, over `scripts/mcp_call.py` on a fresh editor:

1. `get_item_properties` on `Brushed metal` (the Styles folder entry) lists
   `Material.roughness` as local; on `Copper`, `roughness` reads with
   source `style` and `style` reads `Brushed metal`.
2. `set_item_property` `Material.roughness` `0.9 0.9` on the style:
   `Copper`'s `roughness` reads `0.9 0.9`, still source `style`; `undo`
   restores.
3. `set_item_property` `style` `null` on `Copper`: `roughness` reads the
   default; setting `style` back by name restores.
4. A folder with a style: `create_library_folder` `Materials/Styled`,
   move `Floor` into it, set the folder's `style` to `Brushed metal`,
   clear `Floor`'s own `metallic` (value `null`): `Floor` reads `1` with
   source `inherited` (a create-info material carries every value as
   local, so the folder's style shows only through a cleared value).
5. `save_scene` and reopen: the style item, its values, `Copper`'s
   `style` and the folder's `style` are back; `close_scene` is clean.

6. A chain: `create_style` two styles, set a value on the first, set the
   second's `style` to the first and an item's `style` to the second -
   the item reads the first style's value with source `style`; setting
   the first style's `style` to the second is refused and both keep their
   styles; `save_scene` and reopen keeps the chain.

7. Lights through a node: `create_node` an empty node, `create_light` a
   point light and `reparent_node` it below the node;
   `get_addable_item_properties` on the node lists `Light.color`;
   `set_item_property` `Light.color` `1 0 0` on the node and clear the
   light's own `color` (value `null`): the light reads `1 0 0` with
   source `inherited` and `get_scene_lights` reports it. `create_style`,
   set its `Light.intensity`, set the node's `style` to
   it and clear the light's `intensity`: the node lists `Light.intensity`
   with source `style`, the light reads it `inherited`, and a style edit
   reaches the light. Setting `Material.metallic` on the same style
   lists it too (one style, several classes). `save_scene`, `open_scene`:
   the node's `Light.color`, its style and
   the light's inherited sources are back; `close_scene` is clean.

Interactive: select the style in the Scene Hierarchy's Styles folder,
edit a value and watch the metals change; drag the style onto a folder;
Paste Properties as Style from a material; Create Style on the Styles
folder, Add Property on it (Light.*, Camera.*, Material.* all offered)
and on an empty node; Ctrl+Z after each.
