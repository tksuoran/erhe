# config/

## Purpose

Editor configuration loading. Each library's config is loaded from a separate JSON file
in the `config/` directory (e.g. `config/<app_name/erhe_graphics.json`, `config/<app_name>/window.json`).

## Key Types

- **Generated config structs** (`config/generated/`) -- Individual configuration structs
  produced by `erhe_codegen` from Python definitions in `config/definitions/`. Each has
  JSON serialization/deserialization support.

- **`Editor_settings_config`** -- Aggregates runtime-editable settings (camera controls,
  grid, headset, hotbar, HUD, etc.) loaded from `editor_settings.json`.

## Public API / Integration Points

- `load_config<Graphics_config>("config/<app_name>/erhe_graphics.json")` -- per-library config loading
- Individual config structs are stored in the `Editor` class and pointers placed in `App_context`
- Individual config structs are passed to subsystem constructors

## Notes

- `editor_settings.json` is written by the running editor, and settings only materialize
  into it after the editor has run **with a scene open**. To exercise a stored setting
  headlessly: run once with a scene open, kill the editor, edit the JSON, run again. The
  file is user experiment state -- treat edits to it as destructive.
- A stored value always beats a changed default. Lowering a default in
  `config/definitions/` has no effect on a machine whose JSON already holds the old value;
  bump the struct's `_version` (with the codegen migration) if the new default must win.

## Dependencies

- erhe_codegen (generates the config structs)
- simdjson (JSON parsing)
