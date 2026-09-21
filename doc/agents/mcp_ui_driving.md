# Driving the editor user interface over MCP

Stability: mostly stable

Run-book for an AI coding agent that needs the editor's interactive entry
points - menus, docking, property rows, viewport gestures, tool handles -
rather than its scene-scripting tools. The tools live in
`src/editor/mcp/mcp_server_ui.cpp`, their schemas in
`config/editor/mcp_tools.json`, and the server itself is described in
`doc/agents/mcp_server_usage.md`.

Reach for these tools to exercise and verify UI behavior. Set a parameter a
scene-scripting tool exposes with that tool instead; see
[mcp_api_guidelines.md](mcp_api_guidelines.md).

## The loop

1. **Inspect.** `get_imgui_windows` for the window set and their rectangles,
   `get_imgui_items` for what one frame submitted, `get_viewports` for the 3D
   views, `get_transform_handles` for the gizmo.
2. **Resolve.** `get_imgui_item_rect` turns a window plus a label into a
   rectangle and a center, or take `center_x` / `center_y` straight off a
   `get_imgui_items` entry.
3. **Act.** `imgui_click` / `imgui_hover` / `imgui_scroll` resolve and act in
   one call; `mouse_click` / `mouse_drag` / `mouse_wheel` / `key_press` /
   `type_text` act at coordinates.
4. **Confirm.** `capture_screenshot` (optionally with
   `annotate_imgui_items`), and the ordinary queries - `get_selection`,
   `get_item_properties`, `get_node_details`, `get_four_views` - for the state
   the gesture was supposed to change.

## Coordinate space

One space throughout: editor window pixels, origin at the top left, the space
a `capture_screenshot` PNG is in. `get_viewports`, `get_imgui_windows`,
`get_imgui_items`, `get_imgui_item_rect` and `get_transform_handles` all
report rectangles in it, and every `x` / `y` argument takes it.

## Addressing an item

- `window` is optional. Give it to disambiguate; omit it to search every
  window of the host, which is how a menu popup is reached (ImGui names those
  itself, `Window###Menu_00`).
- A window is named by the name or the `display_name` `get_imgui_windows`
  reports, and naming a window also reaches the items of its child regions (a
  scrolling region is a child window).
- `label` is the label as shown, or the raw label with its `##id` part.
  `display_label` is the shown part; matching accepts either spelling.
- `index` picks between items sharing a display label in one window, numbered
  in submission order the way `get_imgui_items` numbers them. It counts only
  the items the `visible_only` rule kept, so it shifts when a matching item
  scrolls out of view - address a row by `id`, or by its position in the
  `get_imgui_items` list, when several rows share a label.
- `id` (the ImGui id `get_imgui_items` reports) addresses an item exactly.
- An unresolvable or ambiguous selector is an error naming what it found.
- Only the desktop host is driven by `imgui_click` / `imgui_hover` /
  `imgui_scroll`. A rendertarget host (the hotbar and the other ImGui surfaces
  drawn into the scene) is inspected with the queries and driven through the
  viewport pixels that show it.

## How an item gets a label

Dear ImGui reports the label it was handed. A widget that draws its own text
hands ImGui `"##something"` and would be recorded unnamed, so erhe names it:

- `erhe::imgui::set_item_debug_label(label)` names the item submitted last
  (an item-tree row does this with the name it draws).
- `erhe::imgui::set_recorded_item_labels(first_index, label)` names a run of
  items, bracketed by `erhe::imgui::get_recorded_item_count()`. A property row
  does this with the name the row shows: one widget takes the row's name,
  several take `<name>.x`, `<name>.y`, `<name>.z`, `<name>.w` and
  `<name>.<position>` past the fourth, so `Translation.x` is one drag field.

Both cost one branch in a frame nobody asked to record, and both build their
text only while `erhe::imgui::is_item_recording()` holds. When a widget turns
out to be unaddressable, name it at the place it is submitted with one of
these rather than clicking at a guessed offset. See
[../erhe/imgui.md](../erhe/imgui.md) for the recorder.

## Tools

