#pragma once

#include "erhe_math/curve_path.hpp"

#include <string>
#include <string_view>

namespace erhe::gltf {

// The ERHE_curve node extension value (doc/gltf_extensions/ERHE_curve.md):
// the object {"closed": ..., "points": [...]} with native JSON numbers in the
// shortest form that reads back to the same float.
[[nodiscard]] auto curve_path_to_json(const erhe::math::Curve_path& path) -> std::string;

// Reads an ERHE_curve value; false with out_error naming the problem, out_path untouched.
[[nodiscard]] auto curve_path_from_json(std::string_view json, erhe::math::Curve_path& out_path, std::string& out_error) -> bool;

} // namespace erhe::gltf
