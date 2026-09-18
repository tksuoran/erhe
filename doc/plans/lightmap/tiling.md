# Lightmap tiling and partition: open work

Status: in progress

Extends section 9 (spatial tiling, bake to disk, streaming) and section 10
(world-space tile partitioning) of
[../../lightmap_baking.md](../../editor/lightmap_baking.md), which describe what the
grid, the clipper, the partitioner, the streamer and the tile persistence do
today. Only the open work is listed here.

## Defects

- **Closing a scene with a prepared partition trips the scene-close leak
  watchdog.** `Lightmap_partitioner::on_scene_closed` drops its store, but
  `Lightmap_baker::m_layout` still holds the piece meshes
  (`Instance_region::mesh` shared pointers) and the baker has no scene-close
  hook, so the pieces' primitives and materials stay alive until the next
  `update_layout`. Clear `m_layout` and the tile state in the baker when its
  layout scene root closes.
- **Undo does not know about the partition.** The partition is deliberately
  outside the undo stack, so undoing a scene edit can leave stale pieces
  standing; the window warns and a re-prepare fixes it. Decide whether an undo
  should trigger the same settle-and-re-prepare path an edit does.
- **Reordering charts does not migrate the baked texels.** A tile reordered by
  "Reorder Charts By Bake" is stale until the next bake, even though the same
  radiance is already on disk under the old packing. Remap the saved payload
  through the old-to-new region rects instead of discarding it.
- **Piece meshes carry no pickable flag and no physics.** They are pure render
  proxies; picking and simulation operate on the originals. Confirm that is the
  wanted end state, or give the pieces the missing attributes.

## Quality

- **Cross-tile seam blend at bake time.** A cut boundary between two tiles is a
  genuine lightmap seam: the two sides are baked and dilated independently, so
  positions are crack-free but the shading is discontinuous. Blend across tile
  boundaries the way the in-tile seam blend does, which needs both tiles
  resident (or the neighbour's boundary texels saved with the payload).
- **The clipper fan-triangulates every facet**, including facets no plane cuts,
  which costs extra facets and charts on polygon meshes. Emit the original
  polygon when it is not cut.
- **Facet-level hover on pieces.** The Lightmap Texture window resolves a source
  mesh to its piece, but a hovered facet cannot be mapped back, because the
  viewer does not read the clipper's `clip_source_facet` facet attribute. Carry
  it through the piece primitive so the viewer can highlight the source facet.

## Unexercised cases

These have never been run and are the first things to check when this area is
touched:

- eviction under camera movement with pieces (the white-sentinel path has only
  been exercised through a budget-1 layout);
- a real glTF scene with duplicate node names, which is what the node index path
  in the manifest exists for;
- undo interactions while a partition is live.