| Tool | Arguments | What it does |
| --- | --- | --- |
| `get_imgui_hosts` | - | The ImGui contexts (desktop host, rendertarget hosts): name, default flag, whether it renders, display size, window count, whether it holds a recorded frame, `hook_calls_total` |
| `get_imgui_windows` | `host` | Windows of one host: name, `display_name`, id, rectangle, active / hidden / collapsed / child / docked / `dock_id` / focused |
| `get_imgui_items` | `host`, `window`, `label_contains`, `visible_only`, `limit` | Items of one recorded frame: id, `label`, `display_label`, window, rectangle and center, `index`, status (`visible`, `hovered`, `active`, `edited`, `checkable`, `checked`, `openable`, `opened`, `inputable`, `disabled`). Reports `total` matched and the first `limit` |
| `get_imgui_item_rect` | `host`, `window`, `label` \| `id`, `index`, `visible_only` | One item's rectangle, center, status and `match_count` |
| `imgui_click` | item selector, `button`, `modifiers`, `double` | Resolve, then click the item's center; returns `target` |
| `imgui_hover` | item selector, `modifiers` | Resolve, then leave the pointer on the item's center; returns `target` |
| `imgui_scroll` | `window` \| item selector, `dx`, `dy`, `modifiers` | Resolve, then turn the wheel over it; positive `dy` scrolls up |
| `mouse_click` | `x`, `y`, `button`, `modifiers`, `double` | Move, settle, press and release without moving |
| `mouse_drag` | `from`, `to`, `button`, `modifiers`, `frames`, `hold` | Move to `from`, settle, press, `frames` interpolated moves carrying `dx` / `dy`, release at `to` unless `hold` |
| `mouse_release` | `button` | End a held drag where the pointer is |
| `mouse_wheel` | `x`, `y`, `dx`, `dy`, `modifiers` | Move, settle, turn the wheel; positive `dy` zooms in over a viewport |
| `key_press` | `key`, `modifiers`, `hold_frames` | Press a key with its modifiers, hold, release. `key` is an `erhe::window::Keycode` by name (`z`, `left shift`, `Key_f5`; case and `_` insensitive) or value |
| `type_text` | `text` | UTF-8 text events to whatever holds keyboard focus, one 31-byte chunk per frame |
| `inject_input_events` | `events` | The raw form: `erhe::window::Input_event` values with a `frame` offset each. Field names are the `*_event` member names; `modifiers` is a list of `ctrl` / `shift` / `super` / `menu` |
| `get_input_state` | - | Pointer position, held buttons, modifier mask, whether cursor-enter / focus were sent, and the stepping gesture if one runs |
| `get_transform_handles` | `viewport` | Per shown gizmo handle, a window point that picks it, the world point under it, its name and `handle_value`; plus the gizmo anchor and radius. Needs a selection |
| `capture_screenshot` | `path`, `annotate_imgui_items`, `annotate_window`, `annotate_limit` | The frame as a PNG; with annotation, numbered magenta rectangles over the recorded items and the number -> item table |

## Standing rules

- **A click does not move.** A move between press and release makes the
  gesture a drag, and an `erhe::commands` click binding then never fires. The
  gesture tools keep the pointer still across a click; a hand-built
  `inject_input_events` sequence must too.
- **Put the pointer over a viewport before a viewport key command.** Keyboard
  commands reach `erhe::commands` only while ImGui does not capture the
  keyboard. `mouse_click` inside the viewport rectangle first, then
  `key_press`.
- **Let the pointer settle.** Hover is computed once per frame from the
  pointer position, so a press acts on the previous frame's hover. The
  gesture tools spend three frames on this before a press or a wheel event
  (`c_pointer_settle_frames`); a raw event list must leave the same room.
- **One gesture at a time.** A second gesture call while one is stepping is an
  error. `get_input_state` reports the gesture that runs.
- **A gesture spans at most 120 frames** (`c_max_frame_offset`), because one
  deferred pass costs an editor frame and the MCP request timeout is five
  seconds. Split a longer drag into a held `mouse_drag` plus `mouse_release`.
- **Menus take two clicks.** A menu item exists only while its menu is open:
  `imgui_click` the menu bar item, then `imgui_click` the item in the popup.
- **`hovered` and `active` are read now, not then.** `get_imgui_items` reports
  the recorded frame's rectangles but the current frame's hovered and active
  ids, which is what a caller about to click wants.
- **A clipped item cannot be clicked.** `visible_only` keeps the items Dear
  ImGui did not clip; `imgui_scroll` the window until the target reports
  `visible`.
- **Opening the four view by label changes the tracked window set.** The four
  view's viewport windows are written to `config/editor/desktop_windows.json`
  when the editor exits, so restore that file with `git checkout` after a run
  that opened one.
- **The windowed build interleaves injected and real input.** Every tool works
  there, and a person at the keyboard is a second source of events; the
  headless build is the reproducible one.

## Verification

`ctest -C Debug -R "Mcp_"` from `build_vs2026_vulkan_headless` covers this
surface: a key chord reaching `erhe::commands`, a viewport press / move /
release picking a mesh, wheel zoom, a gizmo drag compared against
`drag_selection`, the Hierarchy rows listing and their centers being click
targets, a menu item opened by label, a text field typed into, a double click,
hover and scroll, the annotated screenshot table, and a Properties row edited
by label.
