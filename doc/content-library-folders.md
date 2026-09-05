# Content library folders

Folders inside the content library's category folders (Brushes, Materials,
Animations, ...) group entries, are selectable, show in the Properties
window like any item, and carry property values that their member entries
inherit. This document is the design record; the library reference is
`src/editor/content_library/notes.md`, the property mechanics are
`doc/property-system.md` (D8 inheritance, D12 Add / Remove Property) and
the wire format is `doc/gltf_extensions/ERHE_scene.md`.

## 1. Requirements

- R1 Folder tree. A category folder holds entries and folders to any depth.
  A folder is a `Content_library_node` without an item, carrying its
  category's `type_code` / `type_name`; every entry under it belongs to that
  category. The library root holds only the category folders. An item is
  listed once per library: `add` on a category folder finds the entry in
  whichever folder holds it, `remove` removes it from there, and a
  `get_all` cache covers the subtree below the node it was built on.
- R2 Editing. The Scene Hierarchy's nested Content Library offers "Create
  Folder" on a category folder and on a folder inside it; a folder is
  renamed from the Properties window name row, deleted with the tree's
  "Delete", and entries and folders are moved by dragging them onto a
  folder of the same category. Every one of these is one undoable
  operation.
- R3 Selection and Properties. A folder row is selected like any row and
  the Properties window shows the folder: name row, the folder's
  registered properties (its class chain: `Item_base` flags, the
  `Hierarchy` child count) and the Add Property row of D12, so an attached
  property is added to and removed from a folder like on any item.
- R7 Category properties. A folder holds the properties of its
  category's item class (a Materials folder holds `base_color`,
  `roughness`, `bxdf_model`, the texture slots and their UV transforms,
  ...): Add Property offers them, a held one
  shows as a row, "Remove Property" clears it, and every entry below the
  folder without a local value of its own reads it (R4).
- R4 Inheritance. An `inherits` property with no local value on a library
  entry reads the closest ancestor with one: the entry's own node, then its
  folders up to the category folder and the root. Setting or clearing such
  a value on a folder notifies every descendant entry without a local value
  (D8), and moving an entry or a folder between folders notifies the moved
  subtree of the values that changed (the tree-change snapshot of D8).
- R5 Persistence. A scene save keeps every folder (empty ones too), its
  local property values and the folder each entry sits in, for every
  category. Loading recreates the folders, applies the values and places
  the entries, after the entries themselves exist.
- R6 MCP. `create_library_folder` creates a folder from a category-rooted
  path, `move_library_item` accepts a folder path and is undoable, and the
  D13 property tools address a folder by name or id like any item.

## 2. Design

- D1 Inheritance link. `erhe::Item_base` holds an inheritance container
  pointer (`set_inheritance_container` / `get_inheritance_container`,
  `erhe::property::Dependency_object*`, null by default, not copied) and
  its `get_inheritance_parent()` returns it. Classes that derive their
  parent from scene structure (`Hierarchy`, `Node_attachment`) keep their
  own overrides and never use the pointer. The wrapping
  `Content_library_node` is the container of its item: the parent folder's
  `handle_add_child` sets it on an OWNING entry's item (a reference entry
  lists an item owned by another scene, whose folders must not affect it),
  and the node's destructor clears it when the item still names the node.
  `Content_library_node::for_each_inheritance_child` visits the hierarchy
  children and then the owning entry's item, so D8's descendant walk and
  tree-change snapshot reach the items. Setting the pointer inside
  `handle_add_child` is what makes the snapshot correct: `Hierarchy::
  set_parent` captures before the attach and applies after it, so an entry
  attached under a folder with a local value is notified of the change.
- D1b Entry names. Sibling-unique naming (`src/erhe/item/notes.md`
  "Sibling-unique names") applies to entry nodes, and an owning entry wins
  the name over a reference entry: when an owning entry attaches to a folder
  where a reference entry holds the wanted name,
  `Content_library_node::handle_add_child` renames the reference ENTRY NODE
  (never the item it lists, which another container owns), so a scene's
  authored names survive a reload whatever order the entries attach in.
- D2 Folder creation. "Create Folder" queues an `Item_insert_remove_
  operation` inserting a folder node made from the parent's category (the
  `make_folder` constructor form) named "New Folder"; the root is not a
  target (a folder under it would have no category). The same operation in
  remove mode is what "Delete" runs through `Selection::delete_items`,
  which collects the folder's subtree deepest first.
- D3 Moves. `Content_library_move_operation` (`src/editor/operations/`)
  records the moved node, its parent and index before and after; execute
  and undo call `Hierarchy::set_parent(parent, index)` under the library
  mutex. The tree accepts a `Content_library_node` payload on a folder row
  when both carry the same `type_code` and the payload is not the target or
  one of its ancestors; the drop appends to the folder. A drop on the
  Brushes category's brush rows keeps its existing meaning (fork the brush
  with the dropped material).
