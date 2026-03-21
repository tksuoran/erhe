# erhe_geometry -- Code Review

## Summary
A comprehensive polygon mesh geometry library built on Geogram, providing mesh manipulation, Conway operators (ambo, chamfer, dual, gyro, kis, etc.), CSG operations, subdivision (Catmull-Clark, sqrt3), and shape generation. The architecture is well-structured with a clean operation base class. The code is complex but necessarily so for the domain. Main concerns are around the attribute system verbosity and some global state.

## Strengths
- Clean operation hierarchy: `Geometry_operation` base class handles attribute interpolation, vertex/facet/corner source tracking, and post-processing generically
- Comprehensive Conway operator support with correct topology transformations
- `Attribute_present<T>` template elegantly combines attribute data with presence tracking
- `Mesh_attributes` provides a well-organized collection of standard and custom attributes
- Proper use of Geogram's mesh data structures without unnecessary abstraction overhead
- `Mesh_info` and `Mesh_serials` provide clean metadata tracking

## Issues
- **[moderate]** `Attribute_descriptors` uses mutable static state (`s_descriptors` vector, `init()` function) which must be called before any geometry operations. This is a hidden initialization dependency that could cause subtle bugs if forgotten.
- **[moderate]** `Mesh_attributes` has ~30 public members all of type `Attribute_present<T>` (geometry.hpp:251-270+). The `bind()` and `unbind()` methods manually enumerate all 30+ attributes -- adding a new attribute requires changes in 3+ places (declaration, bind, unbind). A registry-based approach would be less error-prone.
- **[moderate]** Free functions `set_point`, `set_pointf`, `get_point`, `get_pointf`, `mesh_vertexf`, `mesh_corner_vertex`, `mesh_facet_normalf`, `mesh_facet_centerf` (geometry.cpp:19-81) are at file scope without namespace qualification, polluting the global namespace.
- **[minor]** `Geometry_operation` constructor initializes both `source` and `lhs` to the same reference (geometry_operation.hpp:27-28), creating redundancy. The single-source constructor should probably only have `source`.
- **[minor]** `pair_hash` struct in `geometry_operation.hpp:11-18` is in the global namespace and uses a weak hash combination (`hash1 ^ (hash2 << 1)`). Consider using a better combining function.
- **[minor]** Several `const char*` string constants at file scope (geometry.hpp:114-131) should be `constexpr std::string_view` for modern C++.
- **[minor]** `Geometry_operation::get_size_to_include()` (geometry_operation.cpp:10-21) has an unusual growth strategy and uses an infinite loop that could theoretically not terminate for very large values (though in practice it always will).

## Suggestions
- Move the free functions in `geometry.cpp` into a `detail` namespace or make them `static` to avoid global namespace pollution.
- Move `pair_hash` into the `erhe::geometry::operation` namespace.
- Consider a registration-based approach for `Mesh_attributes` where attributes are stored in a container, eliminating the need to manually enumerate them in `bind()`/`unbind()`.
- Replace `const char*` string constants with `constexpr std::string_view`.
- Make `Attribute_descriptors::init()` thread-safe or use a static initialization pattern that does not require explicit calling.
- Consider using `std::unordered_map` with a stronger hash function (e.g., boost::hash_combine pattern) for the edge-to-vertex maps.
