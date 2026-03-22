# config/

## Purpose

Editor configuration loading and the aggregated `Editor_config` struct.

## Key Types

- **`Editor_config`** -- Top-level struct aggregating all configuration sub-structs: camera controls, developer mode, graphics, grid, headset, hotbar, HUD, ID renderer, mesh memory, network, physics, renderdoc, renderer, scene, shader monitor, text renderer, threading, thumbnails, transform tool, viewport, and window.

- **Generated config structs** (`config/generated/`) -- Individual configuration structs produced by `erhe_codegen` from Python definitions. Each has JSON serialization/deserialization support.

- **`load_editor_config()`** / **`save_editor_config()`** -- Load/save `Editor_config` from/to a JSON file (default: `erhe.json`).

## Public API / Integration Points

- `load_editor_config("erhe.json")` -- called at startup
- `Editor_config` is stored in the `Editor` class and pointer is placed in `App_context::editor_config`
- Individual config structs are passed to subsystem constructors

## Dependencies

- erhe_codegen (generates the config structs)
- simdjson (JSON parsing)
