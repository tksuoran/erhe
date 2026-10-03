# LightWave-style keyframing / timeline: outstanding work (issue #243 follow-up)

Status: in progress

This plan extends `doc/editor/editor.md` (the `animation/` part, which
describes the built keyframing: timeline strip with key markers and marker
filter, Autokey off / modified / all, Create / Delete Key, automatic channel
and animation creation, MCP keying tools) and `doc/erhe/scene.md`
("Animation playback").

Reference: LightWave 2025 "Animating" and "DopeTrack" documentation. The
LightWave pieces still to match are the frame-centric time display, named
scene markers, the DopeTrack key manipulation and a frame slider that lives
under the viewport.

glTF constraint: a glTF channel keys a whole path (translation vec3 /
rotation quat / scale vec3), so every keying mode below works per path, never
per axis (LightWave's X/Y/Z/H/P/B/SX/SY/SZ channels do not apply).

## Outstanding pieces

### 1. Timeline strip: frames display

Tick labels and the playhead knob show seconds only. Add an optional frames
display with a configurable fps (default 30): LightWave-style UX is
frame-centric while glTF is seconds-based.

### 2. Keying: command, hotkey and scope

Create Key / Delete Key exist only as the Animation window's "+ Key" /
"- Key" buttons (and the MCP tools) acting on the active scene's selected
nodes. Outstanding:

- Register both as editor `Command`s with default input bindings, so they
  work with the Animation window closed.
- Scope choice, as in LightWave's Create Key dialog: selected items (current
  behavior), current item and descendants, all animated items. Remember the
  last choice.

### 3. Autokey: further modes and persistence

- "Existing" mode (Maya default / LightWave Existing): modify keys that
  already exist on the edited paths, never create keys or channels.
- Autokey with no targeted animation keys nothing. Decide whether it should
  instead create and target an animation, as "+ Key" does (Unity record
  behavior).
- Persist the autokey mode and the frames-display setting (item 1) in the
  editor settings; the mode lives only in `Animation_player` for the session.

### 4. Scene markers on the strip

Named, user-placed markers (Shift+double-click, like the DopeTrack), always
visible regardless of selection, highlighted when the playhead reaches them.
Decide when implementing whether they are stored per animation or per scene,
and how they round-trip through glTF save / load.

### 5. DopeTrack-style key manipulation on the strip

Swipe-select keys on the strip, drag to move, Alt-drag to copy, right-click
menu (copy / paste / delete). Reuses the curve editor's `animation_edit`
helpers and `Animation_edit_operation` unchanged.

### 6. Standalone timeline dock

An optional slim dockable "Timeline" window holding the strip and the keying
toolbar, so it can sit under the viewport like LightWave's frame slider while
the curve editor is closed.

## Suggested order

1, 2 and 3 are independent and small. 4 precedes 5 (both add strip
interaction). 6 comes last, once the strip's contents are settled.
