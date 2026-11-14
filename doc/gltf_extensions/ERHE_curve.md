# ERHE_curve

Stability: experimental

## Scope

**Node** extension. Optional (`extensionsUsed` only).

## Overview

Marks a glTF node as an `erhe::scene::Curve` prim and carries its curve path
(`doc/editor/curves.md`, `doc/erhe/math.md` "Curve path"): an ordered list of
control points in the node's own frame and whether the curve is closed. The
node's name, transform, children and `ERHE_node` payload are the prim's as for
any node. A reader without this extension sees an empty node.

The writer never gives a curve node a mesh, a camera or a light. A reader
that finds the extension on a node carrying one of them reads the node as
that prim and logs that the curve was not read.

## JSON layout

```json
{
    "closed": false,
    "points": [
        {"position": [0, 0, 0], "in": [-0.25, 0, 0.25], "out": [0.25, 0, -0.25], "kind": "smooth"},
        {"position": [1, 0, 0], "in": [0, 0, 0],        "out": [0, 0, 0],         "kind": "corner"}
    ]
}
```

- `closed`: `true` makes a span from the last point back to the first.
- `points`: the control points in order.
  - `position`: the point in the node's frame.
  - `in`, `out`: the incoming and outgoing handle, each an offset from
    `position`. A corner ignores both; they are written as stored.
  - `kind`: `corner` (no handles: the neighboring spans leave the point
    straight), `smooth` (the handles stay on one line) or `free`.
- Every number is the shortest decimal that reads back to the same 32-bit
  float, and a negative zero is written `-0.0`, so a save and a load
  reproduce every value bit for bit.

Every key is required and no other key is allowed; a value that does not
read (a missing or unknown key, a vector of the wrong length, a number that
is not a finite float, an unknown kind) is logged with the node and the node
loads as an `Xform`.

## Schema

[schema/ERHE_curve.schema.json](schema/ERHE_curve.schema.json)
