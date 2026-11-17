#include "kit_shape_test_support.hpp"

// The shapes of kit_shapes_build_21_24.cpp.
namespace editor::kit_test {

auto get_build_21_24_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.shapes              = {};
    rows.recreated_originals = {};
    rows.seeded_shapes       = {};
    rows.enum_shapes         = {};
    rows.components          = {};
    rows.walkability_open    = {};
    rows.triangle_counts     = {};
    rows.worst_cases         = {};
    rows.pins                = {};
    return rows;
}

} // namespace editor::kit_test
