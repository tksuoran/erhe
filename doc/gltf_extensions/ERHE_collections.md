# ERHE_collections

## Scope

**Asset-root** extension (the top-level `extensions` object). Optional
(`extensionsUsed` only).

## Overview

Named collections of node references (USD UsdCollectionAPI-inspired, see
doc/usd_compatibility.md). Initially used to persist item TAGS: each
distinct tag becomes a collection named after it whose items are the glTF
node indices of the tagged nodes. Tags were runtime-only before this
extension (never persisted by any format), so this is net-new persistence.

Later uses of the same mechanism (selection sets, render-layer-like
groupings) may add fields; readers ignore fields they do not understand.

## JSON layout

```json
{
    "collections": [
        {"name": "enemy", "items": [4, 7, 9]},
        {"name": "spawn_point", "items": [12]}
    ]
}
```

- `name`: collection name (= the tag string).
- `items`: sorted glTF node indices.

Collections are sorted by name; a tag with no exported carrier nodes is
not written.

## Load semantics

Each listed node gets the collection's name added to its tag set.

## Schema

[schema/ERHE_collections.schema.json](schema/ERHE_collections.schema.json)
