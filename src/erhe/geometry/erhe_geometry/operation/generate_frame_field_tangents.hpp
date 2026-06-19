#pragma once

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

// Generate tangents and bitangents from a Geogram cross (frame) field, computed by
// GEO::FrameField::create_from_surface_mesh(). Unlike generate_tangents() (which
// derives the tangent basis from texture coordinates via MikkTSpace), this needs no
// texture coordinates: it builds a smooth, curvature-aligned 4-symmetry direction
// field over the surface and uses the field's in-plane direction as the tangent.
//
// The result is per-facet (every corner of a facet shares that facet's tangent),
// which suits anisotropic shading / direction-field uses; it is NOT a substitute
// for UV-based normal mapping, because the field is not aligned with any texture
// parameterization.
//
// sharp_angle_threshold: edges whose dihedral angle exceeds this (in degrees) are
// treated as hard feature constraints that the field aligns to.
void generate_frame_field_tangents(const Geometry& source, Geometry& destination, double sharp_angle_threshold = 45.0);

} // namespace erhe::geometry::operation
