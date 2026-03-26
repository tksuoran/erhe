# Conway Join

## Operation

The join operator (Conway notation: **j**) is the dual of ambo. For each
edge shared by two faces, it creates a quadrilateral whose vertices are the
two edge endpoints and the two adjacent face centroids. Join of a Platonic
solid produces the corresponding Catalan solid (e.g., join of cube = rhombic
dodecahedron).

## Topology

- Input: V vertices, E edges, F faces
- Output: V + F vertices, E quadrilateral faces, 2E edges

## Algorithm

1. Copy all source vertices to destination.
2. Create a centroid vertex for each source face.
3. For each source edge with exactly two adjacent faces, determine the
   winding direction in each face, then create a quad connecting
   [vertex_a, centroid_right, vertex_b, centroid_left] with consistent
   outward-facing normals.
4. Interpolate attributes and recompute normals/connectivity.

## Parameters

None currently. A centroid blend factor could control how far the face-center
vertices are from the true centroid, affecting the aspect ratio of the
resulting rhombi.
