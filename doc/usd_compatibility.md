# USD compatibility notes

Companion to `doc/gltf-scene-roundtrip-plan.md`. erhe persists scenes as
glTF (2.1 + extensions); OpenUSD was reviewed and rejected as the
persistence format (see the plan's Alternative-considered section). This
document keeps the door to USD interchange open and cheap:

1. it records the **naming mapping** between erhe's glTF extension
   vocabulary and the equivalent USD concepts, so a future USD
   exporter/importer is a table lookup, not a redesign;
2. it documents **planned future features** (`ERHE_overrides` and friends)
   whose designs are informed by USD but expressed in glTF terms.

erhe's extensions deliberately use erhe/glTF-context naming, NOT USD
vocabulary - USD terms like "primvar interpolation: faceVarying" would be
confusing inside a Khronos/glTF file. The mapping lives here instead.

## Geometry attribute element naming

`ERHE_geometry` (see the plan, phase 2) dumps every geogram attribute with
an `element` field naming what the attribute is attached to. The names are
erhe/geogram's own:

| ERHE_geometry `element` | geogram element | USD primvar interpolation | meaning |
|---|---|---|---|
| `mesh`   | mesh (attribute on the whole mesh) | `constant`    | one value for the whole mesh |
| `facet`  | facet                              | `uniform`     | one value per polygon |
| `vertex` | vertex                             | `vertex`      | one value per vertex (glTF vertex i == geogram vertex i) |
| `corner` | corner                             | `faceVarying` | one value per polygon-corner, in EXT_mesh_polygon ring order |
| `edge`   | edge                               | (none)        | one value per edge; the edge index list is serialized alongside. USD has no edge element - a USD exporter would carry these as namespaced constant arrays plus the edge index list |

## Concept mapping (glTF-side <-> USD-side)

| erhe / glTF mechanism | USD equivalent | notes |
|---|---|---|
| node tree, TRS | prim hierarchy, UsdGeomXformable | glTF quantizes to one T*R*S; USD allows arbitrary xformOp stacks |
| mesh primitive + EXT_mesh_polygon | UsdGeomMesh faceVertexCounts/faceVertexIndices | USD n-gons are native; glTF needs the (draft) polygon extension |
| `ERHE_geometry` attributes | UsdGeomPrimvar | see table above |
| materials + `ERHE_material` | UsdShade / UsdPreviewSurface | PBR metallic-roughness maps to UsdPreviewSurface inputs |
| KHR_lights_punctual | UsdLux | directional/point/spot map to DistantLight/SphereLight/spot cone |
| cameras + `ERHE_camera` | UsdGeomCamera | exposure exists natively in UsdGeomCamera |
| animations (samplers/channels) | time samples / Ts splines | glTF is keyframe-sampler-first, USD time-sample-first; cubic tangents need re-encoding either direction |
| skins | UsdSkel | |
| KHR_physics_rigid_bodies + KHR_implicit_shapes | UsdPhysics | closest semantic match of all; both cover bodies, colliders, joints, materials, filtering/groups |
| glTF 2.1 externalAssets (prefab instances) | references (composition arc) | USD references are strictly stronger (can target any prim, compose lists) |
| `ERHE_collections` | UsdCollectionAPI | named membership sets |
| `ERHE_scene` (per-scene settings) | stage/root-layer metadata + custom API schema | |
| `ERHE_layout` / `ERHE_brushes` / `ERHE_node_graphs` | custom (codeless) schemas | no USD counterpart; these are erhe editor domain |
| planned `ERHE_overrides` | "overs" / sparse opinions on a reference | see below |
| KHR_materials_variants (planned adoption) | variantSets (material-only slice) | |
| EXT_mesh_gpu_instancing (possible future) | UsdGeomPointInstancer / instanceable | |
| (no equivalent; load policy flags possible on ERHE_scene) | payloads (deferred loading) | |
| (non-goal) | sublayers / layer stacks | one scene = one asset + external references remains erhe's model |

## Planned future features

These are informed by USD but specified in glTF terms. None are on the
switchover critical path (plan phases 0-6); they land after `.erhescene`
is gone.

### ERHE_overrides (near-term, highest value)

USD inspiration: composing sparse "over" opinions on top of a reference.

Problem it solves: today, edits made inside a prefab instance subtree are
silently lost on save - the subtree is not serialized and is re-created
verbatim from the referenced glTF on load.

Shape: an extension on the prefab-instance node carrying a list of sparse
overrides applied after the externalAsset subtree is instantiated:

- **Addressing**: each override targets a node inside the referenced asset
  by a stable intra-asset path (name path from the asset root, with an
  index-based disambiguator for duplicate names). This addressing scheme is
  the hard design problem and the reason the feature is not in the
  switchover critical path: it must survive re-exports of the source asset
  as long as names/structure are unchanged, and must fail loudly (keep the
  override, mark unresolved) when the source changed shape.
- **Override kinds** (initial set): node transform (full TRS replace),
  node Item flags (e.g. hide a subtree member), material rebind
  (primitive -> material index in the *outer* asset), removal (prune a
  member). Extendable per-kind.
- **Application order**: instantiate subtree -> apply overrides in file
  order -> attach Prefab_instance marker. Save regenerates the override
  list by diffing the live subtree against a pristine instantiation.

### KHR_materials_variants adoption (near-term)

Ratified glTF extension; fastgltf already parses it. Gives per-primitive
material alternatives (USD: material-binding variantSets). Requires editor
UI for authoring and switching the active variant, and MCP tools for both.

### KHR_animation_pointer adoption (near-term)

Ratified glTF extension: animation channels target arbitrary properties via
JSON pointer (USD: time samples on any attribute). Growth path for the
animation editor (#243) beyond node TRS - lights, material factors, camera
parameters. Needs Animation_player property-binding support.

### ERHE_variants (deferred)

Node-level variant sets (subtree alternatives: LODs, configuration
options). USD: variantSets beyond materials. No current editor feature
needs it; design when one does.

### Instancing (deferred)

Adopt EXT_mesh_gpu_instancing (ratified) if/when erhe grows mass-placement
(scatter) tooling. Do not invent an ERHE extension for this.

### Payload-style load policy (deferred)

externalAssets + Prefab_library caching already give lazy structure;
explicit per-instance load/unload policy flags can ride `ERHE_scene` later
if scenes grow big enough to need it.

### USD interchange target (revisit trigger)

If Android/Quest support lands in OpenUSD (or write-capable tinyusdz
matures) AND erhe needs composition features glTF cannot express, add USD
as an *export/import interchange target* using the mappings above - without
changing the persistence format.
