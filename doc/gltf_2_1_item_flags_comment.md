# glTF and per-node item flags ("exclude from prefab") -- findings + draft issue comment

Written 2026-07-11, in the context of erhe's sealed prefab instances
(doc/gltf-prefabs-plan.md): the editor adds implicit helper content (a
default camera and key/fill lights) to a scene opened from a glTF file that
has none, and needs to mark that content so it is not instantiated when the
file is used as a prefab (glTF 2.1 external asset).

## Does glTF have anything like this?

No, as of 2026-07:

- **glTF 2.0 core**: a `node` carries name, transform, mesh / camera / skin
  references, children, `extensions` and `extras`. There are no flag bits
  and no notion of editor-only / authoring-only content.
- **Ratified extensions**: the closest is `KHR_node_visibility` (per-node
  `visible` boolean), but that is *render* visibility -- not "exclude when
  this asset is instantiated elsewhere". `KHR_lights_punctual` etc. add
  payloads, not flags.
- **glTF 2.1 proposals** (checked 2026-07-11): the External Assets proposal
  (KhronosGroup/glTF#2586) defines only `"externalAsset": <index>` on the
  instancing node, plus the top-level `externalAssets` / `files` arrays
  (#2590). Neither the explainer nor the issue discussion mentions per-node
  flags, authoring-only content, or excluding nodes from instantiation. The
  proposal explicitly defers "customization of assets in the future, such as
  overriding the node transforms ... or otherwise alter the model" to future
  extensions.

Consequence for erhe: erhe-specific item flags ride in node `extras` as
`"extras": { "erhe_flags": ["exclude_from_prefab"] }` (written by the
exporter, restored by the parser; unknown names are ignored so the list can
grow). See `c_serialized_item_flags` in
`src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp`.

## Draft comment for KhronosGroup/glTF#2586 (External Assets)

> **Instantiation-time content filtering / authoring-only nodes**
>
> While implementing External Assets in an editor (glTF files usable as
> prefabs: instantiated by reference, edited by opening the referenced file
> as a scene), we hit a semantic gap worth considering for the explainer or
> a companion extension.
>
> A glTF file that is *both* a self-contained viewable asset and an
> externally-instantiated sub-asset often wants content that only applies to
> the former role. Concrete example: an editor opens `chair.gltf`, and adds
> a default camera and lights so the user can see something while editing
> (or the author deliberately saves a "studio" camera / light rig with the
> asset). When another asset instantiates `chair.gltf` via `externalAsset`,
> that helper content must NOT be instantiated -- otherwise every chair
> instance injects an extra camera and light rig into the referencing scene.
>
> Today the proposal gives instantiation all-or-nothing semantics: the
> referenced asset's (default) scene is instantiated wholesale. There is no
> way for the referenced asset to say "this node is for direct viewing /
> authoring only; skip it when I am instantiated as an external asset".
>
> Questions / suggestions:
>
> 1. Should the spec (or explainer) define whether cameras -- and by
>    analogy `KHR_lights_punctual` lights -- of the referenced asset are
>    instantiated? Cameras seem especially questionable to instantiate.
> 2. More generally, a per-node boolean along the lines of
>    `"instantiable": false` (or an `authoringOnly` marker; naming aside)
>    on nodes of the referenced asset would let one file serve both roles
>    cleanly. It composes well with the existing model: implementations
>    that instantiate simply skip such nodes and their subtrees.
> 3. If this is deemed out of scope for 2.1 core, it may be worth noting in
>    the explainer as an anticipated extension point, next to the
>    already-mentioned transform-override customization.
>
> For now we serialize this as application-specific `extras` on the node,
> which works but is invisible to other implementations instantiating the
> same file.

## Status in erhe

- `erhe::Item_flags::exclude_from_prefab` (bit 26): set on the implicit
  default camera / light nodes `import_gltf` adds; round-trips through node
  `extras`; `attach_prefab_instance` skips flagged template roots when
  instantiating.
- Exporter writes `KHR_lights_punctual` (added together with this work) so
  saved light nodes keep their light payload instead of degrading to empty
  nodes.
