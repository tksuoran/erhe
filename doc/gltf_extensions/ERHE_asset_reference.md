# ERHE_asset_reference

## Scope

**Top-level object** extension, initially emitted on **materials** only.
Optional (`extensionsUsed` only). Status: DRAFT - specified ahead of
implementation (asset system plan, wire-format phase); not yet emitted or
parsed by erhe.

## Overview

Marks a top-level object as a **proxy**: its authoritative definition
lives in another glTF file, identified by a glTF 2.1 `files` array entry
plus a glTF 2.1 unique ID (`uid`). The proxy's inline properties are the
graceful-degradation fallback for loaders that do not know the extension -
a name-only stub material renders as a legal default material anywhere.

glTF 2.1 provides the building blocks (the `files` array for portable
file references, `uid` for stable per-object identity including the
conforming-name fallback) but deliberately defines no cross-file object
reference syntax; this extension is that missing reference site, built
from the 2.1 primitives. If Khronos later standardizes an equivalent
mechanism, migration is mechanical (same file index, same uid).

The extension is defined generically for any child-of-root object;
erhe applies it per managed asset type as support lands (materials
first; animations and others later).

## JSON layout

```json
{
    "asset": { "version": "2.1" },
    "extensionsUsed": [ "ERHE_asset_reference" ],
    "files": [
        { "mimeType": "model/gltf-binary", "uri": "library/materials.glb" }
    ],
    "materials": [
        {
            "name": "Steel",
            "extensions": {
                "ERHE_asset_reference": { "file": 0, "uid": "m-steel-01" }
            }
        }
    ]
}
```

- `file` (integer, required): index into the glTF 2.1 root `files`
  array; the container file holding the definition. Note this indexes
  `files` directly, NOT `externalAssets` (which is node-instantiation
  indirection and unrelated to object references).
- `uid` (string, optional): the target object's glTF 2.1 `uid`. When
  absent, resolution falls back to the proxy's `name` (see Resolution).

Deliberately absent properties:

- No `type` field: placement implies the expected type - an extension on
  a material must resolve to a material; anything else is an error.
- No `name` field: the proxy's standard `name` property serves double
  duty as display name and as the 2.1-sanctioned conforming-name
  fallback identifier. erhe writes the proxy's `name` equal to the
  target's name.
- No array-index field: positional addressing silently re-binds after
  the target file is reordered; it is not identity.

## Resolution

1. Resolve `files[file].uri` relative to the referencing file (glTF URI
   convention). Each container is loaded at most once per session.
2. Look up `uid` among the container's objects of the implied type. If
   `uid` is absent or not found, fall back to a unique,
   charset-conforming name match against the proxy's `name` - the same
   lookup semantics the glTF 2.1 Unique IDs explainer defines.
3. The resolved object replaces the proxy for all uses in the
   referencing file (a primitive's `material` index now binds the shared
   object). The resolved object's own internal references (material ->
   textures -> samplers -> images) resolve within ITS containing file,
   transitively.
4. Self-heal upward: a reference resolved via the name fallback learns
   the target's `uid` and re-serializes with it on the next save.

## Error conditions

Load errors, reported with container path and identifier - never silent:

- `file` out of range; container file missing or unparsable.
- Identity ambiguity in the container: no `uid` match and the proxy name
  is absent, non-conforming, or duplicated in the container (remedies:
  stamp uids into the container, or rename in the authoring tool).
- Type mismatch: the identifier resolves to an object of another kind.
- Reference cycles between files, including self-reference (extends the
  glTF 2.1 External Assets rule that cyclical references are
  prohibited).

An unresolved reference keeps the proxy (default-material appearance)
with a logged warning, and the extension is preserved verbatim on
re-save: a broken reference is never silently converted into a local
definition or dropped.

## Notes

- A proxy carries no definition-side extensions (e.g. `ERHE_material`);
  those belong to the definition site only.
- Coexists with prefab instantiation: both mechanisms share the same
  `files` array; one entry may serve `externalAssets` instantiation and
  object references simultaneously.
- Loaders without the extension see a valid, named material with default
  (or authored fallback) properties; the visual downgrade outside erhe
  is the accepted cost of never embedding referenced definitions.

## Schema

[schema/ERHE_asset_reference.schema.json](schema/ERHE_asset_reference.schema.json)
