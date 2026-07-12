# ERHE_layout

## Scope

**Node** extension. Optional (`extensionsUsed` only).

## Overview

Carries the node's erhe layout attachments: a `Layout` (the node arranges
its direct children inside a volume) and/or a `Layout_item` (per-child
parameters for a parent layout). Two optional sub-objects on one
extension; a node may have either or both. Each sub-object carries its
attachment's persistent Item flags (see [flags.md](flags.md)).

## JSON layout

```json
{
    "layout": {
        "name": "Shelf",
        "type": "grid",
        "volume_min": [-0.5, -0.5, -0.5],
        "volume_max": [0.5, 0.5, 0.5],
        "primary": "pos_x",
        "secondary": "pos_y",
        "tertiary": "pos_z",
        "gap": [0.1, 0, 0],
        "grid_track_count": [3, 2, 1],
        "grid_track_extent_x": [1, 2, 1],
        "grid_track_extent_y": [],
        "grid_track_extent_z": [],
        "flags": ["content", "show_in_ui", "show_debug_visualizations"]
    },
    "layout_item": {
        "name": "Layout item",
        "align": ["negative", "stretch", "positive"],
        "margin_min": [0, 0, 0],
        "margin_max": [0, 0.05, 0],
        "grid_cell_auto": false,
        "grid_cell": [1, 0, 0],
        "grid_span": [2, 1, 1],
        "flags": ["content", "show_in_ui"]
    }
}
```

- `layout.type`: `stack` | `grid` | `flow`.
- `layout.primary` / `secondary` / `tertiary`: signed axis names
  `pos_x` | `neg_x` | `pos_y` | `neg_y` | `pos_z` | `neg_z`.
- `layout.grid_track_extent_{x,y,z}`: per-track extents; empty array =
  uniform tracks.
- `layout_item.align`: per-axis `negative` | `positive` | `stretch`.

## Load semantics

Creates the attachment(s) on the node. Nodes inside prefab-instance
subtrees are never written (the instance root exports as an external-asset
reference), matching every other per-node pass.

## Schema

[schema/ERHE_layout.schema.json](schema/ERHE_layout.schema.json)
