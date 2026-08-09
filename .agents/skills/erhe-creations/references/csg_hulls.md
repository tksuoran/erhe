# CSG, authored hulls & FFD

Boolean carving, authored convex-hull silhouettes and lattice (FFD)
deformation. New tools 2026-08-09; creation 16 (sail ships) is the
reference implementation.

- **`c.csg(target, tools, operation)`** (union / intersection /
  difference, WORLD-space composed): the result REPLACES the target
  mesh's primitives in place (node id, name, transform, children,
  material, physics attachment all survive; collision rebuilt as a
  convex hull of the result) and the TOOL NODES ARE REMOVED (children
  reparent up - use leaf mesh nodes as tools). One undoable operation;
  queued async, the wrapper settles by default.
- **Batch the carves**: pass a LIST of tool node ids - all tools merge
  into one solid and apply in a single boolean pass. Sequential calls
  re-triangulate the whole target once per call and the stacked passes
  show as sliver-triangle shading zigzags on large near-planar facets.
  Creation 16 carves deck well + 10 gunports + 3 transom windows in ONE
  difference.
- Inputs must be closed watertight manifolds (capped cones, boxes,
  spheres, convex hulls, regular polyhedra - NOT open discs/rectangles
  or use_bottom=false cones). CSG on a pooled instance silently goes
  private; create hero targets with reuse=False.
- **`create_shape convex_hull`** (points=[[x,y,z],...] node-local, >=4
  non-coplanar) is the way to get authored silhouettes - ship hulls
  from station points (rail pair + bilge pair + keel per station, stem/
  transom extremes). Convexity is fine for a beamy hull: carve the deck
  well back in with CSG and real bulwarks remain. Hull brushes build
  with SMOOTH normals (fair surface; also hides CSG triangulation).
  Also new: disc (annulus with inner_radius), triangle, quad, rectangle
  (flat XY, thin-box collision - use motion_mode none), and
  regular_polyhedron (tetra/cube/octa/dodeca/icosa/cuboctahedron).
- **`c.lattice_deform(node, offsets, divisions=[2,2,2], ...)`** = FFD:
  cage auto-fits the mesh's local bounds; offsets are sparse
  [i,j,k,dx,dy,dz] control point displacements; bezier default pins
  whatever you leave at rest. Billowed sails: box size [w,h,0.05] steps
  [8,8,1], push the i=1 column's dz (all j/k rows, mid > foot > head) -
  corners stay pinned and the smooth-normal recompute reads as canvas
  creases for free. Pennant ripple: divisions [3,1,1], +amp at i=1,
  -amp at i=2. The source needs interior vertices (a 4-vertex quad
  cannot bend) - use box steps.
- Open-ocean water (vs the shallow-lagoon recipe in SKILL.md
  materials): near-opaque alpha_blend (opacity ~0.85) dark blue
  [0.02,0.15,0.28] over a DEEP dark seabed - a bright shallow seabed
  reads olive at fleet scale. Disable shadow_cast (set_item_flags) on
  sails/flags: their water shadows alias into harsh spikes; keep hull +
  mast shadows.
- Straight trim boxes (wales/strakes) must stay within the parallel
  midbody (~0.5 L) or they poke out of the tapering hull; rigging rods
  are base-origin cones - place the base AT the start point (masthead),
  never at the segment midpoint.
- **Concave profiles on a convex hull come from the carve list** (the
  Vespucci clipper stem, 2026-08-09): a convex hull cannot hollow
  itself, so difference a beam-spanning cylinder (equal-radius capped
  cone laid along X via roll 90) whose surface passes through the two
  profile chord endpoints with the wanted sagitta
  (R = c^2/(8*sag) + sag/2, center at chord mid + normal*(R - sag)) -
  the cut face IS the curved profile. Keep the circle parameters so
  trim can trace the cut exactly.
- **Curved trim = lattice-bent strips, not chained cones** (creation 16
  bow): one thin box (steps ~[2,12,2] along its length) rotated chord-
  aligned, then a [1,3,1] bezier FFD; solve the two interior control
  planes from curve samples at t=1/3, 2/3 (C1 = 3*d13 - 1.5*d23,
  C2 = 3*d23 - 1.5*d13 on the chord deviations, rotated world->local
  with common.quat_rotate + conjugate). One node per band, smooth
  silhouette; see ShipYard.bent_strip. Bands that continue each other
  need ONE cross-section the whole way (a box/strip mix reads as steps
  at every joint), and hull-hugging bands need a per-height hug factor
  (the hull FLARES - at a low stripe's height the surface sits inside
  the rail half-beam; 1.01 low / 1.03 near rail + a 0.11*s-thick strip
  keeps the band proud everywhere - 0.98/1.01 with a thin strip sank
  into the bulged surface in places). Small fittings
  poking through a curved band (gunport dots) are per-side plates just
  proud of the band FACE - beam-spanning boxes poke out of the taper as
  tabs.
- **Hull-hugging bands from PROBED surfaces** (creation 16 stripes are
  the reference implementation): probe stations with rays (`c.
  geometry_query` raycasts toward the centerline - closest_point drifts
  toward the bulkier body near curved ends); ARC-LENGTH parameterize
  the polyline (index parameterization on uneven stations bent the
  cubic meters off course); split long runs so probes land on the
  cubic fit stations (t = 0, 1/3, 2/3, 1) and give hard-turning ends
  their own short run; roll the strip's face onto the surface normal
  and TWIST via per-control-plane corner rotations when the normal
  turns along the run (vertical cross-sections read as ledges on a
  flared hull); add a small outward bias (0.03-0.06 x s) because
  between fit stations the cubic cuts inside an outward-bulging
  surface. STRIP_DEBUG=1 prints fit deviations + twist per strip -
  a deviation of meters means broken parameterization, centimeters is
  healthy hull curvature.
- The convex hull SURFACE bulges past the authored station points, so
  bow furniture (cutwater, figurehead, beak rails) must sit clearly
  FORWARD of the stem line or it ends up buried inside the prow block.
  Scale fixed-size fittings by ship size (s = length / reference) - an
  unscaled 0.14 m spar vanishes on a 58 m hull. Figureheads and similar
  hero fittings want MONUMENTAL scale (the real Vespucci figure is
  ~4 m): a human-proportioned one reads as a flagpole knob from fleet
  distance, and it must lean well forward off the stem head or the
  bulge swallows it.
- Square sails are edge-on RIBBONS from abeam - a broadside camera on a
  square-rigger shows masts and paper edges; shoot from a bow or stern
  quarter (creation 16's Vespucci shot: port bow, ~1.5 ship lengths).
