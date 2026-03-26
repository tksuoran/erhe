# Conway Subdivide

## Operation

Subdivision replaces each N-sided face with N quadrilaterals by introducing
edge midpoints and face centroids, then connecting them. This is equivalent
to the topology step of Catmull-Clark subdivision without vertex smoothing.

## Topology

- Input: V vertices, E edges, F faces
- Output: V + E + F vertices, sum of face corner counts faces (all quads)

## Algorithm

1. Copy all source vertices to destination.
2. Create a centroid vertex for each source face.
3. Create one midpoint vertex per edge at t=0.5.
4. For each corner of each face, create a quad:
   [prev_edge_midpoint, original_vertex, next_edge_midpoint, face_centroid].
5. Interpolate attributes and recompute normals/connectivity.

## Parameters

None currently. A smoothing factor (0.0 = pure subdivision, 1.0 = full
Catmull-Clark smoothing) would be the most useful extension, but this
requires averaging vertex positions which is a separate algorithm step.
