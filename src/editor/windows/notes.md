# windows/

## Purpose

ImGui window implementations for the editor UI, including viewport display, property inspection, settings, and configuration.

## Key Types

- **`Viewport_window`** -- ImGui window that displays a `Viewport_scene_view`. Handles viewport toolbar, mouse position tracking, hover info updates, drag-and-drop for brushes, and a navigation gizmo (`ImViewGuizmo`). Subscribes to `Open_scene_message`. Delegates rendering to the render graph.

- **`Properties`** -- Property inspector window. Displays editable properties for selected items: cameras, lights, meshes, materials, skins, animations, textures, geometry, rendertargets, node physics, and brush placements. Extends `Property_editor` for edit state tracking (clean/dirty).

- **`Property_editor`** -- Base class providing `Editor_state` (clean/dirty) tracking for property editing workflows.

- **`Settings_window`** -- Application-wide settings UI for graphics presets, physics, icons, and ImGui configuration. Can trigger graphics settings changes via `App_message_bus`.

- **`Viewport_config_window`** -- Configures rendering options for a viewport (grid, shadows, selection outline, etc.).

- **`Scene_view_config_window`** -- Configures scene view settings.

- **`Item_tree_window`** -- Generic tree view window used for both scene hierarchy browsing and content library browsing. Supports drag-and-drop, context menus, and custom item callbacks.

## Public API / Integration Points

- `Viewport_window::viewport_scene_view()` -- access the associated scene view
- `Viewport_window::on_mouse_move()` -- called to update pointer position
- `Properties::imgui()` -- draws property editors for current selection
- `Settings_window` communicates changes via `App_message_bus`

## Dependencies

- erhe::imgui, erhe::rendergraph, erhe::scene, erhe::primitive
- editor: App_context, App_message_bus, Scene_view, Viewport_scene_view, Content_library
