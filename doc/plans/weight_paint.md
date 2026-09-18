# Weight painting: outstanding work

Status: proposed

This plan extends `doc/editor/weight_paint.md` (the weight visualization mode and the
weight paint brush the editor has) with the brushes and options it does not.

## Further brushes

- Blur, average and smear. Blur is the one worth doing first: it is cheap and
  useful. Smear needs stroke-direction history.
- X-mirror and symmetry painting.
- Locked vertex groups, lock-relative and multi-paint.
- Tablet pressure, custom falloff curves and brush textures.

## Screen-space brush radius

The radius is a world-space slider. Blender converts a pixel radius to object
space at the hit depth (`paint_calc_object_space_radius`,
`editors/sculpt_paint/paint_utils.cc:97`), which keeps the brush the same size
on screen at any distance.

## Decide what a single-influence paint does with the remainder

With auto-normalize on and every other slot zero, the painted weight is left
summing to less than 1, because erhe's skinning shader does not renormalize.
The options are (a) renormalize in the shader, (b) always distribute the
remainder to the largest other influence, or (c) accept it. Prefer (b) while
auto-normalize is on: it keeps the data valid for any glTF consumer.

## Reach beyond the brush's own primitive

Adding an influence to a vertex of another primitive of the same mesh that is
not under the brush, and a spatial acceleration structure so a very dense mesh
does not test every vertex per dab.

## Painting beyond the vertex format's limits

Painting weights for joints beyond the 4-influence and 256-joint limits of the
current skinned vertex format needs a wider format first.
