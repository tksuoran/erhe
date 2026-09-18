# USD compatibility: remaining work

Status: proposed

This plan extends `doc/erhe/usd_compatibility_design.md`, which states what USD
support holds today: it holds the work that is left, ranked, and the work
items themselves. A USD scene loads, edits and saves without any of them.
Each item is independent of the others except where named, and its substance
is stated here once; the design record and `doc/erhe/usd.md` link here rather
than restating an item.

## Ranking

1. Load and save on a worker, and `.usdc` / `.usdz` output ("Asynchronous
   load" and "Binary and packaged output"). The load moves onto the asset
   manager's request path once the manager learns a second format; the output
   formats are what LightUSD's writer already offers.
2. The round-trip residue ("Node-held secondary values", "Camera
   infinite_z_far", "A writer finding of usdchecker", and the glTF finding of
   "Physics residue"). Small, each one a value that leaves through a save and
   does not come back, or a physics fixture case the import still drops.
3. Shading and imaging the survey names ("A material slot that a texture graph
   feeds and that carries an authored factor", "Image formats", "An
   environment map from a DomeLight texture", "MaterialX"). The slot factor is
   importer work; the rest need a renderer or decoder erhe does not have, and
   MaterialX documents a LightUSD option erhe's build leaves off.
4. Platform coverage ("macOS and Linux wrappers"): the option is on for
   Windows and Android only.
5. Composition beyond what erhe resolves ("Layer-stack editing", "inherits and
   specializes arcs whose target is not a class prim", the `over`-child and
   `.usdz` forms of "Variant opinions a variant set does not carry",
   "Overrides on applied API schemas inside an instance"). Each is a real USD
   feature with no surveyed asset that visibly depends on it, so they wait for
   a file that does.

The items have no ordering constraint among them; each is taken through the
harness of `doc/agent_orchestration_harness.md`, one commit at a time (the
design record's C2).

## Work items

### Card images borrowed by the opposite face

A card face with no image of its own borrows the opposite face's image in
`UsdImagingDrawModeAdapter` (`_GenerateTextureCoordinates`, the
`uv_flipped_s` / `uv_flipped_t` quads); erhe draws such a face flat in the
draw-mode color (the design record's C10). The borrowing is the per-face UV
selection in `draw_mode_cards.cpp` `card_uvs` plus the texture lookup falling
back to the opposite face.

### Physics residue

A glTF export of `physics.usda`'s scene does not re-import: fastgltf rejects
the file ("missing something or has invalid data") on the
`KHR_physics_rigid_bodies` `physicsJoints[].limits` the export writes, while
the same file with the limits stripped parses (bisected on the written file;
colliders and motions are fine). This is glTF-side; the limit spelling the
export uses is the suspect.

### A writer finding of usdchecker

A texture packed in a `.usdz` is written as a path that names no file
(`MissingReferenceChecker`). Closing it means writing the packed bytes next to
the file, or the `archive.usdz[entry]` form (`doc/erhe/usd.md`, "Export").

### Asynchronous load

`load_usd` runs on the calling thread and the editor's import and open are
synchronous, where a glTF import goes through the asset manager's
`Asset_load_request` and the droppable-payload import operation
(`doc/editor/reloadable_asset_loads.md`). The conversion creates no GPU object, so it
moves onto a worker once the asset manager learns a second format. This is
what a large stage's settle time is spent on and the only thing left that
trips the stall watchdog there: of `simpleAssetScene.usd`'s 202 s (the design
record, "A load of a stage holding thousands of prims"), the composition, the
prefab templates and the attach of 6686 prims to the scene run inside one
tick, which the watchdog reports as `usd: attach to scene`.

### Binary and packaged output

The writer emits `.usda` only; a scene opened from `.usdc` or `.usdz` saves
back as `.usda` beside it. LightUSD writes both formats; the `.usdz` case also
needs the packed-texture answer of the usdchecker item above. The writer emits
no `.mtlx` document either (the inline OpenPBR network it writes for an
anisotropic or transmissive material is not one).

### Node-held secondary values

A node-held value of another class (`doc/erhe/property_system.md` D30,
`Light.color` on a plain `Xform`) is written as `erhe:Light:color`, and the
import resolves neither the qualified nor the bare name against a node, so
such a value does not come back.

### Camera infinite_z_far

A camera's `infinite_z_far` has no USD form; the finite `clippingRange` is
written and one warning says so.

### A material slot that a texture graph feeds and that carries an authored factor

The connection replaces the value in both terminals (a `UsdPreviewSurface` or
OpenPBR input is either connected or valued), so the factor of such a slot is
not written and reads back as the default. Closing it means carrying the
factor as the graph connection's `inputs:scale` the way a `UsdUVTexture`
carries erhe's factor, which needs the graph's interface output to pass
through a multiplying node.

### Image formats

Radiance `.hdr` and OpenEXR `.exr` need decoders erhe does not build
(`stb_image.h` sits in the CPM cache of fpng and LightUSD, and nothing in the
tree reads `.exr`); the StandardShaderBall scene's six neutral `.exr` maps are
the surveyed assets that ask for the second.

### An environment map from a DomeLight texture

erhe has no environment map, so a dome's `inputs:texture:file` is named in one
warning and not sampled, and the dome contributes the constant radiance of its
`color`, `intensity` and `exposure` only (`doc/erhe/usd.md`, DomeLight). The
usd-wg McUsd entries are the surveyed assets that author one. Taking it up
means an image-based ambient term in the renderer first; the reader already
keeps the dome prim and its texture path.

### MaterialX

A `.mtlx` document as a reference target (the editor refuses the arc; the
usd-wg chess set, MaterialXTest and the MaterialX color-space tests are the
surveyed assets), and the LightUSD usda reader's rejection of `colorSpace`
metadata on a shader attribute (the survey's one failing entry). The `.mtlx`
reader is behind `LIGHTUSD_WITH_USDMTLX`, off in erhe's build. An inline
`ND_standard_surface_surfaceshader` or `ND_open_pbr_surface_surfaceshader`
network needs none of that and is read and written (the design record's E2).

### Grid depth

The grid's depth does not agree with the content's, so grid lines cross opaque
objects below the horizon (`doc/editor/rendering.md`, Grid). Needs a RenderDoc
session on the windowed build.

### inherits and specializes arcs whose target is not a class prim

usd-wg `inherit_and_specialize.usda` inherits from a `def Cube`: X3 makes a
style only of a class prim, so such an arc composes nothing and is warned
about; the surveyed file overrides every inherited opinion locally, so nothing
visible depends on it there.

### Variant opinions a variant set does not carry

The `def` children of a variant block that the hoist does not reach - one
authored below an `over` child of the variant, and any of them in a `.usdz`
archive, whose asset paths resolve through the archive rather than the file
system - and a property the value reader cannot express. Each is counted in
`Usd_variant_set::unsupported_opinion_count`, reported per set, and named by
the save warning. Taking the first up means hoisting through the `over`
children too, and the last is the value reader's own coverage.

### Overrides on applied API schemas inside an instance

An attachment of an applied schema reads its counterpart through the reference
layer (the design record's C10), and the override walk of
`erhe::scene::instance_override` visits prims only, so a local value on a
`Node_physics`, `Node_joint` or other attachment below a carrier is neither
written as part of the carrier's `over` prims nor kept across a prefab reload.
Taking it up means walking the attachments in the same lockstep the
counterpart link uses and giving each an `over` path (USD authors an applied
schema's attributes on the prim itself).

### Layer-stack editing

A root layer's sublayers are composed at load and a save writes one flattened
layer with no `subLayers` (the mapping's `subLayers` row; `doc/erhe/usd.md`
"Sublayers"), so an edit cannot be written back to the layer that authored the
value. A stack the editor edits layer by layer needs per-value layer
provenance, which `CompositeSublayers` does not keep (the design record's X5).

### macOS and Linux wrappers

`configure_xcode_*.sh` and `configure_ninja_linux_*.sh` leave
`ERHE_USD_LIBRARY` at `none`; turning it on there is the step that first needs
USD on those platforms (`doc/erhe/usd.md` "Configurations").
