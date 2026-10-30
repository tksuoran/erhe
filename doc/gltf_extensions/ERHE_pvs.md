# ERHE_pvs

Stability: experimental

## Contributors / status

erhe project. Draft vendor extension. The plan that introduces it is
[`../plans/pvs.md`](../plans/pvs.md).

## Dependencies

Written against glTF 2.0. Optional (`extensionsUsed` only, never
`extensionsRequired`). The column identities name nodes of the same asset
whose glTF 2.1 `uid` identifies them (`doc/editor/scene_serialization.md`).

## Scope

Asset root (the glTF root object's `extensions`).

## Overview

Stores a scene's potentially visible set (PVS) database: a uniform grid of
cells over a region of the scene and, per cell, one bit per baked draw
entry (a column) telling whether the entry can be seen from some point of
the cell. The in-memory form is `erhe::scene::Pvs_database`
(`doc/erhe/scene.md` "Potentially visible sets"). Cells are grouped into
pages of `page_size`^3 cells; the page is the storage unit, so a reader can
hold or stream single pages. Stock viewers ignore the extension.

A loaded database is `ready`; the state (`ready` / `stale`) is not stored.
A writer writes the extension only for a ready database.

## JSON layout

```json
{
    "version": 1,
    "scene_id": "20261030-101500-ab12",
    "grid_origin": ["-0x1.4p+1", "0x1.99999ap-4", "-0x1.555556p-2"],
    "cell_size": "0x1.19999ap+0",
    "cell_counts": [5, 2, 2],
    "page_size": 4,
    "column_count": 3,
    "bit_word_count": 64,
    "bake": {
        "face_resolution": 1024,
        "tile_size": 8,
        "layer_count": 64,
        "face_fov": "0x1.0c152ap+1",
        "samples_per_face": 1,
        "min_entry_pixels": "0x1p+1",
        "duration_seconds": "0x1.8b0a3ep+3"
    },
    "column_identities": [[0, 0], [1, 0], [1, 1]],
    "columns": 7,
    "pages": 8,
    "bits": 9
}
```

Every float is a float32 written as a C99 hexadecimal floating-point string
(`%a`), so it reads back bit for bit.

- `version`: integer, `1`. A loader rejects any other value.
- `scene_id`: the `Scene_settings::scene_id` of the scene the bake ran on.
- `grid_origin`: the minimum corner of cell (0, 0, 0), world space.
- `cell_size`: the edge length of a cell (positive).
- `cell_counts`: cells per axis, each at least 1. Cell (x, y, z) has index
  `x + cell_counts[0] * (y + cell_counts[1] * z)`.
- `page_size`: cells per page axis, 1 to 4 (a page holds at most 64
  cells). The page dimensions are `ceil(cell_counts / page_size)` per axis.
  Cell (x, y, z) is in page (x / page_size, y / page_size, z / page_size),
  page index `px + page_dims[0] * (py + page_dims[1] * pz)`, at local cell
  `lx + page_size * (ly + page_size * lz)` with `l = cell % page_size`.
- `column_count`: number of columns.
- `bit_word_count`: number of uint32 words in `bits`.
- `bake`: the parameters the bake ran with (`doc/plans/pvs.md` "Bake"):
  `face_resolution`, `tile_size`, `layer_count`, `samples_per_face`
  (integers), `face_fov` (radians), `min_entry_pixels` and
  `duration_seconds` (the bake's wall-clock time).
- `column_identities`: `column_count` pairs `[node, primitive]`: column `c`
  is mesh primitive `primitive` of the mesh of glTF node `node` of this
  asset. A loader resolves the node index to that node's identity (its
  `uid`); the node must carry one.
- `columns`, `pages`, `bits`: bufferView indices (below). `columns` is
  absent when `column_count` is 0 and `bits` when `bit_word_count` is 0
  (glTF requires `byteLength` >= 1).

## Payloads

All three are raw bufferViews without `byteStride` and without `target`,
little-endian. They are not accessors: no glTF accessor type describes a
column record or a page entry.

**`columns`**: `byteLength` exactly `80 * column_count`. Record `c` at
offset `80 * c`:

| Byte offset | Type | Value |
|---|---|---|
| 0 | float32[12] | the baked world transform: `world_from_node` as its 4 columns of 3 rows (x, y, z of each column, translation last) |
| 48 | float32[6] | the baked world bounds: min x, y, z, then max x, y, z |
| 72 | uint64 | `transform_hash` |

`transform_hash` is 64-bit FNV-1a (`erhe::hash::hash()`, offset basis
`0xcbf29ce484222325`, prime `0x100000001b3`, one byte at a time) over the
18 float32 values of bytes 0 to 71 in that order, their bits as stored.

**`pages`**: `byteLength` exactly `16 * page_count`. Entry `p` at offset
`16 * p`:

| Byte offset | Type | Value |
|---|---|---|
| 0 | uint32 | `word_offset`: the page's first word in `bits`, or `0xffffffff` when the page is not stored |
| 4 | uint32 | reserved, 0 |
| 8 | uint64 | `cells_with_pvs`: bit `local cell` set when that cell has a baked PVS |

A page that is not stored has no cell with a PVS: every cell of it reads
as "everything visible", and its `cells_with_pvs` is 0.

**`bits`**: `byteLength` exactly `4 * bit_word_count`. The stored pages
back to back, each exactly `page_size^3 * words_per_cell` uint32 words,
`words_per_cell = ceil(column_count / 32)`. A stored page's `word_offset`
is a multiple of its word count and no two pages share one. Local cell `l`
of a stored page holds its words at `word_offset + l * words_per_cell`;
column `c` is in the cell's PVS when bit `c % 32` of word `c / 32` is set.
A cell with a PVS has its bits past the last column clear. A cell of a
stored page without a PVS (no free sample point, not baked, or outside the
grid at the edge of the grid) holds all-ones words and its
`cells_with_pvs` bit is clear: reading it gives "everything visible"
without a special case.

## Writing

The writer emits the extension when the scene has a database and it is
ready. Each column's node identity is mapped to the node's glTF index in the
written asset; when a column names a node that is not in the export, the
writer logs an error and writes no `ERHE_pvs` at all (a database with a
dangling column would cull an entry it cannot name).

## Validation / fallback

The loader rejects the extension, and the scene opens without a database,
when any of these fails:

- `version` is 1; every member above is present with its type, every float
  string parses as a float32 exactly;
- `cell_size` is positive and finite, `grid_origin` finite, every cell
  count at least 1, `page_size` 1 to 4;
- `column_identities` has `column_count` entries, each naming an existing
  node that carries a `uid`, and no identity repeats;
- each bufferView exists, has no `byteStride` and has the stated
  `byteLength`;
- each column's `transform_hash` equals the hash of its record;
- every page entry's reserved word is 0; a page that is not stored has
  `cells_with_pvs` 0; a stored page's `word_offset` is a multiple of the
  page's word count, unique, inside `bits`, and its `cells_with_pvs` names
  only local cells inside the page and inside the grid;
- `bit_word_count` is the stored page count times the page's word count;
- the padding bits of every cell with a PVS are clear and every stored
  cell without one holds all-ones words.

## Schema

[schema/ERHE_pvs.schema.json](schema/ERHE_pvs.schema.json)
