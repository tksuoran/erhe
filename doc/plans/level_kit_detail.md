# Level kit level of detail

Status: in progress

Converts the level kit shapes to the shared `detail` parameter
([../editor/level_kit_shapes.md](../editor/level_kit_shapes.md) "Level of
detail"), shape by shape, and later builds automatic LOD chains from it.

## Rulings (user, 2026-11-26)

- Procedural only: a shape builds itself coarser or finer from its counts;
  no generic mesh simplification. Converted incrementally; a shape not yet
  converted ignores the property.
- The property is a detail share. The highest detail raises the detail
  above today's, since many shapes, curved ones above all, are low in
  detail: the default (0.5) is the shape's own counts, 1 doubles the
  counts' triangles.

## Converted

The 26 shapes whose `segments` count drives their curves, and the vegetation
(palm, palm clump, palm shrub, leaf plant, succulent, grass tuft, tree, bush,
bamboo); the list is in the shapes reference.

## To convert

Shapes whose curves come from counts fixed inside their generators, the
curved ones first; each gets an int parameter for the count, or a
`Kit_detail_count` on one it has, or a generator reading the scaled count:

- `architecture.arch`: `segments` is the stone count; the curve's 16 fine
  stations (`make_arch_half()`) are fixed.
- Lathes and tubes of fixed point counts: the machine kit (gears' round
  bodies, bearings, nuts, bolts, springs' `spring_stretches`, cams), the
  well's and the fire's lathes, the lantern, the bucket.
- Sweeps and rings of fixed counts: the boat's bands and strakes, the rock
  and the boulder cluster, the shell, the starfish.
- Vegetation left at its own resolution: the fan leaves' and the hearts'
  outlines (points between a finger's tip and its clefts, along the heart's
  outline), the pinnate fronds' rachis stations, the bamboo's leaves, the
  ribbon blades' points across; `nature.blocky_tree` is boxes by design.
- The rest: shapes of boxes have one level of detail and stay as they are.

Each conversion keeps the shape's mesh at the default detail (the pins of
`editor_level_tests` hold it) and passes `Kit_detail` at every detail.

## Later: automatic LOD

A piece's LOD chain from its shape at decreasing detail (for example the
default, 0.3 and 0.15), each level a generated mesh, in place of the
simplified chain of `doc/editor/lod.md`; not started.
