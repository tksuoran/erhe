# Content library: outstanding work

Status: proposed

Extends [`doc/editor/content_library.md`](../editor/content_library.md) and
the design records beside it -
[`doc/editor/content_library_ownership.md`](../editor/content_library_ownership.md),
[`doc/editor/content_library_folders.md`](../editor/content_library_folders.md),
[`doc/editor/style_library.md`](../editor/style_library.md),
[`doc/editor/asset_manager.md`](../editor/asset_manager.md) and
[`doc/editor/import_undo_reference_clearing.md`](../editor/import_undo_reference_clearing.md).

## 1. Managed asset types beyond materials

`doc/editor/asset_manager.md`, "Current restrictions", states what the asset manager
covers today: the managed types are brush, material and animation, while the
workflow verbs (make external, make internal, reference into scene,
import-as-reference) and the `ERHE_asset_reference` wire format cover
materials only. Textures, graph assets, physics materials and skins are
scene-hosted, and file-scope acquisition from a parsed container serves
materials and animations while brushes keep scene-local keys.

Extending a type means, per type: an `Asset_key` scope that can address it in
a container, resolution maps in the container record, the workflow verbs, and
a wire-format entry beside `ERHE_asset_reference`. Textures are the
interesting case, because a surviving manager-owned material already keeps its
texture references alive as a transitive pin.

## 2. Headless coverage for removal announcements

`doc/editor/import_undo_reference_clearing.md` lists three things its smoke test
cannot reach:

- **Brush references.** The test glTF carries no `ERHE_brushes`, and
  `create_shape(add_brush)` records no undoable operation, so there is no way
  to remove a brush from a library over MCP. `Brush_tool`'s clearing rides the
  same handler as the material cases, so it is untested rather than unwired.
- **The ownerless-library silence.** No MCP tool removes a content-library
  item, so the deliberate silence of a library with no owner is documented
  rather than asserted.
- **The inspected-material dirty-edit warning.** The dirty flag is set only
  from the ImGui render path, so the warning is verified interactively.

Each needs an explicit-argument MCP hook of the kind `acquire_asset` and
`debug_set_item_tree_hover` already are (doc/mcp_api_guidelines.md).
