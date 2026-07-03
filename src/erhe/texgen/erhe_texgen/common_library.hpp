#pragma once

#include <string_view>

namespace erhe::texgen {

// The common hash / random / color-space GLSL function library shared by
// generated shaders (rand, rand2, rand3, param_rnd, rgb2hsv, ...).
[[nodiscard]] auto common_library_glsl() -> std::string_view;

// Heuristic: true when the shader text references any function defined in
// the common library (used by Common_library_mode::when_referenced).
[[nodiscard]] auto references_common_library(std::string_view shader_text) -> bool;

} // namespace erhe::texgen
