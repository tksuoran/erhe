# windows/

Stability: stable

## Purpose

ImGui window implementations for the editor UI, including viewport display, property inspection, settings, and configuration.

## Key Types

- **`Viewport_window`** -- ImGui window that displays a `Viewport_scene_view`. Handles viewport toolbar, mouse position tracking, hover info updates, drag-and-drop for brushes, and a navigation gizmo (`ImViewGuizmo`). Delegates rendering to the render graph. Opening a scene never rebinds existing viewport windows; `Scene_open_operation` opens a new viewport for the opened scene instead.

- **`Properties`** -- Property inspector window. Displays editable properties for selected items: cameras, lights, meshes, materials, skins, animations, textures, geometry, rendertargets, node physics, and brush placements. Extends `Property_editor` for edit state tracking (clean/dirty).

- **`Property_editor`** -- Base class providing `Editor_state` (clean/dirty) tracking for property editing workflows.

- **`Settings_window`** -- Application-wide settings UI for graphics presets, physics, icons, and ImGui configuration. Can trigger graphics settings changes via `App_message_bus`.

- **`Viewport_config_window`** -- Configures rendering options for a viewport (grid, shadows, selection outline, etc.).

- **`Scene_view_config_window`** -- Configures scene view settings.

- **`Property_origin`** (`property_origin.{hpp,cpp}`) -- where a property value comes from, in the terms of the file the scene was opened from (`doc/erhe/usd_compatibility_design.md` X5): the layer, the prim path in it, the composition arc and its target, and the attribute the writer spells the value as. `Dependency_property_rows` appends it to a row's tooltip as `Layer:` / `Prim:` / `Arc:` / `Authored as:` lines, through `Property_editor::set_entry_tooltip_extra`, which runs the provider only while that row is hovered - the derivation walks the item's ancestors and formats strings, so no frame pays it per row. MCP `get_item_properties` reports the same values as each entry's `origin`.

- **`Item_tree_window`** -- Generic tree view window used for both scene hierarchy browsing and content library browsing. Supports drag-and-drop, context menus, and custom item callbacks. A node row carries right-aligned feature icons, one per attached value group the node carries (`doc/plans/node_attachments_to_properties.md` D1): rigid body, brush placement, layout, draw mode and geometry-graph mesh. `Icon_set::get_feature_icons()` is the table; each entry asks the group's own `carries_<x>()`, which reads that group's key property, so the row shows exactly the groups the Properties window lists as groups (`doc/erhe/property_system.md` section 4.23).

## Scene Hierarchy drag and drop

The scene hierarchy is USD-like: every `erhe::Typed` item - `Scope`, the `Xformable`s (`Xform`, `Mesh`, `Camera`, `Light`), and every content-library resource (`Material`, `Brush`, `Style`, textures, graph assets, physics resources, ...) - is a prim, and any prim may be the child of any other prim (`doc/erhe/usd_compatibility_design.md` C5). A content library kind scope (`Materials`, `Brushes`, ...) is where a new resource is placed by default and is an ordinary, movable `Scope`. The Scene header row is not a prim. Drops follow "move wins, modifier acts" (`Item_tree::drag_and_drop_target`):

- **Move.** A prim row dragged from any scene's hierarchy and dropped on a prim row moves: the top third of the target row places it as the sibling before the row, the middle third as the row's last child, the bottom third as the sibling after the row, each zone with its own preview. When the dragged row is selected, the whole selection moves (selected prims whose ancestor is selected move with that ancestor). A move is `Item_parent_change_operation` or `Item_reposition_in_parent_operation`, one compound per drop; `erhe::Typed::handle_item_host_update` carries a prim moved to another scene over to that scene's content library.
- **Action (Alt held).** While Alt is held, the action the dragged prim has on the hovered row is offered in place of the move. The actions, in the order they are considered:
  - a `Graph_mesh` of the node's own scene onto a node: bind it by writing the node's `Geometry_graph_mesh.graph_mesh` value (whole row);
  - a `Material` onto a node holding a mesh with primitives: assign it to every primitive (whole row);
  - a `Material` onto a brush of this scene: fork the brush with that material (whole row);
  - a `Brush` onto any prim: place a brush instance before / under / after the prim (three zones);
  - a `Material` another scene's library holds onto any prim of this scene: copy it into this scene's library as the prim's last child (whole row).
  Where none applies to the pair, the move zones stay offered, so Alt never takes a drop away.

  A placed brush instance or prefab instance gets the identity local transform under the prim it joins: its world transform is that of the nearest `Xformable` at or above that parent, identity when there is none.
- **Payloads that are not prim rows** act regardless of modifier: an inventory slot onto any prim places its brush (with the slot material, three zones) or, for a material-only slot, assigns the material to the mesh of the node it is dropped on; an Asset Browser glTF file onto the Scene header row instantiates its prefab as the scene root's last child, and onto any prim before / under / after it; an Asset Browser texture file onto any row of the scene imports it (`import_texture_into_scene`) as the hovered prim's last child, or into the Textures scope when the row is not a prim.

The row context menu follows the same model. "Create" is offered on every prim row and on the Scene header row, where it creates under the scene root node (`Scene_root::make_browser_window`). It lists every creatable kind - Xform, Scope, Mesh, Camera, Light, Rendertarget, Material, Physics Material, Collision Filter, Physics Joint Settings, Style, Graph Texture, Graph Mesh - and each entry inserts the new item, undoably, as the last child of the clicked prim (`Scene_commands::create_new_*`); the kind scopes are only where a resource created without a parent lands. The whole menu is disabled, with the refusal as its tooltip, under a reference instance. On any prim row - node, scope, kind scope or resource - Copy, Cut and Duplicate are offered when the item is clonable, and Paste inserts the clipboard contents as the row's last children. A copy is a clone, so the `erhe::Item_kind::not_clonable` kinds (`erhe::graphics::Texture`, `Brush`, the graph assets `Graph_texture` / `Graph_mesh`) offer Delete and Copy Path but not Copy, Cut or Duplicate, and a clone of a subtree leaves such descendants out. Rows that are not prims (Asset Browser file rows) offer Delete and Copy Path only. Cut, Duplicate and Paste respect instance structure protection like the drops below.

Reference instance structure protection applies to every drop (`prefabs/instance_structure.hpp`, `doc/erhe/usd_compatibility_design.md` X2): a move of a protected prim or under a refusing parent is refused and logged at drop, and the brush, glTF, texture and material-copy drops are not offered where the parent refuses children.

## Public API / Integration Points

- `Viewport_window::viewport_scene_view()` -- access the associated scene view
- `Viewport_window::on_mouse_move()` -- called to update pointer position
- `Properties::imgui()` -- draws property editors for current selection
- `Settings_window` communicates changes via `App_message_bus`

## Dependencies

- erhe::imgui, erhe::rendergraph, erhe::scene, erhe::primitive
- editor: App_context, App_message_bus, Scene_view, Viewport_scene_view, Content_library
