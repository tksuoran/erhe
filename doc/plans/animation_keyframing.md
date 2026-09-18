# LightWave-style keyframing / timeline plan (issue #243 follow-up)

Goal: extend the Animation window (curve editor + Animation_player, added for
issue #243) with a LightWave-Layout-like keyframing workflow: a scrubbable
timeline strip with key markers, autokey, manual key creation for selected
objects, and automatic creation of the needed animation channels.

Reference: LightWave 2025 "Animating" and "DopeTrack" documentation.
LightWave's model, briefly:

- A frame slider under the viewport is the timeline; dragging it moves the
  current time. Key markers for the *currently selected item* are drawn on it
  as pale yellow vertical lines; user-placed named markers are scene-level
  and always visible.
- The DopeTrack is a thick bar directly above the frame slider that expands
  into a mini dope sheet: swipe-select keys, drag to move, Alt-drag to copy,
  right-click menu (copy/paste/delete/bake zones), and a channel-edit mode
  that isolates individual channels.
- "Create Key" (Return) opens a dialog: frame, scope ("Selected Items",
  "Current Item Only", "Current Item and Descendants", "All Items") and
  channel toggles (Position / Rotation / Scale). "Delete Key" mirrors it.
- Auto Key is a persistent mode at the bottom of the UI:
  - Off - nothing is created or modified.
  - Modified - keys are created/modified only on the channels actually
    edited (move -> translation keys only).
  - All Channels - any edit keys all motion channels.
  - Existing - only adjusts keys that already exist, never creates.
  - Fixed - keys land on a fixed configured frame.

How the same ideas appear elsewhere (for calibration):

- Blender: Timeline editor shows the playhead plus key diamonds for the
  *selected objects*; Auto Keying is a record toggle (with "only replace
  existing" and keying-set options); `I` inserts keys with a channel choice;
  Dope Sheet / Graph Editor have "only show selected" filters; scene markers
  are separate from keys.
- Maya: Time Slider draws red ticks for the selected object's keys; Auto
  Keyframe by default only keys attributes that were keyed at least once
  (LightWave "Existing" flavor), Set Key (`S`) keys everything keyable.
- 3ds Max: big red Auto Key toggle tints the UI while active; the track bar
  under the time slider shows the selected object's keys and allows
  move/copy; Set Key mode is the deliberate alternative with channel filters.
- Unity: the Animation window's record toggle auto-keys any property you
  touch and *auto-creates the needed curves/bindings* - the closest match to
  "automatically create necessary animation channels".

## Fit with the existing erhe pieces

Already in place after #243:

- `Animation_player`: single active animation, current time, transport;
  applied every frame from `Editor::tick()`; `seek()` re-poses the scene.
- `Animation_window`: curve canvas with ruler scrubbing, channel lists,
  undoable keyframe move/insert/delete via `animation_edit` helpers +
  `Animation_edit_operation`.
- `Transform_tool::record_transform_operation()`: the single funnel where a
  finished interactive transform becomes an undoable operation - the natural
  autokey hook.
- `Selection` for "selected objects"; `App_message_bus` for events.

glTF constraint worth stating up front: a glTF channel keys a whole path
(translation vec3 / rotation quat / scale vec3). Unlike LightWave's nine
per-axis channels (X/Y/Z/H/P/B/SX/SY/SZ), we cannot key one axis of a path
independently. So "Auto Key: Modified" maps to *modified paths* (moved ->
translation channel), not per-axis. This keeps the data 1:1 exportable via
the existing glTF export.

## Proposed pieces

### 1. Timeline strip (frame-slider + DopeTrack-lite)

A full-width horizontal strip in the Animation window between the transport
row and the channel/curve split (and, later, an optional slim standalone
dockable "Timeline" so it can live under the viewport like LightWave's frame
slider even when the curve editor is closed).

- Playhead: draggable ("move timeline"); shares `Animation_player` time with
  the curve canvas ruler, so both stay in sync for free. Click anywhere =
  seek.
- Tick labels in seconds, with an optional frames display (configurable fps,
  default 30) since LightWave-style UX is frame-centric while glTF is
  seconds-based.
- Key markers: thin vertical lines (LightWave pale-yellow style), drawn from
  the union of key timestamps of the channels chosen by the marker-visibility
  filter (below).
- Scene markers: named, user-placed (Shift+double-click, like the DopeTrack),
  always visible regardless of selection; stored per animation (or per scene,
  to be decided when implemented); highlighted when the playhead reaches
  them.
- Later (DopeTrack proper): swipe-select keys on the strip, drag to move,
  Alt-drag to copy, right-click menu - all of which can reuse the existing
  `animation_edit` + `Animation_edit_operation` machinery unchanged.

### 2. Key-marker visibility filter

A small dropdown on the timeline strip choosing what the markers show:

- Selected objects (default; LightWave / Maya / Max behavior): keys of
  channels whose target node is in the current selection.
- Active animation: keys of all channels of the player's animation.
- Shown channels: keys of exactly the curves visible in the curve editor
  (drives the strip from the channel-pane filters).

### 3. Autokey

A mode selector (not just a toggle), persistent in the timeline strip +
editor settings, mirroring LightWave's proven set minus the exotic ones:

- Off
- Modified paths (default when enabled) - keys only the paths the edit
  changed (translation moved -> translation key).
- All paths - any edit keys T, R and S.
- (Later, if wanted: Existing = modify-only, Maya-style.)

Hook: at the end of `Transform_tool::record_transform_operation()`, when
autokey is active and an animation is targeted, compare before/after TRS per
affected node, and for each changed path write a key at the current player
time through a new `set_or_insert_key(node, path, time, value)` helper:

- If the channel exists: insert (or overwrite the value of) a key at the
  current time - the existing `insert_keyframe` / `set_keyframe_value` paths.
- If not: auto-create the channel (below).
- The resulting `Animation_edit_operation` (+ possible channel-creation op)
  is compounded with the `Node_transform_operation` via the existing
  `Compound_operation`, so one undo removes both the transform and the key.

### 4. Manual "Create Key" / "Delete Key"

LightWave's Return-key dialog, simplified to erhe idioms: a command +
toolbar button ("Key" icon) and hotkey that keys the *selected objects* at
the current time; a small popup offers scope (selected / all animated items)
and path toggles (Position / Rotation / Scale), remembering last choices.
Delete Key mirrors it (removes keys at the current time for the same scope).
Both run through the same helper as autokey, so undo behaves identically.

### 5. Automatic channel (and animation) creation

`ensure_channel(animation, node, path)` helper:

- No channel for (node, path): create an `Animation_sampler` (LINEAR) and an
  `Animation_channel`, seeded with one key at the current time holding the
  node's current TRS value for that path.
- No active animation at all: create a new `Animation` item ("Animation") in
  the scene's content library, target the player/window at it, then proceed.
- Undoable: a new `Animation_channel_add_operation` (samplers/channels
  arrays change shape, which `Animation_edit_operation`'s sampler snapshots
  do not cover); channel creation composes with the key write in one
  compound.
- The channel pane picks new channels up automatically (it re-reads
  `animation->channels` every frame); `ensure_visibility_size()` already
  tolerates channel-count changes.

### 6. Small supporting changes

- `Animation_player::get_time()` is the single source of "current time" for
  all keying; the timeline strip, curve ruler and transport time box already
  share it.
- MCP: `animation_create_key` / `animation_delete_key` (+ autokey mode knob
  in `animation_playback`) so the whole workflow is testable headless.
- Editor settings: autokey mode + fps-display persisted in
  `editor_settings.json`.

## Suggested implementation order

1. Timeline strip with playhead + key markers + visibility filter (pure UI
   over existing data).
2. `ensure_channel` / `set_or_insert_key` helpers + channel-add operation +
   manual Create/Delete Key command and toolbar UI.
3. Autokey modes hooked into `record_transform_operation()`, compounded
   undo.
4. Scene markers on the strip.
5. DopeTrack-style key manipulation on the strip (select/move/copy) - reuses
   the curve editor's edit machinery.
6. Optional standalone slim Timeline dock under the viewport.
