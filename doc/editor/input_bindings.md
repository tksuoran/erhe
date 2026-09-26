# Input bindings

Stability: mostly stable

The user edits which key, mouse or controller input calls which editor command,
and the editor keeps those edits across runs. The binding model itself - default
bindings declared in code, per-command overrides, input kinds, the
`Binding_desc` text form and the deferred rebuild of the dispatch tables - is
`erhe::commands` and is described in [../erhe/commands.md](../erhe/commands.md).
This document covers the editor side: persistence and the MCP tools.

## Persistence

`Input_bindings_store` (`src/editor/input_bindings_store.{hpp,cpp}`) owns
`config/editor/input_bindings.json` (codegen struct `Input_bindings_config`,
`src/editor/config/definitions/input_bindings_config.py`):

```json
{
    "overrides": [
        { "command": "Fly_camera.move_forward", "bindings": ["key:any+i:any", "key:ctrl+up"] },
        { "command": "Geometry.Triangulate",    "bindings": ["key:ctrl+shift+t"] }
    ]
}
```

- Only commands the user has edited are listed, each with its full binding
  list (an empty list unbinds the command). Every other command follows the
  defaults its code declares, so a default changed in code reaches existing
  users; an edited command keeps the user's list until it is reset.
- The file is per user (gitignored) and does not exist until the first edit.
  Desktop and OpenXR sessions share it: OpenXR action bindings are not
  editable.
- `Editor` constructs the store next to `Commands` and calls `load()` right
  after `fill_app_context()`, when every part has registered its commands and
  declared its defaults. The overrides take effect at the first frame's
  `Commands::tick()`.
- Change sites call `save()` right after editing `Commands`; the file is
  written once per edit and nothing polls for changes.
- Entries naming a command this build does not register are kept and written
  back unchanged; entries whose bindings do not match the command's input
  kind are dropped with a warning in `logs/log.txt`, as are unparseable
  binding strings.
- An AI-driven run (`ERHE_AI_DRIVER=1`) neither reads nor writes the file, so
  scripted runs always see the default bindings.

## MCP tools

`src/editor/mcp/mcp_server_input_bindings.cpp`, schemas in
`config/editor/mcp_tools.json`:

- `list_input_bindings {filter?}` - every user-bindable command: `command`,
  `input_kind`, `defaults`, effective `bindings`, `modified`, and `conflicts`
  (`binding`, `other_command`) as of the last rebuild.
- `set_command_bindings {command, bindings}` - replaces the command's bindings
  (text form), refuses a binding of the wrong input kind, saves.
- `reset_command_bindings {command | all}` - back to the defaults, saves.

`Mcp_test.set_command_bindings_replaces_the_default_chord` rebinds Undo to
Ctrl+U through these tools and checks the chords with `key_press`.
