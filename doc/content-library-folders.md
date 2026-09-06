# Content library folders

Folders inside a content library's kind scopes (`Brushes`, `Materials`,
`Animations`, ...) group resources, are selectable, show in the Properties
window like any item, and carry property values that the resources below them
inherit. This document is the design record; the library reference is
`src/editor/content_library/notes.md`, the property mechanics are
`doc/property-system.md` (D8 inheritance, D12 Add / Remove Property) and the
wire format is `doc/gltf_extensions/ERHE_scene.md`.

## 1. Requirements

- R1 Folder tree. A kind scope holds resources and folders to any depth. A
  folder is an `erhe::Scope`; every resource under a kind scope belongs to
  that kind. A resource is listed once per library: `Content_library::add`
  places it under its kind scope when the library does not list it yet,
  `remove` takes it out of the tree, and the queries answer from the
  library's index.
- R2 Editing. The Scene Hierarchy offers "Create Scope" on a kind scope and
  on a folder inside one; a folder is renamed from the Properties window name
  row, deleted with the tree's "Delete", and resources and folders are moved
  by dragging them onto a scope of the same kind. Every one of these is one
  undoable operation.
- R3 Selection and Properties. A folder row is selected like any row and the
  Properties window shows the folder: name row, its registered properties
  (its class chain: `Item_base` flags, the `Hierarchy` child count) and the
  Add Property row of D12, so an attached property is added to and removed
  from a folder like on any item.
- R7 Kind properties. A folder holds the properties of its kind's item class
  (a Materials folder holds `base_color`, `roughness`, `bxdf_model`, the
  texture slots and their UV transforms, ...): Add Property offers them, a
  held one shows as a row, "Remove Property" clears it, and every resource
  below the folder without a local value of its own reads it (R4).
- R4 Inheritance. An `inherits` property with no local value on a resource
  reads the closest ancestor with one: its folder scopes up to the kind scope
  and the scene root. Setting or clearing such a value on a folder notifies
  every descendant without a local value (D8), and moving a resource or a
  folder between scopes notifies the moved subtree of the values that changed
  (the tree-change snapshot of D8).
- R5 Persistence. A scene save keeps every folder (empty ones too), its local
  property values and the folder each resource sits in, for every kind.
  Loading recreates the folders, applies the values and places the resources,
  after the resources themselves exist.
- R6 MCP. `create_library_folder` creates a folder from a kind-scope-rooted
  path, `move_library_item` accepts a folder path and is undoable, and the
  D13 property tools address a folder by name or id like any item.

## 2. Design

- D1 Inheritance link. A resource inherits by the ordinary tree parent: it is
  a prim below its folder scope, so `Hierarchy::get_inheritance_parent()` and
  `Hierarchy::for_each_inheritance_child()` already reach it and D8's
  descendant walk and tree-change snapshot need no library-specific hook.
  `Hierarchy::set_parent` captures the snapshot before the attach and applies
  it after, so a resource attached under a folder with a local value is
  notified of the change.
- D1b Resource names. Sibling-unique naming (`src/erhe/item/notes.md`
  "Sibling-unique names") applies to resource prims like to every other prim:
  two materials of one scope cannot share a name, and the second gets
  `<base>_<n>`.
- D2 Folder creation. "Create Scope" queues an `Item_insert_remove_operation`
  inserting an `erhe::Scope` named "New Scope" under the scope the menu was
  opened on; only a kind scope and the scopes below one are targets. The same
  operation in remove mode is what "Delete" runs through
  `Selection::delete_items`, which collects the folder's subtree deepest
  first.
- D3 Moves. `Content_library_move_operation` (`src/editor/operations/`)
  records the moved prim, its parent and index before and after; execute and
  undo call `Hierarchy::set_parent(parent, index)` under the library mutex.
  The tree accepts a resource prim or a folder scope on a scope row when both
  sit under the same kind scope of the same library and the payload is
  neither the target nor one of its ancestors; the drop appends to the scope.
  A drop on a brush row keeps its existing meaning (fork the brush with the
  dropped material). A move keeps the prim's item host, so the host is not
  told and the library index is untouched - a move is not a removal.