- D8 Category properties (R7). A folder's secondary owner type
  (`doc/property-system.md` D30) is its category's item class:
  `Content_library_node::category_owner_type`, set on each category
  folder by the `Content_library` constructor (`Material::
  property_owner_type()` for Materials, `Brush` for Brushes, ...), copied
  to every folder made below it by `make_folder`, "Create Folder" and
  `create_library_folder`, and reported by `get_secondary_property_owner_
  type()` on folder nodes only (an entry node holds none). The item class
  registers the properties a folder may pass down as `inherits`;
  `Material` does for its values, texture slots and slot transforms
  (section 4.1 of the property design record), the other categories gain
  it when a folder value of theirs is wanted. The held values ride
  `library_folders` `properties` (D5) by qualified name; a texture
  reference travels as the texture's name and resolves on load through
  the folder's library owner (`Content_library_node::
  resolve_expression_object`), and the reference picker of a folder row
  lists the textures of the library's scene (`find_scene_root_for_item`
  knows a library node's scene).
- D9 Folder style. A folder carries a style like any item
  (`doc/style-library.md` R4): the `style` row takes any style, the
  style's values of the category's class are the folder's effective values
  and so reach the entries below it through R4, and `library_folders`
  saves the style by name (D5).
- D4 Properties window. `Properties::item_properties` already unwraps a
  leaf node to its item and shows a folder node as itself; the generic
  `dependency_properties` section (D12) lists the folder's class chain and
  the Add Property row. No folder-specific rows exist.
- D5 Wire format. `ERHE_scene` gains `library_folders`: an array of
  `{"path", "properties", "items"}` objects, one per folder below a
  category folder, depth first. `path` is the folder's slash-separated path
  from the library root, starting with the category folder's name
  (`"Materials/Metals"`); `properties` is the folder's local property map
  in the D14 form (`item_local_properties_to_json`), omitted when empty;
  `items` lists the names of the entries directly in the folder, omitted
  when empty. Entries at the category level are not listed (that is where a
  load puts an entry no folder names). `ERHE_brushes` `folder_path` is
  read for older files and no longer written; `library_folders` is the one
  carrier for every category.
- D6 Load order. `import_gltf_editor_state` appends one
  `Content_library_folders_operation` after every attach operation of the
  import: it creates each listed folder under its category
  (`resolve_library_folder`), applies the properties
  (`apply_item_local_property`) and moves each named entry from wherever
  the attach operations put it. An entry name that matches nothing in the
  category logs a warning; a name that matches several moves the first and
  logs a warning. Undo of an import removes the folders it created.
- D7 MCP. `create_library_folder(scene_name, folder_path)` resolves the
  path from the library root through `resolve_library_folder` and queues
  the D2 insert for the last component; `move_library_item(scene_name,
  item_name, folder_name | folder_path)` keeps `folder_name` (a folder
  under the entry's category, created when missing) and adds `folder_path`
  (category-rooted, must exist), queuing the D3 move. `find_item_in_scene`
  visits every library node (folders included) after the scene items, so
  `get_item_properties` / `set_item_property` / `get_addable_item_
  properties` take a folder by `item_id` or `item_name`.

## 3. Verification

Headless, over `scripts/mcp_call.py` on a fresh editor:

1. `create_library_folder` `Materials/Test`; `get_scene_nodes` shows the
   folder; `get_item_properties` on it lists `visible` (source `default`).
2. `move_library_item` a default material into the folder;
   `set_item_property` `visible` `false` on the folder; the material's
   `visible` reads `false` with source `inherited`; `undo` twice restores
   both.
3. `save_scene` and reopen: the folder, its `visible` local value and the
   moved material are back, and `logs/log.txt` has no
   `library folder` warning. Use a material a mesh uses (`Copper`): the
   scene file never carries unused library materials, so an unused one is
   reported as not held by the category on reload.
4. `close_scene`, wait, grep `logs/log.txt` for `scene-close leak`: clean.
5. Category properties (R7): `get_addable_item_properties` on a Materials
   folder lists `Material.base_color` and no `Material.*_texture` slot;
   `set_item_property` `Material.base_color` `1 0 0` on the folder, clear
   the moved material's own `base_color` (value `null`): the material
   reads `1 0 0` with source `inherited`; save and reopen: the folder's
   value is back as local (the material's is local too, baked by the glTF
   material export).

Interactive: "Create Folder" from the context menu, rename in Properties,
drag a material onto the folder, Ctrl+Z after each.
