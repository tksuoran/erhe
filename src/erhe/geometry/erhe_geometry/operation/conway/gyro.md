# Conway Gyro

## Operation

The gyro operator (Conway notation: **g**) replaces each face with a ring of
pentagons that spiral around the face center. It is a chiral operation — the
result has a twist that breaks mirror symmetry. Applied to a Platonic solid,
gyro produces the corresponding pentagonal polyhedron (e.g., gyro of cube =
pentagonal icositetrahedron).

## Topology

- Input: V vertices, E edges, F faces
- Output: V + F + 2E vertices, 2E pentagonal faces, 5E edges

## Algorithm

1. Copy all source vertices to destination.
2. Create a centroid vertex for each source face.
3. Create two new vertices per edge at t=1/3 and t=2/3.
4. For each corner (vertex b) of each source face, create a pentagon from
   five vertices: [prev_edge_mid_0, prev_edge_mid_1, b, next_edge_mid_0,
   face_centroid].
5. Interpolate attributes and recompute normals/connectivity.

## Parameters

None currently. A `ratio` parameter (default 1/3) controlling the edge split
positions (t=ratio and t=1-ratio) would vary the chirality/twist of the
result.