- D8 Kind properties (R7). A `Scope`'s secondary owner type
  (`doc/property-system.md` D30) is the root owner type, as a `Style`'s is, so
  it holds any class's value properties by qualified name and its descendants
  inherit them. The Add Property list on a scope offers the classes of the
  prims below it first, then the rest
  (`Dependency_property_rows::add_property_row`). The item class registers the
  properties a folder may pass down as `inherits`; `Material` does for its
  values, texture slots and slot transforms (section 4.1 of the property
  design record), the other kinds gain it when a folder value of theirs is
  wanted. The held values ride `library_folders` `properties` (D5) by
  qualified name; a texture reference travels as the texture's name and
  resolves on load through the scope's item host (its scene), and the
  reference picker of a folder row lists the textures of the scope's scene.
- D9 Folder style. A folder carries a style like any item
  (`doc/style-library.md` R4): the `style` row takes any style, the style's
  values of the kind's class are the folder's effective values and so reach
  the resources below it through R4, and `library_folders` saves the style by
  name (D5).
- D4 Properties window. `Properties::item_properties` shows a resource prim
  and a folder scope as themselves; the generic `dependency_properties`
  section (D12) lists the class chain and the Add Property row. No
  folder-specific rows exist.
- D5 Wire format. `ERHE_scene` carries `library_folders`: an array of
  `{"path", "properties", "items"}` objects, one per folder below a kind
  scope, depth first. `path` is the folder's slash-separated scope path from
  the scene root, starting with the kind scope's name (`"Materials/Metals"`);
  `properties` is the folder's local property map in the D14 form
  (`item_local_properties_to_json`), omitted when empty; `items` lists the
  names of the resources directly in the folder, omitted when empty.
  Resources at the kind-scope level are not listed (that is where a load puts
  a resource no folder names). `ERHE_brushes` `folder_path` is read for older
  files and no longer written; `library_folders` is the one carrier for every
  kind.
- D6 Load order. `import_gltf_editor_state` appends one
  `Content_library_folders_operation` after every attach operation of the
  import: it creates each listed folder scope under its kind scope, applies
  the properties (`apply_item_local_property`) and moves each named resource
  from wherever the attach operations put it. A name that matches nothing
  under the kind scope logs a warning; a name that matches several moves the
  first and logs a warning. Undo of an import removes the scopes it created.
- D7 MCP. `create_library_folder(scene_name, folder_path)` resolves the path
  from the kind scopes and queues the D2 insert for the last component;
  `move_library_item(scene_name, item_name, folder_name | folder_path)` keeps
  `folder_name` (a folder under the resource's own parent scope, created when
  missing) and adds `folder_path` (kind-scope-rooted, must exist), queuing the
  D3 move. Resource prims and folder scopes are prims of the scene tree, so
  `find_item_in_scene` reaches them with the scene's own walk and
  `get_item_properties` / `set_item_property` / `get_addable_item_properties`
  take a folder by `item_id`, `item_name` or path.

## 3. Verification

Headless, over `scripts/mcp_call.py` on a fresh editor:

1. `create_library_folder` `Materials/Metals`; `get_scene_nodes` shows the
   scope with type `Scope` under `Materials`.
2. `move_library_item` a material into the folder; `set_item_property`
   `Material.roughness` on the folder; a material below it with no local value
   and no style reads it with source `inherited`; `undo` restores each step.
3. `save_scene` and reopen: the folder, its local value and the moved resource
   are back, and `logs/log.txt` has no `library folder` warning. Use a
   material a mesh uses (`Copper`): the scene file never carries unused
   library materials, so an unused one is reported as not held by the kind on
   reload.
4. `close_scene`, wait, grep `logs/log.txt` for `scene-close leak`: clean.
5. Kind properties (R7): `get_addable_item_properties` on a Materials folder
   lists `Material.base_color` and no `Material.*_texture` slot;
   `set_item_property` `Material.base_color` `1 0 0` on the folder, clear the
   moved material's own `base_color` (value `null`): the material reads
   `1 0 0` with source `inherited`; save and reopen: the folder's value is
   back as local (the material's is local too, baked by the glTF material
   export).

Interactive: "Create Scope" from the context menu, rename in Properties, drag
a material onto the folder, Ctrl+Z after each.
