#pragma once

#include <cstddef>

#include "erhe_gl/wrapper_enums.hpp"

namespace gl_helpers
{

[[nodiscard]] auto size_of_type (gl::Draw_elements_type type) -> std::size_t;
[[nodiscard]] auto size_of_type (gl::Vertex_attrib_type type) -> std::size_t;
[[nodiscard]] auto is_indexed   (gl::Buffer_target type) -> bool;
[[nodiscard]] auto is_compressed(gl::Internal_format format) -> bool;
[[nodiscard]] auto is_integer   (gl::Internal_format format) -> bool;
[[nodiscard]] auto has_color    (gl::Internal_format format) -> bool;
[[nodiscard]] auto has_alpha    (gl::Internal_format format) -> bool;
[[nodiscard]] auto has_depth    (gl::Internal_format format) -> bool;
[[nodiscard]] auto has_stencil  (gl::Internal_format format) -> bool;

void set_error_checking(const bool enable);
void check_error       ();
void initialize_logging();

} // namespace gl_helpers

