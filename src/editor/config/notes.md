# config/

## Purpose

Editor configuration loading. Each library's config is loaded from a separate JSON file
in the `config/` directory (e.g. `config/erhe_graphics.json`, `config/window.json`).

## Key Types

- **Generated config structs** (`config/generated/`) -- Individual configuration structs
  produced by `erhe_codegen` from Python definitions in `config/definitions/`. Each has
  JSON serialization/deserialization support.

- **`Editor_settings_config`** -- Aggregates runtime-editable settings (camera controls,
  grid, headset, hotbar, HUD, etc.) loaded from `editor_settings.json`.

## Public API / Integration Points

- `load_config<Graphics_config>("config/erhe_graphics.json")` -- per-library config loading
- Individual config structs are stored in the `Editor` class and pointers placed in `App_context`
- Individual config structs are passed to subsystem constructors

## Dependencies

- erhe_codegen (generates the config structs)
- simdjson (JSON parsing)
