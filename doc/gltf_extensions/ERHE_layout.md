# ERHE_layout

Stability: mostly stable

## Scope

**Node** extension. Optional (`extensionsUsed` only).

## Overview

Carries the node's erhe `Layout` attachment (the node arranges its direct
children inside a volume) with the attachment's persistent Item flags
(see [flags.md](flags.md)) and local property values.

A child's per-child hints (alignment, margins, grid cell and span) are
attached properties registered by `Layout` and set on the child node
(`doc/erhe/property_system.md` section 4.14), so they ride the child's
[`ERHE_node`](ERHE_node.md) `properties` map by their qualified names
(`Layout.align_x` .. `Layout.grid_span`); this extension carries nothing
for them.

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
        "flags": ["content", "show_in_ui", "show_debug_visualizations"],
        "properties": {}
    }
}
```

- `layout.type`: `stack` | `grid` | `flow`.
- `layout.primary` / `secondary` / `tertiary`: signed axis names
  `pos_x` | `neg_x` | `pos_y` | `neg_y` | `pos_z` | `neg_z`.
- `layout.grid_track_extent_{x,y,z}`: per-track extents; empty array =
  uniform tracks.
- `layout.properties`: the attachment's local property values by name
  (`doc/erhe/property_system.md` D23), the layout's complete local set: the
  explicit fields above are the effective values for readers without the
  property system, and a field the map does not name holds no local
  value after the load, so a value inherited from the node
  (`ERHE_node` `properties`, `Layout.gap`) or a style inherits again.

## Load semantics

Creates the attachment on the node. Nodes inside prefab-instance
subtrees are never written (the instance root exports as an external-asset
reference), matching every other per-node pass.

A legacy `layout_item` sub-object (`align` as three alignment names
`negative` | `positive` | `stretch`, `margin_min`, `margin_max`,
`grid_cell_auto`, `grid_cell`, `grid_span`), written by files that predate
the attached properties, loads as the corresponding `Layout.*` values on
the node; its `name` and `flags` are dropped. It is never written.

## Schema

[schema/ERHE_layout.schema.json](schema/ERHE_layout.schema.json)
